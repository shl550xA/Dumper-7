#pragma once

#if defined(_WIN32) || defined(_WIN64)

#include "Platform/Private/PlatformWindows.h"

namespace Platform = PlatformWindows;

#define PLATFORM_WINDOWS

// A macro to append the correct postfix for this platform to a function name and call the function with the supplied parameters.
#define CALL_PLATFORM_SPECIFIC_FUNCTION(FunctionName, ...) FunctionName##_Windows(__VA_ARGS__)

#if defined(_WIN64)
#define PLATFORM_WINDOWS64
#else
#define PLATFORM_WINDOWS32
#endif

#elif defined (__ANDROID__)

#include "Platform/Private/PlatformAndroid.h"

namespace Platform = PlatformAndroid;

#define PLATFORM_ANDROID
#define PLATFORM_ANDROID64

// Dispatches to an _Android-suffixed overload. All such functions are currently
// stubs that do nothing; see NameArray.cpp, UnrealTypes.cpp, Offsets.cpp etc.
#define CALL_PLATFORM_SPECIFIC_FUNCTION(FunctionName, ...) FunctionName##_Android(__VA_ARGS__)

#elif defined (__linux__)
#error "The dumper does not support linux."
#else
#error "Unknown and unsupported platform."
#endif // _WIN32 || _WIN64
