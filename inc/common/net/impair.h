/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Percentages use basis points so the model stays deterministic and does not
// depend on host floating-point behaviour.  10,000 basis points is 100%.
#define NET_IMPAIR_PERCENT_SCALE 10000u

typedef struct {
    uint32_t seed;
    uint32_t latency_ms;
    uint32_t jitter_ms;
    uint32_t upstream_stall_ms;
    uint32_t rate_bytes_per_sec;
    uint16_t loss_basis_points;
    uint16_t burst_start_basis_points;
    uint16_t reorder_basis_points;
    uint16_t duplicate_basis_points;
    uint16_t corrupt_basis_points;
    uint16_t burst_length;
} net_impair_config_t;

typedef enum {
    NET_IMPAIR_PACKET_NONE = 0,
    // A client-to-server sequenced datagram carries both commands and an ack.
    NET_IMPAIR_PACKET_UPSTREAM_SEQUENCED = 1u << 0,
} net_impair_packet_flags_t;

typedef struct {
    net_impair_config_t config;
    uint64_t rng_state;
    uint64_t next_transmit_us;
    uint64_t packets_seen;
    uint64_t packets_dropped;
    uint64_t packets_burst_dropped;
    uint64_t packets_reordered;
    uint64_t packets_duplicated;
    uint64_t packets_corrupted;
    uint64_t packets_upstream_stalled;
    uint64_t packets_throttled;
    uint32_t burst_remaining;
} net_impair_model_t;

typedef struct {
    bool drop;
    bool burst_drop;
    bool reordered;
    bool corrupt;
    uint8_t copies;
    uint8_t corrupt_xor;
    uint32_t corrupt_offset;
    uint64_t release_ms[2];
} net_impair_decision_t;

void NetImpair_Init(net_impair_model_t *model,
                    const net_impair_config_t *config);
net_impair_decision_t NetImpair_Decide(net_impair_model_t *model,
                                       uint64_t now_ms,
                                       size_t packet_bytes,
                                       net_impair_packet_flags_t flags);
bool NetImpair_ConfigEqual(const net_impair_config_t *a,
                           const net_impair_config_t *b);

#ifdef __cplusplus
}
#endif
