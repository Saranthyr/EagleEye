# Building wall visibility

- Created: 2026-06-07T12:36:16+03:00
- Task: Debug procedural buildings missing walls

## Entries

### 2026-06-07T12:36:33+03:00 - finding: Wall mesh can be null for existing WorldGen instances

- Detail: FBuildingTypeConfig defaults WallMesh/FloorMesh to nullptr. Constructor adds a default cube only
  when BuildingTypes.Num()==0, but placed/serialized actors can keep existing BuildingTypes entries
  with null WallMesh. CreateBuildingPrimitive then uses UBoxComponent fallback, which is hidden in
  game when bDebugBuildings=false.
- Impact: Explains walls missing visually while footprint/nav logic may still exist.

### 2026-06-07T12:37:27+03:00 - decision: Patch runtime building defaults and nav registration

- Detail: Added EnsureBuildingTypesConfigured/ResolveDefaultBuildingMesh. Missing WallMesh now falls back to
  FloorMesh or StarterContent/Engine cube. CreateSection now inserts SectionData into LoadedSections
  before UpdateGeneratedNavigationData so building components get registered.
- Impact: Fixes invisible walls from hidden UBoxComponent fallback and improves nav obstacle update for newly
  streamed buildings.

### 2026-06-07T12:37:59+03:00 - validation: Built EagleEyeEditor

- Detail: Ran Build.sh EagleEyeEditor Linux Development after wall fallback/nav timing changes. Result:
  Succeeded.
- Impact: Confirms code and UHT compile.

## Report Notes

- Main findings:
  - Patched procedural building wall visibility. Runtime now fills missing WallMesh/FloorMesh with StarterContent cube or Engine cube, avoiding hidden UBoxComponent fallback for normal cases. Also moved nav update until after SectionData is inserted into LoadedSections so building walls participate in generated nav data immediately. Build succeeded.
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
