#include "../../misc_modules/multi_receiver/src/capture.h"
#include <core.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <signal_path/multi_receiver.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

static void checkWave(const std::string& filename, uint16_t codec, uint16_t bitDepth, uint32_t bytes) {
    std::ifstream input(filename, std::ios::binary);
    char header[44]{};
    input.read(header, sizeof(header));
    require(input.good(), "Cannot read WAV header: " + filename);
    require(std::memcmp(header, "RIFF", 4) == 0 && std::memcmp(header + 8, "WAVE", 4) == 0, "Invalid WAV container");
    uint32_t rootSize = 0;
    uint32_t dataSize = 0;
    uint16_t actualCodec = 0;
    uint16_t actualDepth = 0;
    std::memcpy(&rootSize, header + 4, 4);
    std::memcpy(&dataSize, header + 40, 4);
    std::memcpy(&actualCodec, header + 20, 2);
    std::memcpy(&actualDepth, header + 34, 2);
    require(rootSize == bytes + 36 && dataSize == bytes, "Invalid WAV sizes");
    require(actualCodec == codec && actualDepth == bitDepth, "Invalid WAV encoding");
    require(std::filesystem::file_size(filename) == bytes + 44, "WAV file was not fully written");
}

static void checkReceiverGroups(const std::filesystem::path& output, const std::string& modulePath) {
    core::args.define('r', "root", "Test root", output.string());
    core::configManager.conf["streams"] = json::object();
    core::configManager.conf["vfoOffsets"] = json::object();
    gui::waterfall.setFullWaterfallUpdate(false);
    gui::waterfall.setCenterFrequency(100000000);
    gui::waterfall.setBandwidth(1000000);
    dsp::stream<dsp::complex_t> input;
    sigpath::iqFrontEnd.init(&input, 1000000, false, 1, false, 1024, 20, IQFrontEnd::BLACKMAN,
        [](void*) -> float* { return nullptr; }, [](void*) {}, nullptr);
    core::moduleManager.loadModule(modulePath);
    require(core::moduleManager.modules.count("multi_receiver") != 0, "Cannot load Multi Receiver");
    require(core::moduleManager.createInstance("Test receiver", "multi_receiver") == 0, "Cannot create Multi Receiver");
    core::moduleManager.postInit("Test receiver");
    core::onFrame.emitSnapshot(0);
    json baseline;
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_GET_SETUP, nullptr, &baseline);
    require(baseline["channels"].empty(), "New receiver unexpectedly contains channels");
    json channel{{"name", "RX 1"}, {"frequency", 102000000}, {"bandwidth", 150000}, {"mode", 1}, {"iqSampleRate", 300000}};
    json invalid{{"centerFrequency", 100000000}, {"channels", json::array({channel})}};
    std::string error;
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_LOAD_SETUP, &invalid, &error);
    require(error.find("outside") != std::string::npos, "Out-of-range channel was accepted");
    channel["frequency"] = 100400000;
    channel["mode"] = 4;
    invalid["channels"] = json::array({channel});
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_LOAD_SETUP, &invalid, &error);
    require(error.find("outside") != std::string::npos, "Out-of-range USB passband was accepted");
    channel["frequency"] = 99600000;
    channel["mode"] = 6;
    invalid["channels"] = json::array({channel});
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_LOAD_SETUP, &invalid, &error);
    require(error.find("outside") != std::string::npos, "Out-of-range LSB passband was accepted");
    channel["mode"] = 1;
    channel["frequency"] = 100000000;
    channel["audio"] = "invalid";
    invalid["channels"] = json::array({channel});
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_LOAD_SETUP, &invalid, &error);
    require(!error.empty(), "Invalid recording flag was accepted");
    channel["audio"] = true;
    invalid["channels"] = json::array({channel, channel});
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_LOAD_SETUP, &invalid, &error);
    require(!error.empty(), "Duplicate channel names were accepted");
    invalid["channels"] = json::array();
    for (int index = 0; index < 33; ++index) { invalid["channels"].push_back(channel); }
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_LOAD_SETUP, &invalid, &error);
    require(error.find("32") != std::string::npos, "Channel safety limit was not enforced");
    json unchanged;
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_GET_SETUP, nullptr, &unchanged);
    require(unchanged == baseline, "Rejected setup modified existing state");
    core::setSessionLocked(true);
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_LOAD_SETUP, &baseline, &error);
    require(!error.empty(), "Locked receiver accepted a group replacement");
    core::setSessionLocked(false);
    baseline["centerFrequency"] = 100100000;
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_LOAD_SETUP, &baseline, &error);
    require(error.empty(), "Valid setup was rejected");
    core::onFrame.emitSnapshot(0);
    core::modComManager.callInterface("Test receiver", MULTI_RECEIVER_GET_SETUP, nullptr, &unchanged);
    require(unchanged["centerFrequency"] == 100100000, "Deferred group application failed");
    core::moduleManager.deleteInstance("Test receiver");
    core::moduleManager.modules.at("multi_receiver").end();
    sigpath::iqFrontEnd.stop();
}

