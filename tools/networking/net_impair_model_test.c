/* Deterministic unit/scenario runner for the production impairment model. */

#include "common/net/impair.h"
#include "common/net/impair_queue.h"
#include "common/net/sequence.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                     \
                    __FILE__, __LINE__, #condition);                            \
            return false;                                                       \
        }                                                                       \
    } while (0)

static bool test_exact_latency(void)
{
    net_impair_config_t config = {.seed = 1, .latency_ms = 50};
    net_impair_model_t model;
    NetImpair_Init(&model, &config);
    const net_impair_decision_t decision =
        NetImpair_Decide(&model, 100, 100, NET_IMPAIR_PACKET_NONE);
    CHECK(!decision.drop);
    CHECK(decision.copies == 1);
    CHECK(decision.release_ms[0] == 150);
    return true;
}

static bool test_loss_and_burst(void)
{
    net_impair_config_t config = {
        .seed = 2,
        .burst_start_basis_points = NET_IMPAIR_PERCENT_SCALE,
        .burst_length = 4,
    };
    net_impair_model_t model;
    NetImpair_Init(&model, &config);
    for (unsigned i = 0; i < 4; ++i) {
        const net_impair_decision_t decision =
            NetImpair_Decide(&model, i, 64, NET_IMPAIR_PACKET_NONE);
        CHECK(decision.drop);
        CHECK(decision.burst_drop);
    }
    CHECK(model.packets_dropped == 4);
    CHECK(model.packets_burst_dropped == 4);

    config.burst_start_basis_points = 0;
    config.loss_basis_points = NET_IMPAIR_PERCENT_SCALE;
    NetImpair_Init(&model, &config);
    CHECK(NetImpair_Decide(&model, 0, 64, NET_IMPAIR_PACKET_NONE).drop);
    return true;
}

static bool test_duplicate_reorder_and_throttle(void)
{
    net_impair_config_t config = {
        .seed = 3,
        .latency_ms = 20,
        .rate_bytes_per_sec = 1000,
        .reorder_basis_points = NET_IMPAIR_PERCENT_SCALE,
        .duplicate_basis_points = NET_IMPAIR_PERCENT_SCALE,
    };
    net_impair_model_t model;
    NetImpair_Init(&model, &config);
    const net_impair_decision_t first =
        NetImpair_Decide(&model, 0, 1000, NET_IMPAIR_PACKET_NONE);
    CHECK(!first.drop);
    CHECK(first.reordered);
    CHECK(first.copies == 2);
    CHECK(first.release_ms[0] >= 31);
    CHECK(first.release_ms[1] >= 1000);
    CHECK(model.packets_reordered == 1);
    CHECK(model.packets_duplicated == 1);

    config.reorder_basis_points = 0;
    config.duplicate_basis_points = 0;
    config.latency_ms = 0;
    NetImpair_Init(&model, &config);
    const net_impair_decision_t a =
        NetImpair_Decide(&model, 0, 1000, NET_IMPAIR_PACKET_NONE);
    const net_impair_decision_t b =
        NetImpair_Decide(&model, 0, 1000, NET_IMPAIR_PACKET_NONE);
    CHECK(a.release_ms[0] == 0);
    CHECK(b.release_ms[0] == 1000);
    return true;
}

static bool test_jitter_bounds(void)
{
    net_impair_config_t config = {
        .seed = 4,
        .latency_ms = 40,
        .jitter_ms = 20,
    };
    net_impair_model_t model;
    NetImpair_Init(&model, &config);
    for (unsigned i = 0; i < 1000; ++i) {
        const uint64_t now = i * 10u;
        const net_impair_decision_t decision =
            NetImpair_Decide(&model, now, 100, NET_IMPAIR_PACKET_NONE);
        CHECK(decision.release_ms[0] >= now + 20u);
        CHECK(decision.release_ms[0] <= now + 60u);
    }
    return true;
}

