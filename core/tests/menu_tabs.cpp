#include <gui/widgets/menu.h>
#include <gui/panel_layout.h>
#include <gui/style.h>
#include <imgui/imgui_internal.h>
#include <core.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

#ifdef SDRPP_MENU_STANDALONE_TEST
namespace core {
    static bool testSessionLocked = false;
    bool isTuningLocked() { return testSessionLocked; }
    void setSessionLocked(bool locked) { testSessionLocked = locked; }
}

namespace style {
    float uiScale = 1;
    void beginDisabled() { ImGui::BeginDisabled(); }
    void endDisabled() { ImGui::EndDisabled(); }
}
#endif

static void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

struct TestModule : ModuleManager::Instance {
    bool enabled = true;
    void postInit() override {}
    void enable() override { enabled = true; }
    void disable() override { enabled = false; }
    bool isEnabled() override { return enabled; }
};

struct TestContent {
    int draws = 0;
    ImVec2 togglePosition;
    float scroll = 0;

    static void draw(void* context) {
        auto* content = static_cast<TestContent*>(context);
        ++content->draws;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        content->togglePosition = ImVec2(window->DC.CursorStartPos.x + 10, window->DC.CursorStartPos.y + ImGui::GetFrameHeight() / 2);
        content->scroll = window->Scroll.y;
        for (int line = 0; line < 70; ++line) { ImGui::Text("Content line %d", line); }
    }
};

static ImVec2 radioTabPosition;
static ImVec2 sourceTabPosition;
static ImVec2 recorderTabPosition;
static ImVec2 tabBarPosition;
static ImVec2 tabBarSize;
static float tabScroll;
static float tabScrollMaximum;
static ImVec2 pickerRadioPosition;

static void frame(Menu& menu, TestContent& source, TestContent& radio, TestContent& recorder, ImVec2 mouse, bool down, bool restore = false, float wheel = 0) {
    source.draws = radio.draws = recorder.draws = 0;
    auto& input = ImGui::GetIO();
    input.AddMousePosEvent(mouse.x, mouse.y);
    input.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    input.AddMouseWheelEvent(0, wheel);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(340, 550));
    ImGui::Begin("Menu test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    menu.draw(restore);
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    ImGuiID stripId = ImGui::GetID("##MenuTabs");
    for (auto* strip : ImGui::GetCurrentContext()->Windows) {
        if (strip->ParentWindow != parent || strip->ChildId != stripId) { continue; }
        tabBarPosition = strip->Pos;
        tabBarSize = strip->Size;
        tabScroll = strip->Scroll.x;
        tabScrollMaximum = strip->ScrollMax.x;
        float sourceWidth = ImGui::CalcTextSize("Source").x + 24.0f * style::uiScale;
        float radioWidth = ImGui::CalcTextSize("Radio").x + 24.0f * style::uiScale;
        float recorderWidth = ImGui::CalcTextSize("Recorder").x + 24.0f * style::uiScale;
        float start = strip->DC.CursorStartPos.x;
        float middle = strip->Pos.y + strip->Size.y / 2;
        sourceTabPosition = ImVec2(start + sourceWidth / 2, middle);
        radioTabPosition = ImVec2(start + sourceWidth + 4.0f * style::uiScale + radioWidth / 2, middle);
        recorderTabPosition = ImVec2(start + sourceWidth + radioWidth + 8.0f * style::uiScale + recorderWidth / 2, middle);
    }
    for (auto* popup : ImGui::GetCurrentContext()->Windows) {
        if (!popup->Active || !(popup->Flags & ImGuiWindowFlags_Popup)) { continue; }
        pickerRadioPosition = ImVec2(popup->DC.CursorStartPos.x + 20, popup->DC.CursorStartPos.y + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetTextLineHeight() / 2);
    }
    ImGui::End();
    ImGui::Render();
    if (menu.tabbed) { require(source.draws + radio.draws + recorder.draws <= 1, "Inactive menu content was drawn"); }
}

