# Navmesh build settings check

- Created: 2026-06-10T13:13:54+03:00
- Task: Check navmesh build settings in project

## Entries

### 2026-06-10T13:14:17+03:00 - finding: Project nav config is minimal and dynamic

- Detail: Config/DefaultEngine.ini sets NavigationSystemV1:
  bGenerateNavigationOnlyAroundNavigationInvokers=False, DataGatheringMode=Lazy. RecastNavMesh:
  RuntimeGeneration=Dynamic. No explicit Recast cell size, agent radius, tile size, or related build
  tuning found in Config or Source.

### 2026-06-10T13:14:17+03:00 - finding: WorldGen builds navmesh around streamed sections

- Detail: AWorldGen exposes bEnableNavMesh=true, bAutoNavBounds=true, NavBoundsHeight=2000. It creates
  transient section NavMeshBoundsVolumes, updates navigation component data for
  terrain/foliage/buildings, and marks dirty areas expanded by cellsize and NavBoundsHeight.

### 2026-06-10T13:14:25+03:00 - validation: Search completed

- Detail: Commands checked Config and Source for Navigation, NavMesh, Recast, RuntimeGeneration,
  DataGatheringMode, CellSize, AgentRadius, TileSizeUU, and related build keys. Only DefaultEngine
  nav keys plus WorldGen runtime nav code were found.

## Report Notes

- Main findings:
  - Navmesh settings check: DefaultEngine.ini enables dynamic Recast runtime generation with lazy gathering and disables nav invokers. WorldGen owns runtime section nav bounds and dirty-area updates using bEnableNavMesh=true, bAutoNavBounds=true, NavBoundsHeight=2000. No explicit Recast cell/agent/tile build tuning found in Config or Source, so defaults likely apply unless overridden in editor assets/map data.
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