int main(int argumentCount, char** arguments) {
    try {
        require(argumentCount == 2 || argumentCount == 3, "Pass a test output directory and optional Multi Receiver module path");
        std::filesystem::path output(arguments[1]);
        std::filesystem::create_directories(output);
        std::vector<float> samples(2048);
        for (size_t index = 0; index < samples.size(); index += 2) {
            samples[index] = 0.25f;
            samples[index + 1] = -0.5f;
        }
        ChannelCapture audio(16384);
        ChannelCapture iq(16384);
        require(audio.start((output / "audio").string(), 48000, false), "Audio capture did not start");
        require(iq.start((output / "iq").string(), 48000, true), "IQ capture did not start");
        for (int block = 0; block < 8; ++block) {
            audio.enqueue(samples.data(), 1024);
            iq.enqueue(samples.data(), 1024);
        }
        audio.stop();
        iq.stop();
        require(audio.getError().empty() && iq.getError().empty(), "Capture failed");
        require(audio.bytesWritten() == 32768 && iq.bytesWritten() == 65536, "Capture dropped queued samples on stop");
        require(audio.fileCount() == 2 && iq.fileCount() == 4, "File segmentation failed");
        for (int part = 1; part <= 2; ++part) { checkWave((output / ("audio_part" + std::to_string(part) + ".wav")).string(), 1, 16, 16384); }
        for (int part = 1; part <= 4; ++part) { checkWave((output / ("iq_part" + std::to_string(part) + ".wav")).string(), 3, 32, 16384); }
        {
            std::ifstream input(output / "iq_part1.wav", std::ios::binary);
            input.seekg(44);
            float pair[2]{};
            input.read(reinterpret_cast<char*>(pair), sizeof(pair));
            require(pair[0] == 0.25f && pair[1] == -0.5f, "IQ values or channel order changed");
        }
        require(!audio.start((output / "missing" / "invalid").string(), 48000, false), "Invalid output directory was accepted");
        require(!audio.getError().empty(), "Open failure was silent");
        require(audio.start((output / "restart").string(), 48000, false), "Restart after failure did not recover");
        audio.enqueue(samples.data(), 1024);
        audio.stop();
        require(audio.getError().empty() && audio.bytesWritten() == 4096, "Restart preserved stale state");
        checkWave((output / "restart_part1.wav").string(), 1, 16, 4096);

        require(iq.start((output / "overflow").string(), 48000, true), "Overflow test did not start");
        iq.enqueue(samples.data(), 5 * 1024 * 1024);
        iq.stop();
        require(iq.getError().find("overflow") != std::string::npos, "Queue overflow was silent");

        require(audio.start((output / "stalled").string(), 48000, false), "Stall test did not start");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(7);
        while (audio.isActive() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        audio.stop();
        require(audio.getError().find("No samples") != std::string::npos, "No-sample recording failure was silent");

        require(core::setScreenOrientation(core::ScreenOrientation::Portrait), "Cannot select portrait orientation");
        require(core::getScreenOrientation() == core::ScreenOrientation::Portrait && !core::isSessionLocked() && !core::isTuningLocked(), "Orientation selection blocks radio controls");
        core::setSessionLocked(true);
        require(core::isSessionLocked() && core::isTuningLocked(), "Session lock failed");
        require(!core::setScreenOrientation(core::ScreenOrientation::Landscape) && core::getScreenOrientation() == core::ScreenOrientation::Portrait, "Screen rotation bypassed session lock");
        core::retainTuningLock();
        core::setSessionLocked(false);
        require(!core::isSessionLocked() && core::isTuningLocked(), "Unlock bypassed an active capture guard");
        require(core::getScreenOrientation() == core::ScreenOrientation::Portrait, "Session unlock discarded orientation selection");
        require(core::setScreenOrientation(core::ScreenOrientation::Landscape), "Recording guard unexpectedly blocks screen rotation");
        require(core::getScreenOrientation() == core::ScreenOrientation::Landscape, "Landscape selection failed");
        core::setSessionLocked(true);
        require(!core::setScreenOrientation(core::ScreenOrientation::Automatic), "Automatic orientation bypassed session lock");
        core::setSessionLocked(false);
        require(core::setScreenOrientation(core::ScreenOrientation::Automatic), "Session orientation guard not released");
        require(!core::setScreenOrientation(static_cast<core::ScreenOrientation>(-1)), "Invalid orientation accepted");
        require(core::getScreenOrientation() == core::ScreenOrientation::Automatic, "Invalid orientation changed selection");
        core::releaseTuningLock();
        require(!core::isTuningLocked(), "Capture guard was not released");
        require(!core::isScreenKeptOn(), "Screen-on option did not default to off");
        core::setKeepScreenOn(true);
        require(core::isScreenKeptOn() && !core::isSessionLocked() && !core::isTuningLocked(), "Screen-on option locks radio controls");
        core::setSessionLocked(true);
        core::setSessionLocked(false);
        require(core::isScreenKeptOn(), "Session unlock discarded screen-on preference");
        core::setKeepScreenOn(false);
        require(!core::isScreenKeptOn(), "Screen-on option did not turn off");
        core::setSessionLocked(true);
        core::setKeepScreenOn(false);
        require(core::isScreenKeptOn(), "Screen-on option bypassed session lock");
        core::setSessionLocked(false);
        require(!core::isScreenKeptOn(), "Session unlock failed to restore screen timeout");
        if (argumentCount == 3) {
            checkReceiverGroups(output, arguments[2]);
            std::cout << "PASS: receiver group validation, rejection preserves state, locked loading, deferred restore\n";
        }
        std::cout << "PASS: parallel audio/IQ, exact sample counts, segmentation, WAV headers, IQ order, restart, open errors, queue overflow, no-data timeout, orientation and keep-screen-on/session guards\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
