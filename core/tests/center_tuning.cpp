#include <core.h>
#include <gui/gui.h>
#include <gui/tuner.h>
#include <signal_path/signal_path.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

static VFOManager::VFO* addChannel(const std::string& name, int reference, double frequency, double bandwidth) {
    auto* channel = sigpath::vfoManager.createVFO(name, reference, frequency - gui::waterfall.getCenterFrequency(),
        bandwidth, 250000, bandwidth, bandwidth, false);
    require(channel != nullptr, "Cannot create test channel");
    double limit = (gui::waterfall.getBandwidth() - bandwidth) / 2;
    channel->wtfVFO->minCenterOffset = -limit;
    channel->wtfVFO->maxCenterOffset = limit;
    return channel;
}

static void checkChannel(const std::string& name, double frequency, double bandwidth) {
    auto* channel = gui::waterfall.vfos.at(name);
    require(std::abs(gui::waterfall.getCenterFrequency() + channel->generalOffset - frequency) < 0.001,
        "Center tuning changed another channel's absolute frequency");
    require(channel->bandwidth == bandwidth, "Center tuning changed a channel's bandwidth");
    require(channel->lowerOffset >= -2000000.001 && channel->upperOffset <= 2000000.001,
        "A channel passband left the captured range");
}

int main() {
    try {
        core::args.define('s', "server", "Test server flag", false);
        core::configManager.conf = json::object();
        gui::waterfall.setFullWaterfallUpdate(false);
        gui::waterfall.setCenterFrequency(100000000);
        gui::waterfall.setBandwidth(4000000);
        gui::waterfall.setViewBandwidth(1000000);
        dsp::stream<dsp::complex_t> input;
        sigpath::iqFrontEnd.init(&input, 4000000, false, 1, false, 1024, 20, IQFrontEnd::BLACKMAN,
            [](void*) -> float* { return nullptr; }, [](void*) {}, nullptr);
        int retunes = 0;
        SourceManager::SourceHandler source{};
        source.stream = &input;
        source.selectHandler = [](void*) {};
        source.deselectHandler = [](void*) {};
        source.tuneHandler = [](double, void* context) { ++*static_cast<int*>(context); };
        source.ctx = &retunes;
        sigpath::sourceManager.registerSource("Center test", &source);
        sigpath::sourceManager.selectSource("Center test");
        auto* first = addChannel("First", ImGui::WaterfallVFO::REF_CENTER, 100000000, 200000);
        auto* second = addChannel("Second", ImGui::WaterfallVFO::REF_CENTER, 100600000, 150000);
        auto* third = addChannel("Third", ImGui::WaterfallVFO::REF_CENTER, 99500000, 100000);
        gui::waterfall.selectedVFO = "Second";
        tuner::centerTuning("Second", 100600000);
        require(gui::waterfall.getCenterFrequency() == 100600000 && second->wtfVFO->generalOffset == 0,
            "Selected channel was not centered");
        checkChannel("First", 100000000, 200000);
        checkChannel("Third", 99500000, 100000);
        int beforeRetunes = retunes;
        double beforeView = gui::waterfall.getViewOffset();
        tuner::centerTuning("First", 100100000);
        require(retunes == beforeRetunes && gui::waterfall.getCenterFrequency() == 100600000 &&
            gui::waterfall.getViewOffset() == beforeView && gui::freqSelect.frequency == 100600000,
            "An unselected channel recentered the receiver or frequency display");
        checkChannel("First", 100100000, 200000);
        tuner::centerTuning("Second", 101000000);
        checkChannel("First", 100100000, 200000);
        checkChannel("Third", 99500000, 100000);
        gui::waterfall.selectedVFO = "Third";
        tuner::centerTuning("Third", 99500000);
        require(gui::waterfall.getCenterFrequency() == 99500000 && third->wtfVFO->generalOffset == 0,
            "Center tuning did not follow the new selection");
        checkChannel("First", 100100000, 200000);
        checkChannel("Second", 101000000, 150000);
        beforeRetunes = retunes;
        for (int repeat = 0; repeat < 20; repeat++) { tuner::centerTuning("Third", 99500000); }
        require(retunes == beforeRetunes, "An already-centered channel repeatedly retuned the source");
        gui::waterfall.selectedVFO = "Second";
        tuner::centerTuning("Second", 102000000);
        require(gui::waterfall.getCenterFrequency() == 101450000,
            "Capture center was not constrained by the other channel's passband");
        require(gui::waterfall.getViewOffset() == second->wtfVFO->generalOffset,
            "The view did not follow the selected channel within the shared capture");
        checkChannel("First", 100100000, 200000);
        checkChannel("Second", 102000000, 150000);
        checkChannel("Third", 99500000, 100000);
        require(core::configManager.conf["frequency"] == gui::waterfall.getCenterFrequency(), "Capture center was not saved");
        for (const auto& [name, channel] : gui::waterfall.vfos) {
            require(core::configManager.conf["vfoOffsets"][name] == channel->generalOffset, "Compensated offset was not saved");
        }
        beforeRetunes = retunes;
        tuner::centerTuning("Second", 103500000);
        require(retunes == beforeRetunes && gui::freqSelect.frequency == 102000000,
            "Impossible multi-channel tuning was not rejected");
        checkChannel("Second", 102000000, 150000);
        core::setSessionLocked(true);
        tuner::centerTuning("Second", 101500000);
        checkChannel("Second", 102000000, 150000);
        core::setSessionLocked(false);
        core::retainTuningLock();
        tuner::centerTuning("Second", 101500000);
        checkChannel("Second", 102000000, 150000);
        core::releaseTuningLock();
        tuner::centerTuning("Second", std::numeric_limits<double>::quiet_NaN());
        tuner::centerTuning("Missing", 100000000);
        require(retunes == beforeRetunes, "Locked or invalid tuning reached the source");
        sigpath::vfoManager.deleteVFO(first);
        sigpath::vfoManager.deleteVFO(second);
        sigpath::vfoManager.deleteVFO(third);
        auto* lower = addChannel("Lower", ImGui::WaterfallVFO::REF_LOWER, 100200000, 100000);
        auto* upper = addChannel("Upper", ImGui::WaterfallVFO::REF_UPPER, 100500000, 100000);
        gui::waterfall.selectedVFO = "Lower";
        tuner::centerTuning("Lower", 100200000);
        require(lower->wtfVFO->generalOffset == 0 && lower->wtfVFO->centerOffset == 50000,
            "Lower-side reference was not preserved");
        checkChannel("Upper", 100500000, 100000);
        gui::waterfall.selectedVFO = "Upper";
        tuner::centerTuning("Upper", 100500000);
        require(upper->wtfVFO->generalOffset == 0 && upper->wtfVFO->centerOffset == -50000,
            "Upper-side reference was not preserved");
        checkChannel("Lower", 100200000, 100000);
        sigpath::vfoManager.deleteVFO(lower);
        sigpath::vfoManager.deleteVFO(upper);
        sigpath::sourceManager.unregisterSource("Center test");
        std::cout << "PASS: selected-only centering, selection changes, independent frequencies, bandwidths, shared capture edges, saved offsets, sidebands, invalid requests and session/recording locks\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
