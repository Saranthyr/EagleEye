# Model Host Ownership Investigation

- Created: 2026-06-10T10:32:03+03:00
- Task: Verify issue selecting closest bots to player to give ownership over model host

## Entries

### 2026-06-10T10:32:45+03:00 - finding: Nearest capture gate exists, model-host queue is arrival-ordered

- Detail: UCrowDetectionShareSubsystem::ShouldRunDetector sorts registered detectors by distance to player.
  UCrowVisionSubsystem::SubmitFrame then admits frames by
  HasActiveRequestForRequesterLocked/CountActiveModelUsersLocked/PendingFrames.Num without distance
  ranking, so farther bots can own active model-user slots if their timers submit first.

### 2026-06-10T10:34:50+03:00 - decision: Make model-host queue distance-aware and align active users with project setting

- Detail: Patch sorts pending shared-vision frames by requester distance to player; closer requester can
  replace farther pending requester when active/queue slots are full. DetectionModelHostActor now
  configures activeUsers from UEagleEyeDetectionSettings::MaxActiveSharedDetectionBots and queue cap
  is at least activeUsers.

### 2026-06-10T10:35:42+03:00 - validation: Build passed

- Detail: Ran Unreal Build.sh EagleEyeEditor Linux Development with Saved/InferenceDeps.env. Result:
  Succeeded. Existing MIGraphX EP missing hint still logged, unrelated to this fix.

## Report Notes

- Main findings:
  - Verified issue in shared vision ownership path. Nearest-bot camera gate sorted detectors by player distance, but CrowVisionSubsystem model-host admission used arrival order and host active-user default was 2 while project setting allowed 3. Fixed by aligning active model users with UEagleEyeDetectionSettings::MaxActiveSharedDetectionBots, forcing queue capacity at least active-user count, sorting pending frames by requester distance to player, and allowing closer bots to replace farther pending frames. In-flight work remains non-preemptive. Build passed.
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
