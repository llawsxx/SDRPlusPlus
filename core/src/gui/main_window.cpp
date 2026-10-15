#include <gui/main_window.h>
#include <gui/gui.h>
#include "imgui.h"
#include <stdio.h>
#include <thread>
#include <complex>
#include <gui/widgets/waterfall.h>
#include <gui/widgets/frequency_select.h>
#include <signal_path/iq_frontend.h>
#include <gui/icons.h>
#include <gui/widgets/bandplan.h>
#include <gui/style.h>
#include <config.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/menus/source.h>
#include <gui/menus/display.h>
#include <gui/menus/bandplan.h>
#include <gui/menus/sink.h>
#include <gui/menus/vfo_color.h>
#include <gui/menus/module_manager.h>
#include <gui/menus/theme.h>
#include <gui/dialogs/credits.h>
#include <filesystem>
#include <signal_path/source.h>
#include <gui/dialogs/loading_screen.h>
#include <gui/colormaps.h>
#include <gui/widgets/snr_meter.h>
#include <gui/tuner.h>
#include <gui/panel_layout.h>

void MainWindow::init() {
    LoadingScreen::show("Initializing UI");
    gui::waterfall.init();
    gui::waterfall.setRawFFTSize(fftSize);

    credits::init();

    core::configManager.acquire();
    json menuElements = core::configManager.conf["menuElements"];
    std::string modulesDir = core::configManager.conf["modulesDirectory"];
    std::string resourcesDir = core::configManager.conf["resourcesDirectory"];
    core::configManager.release();

    // Assert that directories are absolute
    modulesDir = std::filesystem::absolute(modulesDir).string();
    resourcesDir = std::filesystem::absolute(resourcesDir).string();

    // Load menu elements
    gui::menu.order.clear();
    for (auto& elem : menuElements) {
        if (!elem.contains("name")) {
            flog::error("Menu element is missing name key");
            continue;
        }
        if (!elem["name"].is_string()) {
            flog::error("Menu element name isn't a string");
            continue;
        }
        if (!elem.contains("open")) {
            flog::error("Menu element is missing open key");
            continue;
        }
        if (!elem["open"].is_boolean()) {
            flog::error("Menu element name isn't a string");
            continue;
        }
        Menu::MenuOption_t opt;
        opt.name = elem["name"];
        opt.open = elem["open"];
        gui::menu.order.push_back(opt);
    }

    gui::menu.registerEntry("Source", sourcemenu::draw, NULL);
    gui::menu.registerEntry("Sinks", sinkmenu::draw, NULL);
    gui::menu.registerEntry("Band Plan", bandplanmenu::draw, NULL);
    gui::menu.registerEntry("Display", displaymenu::draw, NULL);
    gui::menu.registerEntry("Theme", thememenu::draw, NULL);
    gui::menu.registerEntry("VFO Color", vfo_color_menu::draw, NULL);
    gui::menu.registerEntry("Module Manager", module_manager_menu::draw, NULL);
    gui::menu.registerEntry("Debug", drawDebugMenu, this);
    gui::menu.tabbed = core::configManager.conf.value("tabbedMenu", true);
    gui::menu.selectedEntry = core::configManager.conf.value("selectedMenuTab", std::string("Source"));
    core::setScreenOrientation(static_cast<core::ScreenOrientation>(std::clamp(core::configManager.conf.value("screenOrientation", 0), 0, 2)));
    core::setKeepScreenOn(core::configManager.conf.value("keepScreenOn", false));

    gui::freqSelect.init();

    // Set default values for waterfall in case no source init's it
    gui::waterfall.setBandwidth(8000000);
    gui::waterfall.setViewBandwidth(8000000);

    fft_in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    fft_out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    fftwPlan = fftwf_plan_dft_1d(fftSize, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);

    sigpath::iqFrontEnd.init(&dummyStream, 8000000, true, 1, false, 1024, 20.0, IQFrontEnd::FFTWindow::NUTTALL, acquireFFTBuffer, releaseFFTBuffer, this);
    sigpath::iqFrontEnd.start();

    vfoCreatedHandler.handler = vfoAddedHandler;
    vfoCreatedHandler.ctx = this;
    sigpath::vfoManager.onVfoCreated.bindHandler(&vfoCreatedHandler);

    flog::info("Loading modules");

    // Load modules from /module directory
    if (std::filesystem::is_directory(modulesDir)) {
        for (const auto& file : std::filesystem::directory_iterator(modulesDir)) {
            std::string path = file.path().generic_string();
            if (file.path().extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            flog::info("Loading {0}", path);
            LoadingScreen::show("Loading " + file.path().filename().string());
            core::moduleManager.loadModule(path);
        }
    }
    else {
        flog::warn("Module directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    // Read module config
    core::configManager.acquire();
    std::vector<std::string> modules = core::configManager.conf["modules"];
    auto modList = core::configManager.conf["moduleInstances"].items();
    core::configManager.release();

    // Load additional modules specified through config
    for (auto const& path : modules) {
#ifndef __ANDROID__
        std::string apath = std::filesystem::absolute(path).string();
        flog::info("Loading {0}", apath);
        LoadingScreen::show("Loading " + std::filesystem::path(path).filename().string());
        core::moduleManager.loadModule(apath);
#else
        core::moduleManager.loadModule(path);
#endif
    }

    // Create module instances
    for (auto const& [name, _module] : modList) {
        std::string mod = _module["module"];
        bool enabled = _module["enabled"];
        flog::info("Initializing {0} ({1})", name, mod);
        LoadingScreen::show("Initializing " + name + " (" + mod + ")");
        core::moduleManager.createInstance(name, mod);
        if (!enabled) { core::moduleManager.disableInstance(name); }
    }

    // Load color maps
    LoadingScreen::show("Loading color maps");
    flog::info("Loading color maps");
    if (std::filesystem::is_directory(resourcesDir + "/colormaps")) {
        for (const auto& file : std::filesystem::directory_iterator(resourcesDir + "/colormaps")) {
            std::string path = file.path().generic_string();
            LoadingScreen::show("Loading " + file.path().filename().string());
            flog::info("Loading {0}", path);
            if (file.path().extension().generic_string() != ".json") {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            colormaps::loadMap(path);
        }
    }
    else {
        flog::warn("Color map directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    gui::waterfall.updatePalletteFromArray(colormaps::maps["Turbo"].map, colormaps::maps["Turbo"].entryCount);

    sourcemenu::init();
    sinkmenu::init();
    bandplanmenu::init();
    displaymenu::init();
    vfo_color_menu::init();
    module_manager_menu::init();

    // TODO for 0.2.5
    // Fix gain not updated on startup, soapysdr

    // Update UI settings
    LoadingScreen::show("Loading configuration");
    core::configManager.acquire();
    fftMin = core::configManager.conf["min"];
    fftMax = core::configManager.conf["max"];
    gui::waterfall.setFFTMin(fftMin);
    gui::waterfall.setWaterfallMin(fftMin);
    gui::waterfall.setFFTMax(fftMax);
    gui::waterfall.setWaterfallMax(fftMax);

    double frequency = core::configManager.conf["frequency"];

    showMenu = core::configManager.conf["showMenu"];
    startedWithMenuClosed = !showMenu;

    gui::freqSelect.setFrequency(frequency);
    gui::freqSelect.frequencyChanged = false;
    sigpath::sourceManager.tune(frequency);
    gui::waterfall.setCenterFrequency(frequency);
    bw = 1.0;
    gui::waterfall.vfoFreqChanged = false;
    gui::waterfall.centerFreqMoved = false;
    gui::waterfall.selectFirstVFO();

    menuWidth = core::configManager.conf["menuWidth"];
    newWidth = menuWidth;

    fftHeight = core::configManager.conf["fftHeight"];
    gui::waterfall.setFFTHeight(fftHeight);

    tuningMode = core::configManager.conf["centerTuning"] ? tuner::TUNER_MODE_CENTER : tuner::TUNER_MODE_NORMAL;
    gui::waterfall.VFOMoveSingleClick = (tuningMode == tuner::TUNER_MODE_CENTER);

    core::configManager.release();

    // Correct the offset of all VFOs so that they fit on the screen
    float finalBwHalf = gui::waterfall.getBandwidth() / 2.0;
    for (auto& [_name, _vfo] : gui::waterfall.vfos) {
        if (_vfo->lowerOffset < -finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, (_vfo->bandwidth / 2) - finalBwHalf);
            continue;
        }
        if (_vfo->upperOffset > finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, finalBwHalf - (_vfo->bandwidth / 2));
            continue;
        }
    }

    autostart = core::args["autostart"].b();
    initComplete = true;

    core::moduleManager.doPostInitAll();
}

float* MainWindow::acquireFFTBuffer(void* ctx) {
    return gui::waterfall.getFFTBuffer();
}

void MainWindow::releaseFFTBuffer(void* ctx) {
    gui::waterfall.pushFFT();
}

void MainWindow::vfoAddedHandler(VFOManager::VFO* vfo, void* ctx) {
    MainWindow* _this = (MainWindow*)ctx;
    std::string name = vfo->getName();
    core::configManager.acquire();
    if (!core::configManager.conf["vfoOffsets"].contains(name)) {
        core::configManager.release();
        return;
    }
    double offset = core::configManager.conf["vfoOffsets"][name];
    core::configManager.release();

    double viewBW = gui::waterfall.getViewBandwidth();
    double viewOffset = gui::waterfall.getViewOffset();

    double viewLower = viewOffset - (viewBW / 2.0);
    double viewUpper = viewOffset + (viewBW / 2.0);

    double newOffset = std::clamp<double>(offset, viewLower, viewUpper);

    sigpath::vfoManager.setCenterOffset(name, _this->initComplete ? newOffset : offset);
}

void MainWindow::draw() {
    core::onFrame.emitSnapshot(0);
    ImGui::Begin("Main", NULL, WINDOW_FLAGS);
    std::string recordingFailure = core::recordingError();
    if (!recordingFailure.empty()) {
        ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "Capture / output error: %s", recordingFailure.c_str());
        if (ImGui::Button("Dismiss recording error")) { core::reportRecordingError(""); }
    }
    ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    ImGui::WaterfallVFO* vfo = NULL;
    if (gui::waterfall.selectedVFO != "") {
        vfo = gui::waterfall.vfos[gui::waterfall.selectedVFO];
    }

    // Handle VFO movement
    if (vfo != NULL) {
        if (vfo->centerOffsetChanged || (gui::waterfall.selectedVFOChanged && tuningMode == tuner::TUNER_MODE_CENTER)) {
            if (tuningMode == tuner::TUNER_MODE_CENTER) {
                tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::waterfall.getCenterFrequency() + vfo->generalOffset);
            }
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + vfo->generalOffset);
            gui::freqSelect.frequencyChanged = false;
            core::configManager.acquire();
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
            core::configManager.release(true);
        }
    }

    sigpath::vfoManager.updateFromWaterfall(&gui::waterfall);

    // Handle selection of another VFO
    if (gui::waterfall.selectedVFOChanged) {
        gui::freqSelect.setFrequency((vfo != NULL) ? (vfo->generalOffset + gui::waterfall.getCenterFrequency()) : gui::waterfall.getCenterFrequency());
        gui::waterfall.selectedVFOChanged = false;
        gui::freqSelect.frequencyChanged = false;
    }

    // Handle change in selected frequency
    if (gui::freqSelect.frequencyChanged) {
        gui::freqSelect.frequencyChanged = false;
        tuner::tune(tuningMode, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
        if (vfo != NULL) {
            vfo->centerOffsetChanged = false;
            vfo->lowerOffsetChanged = false;
            vfo->upperOffsetChanged = false;
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        if (vfo != NULL) {
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
        }
        core::configManager.release(true);
    }

    // Handle dragging the frequency scale
    if (gui::waterfall.centerFreqMoved) {
        gui::waterfall.centerFreqMoved = false;
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        if (vfo != NULL) {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + vfo->generalOffset);
        }
        else {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency());
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        core::configManager.release(true);
    }

    int _fftHeight = gui::waterfall.getFFTHeight();
    if (fftHeight != _fftHeight) {
        fftHeight = _fftHeight;
        core::configManager.acquire();
        core::configManager.conf["fftHeight"] = fftHeight;
        core::configManager.release(true);
    }

    // To Bar
    bool sessionLocked = core::isSessionLocked();
    ImGui::BeginGroup();
    if (ImGui::Checkbox("Lock##session", &sessionLocked)) {
        core::setSessionLocked(sessionLocked);
    }
#ifdef __ANDROID__
    ImGui::SameLine();
    const auto displaySize = ImGui::GetIO().DisplaySize;
    bool portrait = displaySize.x < displaySize.y;
    ImGui::BeginDisabled(sessionLocked);
    if (ImGui::Button(portrait ? "Landscape##screen" : "Portrait##screen")) {
        auto orientation = portrait ? core::ScreenOrientation::Landscape : core::ScreenOrientation::Portrait;
        if (core::setScreenOrientation(orientation)) {
            core::configManager.acquire();
            core::configManager.conf["screenOrientation"] = static_cast<int>(orientation);
            core::configManager.conf.erase("lockOrientation");
            core::configManager.release(true);
        }
    }
    ImGui::EndDisabled();
    bool keepScreenOn = core::isScreenKeptOn();
    ImGui::BeginDisabled(sessionLocked);
    if (ImGui::Checkbox("Keep screen on##screen", &keepScreenOn)) {
        core::setKeepScreenOn(keepScreenOn);
        core::configManager.acquire();
        core::configManager.conf["keepScreenOn"] = keepScreenOn;
        core::configManager.release(true);
    }
    ImGui::EndDisabled();
#endif
    ImGui::EndGroup();
    ImGui::SameLine();
    // ImGui::BeginChild("TopBarChild", ImVec2(0, 49.0f * style::uiScale), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImVec2 btnSize(30 * style::uiScale, 30 * style::uiScale);
    ImGui::PushID(ImGui::GetID("sdrpp_menu_btn"));
    if (ImGui::ImageButton(icons::MENU, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_Menu, false)) {
        showMenu = !showMenu;
        core::configManager.acquire();
        core::configManager.conf["showMenu"] = showMenu;
        core::configManager.release(true);
    }
    ImGui::PopID();

    ImGui::SameLine();

    bool tmpPlaySate = playing;
    bool playControlLocked = (playButtonLocked && !tmpPlaySate) || (core::isSessionLocked() && tmpPlaySate);
    if (playControlLocked) { style::beginDisabled(); }
    if (playing) {
        ImGui::PushID(ImGui::GetID("sdrpp_stop_btn"));
        if (ImGui::ImageButton(icons::STOP, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            setPlayState(false);
        }
        ImGui::PopID();
    }
    else { // TODO: Might need to check if there even is a device
        ImGui::PushID(ImGui::GetID("sdrpp_play_btn"));
        if (ImGui::ImageButton(icons::PLAY, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            setPlayState(true);
        }
        ImGui::PopID();
    }
    if (playControlLocked) { style::endDisabled(); }

    // Handle auto-start
    if (autostart) {
        autostart = false;
        setPlayState(true);
    }

    ImGui::SameLine();
    float origY = ImGui::GetCursorPosY();

    sigpath::sinkManager.showVolumeSlider(gui::waterfall.selectedVFO, "##_sdrpp_main_volume_", 248 * style::uiScale, btnSize.x, 5, true);

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY);
    ImGui::BeginDisabled(core::isTuningLocked());
    gui::freqSelect.draw();
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY);
    if (tuningMode == tuner::TUNER_MODE_CENTER) {
        ImGui::PushID(ImGui::GetID("sdrpp_ena_st_btn"));
        if (ImGui::ImageButton(icons::CENTER_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_NORMAL;
            gui::waterfall.VFOMoveSingleClick = false;
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = false;
            core::configManager.release(true);
        }
        ImGui::PopID();
    }
    else { // TODO: Might need to check if there even is a device
        ImGui::PushID(ImGui::GetID("sdrpp_dis_st_btn"));
        if (ImGui::ImageButton(icons::NORMAL_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_CENTER;
            gui::waterfall.VFOMoveSingleClick = true;
            tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = true;
            core::configManager.release(true);
        }
        ImGui::PopID();
    }

    ImGui::SameLine();

    int snrOffset = 87.0f * style::uiScale;
    int snrWidth = std::clamp<int>(ImGui::GetWindowSize().x - ImGui::GetCursorPosX() - snrOffset, 100.0f * style::uiScale, 300.0f * style::uiScale);
    int snrPos = std::max<int>(ImGui::GetWindowSize().x - (snrWidth + snrOffset), ImGui::GetCursorPosX());

    ImGui::SetCursorPosX(snrPos);
    ImGui::SetCursorPosY(origY + (5.0f * style::uiScale));
    ImGui::SetNextItemWidth(snrWidth);
    ImGui::SNRMeter((vfo != NULL) ? gui::waterfall.selectedVFOSNR : 0);

    // Note: this is what makes the vertical size correct, needs to be fixed
    ImGui::SameLine();

    // ImGui::EndChild();

    // Logo button
    ImGui::SetCursorPosX(ImGui::GetWindowSize().x - (48 * style::uiScale));
    ImGui::SetCursorPosY(10.0f * style::uiScale);
    if (ImGui::ImageButton(icons::LOGO, ImVec2(32 * style::uiScale, 32 * style::uiScale), ImVec2(0, 0), ImVec2(1, 1), 0)) {
        showCredits = true;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        showCredits = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showCredits = false;
    }

    // Reset waterfall lock
    lockWaterfallControls = showCredits || core::isTuningLocked();

    // Handle menu resize
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec2 panelOrigin = ImGui::GetCursorPos();
    ImVec2 panelSpace = ImGui::GetContentRegionAvail();
    gui::PanelLayout layout = gui::panelLayout(winSize, panelSpace, style::uiScale, showMenu, menuWidth);
    ImVec2 mousePos = ImGui::GetMousePos();
    if (!lockWaterfallControls && showMenu && !layout.portrait) {
        ImVec2 windowPos = ImGui::GetWindowPos();
        float divider = windowPos.x + panelOrigin.x + layout.menuSize.x;
        float curY = windowPos.y + panelOrigin.y;
        bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (grabbingMenu) {
            newWidth = std::clamp(mousePos.x - windowPos.x - panelOrigin.x, std::min<float>(250.0f * style::uiScale, panelSpace.x * 0.4f), panelSpace.x * 0.55f);
            float preview = windowPos.x + panelOrigin.x + newWidth;
            ImGui::GetForegroundDrawList()->AddLine(ImVec2(preview, curY), ImVec2(preview, curY + panelSpace.y), ImGui::GetColorU32(ImGuiCol_SeparatorActive));
        }
        if (std::abs(mousePos.x - divider) <= 4.0f * style::uiScale && mousePos.y > curY) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (click) {
                grabbingMenu = true;
            }
        }
        else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        }
        if (!down && grabbingMenu) {
            grabbingMenu = false;
            menuWidth = newWidth;
            core::configManager.acquire();
            core::configManager.conf["menuWidth"] = menuWidth;
            core::configManager.release(true);
            layout = gui::panelLayout(winSize, panelSpace, style::uiScale, showMenu, menuWidth);
        }
    }
    else {
        grabbingMenu = false;
    }

    // Process menu keybinds
    displaymenu::checkKeybinds();

    if (showMenu && !layout.portrait) {
        ImGui::SetCursorPos(ImVec2(panelOrigin.x + layout.menuPosition.x, panelOrigin.y + layout.menuPosition.y));
        ImGui::BeginChild("MenuPanel", layout.menuSize, false, gui::menu.tabbed ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0);
        drawMenuPanel();
        ImGui::EndChild();
    }

    ImGui::SetCursorPos(ImVec2(panelOrigin.x + layout.waterfallPosition.x, panelOrigin.y + layout.waterfallPosition.y));
    ImGui::BeginChild("Waterfall", layout.waterfallSize);

    gui::waterfall.draw();

    ImGui::EndChild();

    if (!lockWaterfallControls) {
        // Handle arrow keys
        if (vfo != NULL && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            bool freqChanged = false;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !gui::freqSelect.digitHovered) {
                double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset - vfo->snapInterval;
                nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                freqChanged = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !gui::freqSelect.digitHovered) {
                double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + vfo->snapInterval;
                nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                freqChanged = true;
            }
            if (freqChanged) {
                core::configManager.acquire();
                core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
                if (vfo != NULL) {
                    core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
                }
                core::configManager.release(true);
            }
        }

        // Handle scrollwheel
        int wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0 && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            double nfreq;
            if (vfo != NULL) {
                // Select factor depending on modifier keys
                double interval;
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
                    interval = vfo->snapInterval * 10.0;
                }
                else if (ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
                    interval = vfo->snapInterval * 0.1;
                }
                else {
                    interval = vfo->snapInterval;
                }

                nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + (interval * wheel);
                nfreq = roundl(nfreq / interval) * interval;
            }
            else {
                nfreq = gui::waterfall.getCenterFrequency() - (gui::waterfall.getViewBandwidth() * wheel / 20.0);
            }
            tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
            gui::freqSelect.setFrequency(nfreq);
            core::configManager.acquire();
            core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
            if (vfo != NULL) {
                core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
            }
            core::configManager.release(true);
        }
    }

    ImGui::SetCursorPos(ImVec2(panelOrigin.x + layout.controlsPosition.x, panelOrigin.y + layout.controlsPosition.y));
    ImGui::BeginChild("WaterfallControls", layout.controlsSize);

    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Zoom").x / 2.0));
    ImGui::TextUnformatted("Zoom");
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - 10 * style::uiScale);
    float sliderHeight = std::clamp((layout.controlsSize.y - 3 * (ImGui::GetTextLineHeightWithSpacing() + 2 * ImGui::GetStyle().ItemSpacing.y)) / 3, 40.0f * style::uiScale, 150.0f * style::uiScale);
    ImVec2 wfSliderSize(20.0 * style::uiScale, sliderHeight);
    ImGui::BeginDisabled(core::isTuningLocked());
    if (ImGui::VSliderFloat("##_7_", wfSliderSize, &bw, 1.0, 0.0, "")) {
        double factor = (double)bw * (double)bw;

        // Map 0.0 -> 1.0 to 1000.0 -> bandwidth
        double wfBw = gui::waterfall.getBandwidth();
        double delta = wfBw - 1000.0;
        double finalBw = std::min<double>(1000.0 + (factor * delta), wfBw);

        gui::waterfall.setViewBandwidth(finalBw);
        if (vfo != NULL) {
            gui::waterfall.setViewOffset(vfo->centerOffset); // center vfo on screen
        }
    }

    ImGui::EndDisabled();
    ImGui::NewLine();

    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Max").x / 2.0));
    ImGui::TextUnformatted("Max");
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - 10 * style::uiScale);
    if (ImGui::VSliderFloat("##_8_", wfSliderSize, &fftMax, 0.0, -160.0f, "")) {
        fftMax = std::max<float>(fftMax, fftMin + 10);
        core::configManager.acquire();
        core::configManager.conf["max"] = fftMax;
        core::configManager.release(true);
    }

    ImGui::NewLine();

    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Min").x / 2.0));
    ImGui::TextUnformatted("Min");
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - 10 * style::uiScale);
    ImGui::SetItemUsingMouseWheel();
    if (ImGui::VSliderFloat("##_9_", wfSliderSize, &fftMin, 0.0, -160.0f, "")) {
        fftMin = std::min<float>(fftMax - 10, fftMin);
        core::configManager.acquire();
        core::configManager.conf["min"] = fftMin;
        core::configManager.release(true);
    }

    ImGui::EndChild();

    gui::waterfall.setFFTMin(fftMin);
    gui::waterfall.setFFTMax(fftMax);
    gui::waterfall.setWaterfallMin(fftMin);
    gui::waterfall.setWaterfallMax(fftMax);

    if (showMenu && layout.portrait) {
        ImGui::SetCursorPos(ImVec2(panelOrigin.x + layout.menuPosition.x, panelOrigin.y + layout.menuPosition.y));
        ImGui::BeginChild("MenuPanel", layout.menuSize, false, gui::menu.tabbed ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0);
        drawMenuPanel();
        ImGui::EndChild();
    }

    ImGui::End();

    if (showCredits) {
        credits::show();
    }

    if (demoWindow) {
        ImGui::ShowDemoWindow();
    }
}

