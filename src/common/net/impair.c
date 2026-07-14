/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/impair.h"

#include <limits.h>
#include <string.h>

#define NET_IMPAIR_MAX_DELAY_MS 60000u
#define NET_IMPAIR_MAX_BURST    1024u
#define NET_IMPAIR_MAX_RATE     1000000000u

static uint32_t clamp_u32(uint32_t value, uint32_t maximum)
{
    return value > maximum ? maximum : value;
}

static uint16_t clamp_u16(uint16_t value, uint16_t maximum)
{
    return value > maximum ? maximum : value;
}

static uint64_t add_saturated(uint64_t a, uint64_t b)
{
    return UINT64_MAX - a < b ? UINT64_MAX : a + b;
}

static uint64_t multiply_saturated(uint64_t a, uint64_t b)
{
    return a && b > UINT64_MAX / a ? UINT64_MAX : a * b;
}

static uint64_t ceil_div_u64(uint64_t numerator, uint64_t denominator)
{
    if (!denominator)
        return UINT64_MAX;
    return numerator / denominator + (numerator % denominator != 0);
}

static uint32_t next_random(net_impair_model_t *model)
{
    // PCG-XSH-RR.  A fixed stream and integer-only operations make decisions
    // byte-for-byte repeatable on every supported platform.
    const uint64_t old_state = model->rng_state;
    model->rng_state = old_state * UINT64_C(6364136223846793005) +
                       UINT64_C(1442695040888963407);
    const uint32_t xorshifted = (uint32_t)(((old_state >> 18u) ^ old_state) >> 27u);
    const uint32_t rotation = (uint32_t)(old_state >> 59u);
    return (xorshifted >> rotation) |
           (xorshifted << ((0u - rotation) & 31u));
}

static uint32_t bounded_random(net_impair_model_t *model, uint32_t bound)
{
    if (!bound)
        return 0;

    // Multiply-high avoids modulo bias while consuming exactly one random
    // value per decision.
    return (uint32_t)(((uint64_t)next_random(model) * bound) >> 32u);
}

static bool random_chance(net_impair_model_t *model, uint16_t basis_points)
{
    return basis_points &&
           bounded_random(model, NET_IMPAIR_PERCENT_SCALE) < basis_points;
}

static uint64_t microseconds_to_milliseconds(uint64_t microseconds)
{
    return ceil_div_u64(microseconds, 1000u);
}

void NetImpair_Init(net_impair_model_t *model,
                    const net_impair_config_t *config)
{
    net_impair_config_t normalized = *config;

    normalized.latency_ms =
        clamp_u32(normalized.latency_ms, NET_IMPAIR_MAX_DELAY_MS);
    normalized.jitter_ms =
        clamp_u32(normalized.jitter_ms, NET_IMPAIR_MAX_DELAY_MS);
    normalized.upstream_stall_ms =
        clamp_u32(normalized.upstream_stall_ms, NET_IMPAIR_MAX_DELAY_MS);
    normalized.rate_bytes_per_sec =
        clamp_u32(normalized.rate_bytes_per_sec, NET_IMPAIR_MAX_RATE);
    normalized.loss_basis_points =
        clamp_u16(normalized.loss_basis_points, NET_IMPAIR_PERCENT_SCALE);
    normalized.burst_start_basis_points =
        clamp_u16(normalized.burst_start_basis_points,
                  NET_IMPAIR_PERCENT_SCALE);
    normalized.reorder_basis_points =
        clamp_u16(normalized.reorder_basis_points, NET_IMPAIR_PERCENT_SCALE);
    normalized.duplicate_basis_points =
        clamp_u16(normalized.duplicate_basis_points, NET_IMPAIR_PERCENT_SCALE);
    normalized.corrupt_basis_points =
        clamp_u16(normalized.corrupt_basis_points, NET_IMPAIR_PERCENT_SCALE);
    normalized.burst_length =
        clamp_u16(normalized.burst_length, NET_IMPAIR_MAX_BURST);

    memset(model, 0, sizeof(*model));
    model->config = normalized;
    model->rng_state =
        (UINT64_C(0x853c49e6748fea9b) ^ normalized.seed) +
        UINT64_C(0xda3e39cb94b95bdb);

    // Warm the generator so adjacent small seeds do not share their first
    // decision prefix.
    (void)next_random(model);
}