static bool test_repeatability(void)
{
    const net_impair_config_t config = {
        .seed = 0x12345678u,
        .latency_ms = 75,
        .jitter_ms = 30,
        .rate_bytes_per_sec = 32000,
        .loss_basis_points = 700,
        .burst_start_basis_points = 250,
        .reorder_basis_points = 500,
        .duplicate_basis_points = 300,
        .burst_length = 3,
    };
    net_impair_model_t a, b;
    NetImpair_Init(&a, &config);
    NetImpair_Init(&b, &config);

    for (unsigned i = 0; i < 10000; ++i) {
        const uint64_t now = (uint64_t)i * 7u;
        const size_t bytes = 32u + (i * 131u) % 1368u;
        const net_impair_decision_t da =
            NetImpair_Decide(&a, now, bytes, NET_IMPAIR_PACKET_NONE);
        const net_impair_decision_t db =
            NetImpair_Decide(&b, now, bytes, NET_IMPAIR_PACKET_NONE);
        CHECK(memcmp(&da, &db, sizeof(da)) == 0);
    }
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    return true;
}

static bool test_corruption_and_upstream_stall(void)
{
    net_impair_config_t config = {
        .seed = 5,
        .upstream_stall_ms = 125,
        .corrupt_basis_points = NET_IMPAIR_PERCENT_SCALE,
    };
    net_impair_model_t model;
    NetImpair_Init(&model, &config);
    const net_impair_decision_t decision = NetImpair_Decide(
        &model, 10, 128, NET_IMPAIR_PACKET_UPSTREAM_SEQUENCED);
    CHECK(!decision.drop);
    CHECK(decision.release_ms[0] == 135);
    CHECK(decision.corrupt);
    CHECK(decision.corrupt_offset < 128);
    CHECK(decision.corrupt_xor != 0);
    CHECK(model.packets_corrupted == 1);
    CHECK(model.packets_upstream_stalled == 1);

    NetImpair_Init(&model, &config);
    const net_impair_decision_t one_byte = NetImpair_Decide(
        &model, 0, 1, NET_IMPAIR_PACKET_NONE);
    CHECK(one_byte.corrupt);
    CHECK(one_byte.corrupt_offset == 0);

    NetImpair_Init(&model, &config);
    const net_impair_decision_t empty = NetImpair_Decide(
        &model, 0, 0, NET_IMPAIR_PACKET_NONE);
    CHECK(!empty.corrupt);
    return true;
}

static bool test_queue_and_clock(void)
{
    net_impair_queue_t queue;
    uint16_t ids[4];
    NetImpairQueue_Init(&queue);
    CHECK(NetImpairQueue_Reserve(&queue, 4, 30, &ids[0]));
    CHECK(NetImpairQueue_Reserve(&queue, 4, 10, &ids[1]));
    CHECK(NetImpairQueue_Reserve(&queue, 4, 20, &ids[2]));
    CHECK(NetImpairQueue_Reserve(&queue, 4, 20, &ids[3]));
    CHECK(!NetImpairQueue_Reserve(&queue, 4, 5, &ids[0]));
    CHECK(queue.overflow_count == 1);
    CHECK(queue.high_water == 4);

    const uint16_t expected[] = {ids[1], ids[2], ids[3], ids[0]};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        uint16_t id;
        uint64_t release_ms;
        CHECK(NetImpairQueue_Peek(&queue, &id, &release_ms));
        CHECK(id == expected[i]);
        CHECK(NetImpairQueue_Pop(&queue, &id));
        CHECK(id == expected[i]);
        NetImpairQueue_Release(&queue, id);
    }
    CHECK(queue.heap_count == 0);
    CHECK(queue.free_count == NET_IMPAIR_QUEUE_CAPACITY);
    CHECK(!NetImpairQueue_Pop(&queue, &ids[0]));

    uint16_t reused;
    CHECK(NetImpairQueue_Reserve(&queue, 1, 1, &reused));
    CHECK(NetImpairQueue_Pop(&queue, &reused));
    NetImpairQueue_Release(&queue, reused);

    net_impair_clock_t clock;
    memset(&clock, 0, sizeof(clock));
    CHECK(NetImpairClock_Extend(&clock, UINT32_MAX - 4u) ==
          UINT32_MAX - 4u);
    CHECK(NetImpairClock_Extend(&clock, 7u) ==
          (UINT64_C(1) << 32u) + 7u);
    CHECK(NetImpairClock_Extend(&clock, 8u) ==
          (UINT64_C(1) << 32u) + 8u);
    return true;
}

