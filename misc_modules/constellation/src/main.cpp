#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <dsp/sink/handler_sink.h>
#include <dsp/multirate/channel_rate.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include "samples.h"
#include "digital_recovery.h"

SDRPP_MOD_INFO{
    "constellation", "IQ, PSK/QAM, FSK and broadcast inspection", "SDR++ contributors", 1, 0, 0, 1
};

ConfigManager config;

class ConstellationModule : public ModuleManager::Instance {
public:
    explicit ConstellationModule(std::string instanceName) : name(instanceName) {
        config.acquire();
        try {
            const auto& settings = config.conf;
            followSelected = settings.value("followSelected", true);
            channelName = settings.value("channel", std::string());
            symbolRate = settings.value("symbolRate", 25000.0);
            automaticSymbols = settings.value("automaticSymbols", true);
            mode = std::clamp(settings.value("mode", 1), 0, 10);
            active = settings.value("show", false);
            largeWindow = settings.value("largeWindow", false);
            density = settings.value("density", false);
            rolloff = settings.value("rolloff", 0.35f);
            normalize = settings.value("normalize", true);
            removeMean = settings.value("removeMean", false);
            gain = settings.value("gain", 1.0f);
            rotation = settings.value("rotation", 0.0f);
            pointCount = std::clamp(settings.value("pointCount", 2048), 128, 4096);
        } catch (const std::exception&) {}
        config.release();
        if (!std::isfinite(symbolRate) || symbolRate <= 0) { symbolRate = 25000; }
        if (!std::isfinite(rolloff)) { rolloff = 0.35f; }
        if (!std::isfinite(gain)) { gain = 1; }
        if (!std::isfinite(rotation)) { rotation = 0; }
        rolloff = std::clamp(rolloff, 0.1f, 1.0f);
        gain = std::clamp(gain, 0.1f, 10.0f);
        rotation = std::clamp(rotation, -180.0f, 180.0f);
        frameHandler = EventHandler<int>(frame, this);
        core::onFrame.bindHandler(&frameHandler);
        gui::menu.registerEntry(name, menu, this, this);
    }

    ~ConstellationModule() {
        core::onFrame.unbindHandler(&frameHandler);
        stop();
        gui::menu.removeEntry(name);
    }

    void postInit() { initialized = true; }
    void enable() { enabled = true; }
    void disable() { enabled = false; stop(); }
    bool isEnabled() { return enabled; }

private:
    static void receive(dsp::complex_t* input, int count, void* context) {
        auto* viewer = static_cast<ConstellationModule*>(context);
        try {
            int outputCount = 0;
            const auto* output = viewer->recovery->process(input, count, outputCount);
            std::lock_guard<std::mutex> lock(viewer->metricsMutex);
            if (viewer->recovering != viewer->recovery->isRecovering() || viewer->frequencyView != viewer->recovery->isFrequencyOutput()) { viewer->samples.clear(); }
            viewer->samples.push(output, outputCount);
            viewer->detection = viewer->recovery->result();
            viewer->estimatedRate = viewer->recovery->estimatedSymbolRate();
            viewer->rateConfidence = viewer->recovery->symbolRateConfidence();
            viewer->recovering = viewer->recovery->isRecovering();
            viewer->frequencyView = viewer->recovery->isFrequencyOutput();
            viewer->frequencyOffset = viewer->recovery->frequencyOffset();
            viewer->frequencyDeviation = viewer->recovery->frequencyDeviation();
            viewer->broadcastResult = viewer->recovery->broadcastResult();
            viewer->broadcastAvailable = viewer->recovery->broadcastAvailable();
        } catch (const std::exception& exception) {
            std::lock_guard<std::mutex> lock(viewer->metricsMutex);
            viewer->workerError = exception.what();
        }
    }

    void stop() {
        if (vfo) {
            sink.stop();
            sigpath::iqFrontEnd.removeVFO("__constellation_" + name);
            vfo = nullptr;
        }
        recovery.reset();
        binding = json();
        samples.clear();
        std::lock_guard<std::mutex> lock(metricsMutex);
        detection = {};
        estimatedRate = 0;
        rateConfidence = 0;
        recovering = false;
        frequencyView = false;
        frequencyOffset = 0;
        frequencyDeviation = 0;
        broadcastResult = {};
        broadcastAvailable = false;
        workerError.clear();
    }

