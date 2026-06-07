# Blueprint behavior tree migration

- Created: 2026-06-07T08:24:27+03:00
- Task: Inspect Blueprints/assets and report needed changes after target behavior tree refactor

## Entries

### 2026-06-07T08:25:47+03:00 - finding: Blueprint assets still reference legacy target/person keys and service class

- Detail: Binary text scan over Content/ThirdPerson/Blueprints found CrowBehaviour.uasset and
  AnotherBotBehaviour.uasset referencing BTServ_UpdateCrowPersonDetection, HasDetectedPerson,
  LosePersonAfterSeconds. CrowBlack.uasset and AnotherBotBlack.uasset still contain
  HasDetectedPerson and DetectedPersonLocation. CrowBlack contains DetectedConfidence, while C++
  service default uses DetectionConfidence.

### 2026-06-07T08:26:10+03:00 - finding: Relevant Blueprint dependency shape

- Detail: Binary scan found /Game/ThirdPerson/Blueprints/Bots/CrowBot references CrowBehaviour.
  /Game/ThirdPerson/Blueprints/Bots/AnotherBot and AnotherBotController reference
  AnotherBotBehaviour. No pawn/controller assets matched old blackboard key strings; required manual
  edits are in the two blackboards and two behavior trees.

### 2026-06-07T08:28:47+03:00 - validation: Headless Unreal inspected blackboard assets

- Detail: UnrealEditor-Cmd loaded target assets and reported CrowBlack keys: SelfActor/Object,
  HasDetectedPerson/Bool, DetectedPersonLocation/Vector, DetectedConfidence/Float. AnotherBotBlack
  keys: SelfActor/Object, HasDetectedPerson/Bool, DetectedPersonLocation/Vector,
  DetectionConfidence/Float. Command exited 0; unrelated project startup warning about missing
  BP_ThirdPersonCharacter was present.

## Report Notes

- Main findings:
  - Blueprint inspection for dynamic target migration: old person-target naming remains only in four assets: /Game/ThirdPerson/Blueprints/Bots/CrowBlack, AnotherBotBlack, CrowBehaviour, AnotherBotBehaviour. Required manual editor changes are blackboard key rename/adds and behavior tree service/task/decorator selector updates from HasDetectedPerson/DetectedPersonLocation/BTServ_UpdateCrowPersonDetection to HasDetectedTarget/DetectedTargetLocation/BTServ_UpdateCrowTargetDetection, plus adding DetectedClassId, DetectedClassLabel, and normalized DetectionConfidence keys.
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
