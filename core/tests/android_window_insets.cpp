#include "../backends/android/window_insets.h"
#include <iostream>
#include <stdexcept>

static void requireArea(const backend::WindowContentArea& area, int left, int top, int width, int height) {
    if (area.left != left || area.top != top || area.width != width || area.height != height) {
        throw std::runtime_error("Unexpected native content area");
    }
}

int main() {
    try {
        backend::WindowInsets insets;
        requireArea(insets.contentArea(1080, 2400), 0, 0, 1080, 2400);
        insets = {0, 96, 0, 0, 1080, 2400};
        requireArea(insets.contentArea(1080, 2400), 0, 96, 1080, 2304);
        insets = {96, 0, 0, 0, 2400, 1080};
        requireArea(insets.contentArea(2400, 1080), 96, 0, 2304, 1080);
        insets = {0, 0, 96, 0, 2400, 1080};
        requireArea(insets.contentArea(2400, 1080), 0, 0, 2304, 1080);
        insets = {96, 0, 0, 48, 2400, 1080};
        requireArea(insets.contentArea(2400, 1080), 96, 0, 2304, 1032);
        insets = {0, 96, 0, 900, 1080, 2400};
        requireArea(insets.contentArea(1080, 2400), 0, 96, 1080, 1404);
        insets = {15, 33, 21, 45, 1080, 2400};
        requireArea(insets.contentArea(540, 1200), 8, 17, 521, 1160);
        requireArea(insets.contentArea(2160, 4800), 30, 66, 2088, 4644);
        insets = {-100, -20, 2000, 5000, 1080, 2400};
        requireArea(insets.contentArea(1080, 2400), 0, 0, 0, 0);
        insets = {900, 2000, 900, 2000, 1080, 2400};
        requireArea(insets.contentArea(1080, 2400), 900, 2000, 0, 0);
        requireArea(insets.contentArea(0, 0), 0, 0, 0, 0);
        requireArea(insets.contentArea(-1, -1), 0, 0, 0, 0);
        insets = {96, 0, 0, 0, 0, 0};
        requireArea(insets.contentArea(2400, 1080), 0, 0, 2400, 1080);
        std::cout << "PASS: portrait, both landscape cutout sides, system bars, keyboard, scaled surfaces and invalid bounds\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
