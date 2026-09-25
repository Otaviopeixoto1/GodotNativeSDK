#pragma once

// Public export marker for classes and functions that project code is
// allowed to use or inherit from. Everything without this marker is
// internal to the static library and can change without notice.
#define MY_NATIVE_API

// Bump this when the SDK ABI changes so project builds can check
// compatibility at build time.
#define MY_NATIVE_SDK_VERSION_MAJOR 1
#define MY_NATIVE_SDK_VERSION_MINOR 0
