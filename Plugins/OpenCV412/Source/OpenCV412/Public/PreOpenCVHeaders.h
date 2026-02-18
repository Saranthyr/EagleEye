#pragma once

#ifndef OPENCV_HEADERS_TYPES_GUARD_CUSTOM
#define OPENCV_HEADERS_TYPES_GUARD_CUSTOM
#else
#error Nesting PreOpenCVHeaders.h is not allowed!
#endif

#include "OpenCVUtils.h"

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("check")
#undef check

#if PLATFORM_LINUX
#define int64 cvint64
#define uint64 cvuint64
#endif
