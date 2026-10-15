#pragma once

#include <algorithm>
#include <imgui/imgui.h>

namespace gui {
    struct PanelLayout {
        bool portrait;
        ImVec2 menuPosition;
        ImVec2 menuSize;
        ImVec2 waterfallPosition;
        ImVec2 waterfallSize;
        ImVec2 controlsPosition;
        ImVec2 controlsSize;
    };

    inline PanelLayout panelLayout(ImVec2 windowSize, ImVec2 available, float scale, bool showMenu, float requestedMenuWidth) {
        available.x = std::max<float>(available.x, 1.0f);
        available.y = std::max<float>(available.y, 1.0f);
        PanelLayout layout{};
        layout.portrait = windowSize.x < windowSize.y;
        float gap = std::min<float>(8.0f * scale, available.x * 0.02f);
        float controlsWidth = std::min<float>(60.0f * scale, available.x * 0.15f);
        float contentHeight = available.y;
        float waterfallLeft = 0;
        if (showMenu && layout.portrait) {
            float verticalGap = std::min<float>(gap, available.y * 0.02f);
            layout.menuSize = ImVec2(available.x, available.y * 0.42f);
            contentHeight = available.y - layout.menuSize.y - verticalGap;
            layout.menuPosition = ImVec2(0, contentHeight + verticalGap);
        }
        else if (showMenu) {
            float minimum = std::min<float>(250.0f * scale, available.x * 0.4f);
            layout.menuSize = ImVec2(std::clamp(requestedMenuWidth, minimum, available.x * 0.55f), available.y);
            waterfallLeft = layout.menuSize.x + gap;
        }
        layout.waterfallPosition = ImVec2(waterfallLeft, 0);
        layout.waterfallSize = ImVec2(available.x - waterfallLeft - controlsWidth - gap, contentHeight);
        layout.controlsPosition = ImVec2(available.x - controlsWidth, 0);
        layout.controlsSize = ImVec2(controlsWidth, contentHeight);
        return layout;
    }
}
