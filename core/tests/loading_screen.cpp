#include <gui/dialogs/loading_screen.h>
#include <gui/icons.h>
#include <gui/style.h>
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace style {
    float uiScale = 1.0f;
    ImFont* baseFont = nullptr;
    ImFont* hugeFont = nullptr;
}

namespace icons {
    ImTextureID LOGO = (ImTextureID)(intptr_t)2;
}

namespace backend {
    void beginFrame() { ImGui::NewFrame(); }
    void render(bool) { ImGui::Render(); }
}

static void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

static void checkFrame(ImVec2 size, const std::string& message) {
    ImGui::GetIO().DisplaySize = size;
    LoadingScreen::show(message);
    ImDrawData* data = ImGui::GetDrawData();
    require(data->TotalVtxCount > 0, "Loading screen is empty");
    int logoCommands = 0;
    for (int listIndex = 0; listIndex < data->CmdListsCount; listIndex++) {
        const ImDrawList* drawList = data->CmdLists[listIndex];
        for (const ImDrawVert& vertex : drawList->VtxBuffer) {
            require(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y), "Invalid vertex position");
            require(vertex.pos.x >= -1 && vertex.pos.x <= size.x + 1, "Loading content exceeds viewport width");
            require(vertex.pos.y >= -1 && vertex.pos.y <= size.y + 1, "Loading content exceeds viewport height");
        }
        for (const ImDrawCmd& command : drawList->CmdBuffer) {
            if (command.TextureId != icons::LOGO) { continue; }
            logoCommands++;
            require(command.ElemCount == 6, "Logo is not a complete quad");
            ImVec2 minimum(FLT_MAX, FLT_MAX);
            ImVec2 maximum(-FLT_MAX, -FLT_MAX);
            for (unsigned int element = 0; element < command.ElemCount; element++) {
                unsigned int vertexIndex = drawList->IdxBuffer[command.IdxOffset + element] + command.VtxOffset;
                const ImVec2& position = drawList->VtxBuffer[vertexIndex].pos;
                minimum.x = std::min(minimum.x, position.x);
                minimum.y = std::min(minimum.y, position.y);
                maximum.x = std::max(maximum.x, position.x);
                maximum.y = std::max(maximum.y, position.y);
            }
            require(minimum.x >= 0 && minimum.y >= 0 && maximum.x <= size.x && maximum.y <= size.y, "Logo is clipped");
            float width = maximum.x - minimum.x;
            float height = maximum.y - minimum.y;
            require(width > 0 && std::abs(width - height) < 0.01f, "Logo aspect ratio changed");
        }
    }
    require(logoCommands == 1, "Expected exactly one loading logo");
}

int main() {
    try {
        const ImVec2 viewports[] = {
            ImVec2(2400, 1080), ImVec2(1080, 2400), ImVec2(1920, 540),
            ImVec2(900, 320), ImVec2(320, 900), ImVec2(320, 140), ImVec2(1280, 720)
        };
        const float scales[] = {1.0f, 2.0f, 3.0f, 4.0f};
        for (float scale : scales) {
            ImGui::CreateContext();
            ImGuiIO& input = ImGui::GetIO();
            input.IniFilename = nullptr;
            input.DeltaTime = 1.0f / 60;
            ImFontConfig fontConfig;
            fontConfig.SizePixels = 16.0f * scale;
            style::baseFont = input.Fonts->AddFontDefault(&fontConfig);
            fontConfig.SizePixels = 128.0f * scale;
            style::hugeFont = input.Fonts->AddFontDefault(&fontConfig);
            input.Fonts->Build();
            input.Fonts->SetTexID((ImTextureID)(intptr_t)1);
            style::uiScale = scale;
            LoadingScreen::init();
            for (const ImVec2& viewport : viewports) {
                checkFrame(viewport, "Loading libmulti_receiver.so");
                checkFrame(viewport, "Initializing " + std::string(180, 'M'));
                checkFrame(viewport, "");
            }
            ImGui::DestroyContext();
        }
        std::cout << "PASS: 84 loading frames, landscape/portrait/resize, 100-400% scale, full square logo and unclipped text\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
