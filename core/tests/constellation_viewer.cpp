#include "../../misc_modules/constellation/src/main.cpp"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

int main() {
    try {
        gui::waterfall.setFullWaterfallUpdate(false);
        gui::waterfall.setCenterFrequency(601282000);
        gui::waterfall.setBandwidth(3000000);
        dsp::stream<dsp::complex_t> input;
        sigpath::iqFrontEnd.init(&input, 3000000, false, 1, false, 1024, 20, IQFrontEnd::BLACKMAN,
            [](void*) -> float* { return nullptr; }, [](void*) {}, nullptr);
        core::configManager.conf["vfoOffsets"] = json::object();
        auto* channel = sigpath::vfoManager.createVFO("Test channel", ImGui::WaterfallVFO::REF_CENTER, 0, 673217, 1500000, 1, 3000000, false);
        require(channel != nullptr, "Cannot create test channel");
        gui::waterfall.selectedVFO = "Test channel";
        gui::mainWindow.setPlayState(true);
        for (int mode : {1, 3, 8, 9, 10}) {
            config.conf = {{"show", true}, {"followSelected", true}, {"mode", mode}, {"automaticSymbols", false}, {"symbolRate", mode >= 9 ? 1 : 500000}, {"rolloff", 0.35}};
            ConstellationModule viewer("Test viewer");
            viewer.postInit();
            core::onFrame.emitSnapshot(0);
            auto* duplicate = sigpath::iqFrontEnd.addVFO("__constellation_Test viewer", 1500000, 673217, 0);
            if (duplicate) { sigpath::iqFrontEnd.removeVFO("__constellation_Test viewer"); }
            require(duplicate == nullptr, "Valid manual rate was rejected because bandwidth was below rate x (1 + rolloff)");
            viewer.disable();
            auto* released = sigpath::iqFrontEnd.addVFO("__constellation_Test viewer", 1500000, 673217, 0);
            require(released != nullptr, "Disabling viewer did not release its analysis stream");
            sigpath::iqFrontEnd.removeVFO("__constellation_Test viewer");
        }
        gui::mainWindow.setPlayState(false);
        sigpath::vfoManager.deleteVFO(channel);
        std::cout << "PASS: production viewer accepts valid manual rates, broadcast modes bypass single-carrier baud gating, cleanup works" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
