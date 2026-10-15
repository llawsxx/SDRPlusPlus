#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

static void frame(float horizontal, float vertical, bool pressed) {
    auto& input = ImGui::GetIO();
    input.AddMousePosEvent(horizontal, vertical);
    input.AddMouseButtonEvent(ImGuiMouseButton_Left, pressed);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(input.DisplaySize);
    ImGui::Begin("Touch tuning test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    gui::waterfall.draw();
    ImGui::End();
    ImGui::Render();
}

static void gesture(float horizontal, float vertical, float distance) {
    frame(horizontal, vertical, false);
    frame(horizontal, vertical, true);
    frame(horizontal + distance, vertical, true);
    frame(horizontal + distance, vertical, false);
}

struct TestChannel {
    ImGui::WaterfallVFO selection{};
    EventHandler<double> changed;
    int changes = 0;

    TestChannel() {
        selection.minBandwidth = 10000;
        selection.maxBandwidth = 1000000;
        selection.bandwidthLocked = false;
        selection.snapInterval = 1000;
        selection.setBandwidth(200000);
        selection.setOffset(0);
        updateLimits();
        changed = EventHandler<double>([](double, void* context) {
            auto* channel = static_cast<TestChannel*>(context);
            ++channel->changes;
            channel->updateLimits();
        }, this);
        selection.onUserChangedBandwidth.bindHandler(&changed);
    }

    void updateLimits() {
        selection.minCenterOffset = -(1000000 - selection.bandwidth) / 2;
        selection.maxCenterOffset = (1000000 - selection.bandwidth) / 2;
    }
};

int main() {
    try {
        ImGui::CreateContext();
        auto& input = ImGui::GetIO();
        input.DisplaySize = ImVec2(800, 600);
        input.DeltaTime = 1.0f / 60;
        input.IniFilename = nullptr;
        input.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int atlasWidth;
        int atlasHeight;
        input.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
        style::uiScale = 1;
        gui::mainWindow.lockWaterfallControls = false;
        gui::waterfall.setFullWaterfallUpdate(false);
        gui::waterfall.hideWaterfall();
        gui::waterfall.setRawFFTSize(1024);
        gui::waterfall.setBandwidth(1000000);
        gui::waterfall.setCenterFrequency(100000000);
        TestChannel channel;
        gui::waterfall.vfos["Test channel"] = &channel.selection;
        gui::waterfall.selectedVFO = "Test channel";
        frame(-100, -100, false);
        frame(-100, -100, false);
        float left = gui::waterfall.tuningAreaMin.x;
        float width = gui::waterfall.tuningAreaMax.x - left;
        float frequencyControl = left + width / 4;
        float bandwidthControl = left + 3 * width / 4;
        float vertical = (gui::waterfall.tuningAreaMin.y + gui::waterfall.tuningAreaMax.y) / 2;
        require(gui::waterfall.tuningAreaMax.y < gui::waterfall.freqAreaMin.y, "Controls are not above the frequency scale");
        require(gui::waterfall.tuningAreaMax.y - gui::waterfall.tuningAreaMin.y >= 47, "Controls do not have touch-sized hit targets");
        gesture(frequencyControl, vertical, 0);
        gesture(bandwidthControl, vertical, 0);
        require(channel.selection.generalOffset == 0 && channel.selection.bandwidth == 200000 && channel.changes == 0, "Tapping a control changed tuning");
        for (bool singleClick : {false, true}) {
            gui::waterfall.VFOMoveSingleClick = singleClick;
            float graphVertical = (gui::waterfall.fftAreaMin.y + gui::waterfall.fftAreaMax.y) / 2;
            gesture(left + width * 0.85f, graphVertical, -30);
            require(channel.selection.generalOffset == 0 && channel.selection.bandwidth == 200000, "Spectrum tap or drag retuned a channel");
        }
        gesture(frequencyControl, vertical, 60);
        require(channel.selection.generalOffset > 0 && channel.selection.bandwidth == 200000 && channel.changes == 0, "Frequency control changed the wrong parameter");
        channel.selection.setOffset(0);
        frame(frequencyControl, vertical, false);
        frame(frequencyControl, vertical, true);
        frame(frequencyControl + 2000, vertical, true);
        require(channel.selection.centerOffset == 400000, "Frequency drag did not stop at the capture edge");
        frame(frequencyControl + 2020, vertical, true);
        require(channel.selection.centerOffset == 400000, "Continuing outward drag crossed the capture edge");
        frame(frequencyControl + 2000, vertical, true);
        require(channel.selection.centerOffset < 400000, "Reversing an edge drag had a dead zone");
        frame(frequencyControl + 2000, vertical, false);
        channel.selection.setOffset(0);
        gesture(bandwidthControl, vertical, 50);
        require(channel.selection.bandwidth > 200000 && channel.selection.generalOffset == 0 && channel.changes > 0, "Bandwidth control did not use the bandwidth callback");
        frame(bandwidthControl, vertical, false);
        frame(bandwidthControl, vertical, true);
        frame(bandwidthControl + 2000, vertical, true);
        require(channel.selection.bandwidth == 1000000 && channel.selection.generalOffset == 0, "Bandwidth drag changed the center or exceeded the capture");
        frame(bandwidthControl + 1980, vertical, true);
        require(channel.selection.bandwidth < 1000000, "Reversing a bandwidth drag had a dead zone");
        frame(bandwidthControl + 1980, vertical, false);
        double before = channel.selection.bandwidth;
        core::setSessionLocked(true);
        gesture(bandwidthControl, vertical, -70);
        gesture(frequencyControl, vertical, 70);
        require(channel.selection.bandwidth == before && channel.selection.generalOffset == 0, "Session lock allowed touch tuning");
        core::setSessionLocked(false);
        core::retainTuningLock();
        gesture(bandwidthControl, vertical, -70);
        require(channel.selection.bandwidth == before, "Recording guard allowed touch bandwidth changes");
        core::releaseTuningLock();
        channel.selection.bandwidthLocked = true;
        gesture(bandwidthControl, vertical, -70);
        require(channel.selection.bandwidth == before, "Fixed-bandwidth mode accepted touch resizing");
        channel.selection.bandwidthLocked = false;
        channel.selection.setBandwidth(200000);
        channel.updateLimits();
        channel.selection.setOffset(0);
        frame(frequencyControl, vertical, false);
        frame(frequencyControl, vertical, true);
        frame(frequencyControl + 30, vertical, true);
        double firstFrequency = 100000000 + channel.selection.generalOffset;
        gui::waterfall.setCenterFrequency(firstFrequency);
        channel.selection.setOffset(0);
        frame(frequencyControl + 60, vertical, true);
        double secondFrequency = firstFrequency + channel.selection.generalOffset;
        require(std::abs((secondFrequency - firstFrequency) - (firstFrequency - 100000000)) <= 1000, "Center-tuning recenter accelerated an active drag");
        frame(frequencyControl + 60, vertical, false);
        TestChannel replacement;
        frame(frequencyControl, vertical, false);
        frame(frequencyControl, vertical, true);
        gui::waterfall.vfos["Replacement"] = &replacement.selection;
        gui::waterfall.selectedVFO = "Replacement";
        frame(frequencyControl + 50, vertical, true);
        frame(frequencyControl + 50, vertical, false);
        require(replacement.selection.generalOffset == 0, "Changing channels applied an old drag to the new channel");
        gui::waterfall.vfos.clear();
        gui::waterfall.selectedVFO.clear();
        gesture(frequencyControl, vertical, 50);
        gesture(bandwidthControl, vertical, 50);
        ImGui::DestroyContext();
        std::cout << "PASS: ImGui touch targets, no tap retuning, independent frequency/bandwidth drags, callback, edges/reversal, session/recording locks, fixed width, recenter continuity, channel-change cancellation, no channel" << std::endl;
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << std::endl;
        return 1;
    }
}
