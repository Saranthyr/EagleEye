// Minimal utilities to mirror Epic's OpenCV helper headers without depending on their plugin.
#pragma once

#include "CoreMinimal.h"

namespace OpenCVPluginUtils
{
    using UEInt64 = FPlatformTypes::int64;
    using UEUInt64 = FPlatformTypes::uint64;
}
