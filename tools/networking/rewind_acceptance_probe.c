/*
Copyright (C) 2026 WORR contributors

Deterministic player-bounds rewind acceptance probe.  This executable calls
the same pointer-free policy, history, frozen-scene, and observation journal
APIs linked into sgame.  It intentionally does not emulate gi.trace or damage;
the checked-in evidence manifest records that remaining live-engine gap.
*/

#include "common/net/rewind.h"
#include "common/net/rewind_observation.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct probe_options_s {
  const char *scenario;
  const char *weapon_name;
  uint32_t weapon_id;
  uint32_t latency_ms;
} probe_options;

static bool parse_u32(const char *text, uint32_t *value_out) {
  char *end = NULL;
  unsigned long value;
  if (!text || !value_out || text[0] == '\0')
    return false;
  value = strtoul(text, &end, 10);
  if (!end || *end != '\0' || value > UINT32_MAX)
    return false;
  *value_out = (uint32_t)value;
  return true;
}

static bool token_valid(const char *text) {
  const unsigned char *cursor = (const unsigned char *)text;
  if (!cursor || *cursor == 0)
    return false;
  for (; *cursor != 0; ++cursor) {
    if (!((*cursor >= (unsigned char)'a' &&
           *cursor <= (unsigned char)'z') ||
          (*cursor >= (unsigned char)'0' &&
           *cursor <= (unsigned char)'9') ||
          *cursor == (unsigned char)'-' || *cursor == (unsigned char)'_')) {
      return false;
    }
  }
  return true;
}

static bool scenario_valid(const char *scenario) {
  static const char *const scenarios[] = {
      "normal",       "stale",      "future",
      "cap",          "history_miss", "teleport",
      "death_respawn", "slot_reuse", "disabled",
  };
  size_t index;
  for (index = 0; index < sizeof(scenarios) / sizeof(scenarios[0]); ++index) {
    if (strcmp(scenario, scenarios[index]) == 0)
      return true;
  }
  return false;
}

static bool parse_options(int argc, char **argv, probe_options *options) {
  int i;
  if (!options)
    return false;
  memset(options, 0, sizeof(*options));
  options->scenario = "normal";
  options->weapon_name = "unspecified";
  for (i = 1; i < argc; ++i) {
    if (i + 1 >= argc)
      return false;
    if (strcmp(argv[i], "--scenario") == 0)
      options->scenario = argv[++i];
    else if (strcmp(argv[i], "--weapon-id") == 0) {
      if (!parse_u32(argv[++i], &options->weapon_id))
        return false;
    } else if (strcmp(argv[i], "--weapon-name") == 0)
      options->weapon_name = argv[++i];
    else if (strcmp(argv[i], "--latency-ms") == 0) {
      if (!parse_u32(argv[++i], &options->latency_ms))
        return false;
    } else
      return false;
  }
  return options->weapon_id < WORR_REWIND_WEAPON_POLICY_COUNT &&
         options->latency_ms <= 1000 && token_valid(options->weapon_name) &&
         scenario_valid(options->scenario);
}

static worr_command_record_v1 make_command(uint64_t source_time_us) {
  worr_command_record_v1 command;
  memset(&command, 0, sizeof(command));
  command.struct_size = sizeof(command);
  command.schema_version = WORR_COMMAND_ABI_VERSION;
  command.command_id.epoch = 1;
  command.command_id.sequence = 1;
  command.sample_time_us = UINT64_C(16000);
  command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  command.command.struct_size = sizeof(command.command);
  command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  command.command.duration_ms = 16;
  command.render_watermark.struct_size = sizeof(command.render_watermark);
  command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  command.render_watermark.provenance =
      WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  command.render_watermark.source_server_tick =
      (uint32_t)(source_time_us / UINT64_C(10000));
  command.render_watermark.tick_interval_us = 10000;
  command.render_watermark.source_server_time_us = source_time_us;
  command.render_watermark.rendered_server_time_us = source_time_us;
  return command;
}