net_impair_decision_t NetImpair_Decide(net_impair_model_t *model,
                                       uint64_t now_ms,
                                       size_t packet_bytes,
                                       net_impair_packet_flags_t flags)
{
    net_impair_decision_t decision;
    const net_impair_config_t *config = &model->config;

    memset(&decision, 0, sizeof(decision));

    model->packets_seen++;

    if (model->burst_remaining) {
        model->burst_remaining--;
        decision.drop = true;
        decision.burst_drop = true;
    } else if (random_chance(model, config->loss_basis_points)) {
        decision.drop = true;
    } else if (random_chance(model, config->burst_start_basis_points)) {
        decision.drop = true;
        decision.burst_drop = true;
        if (config->burst_length > 1)
            model->burst_remaining = config->burst_length - 1u;
    }

    if (decision.drop) {
        model->packets_dropped++;
        if (decision.burst_drop)
            model->packets_burst_dropped++;
        return decision;
    }

    decision.copies = 1;
    uint64_t release_ms = add_saturated(now_ms, config->latency_ms);

    if (config->jitter_ms) {
        const uint32_t span = config->jitter_ms * 2u + 1u;
        const int64_t offset =
            (int64_t)bounded_random(model, span) -
            (int64_t)config->jitter_ms;
        if (offset < 0) {
            const uint64_t magnitude = (uint64_t)(-offset);
            release_ms = release_ms > magnitude ? release_ms - magnitude : now_ms;
            if (release_ms < now_ms)
                release_ms = now_ms;
        } else {
            release_ms = add_saturated(release_ms, (uint64_t)offset);
        }
    }

    if (config->rate_bytes_per_sec) {
        const uint64_t now_us = multiply_saturated(now_ms, 1000u);
        const uint64_t transmit_us =
            model->next_transmit_us > now_us ? model->next_transmit_us : now_us;
        const uint64_t duration_us = ceil_div_u64(
            multiply_saturated((uint64_t)packet_bytes, 1000000u),
            config->rate_bytes_per_sec);
        const uint64_t throttled_release =
            microseconds_to_milliseconds(transmit_us);
        if (throttled_release > release_ms) {
            release_ms = throttled_release;
            model->packets_throttled++;
        }
        model->next_transmit_us = add_saturated(transmit_us, duration_us);
    }

    if (random_chance(model, config->reorder_basis_points)) {
        const uint64_t hold_ms =
            1u + config->jitter_ms + config->latency_ms / 2u;
        release_ms = add_saturated(release_ms, hold_ms);
        decision.reordered = true;
        model->packets_reordered++;
    }

    if ((flags & NET_IMPAIR_PACKET_UPSTREAM_SEQUENCED) &&
        config->upstream_stall_ms) {
        release_ms = add_saturated(release_ms, config->upstream_stall_ms);
        model->packets_upstream_stalled++;
    }

    decision.release_ms[0] = release_ms;

    if (packet_bytes && random_chance(model, config->corrupt_basis_points)) {
        const uint32_t bounded_size = packet_bytes > UINT32_MAX
                                          ? UINT32_MAX
                                          : (uint32_t)packet_bytes;
        decision.corrupt = true;
        decision.corrupt_offset = bounded_random(model, bounded_size);
        decision.corrupt_xor = (uint8_t)(1u << bounded_random(model, 8u));
        model->packets_corrupted++;
    }

    if (random_chance(model, config->duplicate_basis_points)) {
        uint64_t duplicate_release = add_saturated(release_ms, 1u);
        if (config->rate_bytes_per_sec) {
            const uint64_t transmit_us = model->next_transmit_us;
            const uint64_t duration_us = ceil_div_u64(
                multiply_saturated((uint64_t)packet_bytes, 1000000u),
                config->rate_bytes_per_sec);
            const uint64_t throttled_release =
                microseconds_to_milliseconds(transmit_us);
            if (throttled_release > duplicate_release) {
                duplicate_release = throttled_release;
                model->packets_throttled++;
            }
            model->next_transmit_us = add_saturated(transmit_us, duration_us);
        }
        decision.copies = 2;
        decision.release_ms[1] = duplicate_release;
        model->packets_duplicated++;
    }

    return decision;
}

bool NetImpair_ConfigEqual(const net_impair_config_t *a,
                           const net_impair_config_t *b)
{
    return a->seed == b->seed &&
           a->latency_ms == b->latency_ms &&
           a->jitter_ms == b->jitter_ms &&
           a->upstream_stall_ms == b->upstream_stall_ms &&
           a->rate_bytes_per_sec == b->rate_bytes_per_sec &&
           a->loss_basis_points == b->loss_basis_points &&
           a->burst_start_basis_points == b->burst_start_basis_points &&
           a->reorder_basis_points == b->reorder_basis_points &&
           a->duplicate_basis_points == b->duplicate_basis_points &&
           a->corrupt_basis_points == b->corrupt_basis_points &&
           a->burst_length == b->burst_length;
}