    void save() {
        json settings{{"followSelected", followSelected}, {"channel", channelName}, {"automaticSymbols", automaticSymbols},
            {"show", active}, {"largeWindow", largeWindow}, {"density", density}, {"symbolRate", symbolRate}, {"mode", mode}, {"rolloff", rolloff},
            {"normalize", normalize}, {"removeMean", removeMean}, {"gain", gain}, {"rotation", rotation}, {"pointCount", pointCount}};
        if (settings == savedSettings) { return; }
        config.acquire();
        config.conf = settings;
        config.release(true);
        savedSettings = settings;
    }

    void update() {
        if (!initialized) { return; }
        save();
        if (!enabled || !active || !gui::mainWindow.isPlaying()) { stop(); return; }
        std::string selected = followSelected ? gui::waterfall.selectedVFO : channelName;
        auto found = gui::waterfall.vfos.find(selected);
        if (found == gui::waterfall.vfos.end()) { stop(); error = "Select an active Radio/RX channel in the waterfall"; return; }
        double inputRate = sigpath::iqFrontEnd.getSampleRate();
        double bandwidth = found->second->bandwidth;
        double offset = found->second->centerOffset;
        if (!std::isfinite(bandwidth) || bandwidth <= 0 || !std::isfinite(inputRate) || inputRate <= 0 ||
            !std::isfinite(offset) || std::abs(offset) + bandwidth / 2 > inputRate / 2) {
            stop(); error = "Analysis bandwidth must fit inside the SDR capture range"; return;
        }
        if (mode && mode < 9 && !automaticSymbols && (!std::isfinite(symbolRate) || symbolRate <= 0)) {
            stop(); error = "Enter a positive manual symbol rate in baud"; return;
        }
        double sampleRate = dsp::multirate::channelSampleRate(inputRate,
            std::max(bandwidth * 1.25, mode && mode < 9 && !automaticSymbols ? symbolRate * 2 : 0.0),
            dsp::multirate::PowerDecimator<dsp::complex_t>::getMaxRatio());
        if (mode && mode < 9 && !automaticSymbols && (sampleRate / symbolRate < 2 || sampleRate / symbolRate > 64)) {
            stop(); error = "Recovery needs 2 to 64 samples/symbol; narrow the channel bandwidth or adjust the symbol rate"; return;
        }
        json requested{{"channel", selected}, {"offset", offset}, {"inputRate", inputRate}, {"bandwidth", bandwidth},
            {"sampleRate", sampleRate}, {"center", gui::waterfall.getCenterFrequency()}, {"mode", mode}, {"symbolRate", symbolRate}, {"rolloff", rolloff}, {"automaticSymbols", automaticSymbols}};
        if (requested == binding && vfo) { return; }
        stop();
        try {
            recovery = std::make_unique<ConstellationRecovery>();
            recovery->configure(mode, sampleRate, symbolRate, rolloff, automaticSymbols, bandwidth);
            vfo = sigpath::iqFrontEnd.addVFO("__constellation_" + name, sampleRate, bandwidth, offset);
            if (!vfo) { recovery.reset(); error = "Cannot attach constellation IQ stream"; return; }
            if (sinkInitialized) { sink.setInput(&vfo->out); }
            else { sink.init(&vfo->out, receive, this); sinkInitialized = true; }
            sink.start();
            binding = requested;
            error.clear();
        } catch (const std::exception& exception) {
            stop();
            error = std::string("Cannot start constellation: ") + exception.what();
        }
    }

