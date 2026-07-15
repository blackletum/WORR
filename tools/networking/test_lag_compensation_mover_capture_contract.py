#!/usr/bin/env python3
"""Regression contract for FR-10-T10 live mover-pose capture wiring."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LAG_COMPENSATION = (
    ROOT / "src/game/sgame/network/lag_compensation.cpp"
).read_text(encoding="utf-8")
GAME_MAIN = (ROOT / "src/game/sgame/gameplay/g_main.cpp").read_text(
    encoding="utf-8"
)
IMPORT_BRIDGE = (
    ROOT / "src/game/sgame/network/rewind_collision_import.hpp"
).read_text(encoding="utf-8")
ENGINE_IMPORT = (ROOT / "inc/shared/rewind_collision.h").read_text(
    encoding="utf-8"
)


def function_body(source: str, signature: str, successor: str) -> str:
    start = source.index(signature)
    end = source.index(successor, start)
    return source[start:end]


class LagCompensationMoverCaptureContractTests(unittest.TestCase):
    def test_sgame_bridge_keeps_the_engine_import_prefix_explicit(self) -> None:
        self.assertIn('"WORR_REWIND_COLLISION_IMPORT_V1"', IMPORT_BRIDGE)
        self.assertIn("constexpr uint32_t kApiVersion = 1u", IMPORT_BRIDGE)
        self.assertIn("constexpr uint32_t kSchemaVersion = 1u", IMPORT_BRIDGE)
        self.assertIn("struct Import", IMPORT_BRIDGE)
        self.assertIn("TraceTransformedFn", IMPORT_BRIDGE)
        for assertion in (
            "sizeof(AssetHandle) == 16",
            "sizeof(Map) == 24",
            "sizeof(Asset) == 64",
            "sizeof(TraceRequest) == 104",
            "offsetof(Import, GetMapIdentity) == 8",
            "offsetof(TraceRequest, start) == 32",
        ):
            self.assertIn(assertion, IMPORT_BRIDGE)
        self.assertIn("WORR_REWIND_COLLISION_IMPORT_V1", ENGINE_IMPORT)
        self.assertIn("worr_rewind_collision_import_v1", ENGINE_IMPORT)

    def test_capture_is_bounded_and_resolves_only_live_bsp_movers(self) -> None:
        self.assertIn("constexpr uint32_t kMoverHistoryCapacity = 64", LAG_COMPENSATION)
        self.assertIn("constexpr uint32_t kMoverTrackCapacity = 64", LAG_COMPENSATION)
        eligible = function_body(
            LAG_COMPENSATION, "[[nodiscard]] bool EligibleLiveMover(", "[[nodiscard]] bool CurrentCollisionMap"
        )
        for requirement in (
            "entity->inUse",
            "!entity->client",
            "entity->linked",
            "entity->solid == SOLID_BSP",
            "MoveType::Push",
            "MoveType::Stop",
        ):
            self.assertIn(requirement, eligible)

        build = function_body(
            LAG_COMPENSATION, "[[nodiscard]] bool BuildMoverPose(", "[[nodiscard]] bool PrepareCaptureTime"
        )
        for requirement in (
            "ResolveMoverAsset(entity, map, asset)",
            "WORR_REWIND_COLLISION_BRUSH_MODEL",
            "pose.collision_asset_id = asset.handle.asset_id",
            "Worr_RewindPoseValidateV1(&pose)",
        ):
            self.assertIn(requirement, build)

    def test_player_pose_carries_mover_relative_provenance(self) -> None:
        build = function_body(
            LAG_COMPENSATION, "[[nodiscard]] bool BuildPose(", "[[nodiscard]] bool BuildMoverPose"
        )
        for requirement in (
            "EligibleLiveMover(entity.groundEntity)",
            "WORR_REWIND_POSE_HAS_MOVER",
            "pose.mover = mover",
            "entity.s.origin - entity.groundEntity->s.origin",
            "entity.s.angles - entity.groundEntity->s.angles",
        ):
            self.assertIn(requirement, build)

        absent = function_body(
            LAG_COMPENSATION,
            "[[nodiscard]] worr_event_entity_ref_v1 AbsentEntityRef()",
            "[[nodiscard]] bool EntityRef(",
        )
        self.assertIn("return {};", absent)
        self.assertNotIn("WORR_EVENT_NO_ENTITY", absent)

    def test_movers_are_captured_then_sealed_into_canonical_scenes(self) -> None:
        capture = function_body(
            LAG_COMPENSATION, "void LagCompensation_RecordMovers()", "trace_t LagCompensation_TraceLine"
        )
        for requirement in (
            "PrepareCaptureTime(timeUs)",
            "CurrentCollisionMap(map)",
            "AdoptAuthoritativeMapEpoch(map.map_epoch)",
            "AcquireMoverTrack(identity)",
            "BuildMoverPose(*entity, timeUs, map, pose)",
            "Worr_RewindHistoryAppendV1(&track->history, &pose, &reason)",
        ):
            self.assertIn(requirement, capture)

        scene = function_body(
            LAG_COMPENSATION, "[[nodiscard]] const worr_rewind_scene_v1 *CanonicalScene(", "void MergeTrace"
        )
        self.assertIn("CaptureSceneRoster(eligibleRoster, eligibleRosterCount)", scene)
        self.assertIn("MoverTrack *track = FindMoverTrack(identity)", scene)
        self.assertIn("WORR_REWIND_COLLISION_BRUSH_MODEL", scene)
        self.assertIn("Worr_RewindSceneSealV1(&cache.scene)", scene)

    def test_capture_runs_after_final_player_state(self) -> None:
        finalization = GAME_MAIN.index("  // --- Finalize Frame ---")
        client_frames = GAME_MAIN.index("  ClientEndServerFrames();", finalization)
        mover_capture = GAME_MAIN.index("    LagCompensation_RecordMovers();", finalization)
        self.assertLess(client_frames, mover_capture)

    def test_sealed_brushes_replace_matching_live_movers_without_edict_rewind(self) -> None:
        context = function_body(
            LAG_COMPENSATION,
            "[[nodiscard]] bool BuildHistoricalBrushContext(",
            "[[nodiscard]] const HistoricalBrushEntry *FindHistoricalBrushEntry",
        )
        for requirement in (
            "Worr_RewindSceneValidateV1(scene)",
            "CurrentCollisionMap(context.map)",
            "context.map.map_epoch != scene->map_epoch",
            "EntityFromRef(pose.entity)",
            "ResolveMoverAsset(*current, context.map, asset)",
            "HistoricalBrushAssetMatches(pose, asset)",
        ):
            self.assertIn(requirement, context)

        current = function_body(
            LAG_COMPENSATION, "trace_t TraceCurrentScene(", "[[nodiscard]] bool InitIgnoreSet"
        )
        self.assertIn("HistoricalBrushExcludesCurrent(historicalBrushes, entity)", current)

        brush = function_body(
            LAG_COMPENSATION, "[[nodiscard]] bool ClipHistoricalBrushPose(", "trace_t NativeTrace"
        )
        for requirement in (
            "FindHistoricalBrushEntry(context, pose.entity)",
            "EntityFromRef(pose.entity) != entry->current",
            "request.asset = entry->asset.handle",
            "CopyVector(request.origin, ToVector(pose.origin))",
            "CopyVector(request.angles, ToVector(pose.angles))",
            "rewindCollisionImport->TraceTransformed(&request, &clipped)",
            "clipped.ent = entry->current",
            "MergeTrace(result, clipped, entry->current)",
        ):
            self.assertIn(requirement, brush)
        self.assertNotIn("PoseTouchesBounds", brush)

        trace = function_body(
            LAG_COMPENSATION, "trace_t TraceHistoricalScene(", "} // namespace"
        )
        self.assertLess(
            trace.index("BuildHistoricalBrushContext(scene, historicalBrushes)"),
            trace.index("TraceCurrentScene("),
        )
        self.assertIn("target.canonical ? &historicalBrushes : nullptr", trace)
        self.assertIn("ClipHistoricalBrushPose(result, candidate.pose", trace)
        self.assertIn("return finishObservation(NativeTrace(", trace)

        authority = function_body(
            LAG_COMPENSATION, "[[nodiscard]] uint64_t AuthoritativeCollisionHash()", "void CopyDecisionToObservation"
        )
        self.assertIn("EligibleLiveMover(&entity)", authority)
        self.assertIn("entity.s.modelIndex", authority)

    def test_historical_brush_blocks_are_observed_as_historical_hits(self) -> None:
        trace = function_body(
            LAG_COMPENSATION, "trace_t TraceHistoricalScene(", "} // namespace"
        )
        self.assertIn("bool historicalBrushHit = false", trace)
        self.assertIn(
            "HistoricalBrushExcludesCurrent(&historicalBrushes, result.ent)", trace
        )
        self.assertIn("result.ent && result.fraction < 1.0f &&", trace)
        self.assertIn("result.ent->client || historicalBrushHit", trace)

    def test_headless_runtime_probe_keeps_a_native_reference_and_a_sealed_trace(self) -> None:
        probe = function_body(
            LAG_COMPENSATION,
            "bool LagCompensation_RunHistoricalBrushRuntimeProbe(",
            "trace_t LagCompensation_TraceLine",
        )
        for requirement in (
            "LagCompensation_RecordMovers();",
            "CanonicalScene(0, decision)",
            "BuildHistoricalBrushContext(scene, historicalBrushes)",
            "ClipEntity(mover, start, nullptr, nullptr, end, MASK_SHOT)",
            "mover->s.angles[YAW] += 90.0f",
            "LagCompensation_RecordFrame(rider);",
            "RuntimeRiderFrameContinuity(",
            "CanonicalScene(riderIndex, normalFrameDecision)",
            "rider_frame_scene_sealed",
            "rider->groundEntity = mover",
            "WORR_REWIND_POSE_HAS_MOVER",
            "rider_provenance_sealed",
            "rotationOnlyX",
            "rotation_control_unblocked",
            "mover->s.origin.x += 96.0f",
            "TraceCurrentScene(",
            "ClipHistoricalBrushPose(",
            "authorityBefore == AuthoritativeCollisionHash()",
            "sg_worr_rewind_mover_selftest_status",
        ):
            self.assertIn(requirement, probe)


if __name__ == "__main__":
    unittest.main()
