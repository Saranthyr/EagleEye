# Procedural buildings

- Created: 2026-06-07T12:14:01+03:00
- Task: Extend procedural generation with buildings. Buildings affect navigation, do not collide with foliage, bots can spawn inside them.

## Entries

### 2026-06-07T12:14:19+03:00 - source: Inspected procedural generation code

- Detail: Read Source/EagleEye/Public/WorldGen.h and Source/EagleEye/Private/WorldGen.cpp. WorldGen streams
  terrain sections, spawns foliage as HISM instances, creates auto nav bounds, registers
  terrain/foliage nav data, then spawns bots.

### 2026-06-07T12:15:23+03:00 - decision: Implement buildings as section-owned hollow components

- Detail: Use generated wall/floor/roof components instead of solid cubes so walls affect nav while interior
  remains spawnable. Spawn buildings before foliage so foliage can avoid footprints. Keep bot
  collision checks, but sample building interiors for walking bot spawn candidates.
- Impact: Matches user constraints: navigation changes, foliage avoidance, bots may spawn inside buildings.

### 2026-06-07T12:19:59+03:00 - validation: Built EagleEyeEditor

- Detail: Ran Unreal BuildTool via Build.sh for EagleEyeEditor Linux Development. Result: Succeeded.
- Impact: Confirms new WorldGen C++ and UHT reflection compile.

### 2026-06-07T12:20:52+03:00 - artifact: Changed WorldGen for procedural buildings

- Detail: Added FBuildingTypeConfig and building runtime state in WorldGen.h. WorldGen.cpp now generates
  hollow section-owned buildings before foliage, destroys them on section unload/regenerate, blocks
  foliage in footprints, offers walking bot interior spawn candidates, and registers building wall
  primitives for dynamic nav.
- Impact: Main implementation artifacts: Source/EagleEye/Public/WorldGen.h,
  Source/EagleEye/Private/WorldGen.cpp.

## Report Notes

- Main findings:
  - Implemented procedural building generation in AWorldGen. Buildings are hollow wall/floor/roof primitive sets spawned before foliage; wall primitives block collision and affect navigation, foliage rejects expanded building footprints, and walking bots can spawn inside building interiors through a configurable chance. Validation: EagleEyeEditor Linux Development build succeeded twice after implementation.
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
