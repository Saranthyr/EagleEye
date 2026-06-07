# Actor-based building generation

- Created: 2026-06-07T14:50:30+03:00
- Task: Adapt procedural building generation to spawn building actors instead of meshes

## Entries

### 2026-06-07T14:52:55+03:00 - decision: Switched building config to actor class

- Detail: FBuildingTypeConfig now uses TSubclassOf<AActor> BuildingClass. Removed building HISM components and
  instance-index tracking. SpawnBuildingsForSection now spawns actor candidates, measures actor
  component bounds, rejects collisions with existing building footprints/player starts, stores
  bounds for foliage avoidance and bot interior spawn, and destroys actors on section unload.
- Impact: Matches correction: user owns building actor, not static mesh.

### 2026-06-07T14:53:08+03:00 - finding: Actor bounds drive footprint

- Detail: Building placement uses PlacementFootprintRadius only for candidate sampling. After spawning, actual
  Actor.GetComponentsBoundingBox(true) drives building footprint, foliage avoidance, nav bounds, and
  interior bot spawn region. If actor has no components/bounds, FallbackBoundsHeight and
  PlacementFootprintRadius define a fallback box.
- Impact: Important setup requirement for building actor assets.

### 2026-06-07T14:53:31+03:00 - validation: Built EagleEyeEditor

- Detail: Ran Build.sh EagleEyeEditor Linux Development after switching building generation to actor spawning.
  Result: Succeeded.
- Impact: Confirms C++ and UHT compile.

## Report Notes

- Main findings:
  - Updated building generation to use actor classes. FBuildingTypeConfig now has BuildingClass and placement/filter/nav options. AWorldGen spawns actor instances per streamed section, measures actor bounds for foliage avoidance/nav bounds/bot interior spawn, sets building actor primitive components as nav-affecting when configured, and destroys actors on section unload. Build succeeded.
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
