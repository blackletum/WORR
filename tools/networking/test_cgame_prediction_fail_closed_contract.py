#!/usr/bin/env python3
"""Regression contract for cgame prediction-invariant recovery wiring."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PREDICT = (ROOT / "src/game/cgame/cg_predict.cpp").read_text(encoding="utf-8")
TIMELINE = (
    ROOT / "src/game/cgame/cg_snapshot_timeline.cpp"
).read_text(encoding="utf-8")
INPUT_ABI = (ROOT / "inc/shared/cgame_prediction.h").read_text(encoding="utf-8")


def function_body(source: str, signature: str, successor: str) -> str:
    start = source.index(signature)
    end = source.index(successor, start)
    return source[start:end]


class CgamePredictionFailClosedContractTests(unittest.TestCase):
    def test_recovery_results_are_append_only_and_named(self) -> None:
        self.assertIn("WORR_CGAME_PREDICTION_INPUT_REPLAY_REJECTED = 10", INPUT_ABI)
        self.assertIn(
            "WORR_CGAME_PREDICTION_INPUT_RETAINED_STATE_MISSING = 11", INPUT_ABI
        )
        self.assertIn(
            "WORR_CGAME_PREDICTION_INPUT_CONFIG_DISCONTINUITY = 12", INPUT_ABI
        )

    def test_hard_resync_discards_all_retained_movement_ring_identity(self) -> None:
        body = function_body(PREDICT, "static void CG_PredictionHardResync(", "void CL_PredictAngles")
        normalized_body = " ".join(body.split())
        for member in (
            "cl.predicted_sequences",
            "cl.predicted_states",
            "cl.predicted_origins",
            "cl.predicted_state_hashes",
            "cl.predicted_collision_hashes",
            "cl.predicted_config_hashes",
            "cl.predicted_replay_chain_hashes",
        ):
            self.assertIn(
                f"memset({member}, 0, sizeof({member}));", normalized_body
            )

    def test_invariant_failures_route_through_hard_resync(self) -> None:
        check_body = function_body(PREDICT, "void CL_CheckPredictionError(", "#define MAX_STEP_CHANGE")
        self.assertIn(
            "WORR_CGAME_PREDICTION_INPUT_RETAINED_STATE_MISSING, input_range",
            check_body,
        )
        self.assertIn(
            "WORR_CGAME_PREDICTION_INPUT_CONFIG_DISCONTINUITY, input_range",
            check_body,
        )

        movement_body = function_body(PREDICT, "void CL_PredictMovement(", "\n}")
        self.assertEqual(
            movement_body.count(
                "WORR_CGAME_PREDICTION_INPUT_REPLAY_REJECTED, input_range"
            ),
            2,
        )

    def test_telemetry_carries_a_machine_readable_correction_reason(self) -> None:
        for reason in (
            "input_range_invalid",
            "retained_state_missing",
            "config_discontinuity",
            "replay_rejected",
            "state_divergence",
            "correction_threshold_exceeded",
        ):
            self.assertIn(f'return "{reason}";', TIMELINE)
        self.assertIn("reason=%s", TIMELINE)


if __name__ == "__main__":
    unittest.main()
