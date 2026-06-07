# Prevent player spawn inside buildings

- Created: 2026-06-07T16:11:07+03:00
- Task: Disable player spawn inside procedural buildings

## Entries

### 2026-06-07T16:11:48+03:00 - decision: Use footprint rejection for player starts/current pawn

- Detail: Added IsFootprintNearAnyPlayerStart. GatherPlayerStartLocations now always gathers PlayerStart
  actors and current player pawn/controller pawn, even when PlayerStartObjectAvoidanceRadius is 0.
  Building placement rejects actor bounds footprint if it contains/overlaps those locations.
- Impact: Prevents buildings from enclosing player spawn/current player location, beyond radius-based center
  checks.

## Report Notes

- Main findings:
  - Prevented buildings from enclosing player spawn/current player location. Added footprint-based rejection against PlayerStart actors and current player pawn/controller pawn, independent of radius-only center checks. Build succeeded.
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
