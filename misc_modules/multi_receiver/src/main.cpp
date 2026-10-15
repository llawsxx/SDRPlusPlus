#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/tuner.h>
#include <gui/widgets/folder_select.h>
#include <signal_path/signal_path.h>
#include <signal_path/multi_receiver.h>
#include <radio_interface.h>
#include <dsp/sink/handler_sink.h>
#include <filesystem>
#include <memory>
#include <set>
#include <cmath>
#include <cctype>
#include "capture.h"

SDRPP_MOD_INFO{
    "multi_receiver", "Multi-channel reception and recording", "SDR++ contributors", 1, 0, 0, 1
};

ConfigManager config;
static const char* modeNames = "NFM\0WFM\0AM\0DSB\0USB\0CW\0LSB\0RAW\0Free (IQ only)\0";

static double passbandCenter(double frequency, double bandwidth, int mode) {
    if (mode == RADIO_IFACE_MODE_USB) { return frequency + bandwidth / 2; }
    if (mode == RADIO_IFACE_MODE_LSB) { return frequency - bandwidth / 2; }
    return frequency;
}

struct ReceiveChannel {
    std::string name;
    double frequency = 0;
    double bandwidth = 150000;
    double iqSampleRate = 300000;
    int mode = RADIO_IFACE_MODE_WFM;
    bool monitor = true;
    bool selectAudio = true;
    bool selectIQ = false;
    bool recording = false;
    bool stopping = false;
    bool audioStarted = false;
    bool iqStarted = false;
    bool tuningHeld = false;
    bool audioBound = false;
    bool iqBound = false;
    bool audioSinkInitialized = false;
    bool iqSinkInitialized = false;
    std::string reportedError;
    dsp::stream<dsp::stereo_t>* audioStream = nullptr;
    dsp::channel::RxVFO* iqVFO = nullptr;
    dsp::sink::Handler<dsp::stereo_t> audioSink;
    dsp::sink::Handler<dsp::complex_t> iqSink;
    ChannelCapture audioCapture;
    ChannelCapture iqCapture;
    std::string error;

    json serialize() const {
        return json{{"name", name}, {"frequency", frequency}, {"bandwidth", bandwidth},
            {"mode", mode}, {"iqSampleRate", iqSampleRate}, {"monitor", monitor},
            {"audio", selectAudio}, {"iq", selectIQ}};
    }
};

class MultiReceiver : public ModuleManager::Instance {
public:
    MultiReceiver(std::string instanceName) : name(instanceName), directory("%ROOT%/recordings") {
        frameHandler = EventHandler<int>(frame, this);
        core::onFrame.bindHandler(&frameHandler);
        gui::menu.registerEntry(name, draw, this, this);
        core::modComManager.registerInterface("multi_receiver", name, interfaceHandler, this);
        config.acquire();
        if (config.conf.contains("directory") && config.conf["directory"].is_string()) { directory.setPath(config.conf["directory"]); }
        pendingSetup = config.conf.value("setup", json::object());
        config.release();
    }

    ~MultiReceiver() {
        core::onFrame.unbindHandler(&frameHandler);
        core::modComManager.unregisterInterface(name);
        gui::menu.removeEntry(name);
        clearChannels();
        sigpath::sinkManager.clearMonitorSelection();
    }

    void postInit() { initialized = true; }
    void enable() { enabled = true; }
    void disable() {
        if (core::isSessionLocked() || anyRecording()) { return; }
        enabled = false;
        sigpath::sinkManager.clearMonitorSelection();
    }
    bool isEnabled() { return enabled; }

private:
    static void audioHandler(dsp::stereo_t* samples, int count, void* context) {
        static_cast<ReceiveChannel*>(context)->audioCapture.enqueue(reinterpret_cast<float*>(samples), count);
    }

    static void iqHandler(dsp::complex_t* samples, int count, void* context) {
        static_cast<ReceiveChannel*>(context)->iqCapture.enqueue(reinterpret_cast<float*>(samples), count);
    }