static worr_rewind_snapshot_time_v1 make_snapshot(void) {
  worr_rewind_snapshot_time_v1 snapshot;
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.struct_size = sizeof(snapshot);
  snapshot.schema_version = WORR_REWIND_ABI_VERSION;
  snapshot.tick_interval_us = 10000;
  snapshot.snapshot_id.epoch = 1;
  snapshot.snapshot_id.sequence = 2;
  snapshot.server_tick = 100;
  snapshot.server_time_us = UINT64_C(1000000);
  snapshot.consumed_command.cursor.epoch = 1;
  snapshot.consumed_command.cursor.contiguous_sequence = 1;
  snapshot.consumed_command.provenance =
      WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
  return snapshot;
}

static worr_rewind_mapping_proof_v1
make_proof(const worr_command_record_v1 *command,
           const worr_rewind_snapshot_time_v1 *snapshot) {
  worr_rewind_mapping_proof_v1 proof;
  memset(&proof, 0, sizeof(proof));
  proof.struct_size = sizeof(proof);
  proof.schema_version = WORR_REWIND_ABI_VERSION;
  proof.flags = WORR_REWIND_MAPPING_AUTHENTICATED_TIMELINE;
  proof.command_id = command->command_id;
  proof.source_snapshot_id.epoch = snapshot->snapshot_id.epoch;
  proof.source_snapshot_id.sequence =
      command->render_watermark.source_server_tick == snapshot->server_tick
          ? snapshot->snapshot_id.sequence
          : snapshot->snapshot_id.sequence - 1u;
  proof.source_server_tick = command->render_watermark.source_server_tick;
  proof.tick_interval_us = command->render_watermark.tick_interval_us;
  proof.watermark_provenance = command->render_watermark.provenance;
  proof.watermark_flags = command->render_watermark.flags;
  proof.source_server_time_us =
      command->render_watermark.source_server_time_us;
  proof.rendered_server_time_us =
      command->render_watermark.rendered_server_time_us;
  proof.mapped_server_time_us =
      command->render_watermark.rendered_server_time_us;
  return proof;
}

static worr_rewind_pose_v1 make_pose(uint32_t generation, uint32_t tick,
                                     uint64_t time_us, float x) {
  worr_rewind_pose_v1 pose;
  unsigned int axis;
  memset(&pose, 0, sizeof(pose));
  pose.struct_size = sizeof(pose);
  pose.schema_version = WORR_REWIND_ABI_VERSION;
  pose.model_revision = WORR_REWIND_POSE_MODEL_REVISION;
  pose.flags = WORR_REWIND_POSE_LINKED | WORR_REWIND_POSE_DAMAGEABLE;
  pose.entity.index = 2;
  pose.entity.generation = generation;
  pose.map_epoch = 1;
  pose.server_tick = tick;
  pose.lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
  pose.solid = 1;
  pose.clip_flags = 1;
  pose.collision_shape = WORR_REWIND_COLLISION_BOUNDS;
  pose.server_time_us = time_us;
  pose.origin[0] = x;
  for (axis = 0; axis < 3; ++axis) {
    pose.mins[axis] = -16.0f;
    pose.maxs[axis] = 16.0f;
  }
  return pose;
}

static bool append_pose(worr_rewind_history_v1 *history,
                        const worr_rewind_pose_v1 *pose) {
  uint32_t reason = WORR_REWIND_APPEND_REJECT_INVALID;
  return Worr_RewindHistoryAppendV1(history, pose, &reason) &&
         reason == WORR_REWIND_APPEND_ACCEPTED;
}