static void checkLayout(ImVec2 window, float scale, bool visible, float width) {
    ImVec2 available(window.x - 16, window.y - 80);
    auto layout = gui::panelLayout(window, available, scale, visible, width);
    require(layout.portrait == (window.x < window.y), "Incorrect orientation");
    require(layout.waterfallSize.x > 0 && layout.waterfallSize.y > 0, "Nonpositive waterfall size");
    require(layout.controlsPosition.x >= layout.waterfallPosition.x + layout.waterfallSize.x, "Controls overlap waterfall");
    require(layout.controlsPosition.x + layout.controlsSize.x <= available.x + 0.01f, "Controls outside window");
    if (!visible) {
        require(layout.menuSize.x == 0 && layout.menuSize.y == 0, "Hidden menu occupies space");
        require(layout.waterfallSize.y == available.y, "Hidden menu leaves empty bottom area");
    }
    else if (layout.portrait) {
        require(layout.menuSize.x == available.x, "Portrait menu not full width");
        require(layout.menuPosition.y >= layout.waterfallSize.y, "Bottom menu overlaps waterfall");
        require(std::abs(layout.menuPosition.y + layout.menuSize.y - available.y) < 0.01f, "Bottom menu outside window");
    }
    else {
        require(layout.menuPosition.x == 0 && layout.menuPosition.y == 0, "Landscape menu not at left");
        require(layout.waterfallPosition.x >= layout.menuSize.x, "Side menu overlaps waterfall");
    }
}

