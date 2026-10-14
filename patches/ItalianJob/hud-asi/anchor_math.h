#pragma once

inline float RightAnchorOffset(int width, int height)
{
    if (width <= 0 || height <= 0) return 0.0f;
    return static_cast<float>(width) / static_cast<float>(height) - 4.0f / 3.0f;
}
