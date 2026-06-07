# Model Loader Actor

- Created: 2026-06-07T07:14:13+03:00
- Task: Add actor that loads model at gameplay start or checks existing model

## Entries

### 2026-06-07T07:14:42+03:00 - source: Inspected existing host actor and GameMode hook

- Detail: Source/EagleEye/Public/AI/DetectionModelHostActor.h,
  Source/EagleEye/Private/DetectionModelHostActor.cpp, Source/EagleEye/Public/EagleEyeGameMode.h,
  Source/EagleEye/Private/EagleEyeGameMode.cpp. Existing ADetectionModelHostActor wraps
  UMyActorComponent and GameMode spawns one at BeginPlay.

### 2026-06-07T07:14:58+03:00 - finding: Host exists but does not preload model

- Detail: UMyActorComponent::BeginPlay resolves paths/names and registers shared host. For shared host it
  returns before timer setup. ProcessSharedVisionFrame lazily calls LoadYOLO when first frame
  arrives.

### 2026-06-07T07:15:15+03:00 - decision: Reuse existing actor and add explicit preload

- Detail: Chosen approach: keep existing ADetectionModelHostActor, add explicit model preload API on
  UMyActorComponent, call from host actor BeginPlay, and update GameMode to find an existing host
  before spawning.

### 2026-06-07T07:15:58+03:00 - artifact: Patched model host startup behavior

- Detail: Added UMyActorComponent::EnsureModelLoaded/IsModelLoaded, ADetectionModelHostActor::BeginPlay
  preload, and GameMode lookup for existing host before spawning.

### 2026-06-07T07:16:22+03:00 - validation: EagleEyeEditor build succeeded

- Detail: Command: /home/saranthyr/Unreal_Engine_5.6.1/Engine/Build/BatchFiles/Linux/Build.sh EagleEyeEditor
  Linux Development EagleEye.uproject -WaitMutex. Result: Succeeded.

## Report Notes

- Main findings:
  - Existing ADetectionModelHostActor kept. Added gameplay-start preload through UMyActorComponent::EnsureModelLoaded, wired host actor BeginPlay to call it by default, and updated AEagleEyeGameMode to reuse an existing host actor before spawning a new one. EagleEyeEditor Linux Development build succeeded.
- Evidence to cite:
  - Source/EagleEye/Private/DetectionModelHostActor.cpp
  - Source/EagleEye/Private/EagleEyeGameMode.cpp
  - Source/EagleEye/Private/MyActorComponent.cpp
- Decisions and rationale:
  - Reuse existing host actor class instead of creating a duplicate actor type.
  - Preload on host BeginPlay so first shared inference frame does not pay model load cost.
- Validation performed:
  - EagleEyeEditor Linux Development build succeeded.
- Unresolved questions:
  - None.
- Suggested report angle:
  - Startup model lifecycle now has one reusable host actor and explicit preload.