    bool anyRecording() const {
        for (const auto& channel : channels) { if (channel->recording) { return true; } }
        return false;
    }

    json snapshot() const {
        json entries = json::array();
        for (const auto& channel : channels) { entries.push_back(channel->serialize()); }
        return json{{"version", 1}, {"centerFrequency", gui::waterfall.getCenterFrequency()},
            {"sampleRate", sigpath::iqFrontEnd.getSampleRate()}, {"output", outputMode},
            {"solo", soloName}, {"channels", entries}};
    }

    bool validate(const json& setup, std::string& message) {
        message.clear();
        try {
            if (!setup.is_object() || !setup.contains("channels") || !setup["channels"].is_array()) {
                message = "Invalid channel group"; return false;
            }
            if (setup.value("version", 1) != 1) { message = "Unsupported channel group version"; return false; }
            if (setup["channels"].size() > 32) { message = "At most 32 channels are supported"; return false; }
            double center = setup.value("centerFrequency", gui::waterfall.getCenterFrequency());
            double sampleRate = sigpath::iqFrontEnd.getSampleRate();
            if (!std::isfinite(center) || center < 0 || !std::isfinite(sampleRate) || sampleRate <= 0) {
                message = "Invalid receiver frequency or sample rate"; return false;
            }
            std::set<std::string> names;
            for (const auto& entry : setup["channels"]) {
                std::string channelName = entry.at("name").get<std::string>();
                double frequency = entry.at("frequency").get<double>();
                double bandwidth = entry.at("bandwidth").get<double>();
                double iqRate = entry.value("iqSampleRate", std::ceil(bandwidth * 1.25 / 1000.0) * 1000.0);
                int mode = entry.at("mode").get<int>();
                for (const auto* flag : {"monitor", "audio", "iq"}) {
                    if (entry.contains(flag) && !entry[flag].is_boolean()) {
                        message = "Invalid channel recording or monitor flag"; return false;
                    }
                }
                if (channelName.empty() || channelName.size() > 64 || channelName.rfind("RX ", 0) != 0 || !names.insert(channelName).second) {
                    message = "Channel names must be unique and begin with RX "; return false;
                }
                bool owned = false;
                for (const auto& channel : channels) { owned |= channel->name == channelName; }
                if (!owned && core::moduleManager.instances.count(channelName)) {
                    message = "Channel name is already used: " + channelName; return false;
                }
                if (!std::isfinite(frequency) || frequency < 0 || !std::isfinite(bandwidth) || bandwidth <= 0 ||
                    !std::isfinite(iqRate) || iqRate < bandwidth || iqRate > sampleRate || mode < 0 || mode > RADIO_IFACE_MODE_FREE) {
                    message = "Invalid channel frequency, bandwidth, IQ sample rate or mode"; return false;
                }
                if (std::abs(passbandCenter(frequency, bandwidth, mode) - center) + bandwidth / 2.0 > sampleRate / 2.0) {
                    message = "Channel is outside the SDR capture range; select a wider source sample rate"; return false;
                }
            }
            int output = setup.value("output", 0);
            if (output < 0 || output > 2) { message = "Invalid output mode"; return false; }
            if (setup.contains("solo") && !setup["solo"].is_string()) { message = "Invalid solo channel"; return false; }
            return true;
        } catch (const std::exception& exception) {
            message = std::string("Invalid channel group: ") + exception.what();
            return false;
        }
    }

