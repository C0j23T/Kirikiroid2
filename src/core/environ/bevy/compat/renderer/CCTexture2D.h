// bevy port: inert stand-in for cocos2d-x's renderer/CCTexture2D.h.
//
// RenderManager.cpp only touches cocos2d::Texture2D inside
// iTVPTexture2D::GetAdapterTexture(), which the cocos2d display layer calls to
// hand layer pixels to OpenGL. The bevy port reads pixels directly
// (krkr_layer_get_pixels) and never calls GetAdapterTexture, so this class
// only has to make that code compile. Its methods do nothing.
#pragma once
#include <cstddef>
#include <sys/types.h>

namespace cocos2d {

struct Size {
    float width = 0, height = 0;
    static const Size ZERO;
};
inline const Size Size::ZERO{};

class Texture2D {
public:
    enum class PixelFormat { RGBA8888, RGB888, A8, I8, AI88 };

    bool initWithData(const void *, ssize_t, PixelFormat, int w, int h, const Size &) {
        _w = w; _h = h; return true;
    }
    bool updateWithData(const void *, int, int, int, int) { return true; }
    int getPixelsWide() const { return _w; }
    int getPixelsHigh() const { return _h; }
    Texture2D *autorelease() { return this; } // leaks by design; never reached in the bevy port

private:
    int _w = 0, _h = 0;
};

} // namespace cocos2d
