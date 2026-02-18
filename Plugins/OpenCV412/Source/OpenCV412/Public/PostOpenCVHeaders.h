#pragma once

#ifdef OPENCV_HEADERS_TYPES_GUARD_CUSTOM
#undef OPENCV_HEADERS_TYPES_GUARD_CUSTOM
#else
#error Mismatched PreOpenCVHeaders.h detected.
#endif


#if PLATFORM_LINUX
namespace OpenCVPluginUtils
{
    using cvint64 = int64;
    using cvuintt64 = uint64;
}

#undef int64
#undef uint64

using int64 = OpenCVPluginUtils::UEInt64;
using uint64 = OpenCVPluginUtils::UEUInt64;
#endif

UE_POP_MACRO("check")
THIRD_PARTY_INCLUDES_END
