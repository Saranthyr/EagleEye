# Navmesh Rebuild Threading

- Created: 2026-06-10T13:55:32+03:00
- Task: Assess moving navmesh rebuild into separate thread

## Entries

### 2026-06-10T13:55:43+03:00 - source: Found project nav rebuild paths

- Detail: rg found NavSystem->Build() in Source/EagleEye/Private/EagleEyeGameMode.cpp and
  Source/EagleEye/Private/WorldGen.cpp; DefaultEngine.ini has NavigationSystemV1
  DataGatheringMode=Lazy and RecastNavMesh RuntimeGeneration=Dynamic.

### 2026-06-10T13:56:10+03:00 - finding: UNavigationSystemV1::Build blocks

- Detail: Engine NavigationSystem.cpp Build() calls RebuildAll(), then loops NavDataSet and calls
  EnsureBuildCompletion(); FRecastNavMeshGenerator::EnsureBuildCompletion() waits on tile async
  tasks. So Build() is synchronous at caller level.

### 2026-06-10T13:56:10+03:00 - finding: Recast already supports async tile generation

- Detail: RecastNavMesh.h has config MaxSimultaneousTileGenerationJobsCount and bDoFullyAsyncNavDataGathering;
  RecastNavMeshGenerator TickAsyncBuild submits tile build tasks and processes completed tiles over
  nav tick.

### 2026-06-10T13:57:06+03:00 - decision: Use engine async nav build instead of custom AsyncTask

- Detail: Custom AsyncTask around NavSystem->Build() rejected because Build() touches UWorld/nav UObjects and
  blocks on EnsureBuildCompletion. Patch removes WorldGen's blocking Build() after section changes
  and queues dirty section bounds for UE navigation tick to process via Recast async tile tasks.

### 2026-06-10T13:57:30+03:00 - validation: EagleEyeEditor build passed

- Detail: Ran Build.sh EagleEyeEditor Linux Development with -WaitMutex; result succeeded. UBT warned ONNX
  Runtime not found, unrelated to nav change.

## Report Notes

- Main findings:
  - Conclusion: do not wrap UNavigationSystemV1::Build() in a custom thread/AsyncTask. Build() is caller-synchronous and waits for async tile jobs; off-thread call would be unsafe for UWorld/navigation UObjects. Implemented non-blocking section-change path in WorldGen: PerformNavRebuild() now updates nav data and marks loaded section bounds dirty through QueueNavigationRebuildForCurrentWorld(), letting UE navigation tick process Recast async tile generation. EagleEyeEditor build succeeded.
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
