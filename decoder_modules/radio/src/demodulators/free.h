#pragma once
#include "../demod.h"
#include <signal_path/signal_path.h>
#include <dsp/sink/null_sink.h>
#include <dsp/multirate/channel_rate.h>
#include <algorithm>

namespace demod {
    class Free : public Demodulator {
    public:
        ~Free() { stop(); }
        void init(std::string, ConfigManager*, dsp::stream<dsp::complex_t>* input, double bandwidth, double) {
            width = bandwidth;
            drain.init(input);
        }
        void start() { drain.start(); }
        void stop() { drain.stop(); }
        void showMenu() { ImGui::TextWrapped("Free / IQ only: no audio demodulation. Channel bandwidth is also the recorded IQ bandwidth."); }
        void setBandwidth(double bandwidth) { width = bandwidth; }
        void setInput(dsp::stream<dsp::complex_t>* input) { drain.setInput(input); }
        void AFSampRateChanged(double) {}
        const char* getName() { return "Free"; }
        double getIFSampleRate() {
            return dsp::multirate::channelSampleRate(getMaxBandwidth(), width * 1.25,
                dsp::multirate::PowerDecimator<dsp::complex_t>::getMaxRatio());
        }
        double getAFSampleRate() { return 48000; }
        double getDefaultBandwidth() { return std::min(250000.0, getMaxBandwidth()); }
        double getMinBandwidth() { return 1; }
        double getMaxBandwidth() { return std::max(1.0, sigpath::iqFrontEnd.getSampleRate()); }
        bool getBandwidthLocked() { return false; }
        double getDefaultSnapInterval() { return 1000; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_CENTER; }
        bool getDeempAllowed() { return false; }
        bool getPostProcEnabled() { return false; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() { return false; }
        bool getNBAllowed() { return false; }
        bool getHighPassAllowed() { return false; }
        bool getSquelchAllowed() { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &silentOutput; }

    private:
        double width = 250000;
        dsp::sink::Null<dsp::complex_t> drain;
        dsp::stream<dsp::stereo_t> silentOutput;
    };
}
