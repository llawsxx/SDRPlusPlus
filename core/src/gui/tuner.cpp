#include <signal_path/signal_path.h>
#include <gui/gui.h>
#include <gui/tuner.h>
#include <string>
#include <core.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace tuner {

    void centerTuning(std::string vfoName, double freq) {
        if (core::isTuningLocked() || !std::isfinite(freq) || freq < 0) { return; }
        if (vfoName != "") {
            auto selected = gui::waterfall.vfos.find(vfoName);
            if (selected == gui::waterfall.vfos.end()) { return; }
            double previousCenter = gui::waterfall.getCenterFrequency();
            if (vfoName != gui::waterfall.selectedVFO) {
                sigpath::vfoManager.setOffset(vfoName, freq - previousCenter);
                return;
            }
            double halfBandwidth = gui::waterfall.getBandwidth() / 2;
            double minimumCenter = 0;
            double maximumCenter = std::numeric_limits<double>::infinity();
            std::vector<std::pair<std::string, double>> frequencies;
            for (const auto& [name, channel] : gui::waterfall.vfos) {
                double frequency = name == vfoName ? freq : previousCenter + channel->generalOffset;
                double centerAdjustment = channel->centerOffset - channel->generalOffset;
                minimumCenter = std::max({minimumCenter,
                    frequency + channel->upperOffset - channel->generalOffset - halfBandwidth,
                    frequency + centerAdjustment - channel->maxCenterOffset});
                maximumCenter = std::min({maximumCenter,
                    frequency + channel->lowerOffset - channel->generalOffset + halfBandwidth,
                    frequency + centerAdjustment - channel->minCenterOffset});
                frequencies.emplace_back(name, frequency);
            }
            if (minimumCenter > maximumCenter) {
                gui::freqSelect.setFrequency(previousCenter + selected->second->generalOffset);
                return;
            }
            double center = std::clamp(freq, minimumCenter, maximumCenter);
            for (const auto& [name, frequency] : frequencies) {
                sigpath::vfoManager.setOffset(name, frequency - center);
            }
            gui::waterfall.setCenterFrequency(center);
            gui::waterfall.setViewOffset(selected->second->generalOffset);
            gui::freqSelect.setFrequency(center + selected->second->generalOffset);
            if (center != previousCenter) { sigpath::sourceManager.tune(center); }
            core::configManager.acquire();
            core::configManager.conf["frequency"] = center;
            for (const auto& [name, channel] : gui::waterfall.vfos) {
                core::configManager.conf["vfoOffsets"][name] = channel->generalOffset;
            }
            core::configManager.release(true);
            return;
        }
        double BW = gui::waterfall.getBandwidth();
        double viewBW = gui::waterfall.getViewBandwidth();
        gui::waterfall.setViewOffset((BW / 2.0) - (viewBW / 2.0));
        gui::waterfall.setCenterFrequency(freq);
        gui::waterfall.setViewOffset(0);
        gui::freqSelect.setFrequency(freq);
        sigpath::sourceManager.tune(freq);
    }

    void normalTuning(std::string vfoName, double freq) {
        if (core::isTuningLocked()) { return; }
        if (vfoName == "") {
            centerTuning(vfoName, freq);
            return;
        }
        if (gui::waterfall.vfos.find(vfoName) == gui::waterfall.vfos.end()) { return; }

        double viewBW = gui::waterfall.getViewBandwidth();
        double BW = gui::waterfall.getBandwidth();

        ImGui::WaterfallVFO* vfo = gui::waterfall.vfos[vfoName];

        double currentOff = vfo->centerOffset;
        double currentTune = gui::waterfall.getCenterFrequency() + vfo->generalOffset;
        double delta = freq - currentTune;

        double newVFO = currentOff + delta;
        double vfoBW = vfo->bandwidth;
        double vfoBottom = newVFO - (vfoBW / 2.0);
        double vfoTop = newVFO + (vfoBW / 2.0);

        double view = gui::waterfall.getViewOffset();
        double viewBottom = view - (viewBW / 2.0);
        double viewTop = view + (viewBW / 2.0);

        double bottom = -(BW / 2.0);
        double top = (BW / 2.0);

        // VFO still fints in the view
        if (vfoBottom > viewBottom && vfoTop < viewTop) {
            sigpath::vfoManager.setCenterOffset(vfoName, newVFO);
            return;
        }

        // VFO too low for current SDR tuning
        if (vfoBottom < bottom) {
            gui::waterfall.setViewOffset((BW / 2.0) - (viewBW / 2.0));
            double newVFOOffset = (BW / 2.0) - (vfoBW / 2.0) - (viewBW / 10.0);
            sigpath::vfoManager.setOffset(vfoName, newVFOOffset);
            gui::waterfall.setCenterFrequency(freq - newVFOOffset);
            sigpath::sourceManager.tune(freq - newVFOOffset);
            return;
        }

        // VFO too high for current SDR tuning
        if (vfoTop > top) {
            gui::waterfall.setViewOffset((viewBW / 2.0) - (BW / 2.0));
            double newVFOOffset = (vfoBW / 2.0) - (BW / 2.0) + (viewBW / 10.0);
            sigpath::vfoManager.setOffset(vfoName, newVFOOffset);
            gui::waterfall.setCenterFrequency(freq - newVFOOffset);
            sigpath::sourceManager.tune(freq - newVFOOffset);
            return;
        }

        // VFO is still without the SDR's bandwidth
        if (delta < 0) {
            double newViewOff = vfoTop - (viewBW / 2.0) + (viewBW / 10.0);
            double newViewBottom = newViewOff - (viewBW / 2.0);

            if (newViewBottom > bottom) {
                gui::waterfall.setViewOffset(newViewOff);
                sigpath::vfoManager.setCenterOffset(vfoName, newVFO);
                return;
            }

            gui::waterfall.setViewOffset((BW / 2.0) - (viewBW / 2.0));
            double newVFOOffset = (BW / 2.0) - (vfoBW / 2.0) - (viewBW / 10.0);
            sigpath::vfoManager.setCenterOffset(vfoName, newVFOOffset);
            gui::waterfall.setCenterFrequency(freq - newVFOOffset);
            sigpath::sourceManager.tune(freq - newVFOOffset);
        }
        else {
            double newViewOff = vfoBottom + (viewBW / 2.0) - (viewBW / 10.0);
            double newViewTop = newViewOff + (viewBW / 2.0);

            if (newViewTop < top) {
                gui::waterfall.setViewOffset(newViewOff);
                sigpath::vfoManager.setCenterOffset(vfoName, newVFO);
                return;
            }

            gui::waterfall.setViewOffset((viewBW / 2.0) - (BW / 2.0));
            double newVFOOffset = (vfoBW / 2.0) - (BW / 2.0) + (viewBW / 10.0);
            sigpath::vfoManager.setCenterOffset(vfoName, newVFOOffset);
            gui::waterfall.setCenterFrequency(freq - newVFOOffset);
            sigpath::sourceManager.tune(freq - newVFOOffset);
        }
    }

    void iqTuning(double freq) {
        if (core::isTuningLocked()) { return; }
        gui::waterfall.setCenterFrequency(freq);
        gui::waterfall.centerFreqMoved = true;
        sigpath::sourceManager.tune(freq);
    }

    void tune(int mode, std::string vfoName, double freq) {
        switch (mode) {
        case TUNER_MODE_CENTER:
            centerTuning(vfoName, freq);
            break;
        case TUNER_MODE_NORMAL:
            normalTuning(vfoName, freq);
            break;
        case TUNER_MODE_LOWER_HALF:
            normalTuning(vfoName, freq);
            break;
        case TUNER_MODE_UPPER_HALF:
            normalTuning(vfoName, freq);
            break;
        case TUNER_MODE_IQ_ONLY:
            iqTuning(freq);
            break;
        }
    }
}