int main() {
    try {
        for (ImVec2 size : {ImVec2(1280, 720), ImVec2(720, 1280), ImVec2(360, 640), ImVec2(640, 360), ImVec2(240, 320)}) {
            for (float scale : {1.0f, 2.0f, 3.0f}) {
                for (bool visible : {false, true}) {
                    checkLayout(size, scale, visible, 300);
                    checkLayout(size, scale, visible, 10000);
                }
            }
        }

        ImGui::CreateContext();
        auto& input = ImGui::GetIO();
        input.DisplaySize = ImVec2(900, 600);
        input.DeltaTime = 1.0f / 60;
        input.IniFilename = nullptr;
        input.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int atlasWidth;
        int atlasHeight;
        input.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
        style::uiScale = 1;

        Menu menu;
        TestModule module;
        TestContent source;
        TestContent radio;
        TestContent recorder;
        menu.order.push_back({"Missing module", true});
        menu.registerEntry("Source", TestContent::draw, &source);
        menu.registerEntry("Radio", TestContent::draw, &radio, &module);
        menu.registerEntry("Recorder", TestContent::draw, &recorder);
        for (int index = 0; index < 12; ++index) {
            menu.registerEntry("Extra module " + std::to_string(index), TestContent::draw, &recorder);
        }
        auto idle = ImVec2(-100, -100);
        frame(menu, source, radio, recorder, idle, false, true);
        frame(menu, source, radio, recorder, idle, false);
        require(menu.selectedEntry == "Source" && source.draws == 1, "First valid menu not selected");

        frame(menu, source, radio, recorder, sourceTabPosition, true);
        frame(menu, source, radio, recorder, sourceTabPosition, false);
        frame(menu, source, radio, recorder, radioTabPosition, true);
        frame(menu, source, radio, recorder, radioTabPosition, false);
        frame(menu, source, radio, recorder, idle, false);
        require(menu.selectedEntry == "Radio" && radio.draws == 1, "Touch tab selection failed");
        require(source.draws == 0 && recorder.draws == 0, "Other menu content still visible");

        frame(menu, source, radio, recorder, sourceTabPosition, true);
        frame(menu, source, radio, recorder, sourceTabPosition, false);
        require(menu.selectedEntry == "Source" && source.draws == 1, "Reverse touch needs a hover or second tap");
        frame(menu, source, radio, recorder, recorderTabPosition, true);
        frame(menu, source, radio, recorder, recorderTabPosition, false);
        require(menu.selectedEntry == "Recorder" && recorder.draws == 1, "Direct touch to a third tab failed");
        frame(menu, source, radio, recorder, radioTabPosition, true);
        frame(menu, source, radio, recorder, ImVec2(radioTabPosition.x + 2, radioTabPosition.y + 1), false);
        require(menu.selectedEntry == "Radio" && radio.draws == 1, "Small finger jitter cancels a tap");

        ImVec2 unselectedStart = sourceTabPosition;
        frame(menu, source, radio, recorder, unselectedStart, true);
        frame(menu, source, radio, recorder, ImVec2(unselectedStart.x - 20, unselectedStart.y), true);
        frame(menu, source, radio, recorder, ImVec2(unselectedStart.x - 20, unselectedStart.y), false);
        frame(menu, source, radio, recorder, idle, false);
        require(menu.selectedEntry == "Radio", "Swiping an unselected tab activates it on release");
        frame(menu, source, radio, recorder, idle, false, true);
        frame(menu, source, radio, recorder, idle, false);

        ImVec2 dragStart = radioTabPosition;
        frame(menu, source, radio, recorder, dragStart, true);
        frame(menu, source, radio, recorder, ImVec2(dragStart.x - 80, dragStart.y), true);
        frame(menu, source, radio, recorder, ImVec2(dragStart.x - 150, dragStart.y), true);
        frame(menu, source, radio, recorder, ImVec2(dragStart.x - 150, dragStart.y), false);
        frame(menu, source, radio, recorder, idle, false);
        require(tabScroll > 100 && tabScrollMaximum > tabScroll, "Swipe does not scroll overflowing tabs");
        require(menu.selectedEntry == "Radio" && radio.draws == 1, "Dragging tabs changes selected category");

        dragStart = ImVec2(tabBarPosition.x + tabBarSize.x / 2, tabBarPosition.y + tabBarSize.y / 2);
        frame(menu, source, radio, recorder, dragStart, true);
        frame(menu, source, radio, recorder, ImVec2(dragStart.x + 1000, dragStart.y), true);
        frame(menu, source, radio, recorder, ImVec2(dragStart.x + 1000, dragStart.y), true);
        require(tabScroll == 0, "Swipe not clamped at strip start");
        frame(menu, source, radio, recorder, ImVec2(dragStart.x + 960, dragStart.y), true);
        frame(menu, source, radio, recorder, ImVec2(dragStart.x + 960, dragStart.y), false);
        require(tabScroll > 0 && tabScroll < 100, "Reversing at scroll boundary leaves a dead zone");
        require(menu.selectedEntry == "Radio", "Edge swipe accidentally switches tabs");
        menu.selectedEntry = "Radio";
        frame(menu, source, radio, recorder, idle, false, true);
        frame(menu, source, radio, recorder, idle, false);

        ImVec2 toggle = radio.togglePosition;
        frame(menu, source, radio, recorder, toggle, false);
        frame(menu, source, radio, recorder, toggle, true);
        frame(menu, source, radio, recorder, toggle, false);
        require(!module.enabled, "Selected module enable toggle missing");
        core::setSessionLocked(true);
        frame(menu, source, radio, recorder, toggle, true);
        frame(menu, source, radio, recorder, toggle, false);
        require(!module.enabled, "Locked module enable toggle changed state");
        core::setSessionLocked(false);

        ImVec2 originalBar = tabBarPosition;
        for (int count = 0; count < 12; ++count) {
            frame(menu, source, radio, recorder, ImVec2(160, 300), false, false, -2);
        }
        require(radio.scroll > 0, "Selected menu content does not scroll");
        require(tabBarPosition.y == originalBar.y, "Tab strip scrolled with content");

        menu.selectedEntry = "Extra module 11";
        frame(menu, source, radio, recorder, idle, false, true);
        frame(menu, source, radio, recorder, idle, false);
        require(menu.selectedEntry == "Extra module 11" && recorder.draws == 1, "Overflow tab restore failed");
        ImVec2 pickerButton(tabBarPosition.x + tabBarSize.x + 15, tabBarPosition.y + tabBarSize.y / 2);
        frame(menu, source, radio, recorder, pickerButton, true);
        frame(menu, source, radio, recorder, pickerButton, false);
        frame(menu, source, radio, recorder, pickerButton, false);
        frame(menu, source, radio, recorder, pickerRadioPosition, true);
        frame(menu, source, radio, recorder, pickerRadioPosition, false);
        frame(menu, source, radio, recorder, idle, false);
        require(menu.selectedEntry == "Radio" && radio.draws == 1, "Overflow picker cannot select a category");
        menu.selectedEntry = "Extra module 11";
        frame(menu, source, radio, recorder, idle, false, true);
        frame(menu, source, radio, recorder, idle, false);
        menu.removeEntry("Extra module 11");
        frame(menu, source, radio, recorder, idle, false);
        frame(menu, source, radio, recorder, idle, false);
        require(menu.selectedEntry == "Source" && source.draws == 1, "Removed tab has no fallback");
        menu.tabbed = false;
        frame(menu, source, radio, recorder, idle, false, true);
        require(source.draws == 1 && radio.draws == 1, "Legacy accordion mode unavailable");
        menu.tabbed = true;
        menu.selectedEntry = "Radio";
        frame(menu, source, radio, recorder, idle, false, true);
        frame(menu, source, radio, recorder, idle, false);
        require(menu.selectedEntry == "Radio" && radio.draws == 1, "Mode switch lost selected tab");

        Menu empty;
        frame(empty, source, radio, recorder, idle, false);
        require(source.draws + radio.draws + recorder.draws == 0, "Empty menu draws content");
        ImGui::DestroyContext();
        std::cout << "PASS: first-tap switching without hover, swipe/edge reversal without accidental selection, single-content tabs, overflow/restore/removal, independent scroll, module enable/lock, legacy mode, responsive layouts\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