    bool addChannel(const json& entry) {
        auto channel = std::make_unique<ReceiveChannel>();
        channel->name = entry.at("name").get<std::string>();
        channel->frequency = entry.at("frequency").get<double>();
        channel->bandwidth = entry.at("bandwidth").get<double>();
        channel->mode = entry.at("mode").get<int>();
        channel->iqSampleRate = entry.value("iqSampleRate", std::ceil(channel->bandwidth * 1.25 / 1000.0) * 1000.0);
        channel->monitor = entry.value("monitor", true);
        channel->selectAudio = entry.value("audio", true);
        channel->selectIQ = entry.value("iq", false);
        if (channel->mode == RADIO_IFACE_MODE_FREE) { channel->selectAudio = false; }
        if (core::moduleManager.createInstance(channel->name, "radio") != 0) {
            error = "Cannot create radio channel: " + channel->name; return false;
        }
        core::moduleManager.instances.at(channel->name).transient = true;
        core::modComManager.callInterface(channel->name, RADIO_IFACE_CMD_SET_MODE, &channel->mode, nullptr);
        float bandwidth = channel->bandwidth;
        core::modComManager.callInterface(channel->name, RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, nullptr);
        sigpath::vfoManager.setOffset(channel->name, channel->frequency - gui::waterfall.getCenterFrequency());
        sigpath::sinkManager.setStreamSink(channel->name, "Audio");
        if (channels.empty()) {
            gui::waterfall.selectedVFO = channel->name;
            gui::waterfall.selectedVFOChanged = true;
        }
        channels.push_back(std::move(channel));
        return true;
    }

    void stopChannel(ReceiveChannel& channel, bool force = false) {
        if (!channel.recording || (core::isSessionLocked() && !force)) { return; }
        if (channel.audioBound) {
            channel.audioSink.stop();
            sigpath::sinkManager.unbindStream(channel.name, channel.audioStream);
            channel.audioStream = nullptr;
            channel.audioBound = false;
        }
        if (channel.iqBound) {
            channel.iqSink.stop();
            sigpath::iqFrontEnd.removeVFO("__record_" + channel.name);
            channel.iqVFO = nullptr;
            channel.iqBound = false;
        }
        channel.stopping = true;
        channel.audioCapture.requestStop();
        channel.iqCapture.requestStop();
        if (!channel.audioCapture.isFinished() || !channel.iqCapture.isFinished()) { return; }
        channel.audioCapture.stop();
        channel.iqCapture.stop();
        if (channel.audioStarted && !channel.audioCapture.getError().empty()) { channel.error = channel.audioCapture.getError(); }
        if (channel.iqStarted && !channel.iqCapture.getError().empty()) { channel.error = channel.iqCapture.getError(); }
        if (channel.tuningHeld) { core::releaseTuningLock(); channel.tuningHeld = false; }
        channel.recording = false;
        channel.stopping = false;
    }

