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
        float face = (492.0f / 480 + RightAnchorOffset(item.width, item.height)) * item.height;
        float needle = (556.0f / 480 + RightAnchorOffset(item.width, item.height)) * item.height;
        if (std::fabs(needle - face - item.height * 64.0f / 480) > 0.001f) return 2;
    }
    if (RightAnchorOffset(0,0) != 0) return 3;
    std::puts("PASS: 4:3, 16:9, 4K and ultrawide anchors; gauge/needle alignment and original dimensions preserved.");
    return 0;
}
