#pragma once

#if defined(_WIN32) || defined(_WIN64) || defined(__linux__) || defined(__ANDROID__)

#include "Platform/Private/Arch_x86.h"

#else
#error "Unknown and unsupported platform."
#endif // _WIN32 || _WIN64
