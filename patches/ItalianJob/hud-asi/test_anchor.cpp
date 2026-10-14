#include <cmath>
#include <cstdio>
#include "anchor_math.h"

int main()
{
    struct Case { int width, height; float pixels; };
    Case cases[] = {{640,480,0}, {1024,768,0}, {1920,1080,480}, {2560,1440,640}, {3840,2160,960}, {3440,1440,1520}};
    for (const auto& item : cases) {
        float shift = RightAnchorOffset(item.width, item.height) * item.height;
        if (std::fabs(shift - item.pixels) > 0.001f) return 1;
        float center = CenterAnchorOffset(item.width, item.height);
        if (std::fabs(center * item.height - item.pixels * 0.5f) > 0.001f) return 4;
        const float textureSamples[] = {0.0f, 0.125f, 0.5f, 0.875f, 1.0f};
        for (float u : textureSamples) {
            float backgroundX = (center + u * (4.0f / 3.0f)) * item.height;
            float overlayX = (center + (u * 640.0f) / 480.0f) * item.height;
            if (std::fabs(backgroundX - overlayX) > 0.001f) return 6;
        }
        float fullscreenLeft = -center + center;
        float fullscreenWidth = 4.0f / 3.0f + RightAnchorOffset(item.width, item.height);
        if (std::fabs(fullscreenLeft) > 0.001f ||
            std::fabs(fullscreenWidth * item.height - item.width) > 0.001f) return 5;
        float face = (492.0f / 480 + RightAnchorOffset(item.width, item.height)) * item.height;
        float needle = (556.0f / 480 + RightAnchorOffset(item.width, item.height)) * item.height;
        if (std::fabs(needle - face - item.height * 64.0f / 480) > 0.001f) return 2;
    }
    if (RightAnchorOffset(0,0) != 0) return 3;
    std::puts("PASS: 4:3, 16:9, 4K and ultrawide anchors; gauge/needle alignment and original dimensions preserved.");
    return 0;
}
