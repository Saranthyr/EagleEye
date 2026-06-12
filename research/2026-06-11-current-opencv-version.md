# Current OpenCV version check

- Created: 2026-06-11T07:57:55+03:00
- Task: Verify whether project currently uses OpenCV 4.12

## Entries

### 2026-06-11T07:58:28+03:00 - finding: Win64 OpenCV is 4.10.0, not 4.12

- Detail: OpenCV412.Build.cs Win64 branch hardcodes opencv_world4100.lib/dll and
  opencv_videoio_ffmpeg4100_64.dll from ThirdParty/OpenCV/Libraries/Win64. Binaries/Win64 contains
  opencv_world4100.dll. Latest Unreal log error reports OpenCV(4.10.0).

## Report Notes

- Main findings:
  - Current Windows build is OpenCV 4.10.0, not 4.12. Plugin name OpenCV412 and plugin-local headers include 4.12.0, but OpenCV412.Build.cs Win64 path prefers project ThirdParty/OpenCV/include and ThirdParty/OpenCV/Libraries/Win64/opencv_world4100.*. Runtime logs confirm OpenCV(4.10.0).
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
