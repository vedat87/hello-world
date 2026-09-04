#pragma once

#include <cstddef>

namespace MuGpuSprite
{
    struct SpriteInstance
    {
        float center[3];      // camera-space center
        float size[2];        // full width/height
        float rotationDeg;
        float color[4];
        float uvRect[4];      // u, v, width, height
    };

    // Uses the existing compatibility OpenGL context. If the driver does not
    // expose instanced arrays/draws, IsAvailable() returns false and Main keeps
    // the original immediate-mode sprite path.
    bool IsAvailable();
    const char* LastError();
    bool Draw(const SpriteInstance* instances, std::size_t count);
    void Shutdown();
}
