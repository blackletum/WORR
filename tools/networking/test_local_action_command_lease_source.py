#!/usr/bin/env python3
"""Production-placement contract for the observation-only weapon lease."""

from __future__ import annotations

import argparse
from pathlib import Path


def require(haystack: str, needle: str, label: str) -> int:
    position = haystack.find(needle)
    if position < 0:
        raise AssertionError(f"missing {label}: {needle}")
    return position


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.repo_root.resolve()
    client = (root / "src/game/sgame/client/client_session_service_impl.cpp").read_text(
        encoding="utf-8"
    )
    observation = (
        root / "src/game/sgame/network/local_action_observation.cpp"
    ).read_text(encoding="utf-8")

    begin_start = require(
        client,
        "void ClientSessionServiceImpl::ClientBeginServerFrame",
        "ClientBeginServerFrame",
    )
    begin_end = require(
        client[begin_start:],
        "ReadyResult ClientSessionServiceImpl::OnReadyToggled",
        "next function",
    )
    begin = client[begin_start : begin_start + begin_end]
    lease_position = require(
        begin,
        "SG_LocalActionObservationFrameLeaseScope localActionLease(ent);",
        "frame lease",
    )
    first_return = require(begin, "return;", "early return")
    assert lease_position < first_return, "frame lease must expire every early-return path"
    require(
        begin,
        "SG_LocalActionObservationLeasedAdvanceScope leasedWeaponAdvance(ent);",
        "leased advance scope",
    )
    assert begin.count("SG_LocalActionObservationLeasedAdvanceScope") == 1

    offer = require(observation, "offer_command_lease(context_);", "exact lease offer")
    record = require(observation, "append_record(record);", "scoped record append")
    assert record < offer, "a lease must not precede a validated scoped record"
    require(
        observation,
        "Worr_LocalActionCommandLeaseBeginFrameV1",
        "frame activation",
    )
    require(observation, "Worr_LocalActionCommandLeaseClaimV1", "one-shot claim")
    require(observation, "Worr_LocalActionCommandLeaseEndFrameV1", "frame expiry")
    require(
        observation,
        "SG_LocalActionObservationCopyJoinedRecordForCommand",
        "fail-closed joined oracle",
    )
    assert "Worr_LocalActionBuildTransactionV2" not in observation
    print("local_action_command_lease_source placement=exact authority=unchanged")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
