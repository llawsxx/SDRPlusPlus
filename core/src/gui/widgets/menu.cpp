#include <gui/widgets/menu.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <gui/style.h>
#include <core.h>
#include <algorithm>
#include <cmath>

Menu::Menu() {
}

void Menu::registerEntry(std::string name, void (*drawHandler)(void* ctx), void* ctx, ModuleManager::Instance* inst) {
    MenuItem_t item;
    item.drawHandler = drawHandler;
    item.ctx = ctx;
    item.inst = inst;
    items[name] = item;
    if (!isInOrderList(name)) {
        MenuOption_t opt;
        opt.name = name;
        opt.open = true;
        order.push_back(opt);
    }
}

void Menu::removeEntry(std::string name) {
    items.erase(name);
}

bool Menu::draw(bool updateStates) {
    return tabbed ? drawTabs(updateStates) : drawSections(updateStates);
}

bool Menu::drawTabs(bool updateStates) {
    std::string previousEntry = selectedEntry;
    if (items.find(selectedEntry) == items.end()) {
        selectedEntry.clear();
        for (const auto& option : order) {
            if (items.find(option.name) == items.end()) { continue; }
            selectedEntry = option.name;
            break;
        }
        updateStates = true;
    }
    if (selectedEntry.empty()) { return previousEntry != selectedEntry; }

    if (updateStates) {
        tabScrollToSelection = true;
        tabGestureActive = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * style::uiScale, 10.0f * style::uiScale));
    float height = ImGui::GetFrameHeight();
    float availableWidth = ImGui::GetContentRegionAvail().x;
    float listWidth = std::min<float>(height, availableWidth * 0.25f);
    float spacing = std::min<float>(ImGui::GetStyle().ItemSpacing.x, availableWidth * 0.02f);
    ImGuiWindowFlags stripFlags = ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("##MenuTabs", ImVec2(std::max<float>(1.0f, availableWidth - listWidth - spacing), height), false, stripFlags)) {
        ImVec2 mouse = ImGui::GetMousePos();
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            tabGestureActive = true;
            tabGestureDragged = false;
            tabGestureStartX = tabGestureLastX = mouse.x;
            tabGestureStartY = mouse.y;
            tabGestureScroll = ImGui::GetScrollX();
        }
        if (tabGestureActive) {
            float threshold = std::max<float>(ImGui::GetIO().MouseDragThreshold, 6.0f * style::uiScale);
            float delta = mouse.x - tabGestureLastX;
            if (!tabGestureDragged && (std::abs(mouse.x - tabGestureStartX) > threshold || std::abs(mouse.y - tabGestureStartY) > threshold)) {
                tabGestureDragged = true;
                delta = mouse.x - tabGestureStartX;
            }
            if (tabGestureDragged) {
                tabGestureScroll = std::clamp(tabGestureScroll - delta, 0.0f, ImGui::GetScrollMaxX());
                ImGui::SetScrollX(tabGestureScroll);
            }
            tabGestureLastX = mouse.x;
        }
        bool firstTab = true;
        for (const auto& option : order) {
            if (items.find(option.name) == items.end()) { continue; }
            if (!firstTab) { ImGui::SameLine(0, 4.0f * style::uiScale); }
            firstTab = false;
            bool selected = option.name == selectedEntry;
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(selected ? ImGuiCol_TabActive : ImGuiCol_Tab));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabHovered));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_TabActive));
            bool pressed = ImGui::Button(option.name.c_str(), ImVec2(0, height));
            ImGui::PopStyleColor(3);
            if (pressed && !(tabGestureActive && tabGestureDragged)) {
                selectedEntry = option.name;
            }
            if (selected && tabScrollToSelection && !tabGestureActive) {
                ImGui::SetScrollHereX(0.5f);
                tabScrollToSelection = false;
            }
        }
    }
    ImGui::EndChild();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { tabGestureActive = false; }
    ImGui::SameLine(0, spacing);
    if (ImGui::Button("...##MenuTabList", ImVec2(listWidth, height))) { ImGui::OpenPopup("##MenuTabPicker"); }
    ImGui::PopStyleVar();
    if (ImGui::BeginPopup("##MenuTabPicker")) {
        for (const auto& option : order) {
            if (items.find(option.name) == items.end()) { continue; }
            if (ImGui::Selectable(option.name.c_str(), option.name == selectedEntry)) {
                selectedEntry = option.name;
                tabScrollToSelection = true;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    bool changed = previousEntry != selectedEntry;
    auto found = items.find(selectedEntry);
    if (found == items.end()) { return changed; }
    MenuItem_t item = found->second;
    ImGui::PushID(selectedEntry.c_str());
    if (ImGui::BeginChild("##MenuContent")) {
        if (item.inst != nullptr) {
            bool enabled = item.inst->isEnabled();
            ImGui::BeginDisabled(core::isTuningLocked());
            if (ImGui::Checkbox("Enabled##MenuModule", &enabled) && !core::isTuningLocked()) {
                enabled ? item.inst->enable() : item.inst->disable();
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::Separator();
        }
        item.drawHandler(item.ctx);
    }
    ImGui::EndChild();
    ImGui::PopID();
    return changed;
}

bool Menu::drawSections(bool updateStates) {
    bool changed = false;
    float menuWidth = ImGui::GetContentRegionAvail().x;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImVec2 checkboxOffset = ImVec2(menuWidth - ImGui::GetTextLineHeight() - (6.0f * style::uiScale), - ImGui::GetTextLineHeight() - (10.0f * style::uiScale));

    int displayedCount = 0;
    int rawId = 0;

    ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

    for (MenuOption_t& opt : order) {
        rawId++;
        if (items.find(opt.name) == items.end()) {
            continue;
        }
        if (opt.name == draggedMenuName) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", draggedMenuName.c_str());
            ImGui::EndTooltip();
            continue;
        }

        // Draw dragged menu item
        if (displayedCount == insertBefore && !draggedMenuName.empty()) {
            if (updateStates) { ImGui::SetNextItemOpen(false); }
            ImVec2 posMin = ImGui::GetCursorScreenPos();
            ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());
            style::beginDisabled();
            ImRect orignalRect = window->WorkRect;
            ImGui::CollapsingHeader((draggedMenuName + "##sdrpp_main_menu_dragging").c_str());
            if (items[draggedOpt.name].inst != NULL) {
                window->WorkRect = orignalRect;
                ImVec2 pos = ImGui::GetCursorPos();
                ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
                ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
                bool enabled = items[draggedOpt.name].inst->isEnabled();
                ImGui::Checkbox(("##_menu_checkbox_" + draggedOpt.name).c_str(), &enabled);
                ImGui::SetCursorPos(pos);
            }
            style::endDisabled();
            window->DrawList->AddRect(posMin, posMax, textColor, 0.0f, 0, style::uiScale);
        }
        displayedCount++;

        MenuItem_t& item = items[opt.name];


        ImRect orginalRect = window->WorkRect;
        if (item.inst != NULL) {
            window->WorkRect = ImRect(orginalRect.Min, ImVec2(orginalRect.Max.x - ImGui::GetTextLineHeight() - (6.0f * style::uiScale), orginalRect.Max.y));
        }

        ImVec2 posMin = ImGui::GetCursorScreenPos();
        ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());

        headerTops[displayedCount - 1] = posMin.y;
        optionIDs[displayedCount - 1] = rawId - 1;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(posMin, posMax)) {
            menuClicked = true;
            clickedMenuName = opt.name;
        }

        bool menuDragged = (menuClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && draggedMenuName.empty() && clickedMenuName == opt.name);
        if (menuDragged && !locked) {
            draggedMenuName = opt.name;
            draggedId = rawId - 1;
            draggedOpt = opt;
            continue;
        }
        else if (menuDragged) {
            ImGui::SetTooltip("Menu Order Locked!");
        }

        // Draw menu header and menu content. There is a lot of boilerplate because the checkbox has to be drawn before the menu, TODO: fix
        if (updateStates) { ImGui::SetNextItemOpen(opt.open); }
        if (ImGui::CollapsingHeader((opt.name + "##sdrpp_main_menu").c_str())) {
            if (item.inst != NULL) {
                window->WorkRect = orginalRect;
                ImVec2 pos = ImGui::GetCursorPos();
                ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
                ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
                bool enabled = item.inst->isEnabled();
                if (ImGui::Checkbox(("##_menu_checkbox_" + opt.name).c_str(), &enabled)) {
                    if (!core::isTuningLocked()) { enabled ? item.inst->enable() : item.inst->disable(); }
                    changed = true;
                }
                ImGui::SetCursorPos(pos);
            }

            // Check if the state changed
            if (!opt.open && !updateStates) {
                opt.open = true;
                changed = true;
            }

            item.drawHandler(item.ctx);
            ImGui::Spacing();
        }
        else if (item.inst != NULL) {
            window->WorkRect = orginalRect;
            ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
            ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
            bool enabled = item.inst->isEnabled();
            if (ImGui::Checkbox(("##_menu_checkbox_" + opt.name).c_str(), &enabled)) {
                if (!core::isTuningLocked()) { enabled ? item.inst->enable() : item.inst->disable(); }
                changed = true;
            }
            ImGui::SetCursorPos(pos);

            if (opt.open && !updateStates) {
                opt.open = false;
                changed = true;
            }
        }
        else if (opt.open && !updateStates) {
            opt.open = false;
            changed = true;
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && menuClicked) {

        if (!draggedMenuName.empty()) {
            // Move menu
            order.erase(order.begin() + draggedId);

            if (insertBefore == displayedCount) {
                order.push_back(draggedOpt);
            }
            else if (insertBeforeName != "") {
                int beforeId = 0;
                for (int i = 0; i < order.size(); i++) {
                    if (order[i].name == insertBeforeName) {
                        beforeId = i;
                        break;
                    }
                }
                order.insert(order.begin() + beforeId, draggedOpt);
            }
            changed = true;
        }

        menuClicked = false;
        draggedMenuName = "";
        insertBeforeName = "";
        insertBefore = -1;
    }

    // TODO: Figure out why the hell this is needed
    if (insertBefore == displayedCount && !draggedMenuName.empty()) {
        if (updateStates) { ImGui::SetNextItemOpen(false); }
        ImVec2 posMin = ImGui::GetCursorScreenPos();
        ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());
        style::beginDisabled();
        ImRect orignalRect = window->WorkRect;
        ImGui::CollapsingHeader((draggedMenuName + "##sdrpp_main_menu_dragging").c_str());
        if (items[draggedOpt.name].inst != NULL) {
            window->WorkRect = orignalRect;
            ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
            ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
            bool enabled = items[draggedOpt.name].inst->isEnabled();
            ImGui::Checkbox(("##_menu_checkbox_" + draggedOpt.name).c_str(), &enabled);
            ImGui::SetCursorPos(pos);
        }
        style::endDisabled();
        window->DrawList->AddRect(posMin, posMax, textColor, 0.0f, 0, style::uiScale);
    }

    if (!draggedMenuName.empty()) {
        insertBefore = displayedCount;
        ImVec2 mPos = ImGui::GetMousePos();
        for (int i = 0; i < displayedCount; i++) {
            if (headerTops[i] > mPos.y) {
                insertBefore = i;
                insertBeforeName = order[optionIDs[i]].name;
                break;
            }
        }
    }

    return changed;
}

bool Menu::isInOrderList(std::string name) {
    for (MenuOption_t opt : order) {
        if (opt.name == name) {
            return true;
        }
    }
    return false;
}