    void startChannel(ReceiveChannel& channel) {
        if (channel.recording || core::isSessionLocked()) { return; }
        channel.error.clear();
        channel.audioStarted = false;
        channel.iqStarted = false;
        if (!gui::mainWindow.isPlaying()) { channel.error = "Start the SDR before recording"; return; }
        if (!directory.pathIsValid()) { channel.error = "Select a writable recording directory"; return; }
        if (!sigpath::vfoManager.vfoExists(channel.name)) { channel.error = "Channel has been disabled or removed"; return; }
        channel.frequency = gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(channel.name);
        channel.bandwidth = sigpath::vfoManager.getBandwidth(channel.name);
        core::modComManager.callInterface(channel.name, RADIO_IFACE_CMD_GET_MODE, nullptr, &channel.mode);
        if (!channel.selectAudio && !channel.selectIQ) { channel.error = "Select Audio and/or IQ"; return; }
        if (channel.mode == RADIO_IFACE_MODE_FREE && channel.selectAudio) { channel.error = "Free mode has no audio; select IQ recording"; return; }
        double sampleRate = sigpath::iqFrontEnd.getSampleRate();
        double offset = gui::waterfall.vfos.at(channel.name)->centerOffset;
        if (std::abs(offset) + channel.bandwidth / 2 > sampleRate / 2) {
            channel.error = "Channel is outside the SDR capture range"; return;
        }
        if (channel.selectIQ && (!std::isfinite(channel.iqSampleRate) || channel.iqSampleRate < channel.bandwidth || channel.iqSampleRate > sampleRate)) {
            channel.error = "IQ sample rate must cover channel bandwidth and not exceed source sample rate"; return;
        }
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::string safeName = channel.name;
        for (char& character : safeName) {
            if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-') { character = '_'; }
        }
        std::string prefix = directory.expandString(directory.path) + "/" + safeName + "_";
        std::string suffix = "Hz_" + std::to_string(timestamp);
        std::string filename = prefix + std::to_string(static_cast<uint64_t>(channel.frequency)) + suffix;
        channel.recording = true;
        core::retainTuningLock();
        channel.tuningHeld = true;
        if (channel.selectAudio) {
            uint64_t audioRate = sigpath::sinkManager.getStreamSampleRate(channel.name);
            if (!channel.audioCapture.start(filename + "_audio", audioRate, false)) {
                channel.error = channel.audioCapture.getError(); stopChannel(channel, true); return;
            }
            channel.audioStarted = true;
            channel.audioStream = sigpath::sinkManager.bindStream(channel.name);
            if (!channel.audioStream) { channel.error = "Cannot attach audio recording stream"; stopChannel(channel, true); return; }
            if (channel.audioSinkInitialized) { channel.audioSink.setInput(channel.audioStream); }
            else { channel.audioSink.init(channel.audioStream, audioHandler, &channel); channel.audioSinkInitialized = true; }
            channel.audioBound = true;
            channel.audioSink.start();
        }
        if (channel.selectIQ) {
            double iqFrequency = gui::waterfall.getCenterFrequency() + offset;
            std::string iqFilename = prefix + std::to_string(static_cast<uint64_t>(iqFrequency)) + suffix + "_IQ";
            if (!channel.iqCapture.start(iqFilename, channel.iqSampleRate, true)) {
                channel.error = channel.iqCapture.getError(); stopChannel(channel, true); return;
            }
            channel.iqStarted = true;
            channel.iqVFO = sigpath::iqFrontEnd.addVFO("__record_" + channel.name, channel.iqSampleRate, channel.bandwidth, offset);
            if (!channel.iqVFO) { channel.error = "Cannot attach IQ recording channel"; stopChannel(channel, true); return; }
            if (channel.iqSinkInitialized) { channel.iqSink.setInput(&channel.iqVFO->out); }
            else { channel.iqSink.init(&channel.iqVFO->out, iqHandler, &channel); channel.iqSinkInitialized = true; }
            channel.iqBound = true;
            channel.iqSink.start();
        }
    }

    void clearChannels() {
        for (auto& channel : channels) { stopChannel(*channel, true); }
        for (auto& channel : channels) { core::moduleManager.deleteInstance(channel->name); }
        channels.clear();
    }

    void updateMonitor() {
        if (!enabled || channels.empty()) { sigpath::sinkManager.clearMonitorSelection(); return; }
        if (outputMode == 1) {
            bool found = false;
            for (const auto& channel : channels) { found |= channel->name == soloName; }
            if (!found) { soloName = channels.front()->name; }
        }
        std::vector<std::string> selected;
        for (const auto& channel : channels) {
            if (channel->mode == RADIO_IFACE_MODE_FREE) { continue; }
            if ((outputMode == 0 && channel->monitor) || (outputMode == 1 && channel->name == soloName)) {
                selected.push_back(channel->name);
            }
        }
        sigpath::sinkManager.setMonitorSelection(selected);
    }

    void save() {
        json setup = snapshot();
        if (setup == savedSetup && directory.path == savedDirectory) { return; }
        savedSetup = setup;
        savedDirectory = directory.path;
        config.acquire();
        config.conf["setup"] = setup;
        config.conf["directory"] = directory.path;
        config.release(true);
    }

    static void frame(int, void* context) {
        static_cast<MultiReceiver*>(context)->tick();
    }

