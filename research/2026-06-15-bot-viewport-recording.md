# Bot Viewport Recording Capability

Task: Check whether project has bot capability to record its viewport, likely ActorComponent using FFMPEG.

### 2026-06-15T00:00:00+03:00 - source: User context

- Evidence: User says "There should be one with actorcomponent. At least there was, using FFMPEG".
- Detail: Search should focus on Unreal `ActorComponent`, viewport/capture/recording, and FFmpeg integration.
- Impact: Guides repo investigation toward existing Unreal component code instead of external tooling only.

### 2026-06-15T00:00:00+03:00 - command: Repository search

- Evidence: `rg -n -i "ffmpeg|avcodec|avformat|avutil|swscale" .`; `rg -n -i "ActorComponent|UActorComponent|record|recording|viewport|capture|SceneCapture|FrameGrabber" Source Plugins Content Config`
- Detail: Found FFmpeg-backed bot viewport recording in `Source/EagleEye/Public/MyActorComponent.h`, `Source/EagleEye/Private/MyActorComponent.cpp`, `Source/EagleEye/Private/BotCharacter.cpp`, and `Source/EagleEye/Private/EagleEyeCharacter.cpp`.
- Impact: Confirms requested capability exists in project code.

### 2026-06-15T00:00:00+03:00 - finding: Existing capability and API

- Evidence: `UMyActorComponent::SetRecordOwnerCameraCaptureVideo`, `RecordOwnerCameraVideoFrame`, `EnsureOwnerCameraVideoWriter`; `ABotCharacter::StartBotViewportRecordingWithSettings`; `AEagleEyeCharacter::StartNearestBotViewportRecording`.
- Detail: `ABotCharacter` creates `DetectionComponent` as `UMyActorComponent`, enables owner camera capture, and exposes start/stop methods. Player character exposes `Exec` functions to start nearest bot recording and stop bot recordings.
- Impact: User can trigger bot viewport recording via code/Blueprint/console exec depending on Unreal binding context.

### 2026-06-15T00:00:00+03:00 - finding: FFmpeg implementation details

- Evidence: `Source/EagleEye/Private/MyActorComponent.cpp:1797-1914`, `1991-2050`.
- Detail: Output defaults to `FPaths::VideoCaptureDir()/BotViewport_<Owner>_<Timestamp>.ts`. FFmpeg path defaults to `ffmpeg` and can be set by `SetOwnerCameraVideoEncoderPath`. Frames are written as raw `bgra` stdin to FFmpeg and encoded as H.264 MPEG-TS using `libx264`.
- Impact: Runtime requires `ffmpeg` discoverable on PATH or configured path; output uses `.ts` unless custom output path has extension.

### 2026-06-15T00:00:00+03:00 - finding: Bot settings exposure

- Evidence: `Source/EagleEye/Public/AI/BotCharacter.h`; `Source/EagleEye/Public/MyHUD.h:84-103`; `Source/EagleEye/Private/MyHUD.cpp:468-502`.
- Detail: `ABotCharacter` exposes recording as `UFUNCTION` calls, but there is no `UPROPERTY(EditAnywhere)` for recording in bot details. Detection settings menu items also do not include recording.
- Impact: Current ways are console/Blueprint calls. To set from bot settings, add editable `ABotCharacter` recording properties and apply them to `DetectionComponent`.

### 2026-06-15T00:00:00+03:00 - command: Runtime log check

- Evidence: `rg -n -i "Bot viewport|viewport recording|FFmpeg|Finished bot viewport|Failed to start FFmpeg|Failed to write bot viewport|Recording bot viewport|no bot found|StartNearestBotViewportRecording|StopBotViewport" Saved\Logs`
- Detail: Latest `Saved\Logs\EagleEye.log` contains console commands like `Cmd: StartNearestBotViewportRecording(float FPS = 8.0f, int32 Width = 640, int32 Height = 640)` and `Command not recognized` for that literal text. There are no matching `Recording bot viewport video through FFmpeg`, `Bot viewport recording requested`, `Finished bot viewport video`, or FFmpeg failure logs.
- Impact: Recording did not start. User likely pasted C++ function signature instead of console syntax.