typedef struct {
    bool accepted_ack;
    bool stale_ack_rejected;
    bool future_ack_rejected;
    bool reliable_ack_conflict_rejected;
    bool sequence_wrap_fail_closed;
} sequence_evidence_t;

static sequence_evidence_t collect_sequence_evidence(void)
{
    sequence_evidence_t evidence;
    memset(&evidence, 0, sizeof(evidence));

    evidence.accepted_ack =
        NetSequence_ClassifyAck(41u, 40u, 42u) ==
        NET_SEQUENCE_ACK_ACCEPTED;
    evidence.stale_ack_rejected =
        NetSequence_ClassifyAck(39u, 40u, 42u) == NET_SEQUENCE_ACK_STALE;
    evidence.future_ack_rejected =
        NetSequence_ClassifyAck(42u, 40u, 42u) == NET_SEQUENCE_ACK_FUTURE;
    evidence.reliable_ack_conflict_rejected =
        !NetSequence_ReliableAckConsistent(40u, true, 40u, false) &&
        NetSequence_ReliableAckConsistent(41u, true, 40u, false);

    const uint32_t old_end = UINT32_C(1) << 31u;
    const uint32_t new_end = UINT32_C(1) << 30u;
    evidence.sequence_wrap_fail_closed =
        NetSequence_IsNewer(old_end - 1u, old_end - 2u) &&
        !NetSequence_IsNewer(0u, old_end - 1u) &&
        !NetSequence_IsNewer(0u, new_end - 1u) &&
        !NetSequence_NearExhaustion(old_end - 257u, 31u, 256u) &&
        NetSequence_NearExhaustion(old_end - 256u, 31u, 256u) &&
        !NetSequence_NearExhaustion(new_end - 257u, 30u, 256u) &&
        NetSequence_NearExhaustion(new_end - 256u, 30u, 256u);
    return evidence;
}

static bool test_sequence_and_ack_validation(void)
{
    const sequence_evidence_t evidence = collect_sequence_evidence();
    CHECK(evidence.accepted_ack);
    CHECK(evidence.stale_ack_rejected);
    CHECK(evidence.future_ack_rejected);
    CHECK(evidence.reliable_ack_conflict_rejected);
    CHECK(evidence.sequence_wrap_fail_closed);
    return true;
}