    void plot(float width) {
        bool frequencyPlot;
        {
            std::lock_guard<std::mutex> lock(metricsMutex);
            frequencyPlot = frequencyView;
        }
        width = std::max(width, 80.0f);
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##constellation", ImVec2(width, width));
        auto* draw = ImGui::GetWindowDrawList();
        ImVec2 end(origin.x + width, origin.y + width);
        ImVec2 center(origin.x + width / 2, origin.y + width / 2);
        draw->AddRectFilled(origin, end, IM_COL32(9, 14, 20, 255));
        for (int division = 1; division < 4; ++division) {
            float location = width * division / 4;
            draw->AddLine(ImVec2(origin.x + location, origin.y), ImVec2(origin.x + location, end.y), IM_COL32(40, 50, 60, 255));
            draw->AddLine(ImVec2(origin.x, origin.y + location), ImVec2(end.x, origin.y + location), IM_COL32(40, 50, 60, 255));
        }
        draw->AddCircle(center, width / 3.2f, IM_COL32(55, 70, 80, 255), 64);
        draw->AddText(ImVec2(end.x - (frequencyPlot ? 70 : 20), center.y + 3), IM_COL32(180, 190, 200, 255), frequencyPlot ? "Frequency" : "I");
        if (!frequencyPlot) { draw->AddText(ImVec2(center.x + 3, origin.y + 3), IM_COL32(180, 190, 200, 255), "+Q"); }
        auto points = samples.snapshot(pointCount, frequencyPlot ? false : normalize, frequencyPlot ? false : removeMean, gain, frequencyPlot ? 0 : rotation);
        size_t clipped = 0;
        std::array<int, 4096> cells{};
        int peak = 1;
        draw->PushClipRect(origin, end, true);
        for (const auto& point : points) {
            if (std::abs(point.re) > 1.6f || std::abs(point.im) > 1.6f) { ++clipped; continue; }
            ImVec2 position(center.x + point.re * width / 3.2f, center.y - point.im * width / 3.2f);
            if (density) {
                int column = std::clamp(static_cast<int>((point.re + 1.6f) * 20), 0, 63);
                int row = std::clamp(static_cast<int>((1.6f - point.im) * 20), 0, 63);
                peak = std::max(peak, ++cells[row * 64 + column]);
            } else { draw->AddCircleFilled(position, 1.5f, IM_COL32(80, 235, 175, 170), 6); }
        }
        if (density) {
            for (int row = 0; row < 64; ++row) {
                for (int column = 0; column < 64; ++column) {
                    int count = cells[row * 64 + column];
                    if (!count) { continue; }
                    float intensity = std::log1p(static_cast<float>(count)) / std::log1p(static_cast<float>(peak));
                    ImVec2 corner(origin.x + column * width / 64, origin.y + row * width / 64);
                    draw->AddRectFilled(corner, ImVec2(corner.x + width / 64, corner.y + width / 64),
                        IM_COL32(static_cast<int>(40 + 215 * intensity), static_cast<int>(130 + 110 * intensity), static_cast<int>(230 - 160 * intensity), 220));
                }
            }
        }
        constellation::Modulation referenceMode;
        {
            std::lock_guard<std::mutex> lock(metricsMutex);
            referenceMode = frequencyPlot ? constellation::Modulation::FSK2 : mode >= 2 && mode <= 7 ? static_cast<constellation::Modulation>(mode - 1) : detection.modulation;
        }
        if (showReference && mode) {
            for (const auto& point : constellation::reference(referenceMode)) {
                ImVec2 position(center.x + point.re * width / 3.2f, center.y - point.im * width / 3.2f);
                draw->AddLine(ImVec2(position.x - 3, position.y), ImVec2(position.x + 3, position.y), IM_COL32(235, 235, 235, 170));
                draw->AddLine(ImVec2(position.x, position.y - 3), ImVec2(position.x, position.y + 3), IM_COL32(235, 235, 235, 170));
            }
        }
        draw->PopClipRect();
        ImGui::Text("%d points, %d outside view%s", static_cast<int>(points.size()), static_cast<int>(clipped), frozen ? " (frozen)" : "");
        if (frequencyPlot) { ImGui::TextWrapped("FSK frequency levels at symbol centers, not an I/Q constellation. Reference: -1 / +1."); }
        if (vfo && !frozen && samples.age() > 2) { ImGui::TextWrapped("No recent samples; check the SDR source."); }
        if (vfo && !frozen && samples.age() < 0) { ImGui::TextWrapped("Waiting for samples / recovery..."); }
    }

