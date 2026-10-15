#include <core.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <radio_interface.h>
#include <recorder_interface.h>
#include <dsp/sink/handler_sink.h>
#include "../../misc_modules/multi_receiver/src/capture.h"
#include <atomic>
#include <chrono>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

static void require(bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

static void checkFrameMutation() {
    struct State {
        Event<int> event;
        EventHandler<int> first;
        EventHandler<int> removed;
        EventHandler<int> added;
        int calls = 0;
    } state;
    state.first = EventHandler<int>([](int, void* context) {
        auto* state = static_cast<State*>(context);
        ++state->calls;
        state->event.unbindHandler(&state->first);
        state->event.unbindHandler(&state->removed);
        state->event.bindHandler(&state->added);
    }, &state);
    state.removed = EventHandler<int>([](int, void* context) { static_cast<State*>(context)->calls += 100; }, &state);
    state.added = EventHandler<int>([](int, void* context) { static_cast<State*>(context)->calls += 10; }, &state);
    state.event.bindHandler(&state.first);
    state.event.bindHandler(&state.removed);
    state.event.emitSnapshot(0);
    require(state.calls == 1, "Frame snapshot called a removed or newly added handler");
    state.event.emitSnapshot(0);
    require(state.calls == 11, "Frame snapshot did not call the deferred handler");
}

static void checkRecorder(const std::filesystem::path& output, const char* modulePath) {
    json settings{{"Test recorder", {{"mode", RECORDER_MODE_BASEBAND}, {"recPath", output.string()}, {"nameTemplate", "legacy_IQ"}}}};
    std::ofstream(output / "recorder_config.json") << settings.dump();
    core::moduleManager.loadModule(modulePath);
    require(core::moduleManager.createInstance("Test recorder", "recorder") == 0, "Cannot create legacy Recorder");
    core::moduleManager.postInit("Test recorder");
    gui::mainWindow.setPlayState(true);
    auto command = [](int code) { core::modComManager.callInterface("Test recorder", code, nullptr, nullptr); };
    for (int cycle = 0; cycle < 2; ++cycle) {
        command(RECORDER_IFACE_CMD_START);
        require(core::isTuningLocked() && !core::isSessionLocked(), "Legacy IQ recording did not automatically hold tuning");
        float bandwidth = 1000000;
        core::modComManager.callInterface("Test IQ", RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, nullptr);
        core::modComManager.callInterface("Test IQ", RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &bandwidth);
        require(bandwidth == 2000000, "IQ recording allowed a channel bandwidth change");
        sigpath::iqFrontEnd.setSampleRate(1000000);
        require(sigpath::iqFrontEnd.getSampleRate() == 2000000, "IQ recording allowed a source sample-rate change");
        core::setSessionLocked(true);
        command(RECORDER_IFACE_CMD_STOP);
        core::setSessionLocked(false);
        require(core::isTuningLocked(), "Manual unlock discarded the recording guard");
        core::retainTuningLock();
        command(RECORDER_IFACE_CMD_STOP);
        require(core::isTuningLocked(), "Stopping legacy IQ released another recorder's guard");
        core::releaseTuningLock();
        require(!core::isTuningLocked(), "Stopping legacy IQ leaked its guard");
    }
    command(RECORDER_IFACE_CMD_START);
    std::this_thread::sleep_for(std::chrono::milliseconds(5100));
    core::onFrame.emitSnapshot(0);
    require(!core::isTuningLocked(), "No-data failure leaked the legacy IQ guard");
    gui::mainWindow.setPlayState(false);
    require(core::moduleManager.deleteInstance("Test recorder") == 0, "Cannot remove legacy Recorder");
    core::moduleManager.modules.at("recorder").end();
    std::cout << "PASS: legacy full-band IQ auto-lock, bandwidth/sample-rate guards, restart, reference counting, no-data failure cleanup" << std::endl;
}

class CountingSink : public SinkManager::Sink {
public:
    CountingSink(SinkManager::Stream* stream, bool* state) : running(state) { drain.init(stream->sinkOut); }
    ~CountingSink() { stop(); }
    void start() override { *running = true; drain.start(); }
    void stop() override { drain.stop(); *running = false; }
    void menuHandler() override {}
private:
    bool* running;
    dsp::sink::Null<dsp::stereo_t> drain;
};

struct CaptureContext {
    ChannelCapture capture;
    std::atomic<uint64_t> count{0};
    uint64_t measured = 0;
    std::complex<double> wanted{};
    std::complex<double> rejected{};
};

static void consume(dsp::complex_t* input, int count, void* context) {
    auto* state = static_cast<CaptureContext*>(context);
    state->capture.enqueue(reinterpret_cast<float*>(input), count);
    uint64_t start = state->count.load();
    for (int index = 0; index < count; ++index) {
        uint64_t position = start + index;
        if (position < 4096) { continue; }
        std::complex<double> sample(input[index].re, input[index].im);
        double wantedPhase = -2 * 3.14159265358979323846 * 400000 * position / 1250000;
        double rejectedPhase = -2 * 3.14159265358979323846 * 600000 * position / 1250000;
        state->wanted += sample * std::complex<double>(std::cos(wantedPhase), std::sin(wantedPhase));
        state->rejected += sample * std::complex<double>(std::cos(rejectedPhase), std::sin(rejectedPhase));
        ++state->measured;
    }
    state->count.fetch_add(count);
}

int main(int argumentCount, char** arguments) {
    std::thread watchdog([] {
        std::this_thread::sleep_for(std::chrono::seconds(45));
        std::cerr << "FAIL: lifecycle timeout" << std::endl;
        std::_Exit(2);
    });
    watchdog.detach();
    try {
        require(argumentCount == 3 || argumentCount == 4, "Pass output directory, Radio module path and optional Recorder module path");
        checkFrameMutation();
        std::filesystem::path output(arguments[1]);
        std::filesystem::create_directories(output);
        core::args.define('r', "root", "Test root", output.string());
        json radioSettings{{"Test IQ", {{"selectedDemodId", RADIO_IFACE_MODE_FREE}, {"Free", {{"bandwidth", 1000000}}}}}};
        std::ofstream(output / "radio_config.json") << radioSettings.dump();
        core::configManager.conf["streams"] = json::object();
        core::configManager.conf["vfoOffsets"] = json::object();
        gui::waterfall.setFullWaterfallUpdate(false);
        gui::waterfall.setCenterFrequency(100000000);
        gui::waterfall.setBandwidth(4000000);
        dsp::stream<dsp::complex_t> input;
        sigpath::iqFrontEnd.init(&input, 4000000, false, 1, false, 1024, 20, IQFrontEnd::BLACKMAN,
            [](void*) -> float* { return nullptr; }, [](void*) {}, nullptr);
        core::moduleManager.loadModule(arguments[2]);
        require(core::moduleManager.createInstance("Test IQ", "radio") == 0, "Cannot instantiate Radio");
        bool audioRunning = false;
        SinkManager::SinkProvider provider;
        provider.ctx = &audioRunning;
        provider.create = [](SinkManager::Stream* stream, std::string, void* context) -> SinkManager::Sink* { return new CountingSink(stream, static_cast<bool*>(context)); };
        sigpath::sinkManager.registerSinkProvider("Test output", provider);
        sigpath::sinkManager.setStreamSink("Test IQ", "Test output");
        core::moduleManager.postInit("Test IQ");
        auto command = [](int code, void* input, void* output) { core::modComManager.callInterface("Test IQ", code, input, output); };
        auto adjustmentStart = std::chrono::steady_clock::now();
        double slowestAdjustment = 0;
        for (int step = 0; step < 48; ++step) {
            float requested = 251003 + step * 47939;
            auto started = std::chrono::steady_clock::now();
            command(RADIO_IFACE_CMD_SET_BANDWIDTH, &requested, nullptr);
            double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            slowestAdjustment = std::max(slowestAdjustment, elapsed);
            float actual = 0;
            command(RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &actual);
            require(actual == requested, "Efficient sample-rate choice changed the selected bandwidth");
        }
        double adjustmentTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - adjustmentStart).count();
        std::cout << "48 idle bandwidth changes: " << adjustmentTime << " ms total, slowest " << slowestAdjustment << " ms" << std::endl;
        require(slowestAdjustment < 250 && adjustmentTime < 3000, "Idle bandwidth changes stalled the UI thread");
        float wideBandwidth = 2750013;
        command(RADIO_IFACE_CMD_SET_BANDWIDTH, &wideBandwidth, nullptr);
        auto tuningStart = std::chrono::steady_clock::now();
        double slowestTune = 0;
        for (int step = 0; step < 120; ++step) {
            auto started = std::chrono::steady_clock::now();
            sigpath::vfoManager.setOffset("Test IQ", -595000 + step * 10000);
            gui::waterfall.setCenterFrequency(100000000 + step * 12345);
            core::onFrame.emitSnapshot(0);
            double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            slowestTune = std::max(slowestTune, elapsed);
        }
        double tuningTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tuningStart).count();
        std::cout << "120 idle wide-channel tunes: " << tuningTime << " ms total, slowest " << slowestTune << " ms" << std::endl;
        require(slowestTune < 250 && tuningTime < 3000, "Idle wide-channel tuning stalled the UI thread");
        sigpath::vfoManager.setOffset("Test IQ", 0);
        gui::waterfall.setCenterFrequency(100000000);
        float restoredBandwidth = 1000000;
        command(RADIO_IFACE_CMD_SET_BANDWIDTH, &restoredBandwidth, nullptr);
        int mode = -1;
        command(RADIO_IFACE_CMD_GET_MODE, nullptr, &mode);
        require(mode == RADIO_IFACE_MODE_FREE, "Free mode was not restored");
        require(!audioRunning, "Free mode started an audio output stream");
        float bandwidth = 0;
        command(RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &bandwidth);
        require(bandwidth == 1000000, "Free bandwidth was capped at 250 kHz");
        for (double direction : {-1.0, 1.0}) {
            sigpath::vfoManager.setOffset("Test IQ", direction * 1900000);
            require(sigpath::vfoManager.getOffset("Test IQ") == direction * 1500000, "Programmatic drag crossed an edge before the next frame");
            auto* selection = gui::waterfall.vfos.at("Test IQ");
            require(selection->getMaxDragBandwidth() == 1000000, "Bandwidth grip could be dragged beyond the capture edge");
            for (int repeat = 0; repeat < 20; ++repeat) {
                selection->setOffset(direction * (1900000 + repeat * 10000));
                require(selection->centerOffset == direction * 1500000, "Pointer drag moved beyond the capture edge");
            }
            sigpath::vfoManager.setCenterOffset("Test IQ", direction * 1900000);
            require(selection->centerOffset == direction * 1500000, "Center-offset setter bypassed the immediate edge limit");
            core::onFrame.emitSnapshot(0);
            command(RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &bandwidth);
            require(bandwidth == 1000000, "Dragging to a capture edge silently reduced the bandwidth");
            require(sigpath::vfoManager.getOffset("Test IQ") == direction * 1500000, "Free channel passband left the capture range");
        }
        bandwidth = 2400000;
        command(RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, nullptr);
        command(RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &bandwidth);
        require(bandwidth == 2400000 && sigpath::vfoManager.getOffset("Test IQ") == 800000, "Widening at the edge changed bandwidth instead of moving the channel");
        bandwidth = 1000000;
        command(RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, nullptr);
        sigpath::vfoManager.setOffset("Test IQ", 0);
        require(gui::waterfall.vfos.at("Test IQ")->getMaxDragBandwidth() == 4000000, "Moving away from the edge did not release the bandwidth grip limit");
        core::setSessionLocked(true);
        float rejectedBandwidth = 500000;
        command(RADIO_IFACE_CMD_SET_BANDWIDTH, &rejectedBandwidth, nullptr);
        mode = RADIO_IFACE_MODE_WFM;
        command(RADIO_IFACE_CMD_SET_MODE, &mode, nullptr);
        command(RADIO_IFACE_CMD_GET_MODE, nullptr, &mode);
        command(RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &bandwidth);
        require(mode == RADIO_IFACE_MODE_FREE && bandwidth == 1000000, "Session lock changed the Free channel");
        core::setSessionLocked(false);
        sigpath::vfoManager.setOffset("Test IQ", 300000);
        core::onFrame.emitSnapshot(0);
        CaptureContext state;
        require(state.capture.start((output / "wide_IQ").string(), 1250000, true), "Cannot start synthetic IQ capture");
        auto* recording = sigpath::iqFrontEnd.addVFO("__test_recording", 1250000, bandwidth, 300000);
        require(recording != nullptr, "Cannot create independent IQ channelizer");
        dsp::sink::Handler<dsp::complex_t> sink(&recording->out, consume, &state);
        sink.start();
        sigpath::iqFrontEnd.start();
        for (int block = 0; block < 160; ++block) {
            for (int index = 0; index < 4096; ++index) {
                double position = block * 4096 + index;
                double wanted = 2 * 3.14159265358979323846 * 700000 * position / 4000000;
                double rejected = 2 * 3.14159265358979323846 * 900000 * position / 4000000;
                input.writeBuf[index] = {static_cast<float>(0.5 * std::cos(wanted) + 0.25 * std::cos(rejected)),
                    static_cast<float>(0.5 * std::sin(wanted) + 0.25 * std::sin(rejected))};
            }
            require(input.swap(4096), "Synthetic IQ source stopped");
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (state.count.load() < 204000 && std::chrono::steady_clock::now() < deadline) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        sigpath::iqFrontEnd.stop();
        sink.stop();
        state.capture.stop();
        sigpath::iqFrontEnd.removeVFO("__test_recording");
        require(state.count.load() >= 204000, "Wide IQ channel stopped producing samples");
        require(state.capture.getError().empty(), "Wide IQ capture failed: " + state.capture.getError());
        require(state.capture.bytesWritten() == state.count.load() * 8, "IQ recorder lost channelizer samples");
        double wantedAmplitude = std::abs(state.wanted) / state.measured;
        double rejectedAmplitude = std::abs(state.rejected) / state.measured;
        std::cout << "1 MHz channel: +400 kHz amplitude=" << wantedAmplitude << ", +600 kHz amplitude=" << rejectedAmplitude << std::endl;
        require(wantedAmplitude > 0.2 && rejectedAmplitude < wantedAmplitude / 30, "IQ passband did not match the configured shared bandwidth");
        mode = RADIO_IFACE_MODE_WFM;
        command(RADIO_IFACE_CMD_SET_MODE, &mode, nullptr);
        bandwidth = 1000000;
        command(RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, nullptr);
        command(RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &bandwidth);
        require(bandwidth == 250000 && audioRunning, "WFM did not restore its audio/width limits");
        require(std::isinf(gui::waterfall.vfos.at("Test IQ")->maxCenterOffset), "Free-mode drag limits leaked into WFM");
        mode = RADIO_IFACE_MODE_FREE;
        command(RADIO_IFACE_CMD_SET_MODE, &mode, nullptr);
        require(!audioRunning, "Returning to Free mode left audio running");
        sigpath::vfoManager.setOffset("Test IQ", 0);
        bandwidth = 4000000;
        command(RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, nullptr);
        command(RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &bandwidth);
        require(bandwidth == 4000000, "Full capture bandwidth was rejected");
        sigpath::vfoManager.setOffset("Test IQ", 100000);
        require(sigpath::vfoManager.getOffset("Test IQ") == 0, "Full-span channel moved before the next frame");
        sigpath::iqFrontEnd.setSampleRate(2000000);
        core::onFrame.emitSnapshot(0);
        command(RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &bandwidth);
        require(bandwidth == 2000000, "Free bandwidth did not follow a reduced source span");
        if (argumentCount == 4) { checkRecorder(output, arguments[3]); }
        require(core::moduleManager.deleteInstance("Test IQ") == 0, "Cannot remove Free channel");
        core::moduleManager.modules.at("radio").end();
        std::cout << "PASS: Free mode, no audio stream, lock guards, MHz-width IQ/filtering, mode switches, full span, source-rate change, cleanup" << std::endl;
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << std::endl;
        return 1;
    }
}
