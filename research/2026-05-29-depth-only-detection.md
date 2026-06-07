# Depth-only detection improvements

- Created: 2026-05-29T08:13:08+03:00
- Task: Improve detection using scene depth only, without physics rays

## Entries

### 2026-05-29T08:14:41+03:00 - decision: Use depth-only cluster scoring instead of hard nearest-depth/reference seed

- Detail: TryResolveTargetFromSceneDepth now evaluates depth clusters using sample ratio, target-pixel
  proximity, soft reference proximity, and slight near-depth bias. No physics raycasts/line traces
  added; only projection math from camera pixel and scene depth remains.

### 2026-05-29T08:14:59+03:00 - validation: First build failed on duplicate CameraForward local

- Detail: BTServ_UpdateCrowPersonDetection.cpp redefined CameraForward after cluster scoring introduced a
  wider-scope variable. Removed duplicate declaration before retry.

### 2026-05-29T08:15:16+03:00 - validation: EagleEyeEditor build succeeded after depth-only cluster scoring change

- Detail: Ran Unreal Build.sh for EagleEyeEditor Linux Development. BTServ_UpdateCrowPersonDetection.cpp
  compiled and libUnrealEditor-EagleEye.so linked successfully.

## Report Notes

- Main findings:
  - Implemented depth-only improvement: scene-depth resolver now evaluates all candidate depth clusters and chooses best by depth sample support, target-pixel proximity, soft prior memory proximity, and slight near-depth bias. No physics LineTrace/Sweep added. Build succeeded. Next validation should compare clusterScore/clusterPixelDist/clusterRefDist against realDelta2D in new PIE logs.
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
