// bevy port: stand-in for cocos2d-x's platform/CCPlatformConfig.h.
// Only the platform-selection macros are used by the engine core.
#pragma once
#define CC_PLATFORM_UNKNOWN 0
#define CC_PLATFORM_IOS     1
#define CC_PLATFORM_ANDROID 2
#define CC_PLATFORM_WIN32   3
#define CC_PLATFORM_LINUX   5
#define CC_PLATFORM_MAC     8
#ifndef CC_TARGET_PLATFORM
#define CC_TARGET_PLATFORM CC_PLATFORM_LINUX
#endif