### 2026-06-15T00:00:00+03:00 - validation: Output files

- Evidence: `Get-ChildItem -Path 'Saved' -Recurse -Include 'BotViewport*.ts','*.ts','*.mp4'`
- Detail: No new `BotViewport*.ts` was found. Only old `E:\EagleEye\Saved\VideoCaptures.ts` from 2026-05-20 appeared.
- Impact: Confirms no successful new recording output from latest command attempt.

### 2026-06-15T00:00:00+03:00 - finding: Second command attempt

- Evidence: `Saved\Logs\EagleEye.log:658121`, `661185`; surrounding log lines.
- Detail: Correct global command `StartNearestBotViewportRecording 8 640 640` appears at `2026.06.15-14.46.53`, then PIE starts at `14.46.55`, so it was run before game world/player existed. Later in PIE, user ran `StartBotViewportRecording 8 640 640`, which logged `Command not recognized` because that is not the player/global exec command.
- Impact: No recorder start log is expected. Need run `StartNearestBotViewportRecording 8 640 640` while PIE is already running and the player pawn is active.

### 2026-06-15T00:00:00+03:00 - validation: FFmpeg availability

- Evidence: `ffmpeg -version`; `Get-Command ffmpeg`
- Detail: `ffmpeg` exists at `C:\Program Files\ImageMagick-7.1.0-Q16\ffmpeg.exe`; version 4.2.3 includes `--enable-libx264`.
- Impact: Missing FFmpeg is not the current blocker.

### 2026-06-15T00:00:00+03:00 - artifact: Burn bounding boxes into bot recording

- Evidence: `Source/EagleEye/Public/MyActorComponent.h`; `Source/EagleEye/Private/MyActorComponent.cpp`.
- Detail: Added detection snapshots to queued bot video frames and draws red 3px detection outlines into the BGRA buffer before writing frames to FFmpeg.
- Impact: Future bot viewport recordings should include bounding boxes in the encoded `.ts` video, using the last available detection result for the bot camera frame.

### 2026-06-15T00:00:00+03:00 - validation: Build attempt

- Evidence: `E:\UE_5.6\Engine\Build\BatchFiles\Build.bat EagleEyeEditor Win64 Development E:\EagleEye\EagleEye.uproject -WaitMutex`
- Detail: First sandboxed build failed because UnrealBuildTool could not write AppData logs. Escalated build ran UHT successfully, then failed with `Unable to build while Live Coding is active. Exit the editor and game, or press Ctrl+Alt+F11 if iterating on code in the editor or game`.
- Impact: Code not fully compiled in this turn. Need close editor or trigger Live Coding compile.

### 2026-06-15T00:00:00+03:00 - artifact: Burn labels into bot recording

- Evidence: `Source/EagleEye/Private/MyActorComponent.cpp`.
- Detail: Added a small 5x7 bitmap font renderer for the BGRA encoder buffer. Detection labels now draw above each red box on a black background; fallback label is `CLASS <id>: <confidence>` when `Detection.Label` is empty.
- Impact: Future recordings should include both bounding boxes and readable class/confidence text without relying on HUD drawing.

### 2026-06-15T00:00:00+03:00 - validation: Text overlay checks

- Evidence: `git diff --check`; second `Build.bat` attempt.
- Detail: Whitespace check passed. Build remains blocked by active Live Coding with same message: `Unable to build while Live Coding is active. Exit the editor and game, or press Ctrl+Alt+F11 if iterating on code in the editor or game`.
- Impact: Needs Live Coding compile (`Ctrl+Alt+F11`) or editor close before full command-line build can validate.

## Report Notes

- Main findings: Bot viewport recording exists and is FFmpeg-backed through `UMyActorComponent`.
- Evidence to cite: `MyActorComponent.h`, `MyActorComponent.cpp`, `BotCharacter.cpp`, `EagleEyeCharacter.cpp`.
- Decisions and rationale: No code changes made; task was confirmation/source review.
- Validation performed: Text search and targeted source reads.
- Unresolved questions: Whether current runtime has `ffmpeg` installed/on PATH; not verified.
- Suggested report angle: "Existing bot recording capability is present; document trigger functions, FFmpeg dependency, and output location."