void MainWindow::drawMenuPanel() {
    bool wasTabbed = gui::menu.tabbed;
    if (gui::menu.draw(firstMenuRender)) {
        core::configManager.acquire();
        json elements = json::array();
        for (const auto& option : gui::menu.order) {
            elements.push_back({{"name", option.name}, {"open", option.open}});
        }
        core::configManager.conf["menuElements"] = elements;
        core::configManager.conf["selectedMenuTab"] = gui::menu.selectedEntry;
        for (const auto& [name, entry] : core::moduleManager.instances) {
            if (!core::configManager.conf["moduleInstances"].contains(name)) { continue; }
            core::configManager.conf["moduleInstances"][name]["enabled"] = entry.instance->isEnabled();
        }
        core::configManager.release(true);
    }
    if (startedWithMenuClosed) { startedWithMenuClosed = false; }
    else { firstMenuRender = wasTabbed != gui::menu.tabbed; }
}

void MainWindow::drawDebugMenu(void* ctx) {
    auto* window = static_cast<MainWindow*>(ctx);
    ImGui::Text("Frame time: %.3f ms/frame", ImGui::GetIO().DeltaTime * 1000.0f);
    ImGui::Text("Framerate: %.1f FPS", ImGui::GetIO().Framerate);
    ImGui::Text("Center Frequency: %.0f Hz", gui::waterfall.getCenterFrequency());
    ImGui::Text("Source name: %s", window->sourceName.c_str());
    ImGui::Checkbox("Show demo window", &window->demoWindow);
    ImGui::Text("ImGui version: %s", ImGui::GetVersion());
    if (ImGui::Button("Test Bug")) { flog::error("Will this make the software crash?"); }
    if (ImGui::Button("Testing something") && !gui::menu.order.empty()) {
        gui::menu.order.front().open = true;
        window->firstMenuRender = true;
    }
    ImGui::Checkbox("WF Single Click", &gui::waterfall.VFOMoveSingleClick);
    ImGui::Checkbox("Lock Menu Order", &gui::menu.locked);
}

void MainWindow::setPlayState(bool _playing) {
    if (!_playing && core::isSessionLocked()) { return; }
    if (_playing == playing) { return; }
    if (_playing) {
        sigpath::iqFrontEnd.flushInputBuffer();
        sigpath::sourceManager.start();
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        playing = true;
        onPlayStateChange.emit(true);
    }
    else {
        playing = false;
        onPlayStateChange.emit(false);
        sigpath::sourceManager.stop();
        sigpath::iqFrontEnd.flushInputBuffer();
    }
}

void MainWindow::setViewBandwidthSlider(float bandwidth) {
    bw = bandwidth;
}

bool MainWindow::sdrIsRunning() {
    return playing;
}

bool MainWindow::isPlaying() {
    return playing;
}

void MainWindow::setFirstMenuRender() {
    firstMenuRender = true;
}
