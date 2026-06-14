# Target correctness logging fix

- Created: 2026-06-14T11:49:06+03:00
- Task: Fix detection performance CSV correctness to evaluate bot target rather than generic person class

## Entries

### 2026-06-14T11:51:46+03:00 - finding: Correctness changed from person-class to bot target pixel

- Detail: Performance CSV path now samples TargetActor from bot blackboard (fallback player pawn), projects
  that target into owner camera, and marks detection success only when a detection box contains that
  target pixel while expected in FOV. This replaces HasPersonDetection-based correctness for
  performance CSV rows.

### 2026-06-14T11:52:12+03:00 - validation: EagleEyeEditor build succeeded

- Detail: Ran Build.sh EagleEyeEditor Linux Development EagleEye.uproject -WaitMutex. Build succeeded;
  existing ONNX Runtime dependency warning remains non-fatal.

## Report Notes

- Main findings:
  - Fixed performance CSV correctness to target bot target actor, not person class. Target actor is read from blackboard TargetActor with player pawn fallback, projected into owner camera on game thread, carried through local/shared frame data, and matched against detection boxes by target pixel on worker side.
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
