# Foliage-style building generation

- Created: 2026-06-07T14:27:04+03:00
- Task: Remove auto-generated buildings and replace with building generation system akin to foliage generation

## Entries

### 2026-06-07T14:27:15+03:00 - finding: Existing building code still auto-builds geometry

- Detail: FBuildingTypeConfig has wall/floor/roof fields and SpawnBuildingsForSection creates primitive wall
  pieces. User reports nothing appears and asks to remove this auto-generation, so replacement
  should mirror foliage: configured mesh component per type, seeded random placement, section
  instance tracking, swap-removal index repair.
- Impact: Design target for patch.

### 2026-06-07T14:31:40+03:00 - decision: Use HISM building meshes

- Detail: Replaced procedural wall/floor/roof generation with BuildingComponents, one
  UHierarchicalInstancedStaticMeshComponent per FBuildingTypeConfig. Sections now track
  BuildingInstanceIndices like foliage and remove instances with swap-index repair.
- Impact: Matches user request: no auto-generated buildings; configured building meshes behave like foliage
  instances while retaining nav/collision support.

### 2026-06-07T14:32:22+03:00 - validation: Built EagleEyeEditor

- Detail: Ran Build.sh EagleEyeEditor Linux Development after replacing auto-generated buildings with HISM
  mesh placement. Result: Succeeded.
- Impact: Confirms new UHT fields and C++ compile.

## Report Notes

- Main findings:
  - Removed auto-generated building geometry. Building generation now mirrors foliage: FBuildingTypeConfig supplies a mesh/material/count or density/scale/collision/nav options; AWorldGen creates one HISM component per building type, places instances per streamed section, tracks instance indices for swap-removal, keeps footprints for foliage avoidance and bot interior spawn sampling, and registers building components with nav. Build succeeded.
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
