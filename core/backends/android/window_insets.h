#pragma once
#include <algorithm>
#include <cstdint>

namespace backend {
    struct WindowContentArea {
        int left = 0;
        int top = 0;
        int width = 0;
        int height = 0;
    };

    struct WindowInsets {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        int width = 0;
        int height = 0;

        WindowContentArea contentArea(int surfaceWidth, int surfaceHeight) const {
            surfaceWidth = std::max(0, surfaceWidth);
            surfaceHeight = std::max(0, surfaceHeight);
            if (width <= 0 || height <= 0) { return {0, 0, surfaceWidth, surfaceHeight}; }
            auto scaledInset = [](int inset, int surfaceSize, int viewSize) {
                int64_t scaled = (static_cast<int64_t>(std::clamp(inset, 0, viewSize)) * surfaceSize + viewSize - 1) / viewSize;
                return static_cast<int>(scaled);
            };
            int safeLeft = scaledInset(left, surfaceWidth, width);
            int safeTop = scaledInset(top, surfaceHeight, height);
            int safeRight = std::min(scaledInset(right, surfaceWidth, width), surfaceWidth - safeLeft);
            int safeBottom = std::min(scaledInset(bottom, surfaceHeight, height), surfaceHeight - safeTop);
            return {safeLeft, safeTop, surfaceWidth - safeLeft - safeRight, surfaceHeight - safeTop - safeBottom};
        }
    };
}
