#include <gui/dialogs/loading_screen.h>
#include <imgui.h>
#include <gui/icons.h>
#include <gui/style.h>
#include <backend.h>
#include <algorithm>
#include <cfloat>

namespace LoadingScreen {
    ImVec2 imageSize(128.0f, 128.0f);

    void init() {
        imageSize = ImVec2(128.0f * style::uiScale, 128.0f * style::uiScale);
    }

    void show(std::string msg) {
        backend::beginFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport->Size.x <= 0 || viewport->Size.y <= 0) {
            backend::render(false);
            return;
        }
        float padding = std::min(20.0f * style::uiScale, std::min(viewport->Size.x, viewport->Size.y) * 0.05f);
        float gap = 20.0f * style::uiScale;
        ImVec2 titleSize = style::hugeFont->CalcTextSizeA(style::hugeFont->FontSize, FLT_MAX, 0, "SDR++");
        ImVec2 messageSize = style::baseFont->CalcTextSizeA(style::baseFont->FontSize, FLT_MAX, 0, msg.c_str());
        ImVec2 headerSize(titleSize.x + gap + imageSize.x, std::max(titleSize.y, imageSize.y));
        ImVec2 contentSize(std::max(headerSize.x, messageSize.x), headerSize.y + gap + messageSize.y);
        float fit = std::min({1.0f, (viewport->Size.x - 2 * padding) / contentSize.x,
            (viewport->Size.y - 2 * padding) / contentSize.y});
        ImVec2 origin(viewport->Pos.x + (viewport->Size.x - contentSize.x * fit) / 2,
            viewport->Pos.y + (viewport->Size.y - contentSize.y * fit) / 2);
        float headerLeft = origin.x + (contentSize.x - headerSize.x) * fit / 2;
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        drawList->AddText(style::hugeFont, style::hugeFont->FontSize * fit,
            ImVec2(headerLeft, origin.y + (headerSize.y - titleSize.y) * fit / 2), textColor, "SDR++");
        ImVec2 imagePosition(headerLeft + (titleSize.x + gap) * fit,
            origin.y + (headerSize.y - imageSize.y) * fit / 2);
        drawList->AddImage(icons::LOGO, imagePosition,
            ImVec2(imagePosition.x + imageSize.x * fit, imagePosition.y + imageSize.y * fit));
        drawList->AddText(style::baseFont, style::baseFont->FontSize * fit,
            ImVec2(origin.x + (contentSize.x - messageSize.x) * fit / 2, origin.y + (headerSize.y + gap) * fit),
            textColor, msg.c_str());

        backend::render(false);
    }
}
