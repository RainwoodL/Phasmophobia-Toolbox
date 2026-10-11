# Phasmophobia 0.19.0.2 compatibility audit

Scope: the main `payload/`, `injector/`, and root build only. Existing deletions
under `定制/` were preserved and were not part of this audit.

## Baseline

- Game version supplied by the operator: `0.19.0.2`.
- Root `dump.cs` SHA-256:
  `EB07F3D16E203F90A161B461B1A798AC99DA50CC86DBA5C189F897BF409531B9`.
- Installed `GameAssembly.dll` SHA-256:
  `1785CE30C04666998E3C06A44C7E31F04B055796CF1163BFCFA9A03385A53463`.
- Installed `global-metadata.dat` SHA-256:
  `5404E29781DD9775C5FC9F9D96FDFB983FC8BCA8238E5FBCF468D1B93308CED7`.

The game binary and metadata differ from the previous audit baseline. Method
RVAs changed, but ToolsBOX resolves method entries from IL2CPP metadata at
runtime and does not embed those RVAs.

## Compatibility results

| Feature | 0.19.0.2 evidence | Result |
| --- | --- | --- |
| Smudge timer | `WhiteSage.UseNetworked(bool, PhotonMessageInfo)`, RVA `0x809240` | Name and ABI unchanged |
| Game time | `LevelStats.get_Instance`; `timeSpentInvestigating` remains `0x34` | Compatible |
| Hunt/status | `GhostAI.Hunting(bool, int, PhotonMessageInfo)`, RVA `0x205D020`; state `0x30`, hunting `0xF3` | Name, ABI, and layout unchanged |
| Ghost speed/distance | `GhostAI` NavMeshAgent remains `0x40`; Network/LocalPlayer object chains remain uniquely typed | Runtime metadata resolution remains valid |
| Crucifix timer | `CrucifixUseNetworked(bool, PhotonMessageInfo)`, RVA `0x5DAAB0`; `useCount` remains `0x410` | Compatible |
| Ouija translation | `PlayMessageSequence(string, PhotonMessageInfo)`, RVA `0x6F53E0`; `List<string>` remains `0x3F0` | Compatible |
| Crosshair info | `PCPropGrab` canvas `0x28`, colliders `0x80/0xD8`; `PCCanvas` enum `0x88` | Dynamic lookup assumptions unchanged |
| Inventory | `List<PhotonObjectInteract>` `0x50`, active slot `0x58`, `Equipment` StoreItem `0x180` | Dynamic lookup assumptions unchanged |
| Stamina | `Network` LocalPlayer `0x30`, LocalPlayer PlayerStamina `0xA0`, current stamina `0x40` | Compatible |
| Sanity | player list `0x38`, PlayerSanity `0x90`, insanity `0x30`, current Difficulty `0x38`, `sanityMonitor` `0x90` | Compatible |
| Player nicknames | death byte `0x28`, head target `0x88`, head socket `0x78`, `modelHeadSpot` `0x30` | Compatible |
| Photon speed sync | EventData `Code` `0x10`; RaiseEventOptions `Receivers` `0x20`; required methods retain their signatures | Compatible |

`PhotonMessageInfo` remains a 24-byte x64 value with `Sender` at `0x8` and
`PhotonView` at `0x10`, so the four RPC hook declarations require no ABI change.

## Implementation decision

No payload source change is required for `0.19.0.2`. Updating code with the new
RVAs would regress the existing runtime-resolution design. Root `dump.cs` is
now ignored so the generated 55 MB file cannot be committed accidentally.

## Verification

- Debug build and all 8 CTest targets pass.
- Release `payload.dll` and `PhasmophobiaInjector.exe` build successfully.
- Release MSVC `/analyze` build for both payload and injector passes.
- Repository hygiene and secret scans pass. Both scripts now skip tracked paths
  that are absent from a dirty worktree, so existing `定制/` deletions remain
  untouched without aborting the checks.
- Static metadata and local build checks do not replace an in-game injection,
  multiplayer, scene-transition, and interactive-object smoke test.
