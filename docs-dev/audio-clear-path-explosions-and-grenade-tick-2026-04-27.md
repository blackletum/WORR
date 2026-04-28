# Clear-Path Explosions and Held Grenade Tick Fixes (2026-04-27)

Task ID: `FR-06-T01`

## Summary

This update fixes two gameplay-facing audio regressions found after the spatial audio source-path work:

- rocket, grenade, and other fixed-origin explosion sounds could be treated as occluded even when there was a clear listener-to-source path
- the held hand grenade tick/fuse sound was not reliably audible to the local player or as a world cue for nearby players

## Explosion Occlusion

Explosion temp entities already start their sounds at fixed world origins. The false muffling came later in the OpenAL spatial path: `AL_ApplySourcePathState(...)` could raise the direct-path occlusion value to a source-room floor after the shared multi-ray trace had reported a clear path.

`src/client/sound/al.cpp` now treats `S_OCCLUSION_CLEAR_MARGIN` as the handoff between direct geometry tracing and source-path damping. Portal route improvements and per-source reverb send colouring still apply, but path occlusion floors and HF ceilings are skipped while the direct multi-ray path remains clear. This preserves the spatial-room model for genuinely blocked sources without making visible midair explosions sound like they are behind a wall.

## Held Grenade Tick

The held grenade tick uses `weapons/hgrenc1b.wav`, a one-second tick cycle without embedded loop metadata. The player weapon path already stores it in `client->weaponSound`, which is the legacy route for broadcasting a player-held loop as `ent->s.sound`.

`src/game/sgame/player/p_weapon.cpp` now centralizes primed throwable loop assignment through `Throw_SetPrimedLoopSound(...)`:

- sets `client->weaponSound` for the existing player loop route
- mirrors the sound immediately onto `ent->s.sound` with `ATTN_NORM` loop attenuation so the current server frame has a concrete world loop state
- emits a one-shot `CHAN_AUX` start cue for `weapons/hgrenc1b.wav` when the hand grenade first enters the primed loop, giving the owner and nearby players an immediate audible tick while the entity loop carries the continuing fuse sound

The explicit one-shot is limited to the hand grenade tick asset so other throwable primed loops, such as trap loop sounds, are not accidentally started as independent dynamic loop channels.

## Local Validation

- `meson compile -C builddir-win-bootstrap-hosted worr_engine_x86_64 sgame_x86_64 copy_sgame_dll`
- `python tools\refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
- `git diff --check --` (passed with only the repository's existing LF-to-CRLF warnings)