    void drawMenu() {
        ImGui::BeginDisabled(!enabled);
        ImGui::Checkbox("Show constellation", &active);
        ImGui::Checkbox("Follow selected channel", &followSelected);
        if (!followSelected && ImGui::BeginCombo("Channel", channelName.empty() ? "Select channel" : channelName.c_str())) {
            for (const auto& entry : gui::waterfall.vfos) {
                if (ImGui::Selectable(entry.first.c_str(), channelName == entry.first)) { channelName = entry.first; }
            }
            ImGui::EndCombo();
        }
        ImGui::Combo("Processing", &mode, "Raw IQ (unsynchronized)\0Auto detect\0BPSK\0QPSK\0" "8PSK\0" "16-QAM\0" "64-QAM\0" "256-QAM\0" "2-FSK / CPFSK-like\0" "CDR detection (FM hybrid / digital)\0" "DAB / DAB+ detection\0");
        if (mode && mode < 9) {
            ImGui::Checkbox("Automatic symbol rate", &automaticSymbols);
            double requestedRate = symbolRate;
            if (ImGui::InputDouble("Manual rate (baud)", &requestedRate, 100, 1000, "%.0f") && std::isfinite(requestedRate) && requestedRate > 0) { symbolRate = requestedRate; automaticSymbols = false; }
            ImGui::TextWrapped(automaticSymbols ? "Editing Manual rate switches off automatic estimation." : "Using manual symbol rate; automatic estimation is off.");
            if (mode != 8) { ImGui::SliderFloat("RRC rolloff", &rolloff, 0.1f, 1.0f, "%.2f"); }
            ImGui::TextWrapped("PSK/QAM: RRC + carrier/timing recovery. FSK: frequency discrimination + transition timing. Candidates are heuristic, not verified protocol lock.");
            auto selected = gui::waterfall.vfos.find(followSelected ? gui::waterfall.selectedVFO : channelName);
            if (mode != 8 && !automaticSymbols && selected != gui::waterfall.vfos.end() && selected->second->bandwidth < symbolRate * (1 + rolloff)) {
                ImGui::TextWrapped("Warning: bandwidth is below rate x (1 + rolloff). PSK/QAM pulses may be truncated; recovery is still allowed. This RRC rule does not apply to FSK.");
            }
        } else if (!mode) {
            ImGui::TextWrapped("Raw channel IQ, before demodulation. Without carrier/timing recovery, digital signals may form rings or clouds rather than distinct symbols.");
        }
        if (mode == 1 || mode >= 9) {
            ImGui::TextWrapped("Broadcast candidates: CDR modes 1-3; DAB modes I-IV. Detection only, not audio/service decoding or verified lock. The plot remains raw IQ for OFDM.");
            ImGui::TextWrapped("CDR: select 40-650 kHz BW (about 500 kHz for the full FM hybrid); digital sidebands are inspected separately from central analog FM. DAB: center the ensemble and use 1.536-2.5 MHz BW (about 1.7 MHz recommended).");
            ImGui::TextWrapped("DAB and DAB+ share the same waveform; the audio codec is not identified. OFDM does not use the single-carrier baud/RRC settings.");
        }
        ImGui::TextWrapped("Uses the channel's one shared bandwidth. Choose Free (IQ only) on Radio/RX for arbitrary bandwidth without audio.");
        if (vfo && !binding.is_null()) {
            ImGui::Text("Channel: %s", binding["channel"].get<std::string>().c_str());
            ImGui::Text("IQ: %.0f S/s, BW: %.0f Hz", binding["sampleRate"].get<double>(), binding["bandwidth"].get<double>());
        }
        {
            std::lock_guard<std::mutex> lock(metricsMutex);
            bool broadcastView = broadcastResult.type != constellation::Broadcast::Unknown;
            if (mode < 9 && !broadcastView) {
                if (automaticSymbols && estimatedRate > 0) { ImGui::Text("Estimated: %.1f baud (score %.0f%%)", estimatedRate, rateConfidence * 100); }
                else if (automaticSymbols && active) { ImGui::TextWrapped("Symbol rate: waiting for a reliable periodic feature (manual override available)"); }
                else if (mode) { ImGui::Text("Manual: %.1f baud", symbolRate); }
            }
            if (mode >= 9 && vfo && !broadcastAvailable) { ImGui::TextWrapped("Broadcast detection inactive: check channel bandwidth and analysis rate (maximum 6.144 MS/s)."); }
            if (detection.modulation != constellation::Modulation::Unknown) {
                ImGui::Text("Candidate: %s (score %.0f%%)", constellation::label(detection.modulation), detection.confidence * 100);
                if (broadcastView) {
                    ImGui::Text("Transmission mode: %d, prefix correlation: %.0f%%", broadcastResult.mode, broadcastResult.prefix * 100);
                    if (broadcastResult.type == constellation::Broadcast::CDR) { ImGui::Text("Analyzed digital band: %+.1f kHz from channel center", broadcastResult.bandOffset / 1000); }
                    else { ImGui::Text("Periodic null depth: %.0f%%", broadcastResult.nullDepth * 100); }
                    ImGui::TextWrapped("Compatible timing is evidence, not a validated broadcast service. No recovered subcarrier constellation or audio is produced.");
                } else if (frequencyView) {
                    ImGui::Text("Frequency spread: %.1f%% of deviation", detection.evm * 100);
                    ImGui::Text("Offset: %+.1f Hz, deviation: +/-%.1f Hz", frequencyOffset, frequencyDeviation);
                } else { ImGui::Text("Fit residual: %.1f%% RMS", detection.evm * 100); }
            } else if (mode && active) { ImGui::TextWrapped("Modulation: Unknown / insufficient evidence"); }
            if (!workerError.empty()) { ImGui::TextWrapped("Analysis error: %s", workerError.c_str()); }
        }
        if (ImGui::Checkbox("Freeze display", &frozen)) { samples.setFrozen(frozen); }
        ImGui::SameLine();
        if (ImGui::Button("Clear / restart")) { restart = true; }
        ImGui::Checkbox("Normalize RMS (display)", &normalize);
        ImGui::Checkbox("Remove mean (display)", &removeMean);
        ImGui::SliderFloat("Display gain", &gain, 0.1f, 10.0f, "%.2f");
        ImGui::SliderFloat("Display rotation", &rotation, -180, 180, "%.0f deg");
        ImGui::SliderInt("Points", &pointCount, 128, 4096);
        ImGui::Checkbox("Density view", &density);
        ImGui::Checkbox("Ideal reference points", &showReference);
        ImGui::Checkbox("Larger window", &largeWindow);
        if (active) { plot(ImGui::GetContentRegionAvail().x); }
        if (!active) { ImGui::TextWrapped("Constellation is hidden; its analysis stream is stopped."); }
        else if (!gui::mainWindow.isPlaying()) { ImGui::TextWrapped("Start the SDR to receive constellation samples."); }
        if (!error.empty()) { ImGui::TextColored(ImVec4(1, 0.35f, 0.25f, 1), "%s", error.c_str()); }
        ImGui::EndDisabled();
    }