static bool run_tests(void)
{
    return test_exact_latency() &&
           test_loss_and_burst() &&
           test_duplicate_reorder_and_throttle() &&
           test_jitter_bounds() &&
           test_repeatability() &&
           test_corruption_and_upstream_stall() &&
           test_queue_and_clock() &&
           test_sequence_and_ack_validation();
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) {
        hash ^= (uint8_t)(value >> (i * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

typedef struct {
    net_impair_config_t config;
    uint32_t packets;
    uint32_t step_ms;
    uint32_t upstream_every;
} report_profile_t;

static report_profile_t default_report_profile(void)
{
    report_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.config.seed = 0x57524f52u;
    profile.config.burst_length = 3;
    profile.packets = 10000;
    profile.step_ms = 10;
    profile.upstream_every = 3;
    return profile;
}

static bool parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    if (!text || !*text || *text == '-' || *text == '+')
        return false;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    if (errno || !end || *end || parsed > UINT32_MAX)
        return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_u16(const char *text, uint16_t *value)
{
    uint32_t parsed;
    if (!parse_u32(text, &parsed) || parsed > UINT16_MAX)
        return false;
    *value = (uint16_t)parsed;
    return true;
}

static bool parse_profile_arg(report_profile_t *profile,
                              const char *name, const char *value)
{
    if (!strcmp(name, "--seed"))
        return parse_u32(value, &profile->config.seed);
    if (!strcmp(name, "--packets"))
        return parse_u32(value, &profile->packets) &&
               profile->packets > 0 && profile->packets <= 1000000u;
    if (!strcmp(name, "--step-ms"))
        return parse_u32(value, &profile->step_ms) &&
               profile->step_ms <= 60000u;
    if (!strcmp(name, "--upstream-every"))
        return parse_u32(value, &profile->upstream_every);
    if (!strcmp(name, "--latency-ms"))
        return parse_u32(value, &profile->config.latency_ms);
    if (!strcmp(name, "--jitter-ms"))
        return parse_u32(value, &profile->config.jitter_ms);
    if (!strcmp(name, "--upstream-stall-ms"))
        return parse_u32(value, &profile->config.upstream_stall_ms);
    if (!strcmp(name, "--rate-bytes-per-sec"))
        return parse_u32(value, &profile->config.rate_bytes_per_sec);
    if (!strcmp(name, "--loss-bp"))
        return parse_u16(value, &profile->config.loss_basis_points);
    if (!strcmp(name, "--burst-bp"))
        return parse_u16(value, &profile->config.burst_start_basis_points);
    if (!strcmp(name, "--burst-length"))
        return parse_u16(value, &profile->config.burst_length);
    if (!strcmp(name, "--reorder-bp"))
        return parse_u16(value, &profile->config.reorder_basis_points);
    if (!strcmp(name, "--duplicate-bp"))
        return parse_u16(value, &profile->config.duplicate_basis_points);
    if (!strcmp(name, "--corrupt-bp"))
        return parse_u16(value, &profile->config.corrupt_basis_points);
    return false;
}

static void print_json_report(const report_profile_t *profile)
{
    net_impair_model_t model;
    const sequence_evidence_t sequence = collect_sequence_evidence();
    uint64_t digest = UINT64_C(1469598103934665603);
    uint64_t delay_min = UINT64_MAX;
    uint64_t delay_max = 0;
    uint64_t delay_sum = 0;
    uint64_t delivered = 0;
    uint64_t scheduled_copies = 0;
    uint64_t release_inversions = 0;
    uint64_t previous_primary_release = 0;
    bool have_previous_primary_release = false;
    uint64_t upstream_packets = 0;
    NetImpair_Init(&model, &profile->config);

    for (uint32_t i = 0; i < profile->packets; ++i) {
        const uint64_t now = (uint64_t)i * profile->step_ms;
        const size_t bytes = 64u + (i * 193u) % 1200u;
        const bool is_upstream = profile->upstream_every &&
                                 (i % profile->upstream_every) == 0;
        if (is_upstream)
            upstream_packets++;
        const net_impair_decision_t decision =
            NetImpair_Decide(&model, now, bytes,
                             is_upstream
                                 ? NET_IMPAIR_PACKET_UPSTREAM_SEQUENCED
                                 : NET_IMPAIR_PACKET_NONE);
        digest = hash_u64(digest, decision.drop);
        digest = hash_u64(digest, decision.burst_drop);
        digest = hash_u64(digest, decision.reordered);
        digest = hash_u64(digest, decision.corrupt);
        digest = hash_u64(digest, decision.copies);
        digest = hash_u64(digest, decision.release_ms[0]);
        digest = hash_u64(digest, decision.release_ms[1]);
        digest = hash_u64(digest, decision.corrupt_offset);
        digest = hash_u64(digest, decision.corrupt_xor);
        if (!decision.drop) {
            const uint64_t delay = decision.release_ms[0] - now;
            scheduled_copies += decision.copies;
            if (have_previous_primary_release &&
                decision.release_ms[0] < previous_primary_release) {
                release_inversions++;
            }
            previous_primary_release = decision.release_ms[0];
            have_previous_primary_release = true;
            if (delay < delay_min)
                delay_min = delay;
            if (delay > delay_max)
                delay_max = delay;
            delay_sum += delay;
            delivered++;
        }
    }

    if (!delivered)
        delay_min = 0;

    printf("{\n");
    printf("  \"schema\": \"worr.networking.impairment-baseline.v1\",\n");
    printf("  \"seed\": %u,\n", profile->config.seed);
    printf("  \"packets\": %u,\n", profile->packets);
    printf("  \"step_ms\": %u,\n", profile->step_ms);
    printf("  \"upstream_every\": %u,\n", profile->upstream_every);
    printf("  \"config\": {\n");
    printf("    \"latency_ms\": %u,\n", profile->config.latency_ms);
    printf("    \"jitter_ms\": %u,\n", profile->config.jitter_ms);
    printf("    \"upstream_stall_ms\": %u,\n",
           profile->config.upstream_stall_ms);
    printf("    \"rate_bytes_per_sec\": %u,\n",
           profile->config.rate_bytes_per_sec);
    printf("    \"loss_basis_points\": %u,\n",
           profile->config.loss_basis_points);
    printf("    \"burst_start_basis_points\": %u,\n",
           profile->config.burst_start_basis_points);
    printf("    \"burst_length\": %u,\n", profile->config.burst_length);
    printf("    \"reorder_basis_points\": %u,\n",
           profile->config.reorder_basis_points);
    printf("    \"duplicate_basis_points\": %u,\n",
           profile->config.duplicate_basis_points);
    printf("    \"corrupt_basis_points\": %u\n",
           profile->config.corrupt_basis_points);
    printf("  },\n");
    printf("  \"delivered\": %" PRIu64 ",\n", delivered);
    printf("  \"scheduled_copies\": %" PRIu64 ",\n",
           scheduled_copies);
    printf("  \"release_inversions\": %" PRIu64 ",\n",
           release_inversions);
    printf("  \"upstream_packets\": %" PRIu64 ",\n",
           upstream_packets);
    printf("  \"dropped\": %" PRIu64 ",\n", model.packets_dropped);
    printf("  \"burst_dropped\": %" PRIu64 ",\n",
           model.packets_burst_dropped);
    printf("  \"reordered\": %" PRIu64 ",\n", model.packets_reordered);
    printf("  \"duplicated\": %" PRIu64 ",\n", model.packets_duplicated);
    printf("  \"corrupted\": %" PRIu64 ",\n", model.packets_corrupted);
    printf("  \"upstream_stalled\": %" PRIu64 ",\n",
           model.packets_upstream_stalled);
    printf("  \"throttled\": %" PRIu64 ",\n", model.packets_throttled);
    printf("  \"delay_min_ms\": %" PRIu64 ",\n", delay_min);
    printf("  \"delay_max_ms\": %" PRIu64 ",\n", delay_max);
    printf("  \"delay_mean_ms\": %" PRIu64 ",\n",
           delivered ? delay_sum / delivered : 0);
    printf("  \"sequence_wrap_fail_closed\": %s,\n",
           sequence.sequence_wrap_fail_closed ? "true" : "false");
    printf("  \"stale_ack_rejected\": %s,\n",
           sequence.stale_ack_rejected ? "true" : "false");
    printf("  \"future_ack_rejected\": %s,\n",
           sequence.future_ack_rejected ? "true" : "false");
    printf("  \"reliable_ack_conflict_rejected\": %s,\n",
           sequence.reliable_ack_conflict_rejected ? "true" : "false");
    printf("  \"digest\": \"%016" PRIx64 "\"\n", digest);
    printf("}\n");
}

int main(int argc, char **argv)
{
    report_profile_t profile = default_report_profile();
    bool json = false;

    if (!run_tests())
        return EXIT_FAILURE;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--json")) {
            json = true;
            continue;
        }
        if (i + 1 >= argc ||
            !parse_profile_arg(&profile, argv[i], argv[i + 1])) {
            fprintf(stderr, "invalid argument: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
        i++;
    }

    if (json) {
        print_json_report(&profile);
    } else {
        puts("network impairment model tests passed");
    }
    return EXIT_SUCCESS;
}
