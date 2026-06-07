# Depth detection improvement check

- Created: 2026-05-29T08:01:51+03:00
- Task: Check current logs for depth detection issues and identify improvements

## Entries

### 2026-05-29T08:02:56+03:00 - finding: Latest logs show depth often accurate, but memory/detection target identity still unstable

- Detail: Latest EagleEye.log at 2026-05-29 08:01 local: close phase shows realDelta2D often 4-20 cm with high
  depth sample ratios. Worst realDelta2D > 1600 cm has coherent samples and high IoU, meaning scene
  depth resolved a real nearby surface/person, but not the debug player pawn.

### 2026-05-29T08:03:10+03:00 - finding: Existing player snap config is unused

- Detail: BTServ_UpdateCrowPersonDetection.h exposes PlayerRaySnapRadius, bPreferPlayerPawnLocation, and
  bRequireRaySnapForPlayerPawnLocation, but rg shows only declarations except fallback/always-follow
  branch. This is a direct improvement path if intended target is player pawn.

## Report Notes

- Main findings:
  - Latest logs show scene-depth math is usually accurate when YOLO box is on the player, but large errors mostly come from target identity and memory policy: YOLO resolves a coherent nearby 'person' surface that is not the player, and SameTargetLocationThreshold=450 keeps old memory for large player movement within that radius. Best improvements: enable/implement player snap/require logic, reject/penalize non-player pawn hits from depth ray, and reduce or split same-target threshold vs movement update threshold.
- Evidence to cite:
  -
- Decisions and rationale:
  -
- Validation performed:
  -
- Unresolved questions:
  -
- Suggested report angle:
  -