    static void frame(int, void* context) {
        auto* viewer = static_cast<ConstellationModule*>(context);
        if (viewer->restart) { viewer->stop(); viewer->restart = false; }
        viewer->update();
        if (viewer->enabled && viewer->active && viewer->largeWindow) {
            ImVec2 display = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowSize(ImVec2(std::min(600.0f, display.x * 0.9f), std::min(670.0f, display.y * 0.9f)), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(("Constellation###" + viewer->name + "_plot").c_str(), &viewer->largeWindow)) {
                viewer->plot(std::min(ImGui::GetContentRegionAvail().x, std::max(80.0f, ImGui::GetContentRegionAvail().y - 65)));
                std::lock_guard<std::mutex> lock(viewer->metricsMutex);
                ImGui::TextUnformatted(viewer->broadcastResult.type != constellation::Broadcast::Unknown ? "OFDM candidate: raw IQ, not recovered subcarriers" : viewer->frequencyView ? "FSK frequency levels (not verified data lock)" : viewer->recovering ? "Recovered symbols (not a lock indicator)" : "Raw IQ / waiting for recovery");
            }
            ImGui::End();
        }
    }

    static void menu(void* context) { static_cast<ConstellationModule*>(context)->drawMenu(); }

    std::string name;
    std::string channelName;
    std::string error;
    EventHandler<int> frameHandler;
    ConstellationSamples samples;
    std::unique_ptr<ConstellationRecovery> recovery;
    dsp::channel::RxVFO* vfo = nullptr;
    dsp::sink::Handler<dsp::complex_t> sink;
    json binding;
    json savedSettings;
    double symbolRate = 25000;
    std::mutex metricsMutex;
    constellation::Detection detection;
    double estimatedRate = 0;
    double rateConfidence = 0;
    bool recovering = false;
    bool frequencyView = false;
    double frequencyOffset = 0;
    double frequencyDeviation = 0;
    constellation::BroadcastResult broadcastResult;
    bool broadcastAvailable = false;
    std::string workerError;
    float rolloff = 0.35f;
    float gain = 1;
    float rotation = 0;
    int mode = 1;
    int pointCount = 2048;
    bool enabled = true;
    bool initialized = false;
    bool active = false;
    bool followSelected = true;
    bool automaticSymbols = true;
    bool density = false;
    bool showReference = true;
    bool normalize = true;
    bool removeMean = false;
    bool frozen = false;
    bool largeWindow = false;
    bool sinkInitialized = false;
    bool restart = false;
};

MOD_EXPORT void _INIT_() {
    config.setPath(core::args["root"].s() + "/constellation_config.json");
    config.load(json::object());
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) { return new ConstellationModule(name); }
MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) { delete instance; }
MOD_EXPORT void _END_() { config.disableAutoSave(); config.save(); }