static void copy_decision(const worr_rewind_policy_decision_v1 *decision,
                          worr_rewind_observation_v1 *observation) {
  observation->policy_reason = decision->reason;
  observation->command_id = decision->command_id;
  observation->snapshot_id = decision->snapshot_id;
  observation->source_snapshot_id = decision->source_snapshot_id;
  observation->requested_time_us = decision->requested_time_us;
  observation->mapped_time_us = decision->mapped_time_us;
  observation->applied_time_us = decision->applied_time_us;
  observation->mapping_error_bound_us = decision->mapping_error_bound_us;
}

int main(int argc, char **argv) {
  probe_options options;
  worr_rewind_policy_config_v1 config;
  worr_rewind_policy_state_v1 state;
  worr_rewind_policy_decision_v1 decision;
  worr_command_record_v1 command;
  worr_rewind_snapshot_time_v1 snapshot;
  worr_rewind_mapping_proof_v1 proof;
  worr_rewind_history_config_v1 history_config;
  worr_rewind_pose_v1 history_storage[8];
  worr_rewind_history_v1 history;
  worr_rewind_pose_query_v1 query;
  worr_rewind_pose_result_v1 result;
  worr_rewind_scene_candidate_v1 scene_storage[2];
  worr_rewind_scene_v1 scene;
  worr_rewind_observation_v1 observation;
  worr_rewind_observation_v1 journal_storage[2];
  worr_rewind_observation_journal_v1 journal;
  worr_rewind_observation_v1 copied[2];
  worr_rewind_pose_v1 authoritative_pose;
  uint64_t authoritative_before = 0;
  uint64_t authoritative_after = 0;
  uint64_t semantic_hash = 0;
  uint64_t sequence = 0;
  uint64_t source_time_us;
  uint32_t append_tick = 1;
  uint32_t copied_count = 0;
  bool policy_evaluated = false;
  bool policy_accepted = false;
  bool query_evaluated = false;
  bool scene_sealed = false;
  bool ok = true;

  if (!parse_options(argc, argv, &options)) {
    fprintf(stderr,
            "usage: %s --scenario NAME --weapon-id N --weapon-name NAME "
            "--latency-ms N\n",
            argv[0]);
    return 2;
  }

  source_time_us = UINT64_C(1000000) -
                   (uint64_t)options.latency_ms * UINT64_C(1000);
  if (strcmp(options.scenario, "stale") == 0)
    source_time_us = UINT64_C(740000);
  else if (strcmp(options.scenario, "cap") == 0)
    source_time_us = UINT64_C(790000);
  else if (strcmp(options.scenario, "future") == 0)
    source_time_us = UINT64_C(1000000);

  command = make_command(source_time_us);
  snapshot = make_snapshot();
  if (strcmp(options.scenario, "future") == 0) {
    command.render_watermark.flags = WORR_COMMAND_RENDER_EXTRAPOLATED;
    command.render_watermark.rendered_server_time_us = UINT64_C(1030000);
  }
  proof = make_proof(&command, &snapshot);

  Worr_RewindPolicyConfigDefaultsV1(&config);
  ok = Worr_RewindPolicyStateInitV1(&state) && ok;
  memset(&decision, 0, sizeof(decision));
  memset(&result, 0, sizeof(result));

  authoritative_pose = make_pose(1, 100, UINT64_C(1000000), 100.0f);
  ok = Worr_RewindPoseHashV1(&authoritative_pose, &authoritative_before) && ok;
  ok = Worr_RewindObservationInitV1(&observation, options.weapon_id) && ok;
  observation.flags = WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED |
                      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED;
  observation.authoritative_hash_before = authoritative_before;

  if (strcmp(options.scenario, "disabled") == 0) {
    observation.path = WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD;
    observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_MISS;
    observation.fallback_reason =
        WORR_REWIND_OBSERVATION_FALLBACK_MASTER_DISABLED;
    observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
  } else {
    policy_evaluated = Worr_RewindPolicyResolveV1(
        &state, &config, &command, &snapshot, &proof, &decision);
    ok = policy_evaluated && ok;
    policy_accepted =
        policy_evaluated &&
        (decision.flags & WORR_REWIND_DECISION_ACCEPTED) != 0;
    observation.path = WORR_REWIND_OBSERVATION_PATH_CANONICAL;
    observation.flags |= WORR_REWIND_OBSERVATION_MASTER_ENABLED |
                         WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT;
    copy_decision(&decision, &observation);

    if (!policy_accepted) {
      observation.outcome =
          WORR_REWIND_OBSERVATION_OUTCOME_POLICY_REJECTED;
      observation.fallback_reason =
          WORR_REWIND_OBSERVATION_FALLBACK_POLICY_REJECTED;
      observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
    } else {
      worr_rewind_pose_v1 older;
      worr_rewind_pose_v1 newer;
      observation.flags |= WORR_REWIND_OBSERVATION_POLICY_ACCEPTED |
                           WORR_REWIND_OBSERVATION_HISTORICAL_QUERY;
      Worr_RewindHistoryConfigDefaultsV1(&history_config);
      ok = Worr_RewindHistoryInitV1(&history, history_storage, 8, 2,
                                    &history_config) &&
           ok;

      if (strcmp(options.scenario, "history_miss") != 0 &&
          strcmp(options.scenario, "slot_reuse") != 0 &&
          strcmp(options.scenario, "death_respawn") != 0) {
        if (options.latency_ms == 0 &&
            strcmp(options.scenario, "cap") != 0 &&
            strcmp(options.scenario, "teleport") != 0) {
          older = make_pose(1, append_tick++, decision.applied_time_us, 10.0f);
          ok = append_pose(&history, &older) && ok;
        } else {
          older = make_pose(1, append_tick++,
                            decision.applied_time_us - UINT64_C(10000), 0.0f);
          newer = make_pose(1, append_tick++,
                            decision.applied_time_us + UINT64_C(10000), 20.0f);
          if (strcmp(options.scenario, "teleport") == 0) {
            newer.flags |= WORR_REWIND_POSE_DISCONTINUITY_TELEPORT;
            newer.origin[0] = 1000.0f;
          }
          ok = append_pose(&history, &older) && ok;
          ok = append_pose(&history, &newer) && ok;
        }
      } else if (strcmp(options.scenario, "slot_reuse") == 0) {
        older = make_pose(1, append_tick++,
                          decision.applied_time_us - UINT64_C(10000), 0.0f);
        newer = make_pose(1, append_tick++,
                          decision.applied_time_us + UINT64_C(10000), 20.0f);
        ok = append_pose(&history, &older) && ok;
        ok = append_pose(&history, &newer) && ok;
      } else if (strcmp(options.scenario, "death_respawn") == 0) {
        older = make_pose(1, append_tick++,
                          decision.applied_time_us - UINT64_C(20000), 0.0f);
        newer = make_pose(1, append_tick++,
                          decision.applied_time_us - UINT64_C(10000), 1.0f);
        newer.lifecycle = WORR_REWIND_LIFECYCLE_DEAD;
        newer.flags &= ~((uint32_t)WORR_REWIND_POSE_DAMAGEABLE);
        ok = append_pose(&history, &older) && ok;
        ok = append_pose(&history, &newer) && ok;
        newer = make_pose(2, append_tick++,
                          decision.applied_time_us + UINT64_C(10000), 50.0f);
        ok = append_pose(&history, &newer) && ok;
      }

      memset(&query, 0, sizeof(query));
      query.struct_size = sizeof(query);
      query.schema_version = WORR_REWIND_ABI_VERSION;
      query.entity.index = 2;
      query.entity.generation =
          strcmp(options.scenario, "slot_reuse") == 0 ||
                  strcmp(options.scenario, "death_respawn") == 0
              ? 2u
              : 1u;
      query.map_epoch = 1;
      query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
      query.target_time_us = decision.applied_time_us;
      memset(&result, 0, sizeof(result));
      query_evaluated = Worr_RewindHistoryQueryV1(&history, &query, &result);
      ok = query_evaluated && ok;
      observation.query_reason = result.reason;

      if (query_evaluated && result.found) {
        ok = Worr_RewindSceneInitV1(&scene, scene_storage, 2, &decision) && ok;
        ok = Worr_RewindSceneAddResultV1(&scene, &result) && ok;
        scene_sealed = Worr_RewindSceneSealV1(&scene);
        ok = scene_sealed && ok;
        if (scene_sealed) {
          observation.flags |= WORR_REWIND_OBSERVATION_HISTORICAL_SCENE;
          observation.candidate_count = scene.count;
          observation.scene_hash = scene.scene_hash;
          observation.outcome =
              WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT;
          observation.hit_entity = result.pose.entity;
          observation.trace_fraction = 0.5f;
        }
      } else {
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_HISTORY_MISS;
        observation.fallback_reason =
            WORR_REWIND_OBSERVATION_FALLBACK_HISTORY_MISS;
        observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
      }
    }
  }

  ok = Worr_RewindPoseHashV1(&authoritative_pose, &authoritative_after) && ok;
  observation.authoritative_hash_after = authoritative_after;
  ok = authoritative_before == authoritative_after && ok;
  ok = Worr_RewindObservationValidateV1(&observation) && ok;
  ok = Worr_RewindObservationHashV1(&observation, &semantic_hash) && ok;
  ok = Worr_RewindObservationJournalInitV1(&journal, journal_storage, 2) && ok;
  ok = Worr_RewindObservationJournalAppendV1(&journal, &observation,
                                             &sequence) &&
       ok;
  ok = Worr_RewindObservationJournalCopyV1(&journal, copied, 2,
                                           &copied_count) &&
       ok;
  ok = copied_count == 1 && sequence == 1 && ok;

  printf(
      "{\"schema\":\"worr.networking.rewind-acceptance-probe.v1\","
      "\"pass\":%s,\"scenario\":\"%s\",\"weapon_id\":%u,"
      "\"weapon_name\":\"%s\",\"latency_ms\":%u,"
      "\"policy_evaluated\":%s,\"policy_accepted\":%s,"
      "\"policy_reason\":%u,\"requested_time_us\":%llu,"
      "\"mapped_time_us\":%llu,\"applied_time_us\":%llu,"
      "\"query_evaluated\":%s,\"query_found\":%s,"
      "\"query_reason\":%u,\"scene_sealed\":%s,"
      "\"candidate_count\":%u,\"scene_hash\":\"%016llx\","
      "\"path\":%u,\"outcome\":%u,\"fallback_reason\":%u,"
      "\"observation_flags\":%u,\"semantic_observation_hash\":"
      "\"%016llx\",\"journal_count\":%u,"
      "\"authoritative_hash_before\":\"%016llx\","
      "\"authoritative_hash_after\":\"%016llx\","
      "\"authoritative_unchanged\":%s}\n",
      ok ? "true" : "false", options.scenario, options.weapon_id,
      options.weapon_name, options.latency_ms,
      policy_evaluated ? "true" : "false",
      policy_accepted ? "true" : "false", decision.reason,
      (unsigned long long)observation.requested_time_us,
      (unsigned long long)observation.mapped_time_us,
      (unsigned long long)observation.applied_time_us,
      query_evaluated ? "true" : "false",
      query_evaluated && result.found ? "true" : "false", result.reason,
      scene_sealed ? "true" : "false", observation.candidate_count,
      (unsigned long long)observation.scene_hash, observation.path,
      observation.outcome, observation.fallback_reason, observation.flags,
      (unsigned long long)semantic_hash, copied_count,
      (unsigned long long)authoritative_before,
      (unsigned long long)authoritative_after,
      authoritative_before == authoritative_after ? "true" : "false");
  return ok ? 0 : 1;
}