    void tick() {
        if (!initialized) { return; }
        if (enabled != appliedEnabled) {
            for (auto& channel : channels) {
                if (!core::moduleManager.instances.count(channel->name)) { continue; }
                if (enabled && !core::moduleManager.instanceEnabled(channel->name)) {
                    core::moduleManager.enableInstance(channel->name);
                    sigpath::vfoManager.setOffset(channel->name, channel->frequency - gui::waterfall.getCenterFrequency());
                }
                if (!enabled && core::moduleManager.instanceEnabled(channel->name)) { core::moduleManager.disableInstance(channel->name); }
            }
            appliedEnabled = enabled;
            updateMonitor();
        }
        if (!enabled) { return; }
        if (!pendingSetup.empty()) {
            if (!core::isTuningLocked() && !anyRecording() && validate(pendingSetup, error)) {
                json setup = pendingSetup;
                clearChannels();
                double center = setup.value("centerFrequency", gui::waterfall.getCenterFrequency());
                tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", center);
                outputMode = setup.value("output", 0);
                soloName = setup.value("solo", std::string());
                for (const auto& entry : setup["channels"]) { if (!addChannel(entry)) { break; } }
                updateMonitor();
            }
            pendingSetup = json::object();
        }
        if (pendingAdd && !core::isTuningLocked()) {
            pendingAdd = false;
            int suffix = 1;
            while (core::moduleManager.instances.count("RX " + std::to_string(suffix))) { ++suffix; }
            double sampleRate = sigpath::iqFrontEnd.getSampleRate();
            double bandwidth = std::min(150000.0, sampleRate / 2.0);
            if (channels.size() >= 32) { error = "At most 32 channels are supported"; }
            else if (addChannel(json{{"name", "RX " + std::to_string(suffix)}, {"frequency", gui::waterfall.getCenterFrequency()},
                {"bandwidth", bandwidth}, {"mode", RADIO_IFACE_MODE_WFM}, {"iqSampleRate", std::min(300000.0, sampleRate)}})) {
                error.clear(); updateMonitor();
            }
        }
        if (!pendingDelete.empty() && !core::isTuningLocked()) {
            for (auto iterator = channels.begin(); iterator != channels.end(); ++iterator) {
                if ((*iterator)->name == pendingDelete) {
                    stopChannel(**iterator, true);
                    core::moduleManager.deleteInstance((*iterator)->name);
                    channels.erase(iterator);
                    break;
                }
            }
            pendingDelete.clear(); updateMonitor();
        }
        for (auto& channel : channels) {
            if (channel->stopping) { stopChannel(*channel, true); }
            if (channel->recording) {
                if ((channel->audioBound && !channel->audioCapture.getError().empty()) ||
                    (channel->iqBound && !channel->iqCapture.getError().empty())) { stopChannel(*channel, true); }
                if ((channel->audioStarted && !channel->audioCapture.isFinished() && channel->audioCapture.secondsWithoutProgress() > 5) ||
                    (channel->iqStarted && !channel->iqCapture.isFinished() && channel->iqCapture.secondsWithoutProgress() > 5)) {
                    channel->error = "No write progress for 5 seconds; storage may be stalled";
                }
            }
            if (sigpath::vfoManager.vfoExists(channel->name)) {
                channel->frequency = gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(channel->name);
                double actualBandwidth = sigpath::vfoManager.getBandwidth(channel->name);
                if (channel->bandwidth != actualBandwidth || channel->iqSampleRate < actualBandwidth || channel->iqSampleRate > sigpath::iqFrontEnd.getSampleRate()) {
                    channel->iqSampleRate = std::min(sigpath::iqFrontEnd.getSampleRate(), std::ceil(actualBandwidth * 1.25 / 1000.0) * 1000.0);
                }
                channel->bandwidth = actualBandwidth;
                int previousMode = channel->mode;
                core::modComManager.callInterface(channel->name, RADIO_IFACE_CMD_GET_MODE, nullptr, &channel->mode);
                if (channel->mode == RADIO_IFACE_MODE_FREE) { channel->selectAudio = false; }
                if (channel->mode != previousMode) { updateMonitor(); }
            }
            if (!channel->error.empty() && channel->error != channel->reportedError) {
                channel->reportedError = channel->error;
                core::reportRecordingError(channel->name + ": " + channel->error);
            }
            if (channel->error.empty()) { channel->reportedError.clear(); }
        }
        save();
    }

