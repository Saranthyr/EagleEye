# BeginPlay and runtime navmesh generation

- Created: 2026-06-10T13:16:41+03:00
- Task: Set game to generate navmesh at BeginPlay and regenerate when new world section generated

## Entries

### 2026-06-10T13:17:13+03:00 - finding: Existing nav runtime flow is partial

- Detail: WorldGen already creates per-section NavMeshBoundsVolume and marks dirty areas on
  CreateSection/DestroySection. RequestNavRebuild only calls UpdateGeneratedNavigationData via
  PerformNavRebuild; it does not explicitly call UNavigationSystemV1::Build().

### 2026-06-10T13:17:13+03:00 - source: Unreal nav build API checked

- Detail: Engine NavigationSystem.h exposes UNavigationSystemV1::Build() to trigger navigation building on all
  eligible nav data. Implementation discards chunks, spawns missing nav data, processes registration
  candidates, rebuilds all, and waits for completion.

### 2026-06-10T13:17:59+03:00 - decision: Build nav at GameMode BeginPlay

- Detail: Added bBuildNavigationOnBeginPlay to AEagleEyeGameMode and calls UNavigationSystemV1::Build() during
  BeginPlay. This covers prebuilt maps that already have NavMeshBoundsVolume/navigation-relevant
  geometry.

### 2026-06-10T13:17:59+03:00 - decision: Explicit WorldGen section-change nav rebuild

- Detail: Added bRebuildNavMeshOnSectionChanges to AWorldGen. Existing RequestNavRebuild debounce now calls
  UpdateGeneratedNavigationData() then UNavigationSystemV1::Build() after section
  create/destroy/streaming changes.

### 2026-06-10T13:18:28+03:00 - validation: Unreal build succeeded

- Detail: Ran Build.sh EagleEyeEditor Linux Development EagleEye.uproject -WaitMutex. UHT, compile, and link
  succeeded. Existing non-fatal ONNX Runtime warning remains.

## Report Notes

- Main findings:
  - Implemented BeginPlay and runtime navmesh generation. AEagleEyeGameMode now builds navigation on BeginPlay via UNavigationSystemV1::Build(). AWorldGen now has bRebuildNavMeshOnSectionChanges and its deferred section-change rebuild updates generated nav data then calls UNavigationSystemV1::Build(). EagleEyeEditor Linux Development build succeeded.
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