    static void interfaceHandler(int command, void* input, void* output, void* context) {
        auto* receiver = static_cast<MultiReceiver*>(context);
        if (command == MULTI_RECEIVER_GET_SETUP && output) { *static_cast<json*>(output) = receiver->snapshot(); }
        if (command == MULTI_RECEIVER_GET_ERROR && output) { *static_cast<std::string*>(output) = receiver->error; }
        if (command == MULTI_RECEIVER_LOAD_SETUP && input && output) {
            auto& message = *static_cast<std::string*>(output);
            message.clear();
            if (core::isTuningLocked() || receiver->anyRecording()) { message = "Unlock and stop channel recordings before loading a group"; return; }
            if (receiver->validate(*static_cast<json*>(input), message)) {
                receiver->error.clear();
                receiver->pendingSetup = *static_cast<json*>(input);
            }
        }
    }

    static void draw(void* context) {
        static_cast<MultiReceiver*>(context)->drawMenu();
    }

    void drawMenu() {
        ImGui::TextWrapped("All channels share the SDR capture range. Frequencies and bandwidths are in Hz.");
        ImGui::Text("Capture bandwidth: %.3f MHz", sigpath::iqFrontEnd.getSampleRate() / 1000000.0);
        if (!error.empty()) { ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "%s", error.c_str()); }
        ImGui::BeginDisabled(!enabled || core::isTuningLocked());
        if (ImGui::Button("Add channel")) { pendingAdd = true; }
        ImGui::EndDisabled();
        if (directory.render("##multi_recording_directory")) { save(); }
        if (ImGui::Combo("Output", &outputMode, "Mix\0Single channel\0Mute\0")) { updateMonitor(); }
        if (outputMode == 1) {
            if (ImGui::BeginCombo("Listen to", soloName.c_str())) {
                for (const auto& channel : channels) {
                    if (ImGui::Selectable(channel->name.c_str(), soloName == channel->name)) { soloName = channel->name; updateMonitor(); }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::BeginDisabled(!enabled || core::isSessionLocked());
        if (ImGui::Button("Record selected tracks")) {
            for (auto& channel : channels) { if (channel->selectAudio || channel->selectIQ) { startChannel(*channel); } }
        }
        if (ImGui::Button("Stop all channel recordings")) {
            for (auto& channel : channels) { stopChannel(*channel); }
        }
        ImGui::EndDisabled();
        if (anyRecording()) { ImGui::TextWrapped("Tuning is held while channel recordings are active."); }
        for (auto& channel : channels) {
            ImGui::PushID(channel->name.c_str());
            if (ImGui::CollapsingHeader(channel->name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BeginDisabled(!enabled || core::isTuningLocked());
                double frequency = channel->frequency;
                if (ImGui::InputDouble("Frequency", &frequency, 1000, 100000, "%.0f")) {
                    if (std::isfinite(frequency) && frequency >= 0 && std::abs(passbandCenter(frequency, channel->bandwidth, channel->mode) - gui::waterfall.getCenterFrequency()) + channel->bandwidth / 2 <= sigpath::iqFrontEnd.getSampleRate() / 2) {
                        sigpath::vfoManager.setOffset(channel->name, frequency - gui::waterfall.getCenterFrequency());
                        channel->error.clear();
                    } else { channel->error = "Frequency is outside the SDR capture range"; }
                }
                int mode = channel->mode;
                if (ImGui::Combo("Mode", &mode, modeNames)) {
                    core::modComManager.callInterface(channel->name, RADIO_IFACE_CMD_SET_MODE, &mode, nullptr);
                    float actualBandwidth = channel->bandwidth;
                    core::modComManager.callInterface(channel->name, RADIO_IFACE_CMD_GET_BANDWIDTH, nullptr, &actualBandwidth);
                    channel->bandwidth = actualBandwidth;
                    channel->iqSampleRate = std::min(sigpath::iqFrontEnd.getSampleRate(), std::ceil(actualBandwidth * 1.25 / 1000.0) * 1000.0);
                    channel->mode = mode;
                    if (mode == RADIO_IFACE_MODE_FREE) { channel->selectAudio = false; channel->selectIQ = true; }
                    updateMonitor();
                }
                double bandwidth = channel->bandwidth;
                if (ImGui::InputDouble("Bandwidth", &bandwidth, 1000, 10000, "%.0f")) {
                    if (std::isfinite(bandwidth) && bandwidth > 0 && std::abs(passbandCenter(channel->frequency, bandwidth, channel->mode) - gui::waterfall.getCenterFrequency()) + bandwidth / 2 <= sigpath::iqFrontEnd.getSampleRate() / 2) {
                        float requested = bandwidth;
                        core::modComManager.callInterface(channel->name, RADIO_IFACE_CMD_SET_BANDWIDTH, &requested, nullptr);
                        channel->error.clear();
                    } else { channel->error = "Bandwidth is outside the SDR capture range"; }
                }
                if (ImGui::Button("Remove channel")) { pendingDelete = channel->name; }
                ImGui::EndDisabled();
                if (ImGui::Checkbox("Include in mix", &channel->monitor)) { updateMonitor(); }
                ImGui::BeginDisabled(channel->recording || core::isSessionLocked());
                ImGui::BeginDisabled(channel->mode == RADIO_IFACE_MODE_FREE);
                ImGui::Checkbox("Record audio", &channel->selectAudio);
                ImGui::EndDisabled();
                ImGui::Checkbox("Record IQ", &channel->selectIQ);
                double iqRate = channel->iqSampleRate;
                if (ImGui::InputDouble("IQ sample rate", &iqRate, 1000, 100000, "%.0f")) {
                    if (std::isfinite(iqRate) && iqRate >= channel->bandwidth && iqRate <= sigpath::iqFrontEnd.getSampleRate()) {
                        channel->iqSampleRate = iqRate; channel->error.clear();
                    } else { channel->error = "IQ sample rate must cover bandwidth and not exceed source sample rate"; }
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!enabled || core::isSessionLocked());
                if (ImGui::Button(channel->recording ? "Stop recording" : "Record channel")) {
                    if (channel->recording) { stopChannel(*channel); } else { startChannel(*channel); }
                }
                ImGui::EndDisabled();
                ImGui::Text("Audio: %.2f MiB (%d files)", channel->audioCapture.bytesWritten() / 1048576.0, channel->audioCapture.fileCount());
                ImGui::Text("IQ: %.2f MiB (%d files)", channel->iqCapture.bytesWritten() / 1048576.0, channel->iqCapture.fileCount());
                if (channel->recording) { ImGui::TextColored(ImVec4(1, 0.3f, 0.2f, 1), "%s", channel->stopping ? "Finishing files..." : "RECORDING"); }
                if (!channel->error.empty()) { ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "%s", channel->error.c_str()); }
            }
            ImGui::PopID();
        }
    }

    std::string name;
    FolderSelect directory;
    std::vector<std::unique_ptr<ReceiveChannel>> channels;
    EventHandler<int> frameHandler;
    json pendingSetup = json::object();
    json savedSetup;
    std::string savedDirectory;
    std::string pendingDelete;
    std::string soloName;
    std::string error;
    int outputMode = 0;
    bool pendingAdd = false;
    bool enabled = true;
    bool initialized = false;
    bool appliedEnabled = true;
};

MOD_EXPORT void _INIT_() {
    std::string root = core::args["root"].s();
    std::error_code error;
    std::filesystem::create_directories(root + "/recordings", error);
    config.setPath(root + "/multi_receiver_config.json");
    config.load(json::object());
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) { return new MultiReceiver(name); }
MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) { delete instance; }
MOD_EXPORT void _END_() { config.disableAutoSave(); config.save(); }
