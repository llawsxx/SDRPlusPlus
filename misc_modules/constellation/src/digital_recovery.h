#pragma once
#include <dsp/filter/fir.h>
#include <dsp/loop/fast_agc.h>
#include <dsp/loop/costas.h>
#include <dsp/taps/root_raised_cosine.h>
#include <array>
#include <memory>
#include <stdexcept>
#include "samples.h"
#include "classifier.h"
#include "symbol_rate.h"
#include "fsk.h"
#include "broadcast.h"

class GardnerTiming {
public:
    explicit GardnerTiming(double samplesPerSymbol) : nominal(samplesPerSymbol), omega(samplesPerSymbol), next(samplesPerSymbol * 2) {}

    void process(const dsp::complex_t* input, int count, std::vector<dsp::complex_t>& output) {
        output.clear();
        for (int index = 0; index < count; ++index, ++received) {
            history[received % history.size()] = input[index];
            if (received < std::ceil(next)) { continue; }
            auto current = interpolate(next);
            auto middle = interpolate(next - omega / 2);
            if (previousValid) {
                double error = (previous.re - current.re) * middle.re + (previous.im - current.im) * middle.im;
                error = std::clamp(error, -1.0, 1.0);
                omega = std::clamp(omega + 0.00005 * error, nominal * 0.99, nominal * 1.01);
                next += omega + 0.01 * error;
            } else { next += omega; }
            previous = current;
            previousValid = true;
            output.push_back(current);
        }
    }

private:
    dsp::complex_t interpolate(double time) const {
        auto position = static_cast<uint64_t>(std::floor(time));
        double fraction = time - position;
        const auto& first = history[position % history.size()];
        const auto& second = history[(position + 1) % history.size()];
        return {static_cast<float>(first.re * (1 - fraction) + second.re * fraction), static_cast<float>(first.im * (1 - fraction) + second.im * fraction)};
    }

    std::array<dsp::complex_t, 512> history{};
    uint64_t received = 0;
    double nominal;
    double omega;
    double next;
    dsp::complex_t previous{};
    bool previousValid = false;
};

class ConstellationRecovery {
    struct BranchBase {
        virtual ~BranchBase() = default;
        virtual void process(dsp::complex_t* input, int count) = 0;
        std::vector<dsp::complex_t> symbols;
        ConstellationSamples history;
    };

    template<int order>
    struct Branch : BranchBase {
        explicit Branch(double samplesPerSymbol) : timing(samplesPerSymbol) {
            carrier.init(nullptr, 0.0004);
            carrier.out.free();
        }
        void process(dsp::complex_t* input, int count) override {
            timing.process(input, count, this->symbols);
            if (!coarseReady) {
                for (const auto& symbol : this->symbols) {
                    coarse.push_back(symbol);
                    if (coarse.size() == 2048) { acquireCarrier(); break; }
                }
            }
            carrier.process(static_cast<int>(this->symbols.size()), this->symbols.data(), this->symbols.data());
            this->history.push(this->symbols.data(), static_cast<int>(this->symbols.size()));
        }
        dsp::loop::Costas<order> carrier;
        GardnerTiming timing;

        void acquireCarrier() {
            std::vector<std::complex<double>> spectrum(coarse.size());
            for (size_t index = 0; index < coarse.size(); ++index) {
                std::complex<double> point(coarse[index].re, coarse[index].im);
                std::complex<double> power(1, 0);
                for (int exponent = 0; exponent < order; ++exponent) { power *= point; }
                spectrum[index] = power * (0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * index / (coarse.size() - 1)));
            }
            constellation::fft(spectrum);
            size_t peak = 0;
            std::vector<double> powers;
            for (size_t index = 0; index < spectrum.size(); ++index) {
                powers.push_back(std::norm(spectrum[index]));
                if (powers.back() > std::norm(spectrum[peak])) { peak = index; }
            }
            auto middle = powers.begin() + powers.size() / 2;
            std::nth_element(powers.begin(), middle, powers.end());
            if (std::norm(spectrum[peak]) > std::max(1e-30, *middle) * 25) {
                double left = std::log(std::max(std::norm(spectrum[(peak + spectrum.size() - 1) % spectrum.size()]), 1e-30));
                double center = std::log(std::max(std::norm(spectrum[peak]), 1e-30));
                double right = std::log(std::max(std::norm(spectrum[(peak + 1) % spectrum.size()]), 1e-30));
                double denominator = left - 2 * center + right;
                double fraction = std::abs(denominator) > 1e-12 ? std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5) : 0;
                double bin = peak > spectrum.size() / 2 ? static_cast<double>(peak) - spectrum.size() : peak;
                carrier.setInitialFreq(2 * 3.14159265358979323846 * (bin + fraction) / (spectrum.size() * order));
                carrier.reset();
                coarseReady = true;
                this->history.clear();
            }
            coarse.clear();
        }

        bool coarseReady = false;
        std::vector<dsp::complex_t> coarse;
    };

public:
    ~ConstellationRecovery() { clear(); }

    void configure(int selectedMode, double inputRate, double manualRate, double beta, bool estimateSymbols = false, double bandwidth = 0) {
        if (selectedMode < 0 || selectedMode > 10 || !std::isfinite(inputRate) || inputRate <= 0 ||
            !std::isfinite(beta) || beta < 0.1 || beta > 1) { throw std::invalid_argument("Invalid constellation settings"); }
        if (selectedMode && selectedMode < 9 && !estimateSymbols && (!std::isfinite(manualRate) || manualRate <= 0 || inputRate / manualRate < 2 || inputRate / manualRate > 64)) {
            throw std::invalid_argument("Recovery needs 2 to 64 IQ samples per symbol");
        }
        clear();
        mode = selectedMode;
        sampleRate = inputRate;
        symbolRate = manualRate;
        rolloff = beta;
        automaticSymbols = estimateSymbols;
        estimator = std::make_unique<SymbolRateEstimator>(sampleRate);
        fsk = (mode == 1 || mode == 8) ? std::make_unique<constellation::FskDetector>(sampleRate, automaticSymbols ? 0 : symbolRate) : nullptr;
        broadcast = (mode == 1 || mode >= 9) ? std::make_unique<constellation::BroadcastDetector>(sampleRate, bandwidth > 0 ? bandwidth : sampleRate, mode) : nullptr;
        frequencyOutput = false;
        if (mode && mode < 8 && !automaticSymbols) { build(symbolRate); }
    }

    const dsp::complex_t* process(const dsp::complex_t* input, int count, int& outputCount) {
        if (count < 0 || count > STREAM_BUFFER_SIZE) { throw std::invalid_argument("Invalid constellation block size"); }
        outputCount = count;
        if (!count) { return input; }
        if (broadcast) {
            broadcast->push(input, count);
            const auto& candidate = broadcast->result;
            if (candidate.type != constellation::Broadcast::Unknown) {
                frequencyOutput = false;
                detection = {candidate.type == constellation::Broadcast::CDR ? constellation::Modulation::CDR : constellation::Modulation::DAB, candidate.confidence, 0, 0, 1 - candidate.confidence};
                return input;
            }
            if (detection.modulation == constellation::Modulation::CDR || detection.modulation == constellation::Modulation::DAB) { detection = {}; }
            if (mode >= 9) { return input; }
        }
        if (automaticSymbols) { estimator->push(input, count); }
        if (fsk) {
            bool previousFrequencyOutput = frequencyOutput;
            fsk->push(input, count);
            frequencyOutput = fsk->result.symbolRate > 0;
            if (previousFrequencyOutput && !frequencyOutput) { detection = {}; }
            if (frequencyOutput) {
                const auto& result = fsk->result;
                detection = {constellation::Modulation::FSK2, std::clamp((result.coherence - 0.6) / 0.4 * (1 - result.spread / 0.3), 0.0, 1.0), result.spread, 0, result.spread};
                outputCount = static_cast<int>(fsk->points.size());
                return fsk->points.data();
            }
            if (mode == 8) { detection = {}; return input; }
        }
        if (automaticSymbols) {
            double candidate = estimator->symbolRate();
            if (!candidate) { detection = {}; return input; }
            if (!activeRate || std::abs(candidate / activeRate - 1) > 0.002) { build(candidate); }
        }
        if (!mode || !activeRate) { return input; }
        filtered.resize(count);
        filter->process(count, input, filtered.data());
        agc->process(count, filtered.data(), filtered.data());
        for (auto& branch : branches) { branch->process(filtered.data(), count); }
        sinceClassification += count;
        if (sinceClassification >= sampleRate / 2) {
            sinceClassification = 0;
            detection = {};
            for (size_t index = 0; index < branches.size(); ++index) {
                auto points = branches[index]->history.snapshot(2048, false, false, 1, 0);
                auto requested = mode == 1 ? constellation::Modulation::Unknown : static_cast<constellation::Modulation>(mode - 1);
                auto result = constellation::classify(points, requested);
                if (mode == 1 && ((index == 0 && result.modulation != constellation::Modulation::BPSK) ||
                    (index == 1 && (result.modulation == constellation::Modulation::BPSK || result.modulation == constellation::Modulation::PSK8)) ||
                    (index == 2 && result.modulation != constellation::Modulation::PSK8))) { continue; }
                if (result.score < detection.score) { detection = result; selectedBranch = index; }
            }
        }
        const auto& symbols = branches[selectedBranch]->symbols;
        outputCount = static_cast<int>(symbols.size());
        aligned.resize(symbols.size());
        double cosine = std::cos(detection.phase);
        double sine = std::sin(detection.phase);
        for (size_t index = 0; index < symbols.size(); ++index) {
            aligned[index] = {static_cast<float>(symbols[index].re * cosine + symbols[index].im * sine),
                static_cast<float>(symbols[index].im * cosine - symbols[index].re * sine)};
        }
        return aligned.data();
    }

    constellation::Detection result() const { return detection; }
    double estimatedSymbolRate() const { return mode >= 9 || isBroadcastOutput() ? 0 : frequencyOutput ? fsk->result.symbolRate : automaticSymbols ? estimator->symbolRate() : symbolRate; }
    double symbolRateConfidence() const { return mode >= 9 || isBroadcastOutput() ? 0 : frequencyOutput ? fsk->result.coherence : automaticSymbols ? estimator->confidence() : 1; }
    bool isRecovering() const { return !isBroadcastOutput() && (frequencyOutput || (mode && mode < 8 && activeRate > 0 && (!automaticSymbols || estimator->symbolRate() > 0))); }
    bool isBroadcastOutput() const { return broadcast && broadcast->result.type != constellation::Broadcast::Unknown; }
    constellation::BroadcastResult broadcastResult() const { return broadcast ? broadcast->result : constellation::BroadcastResult{}; }
    bool broadcastAvailable() const { return broadcast && broadcast->available(); }
    bool isFrequencyOutput() const { return frequencyOutput; }
    double frequencyOffset() const { return frequencyOutput ? fsk->result.offset : 0; }
    double frequencyDeviation() const { return frequencyOutput ? fsk->result.deviation : 0; }

private:
    void clear() {
        branches.clear();
        filter.reset();
        agc.reset();
        if (taps.taps) { dsp::taps::free(taps); taps.taps = nullptr; }
        activeRate = 0;
        sinceClassification = 0;
        selectedBranch = 0;
        detection = {};
    }

    void build(double rate) {
        clear();
        if (!mode) { return; }
        if (sampleRate / rate < 2 || sampleRate / rate > 64) { return; }
        int tapCount = static_cast<int>(std::ceil(8 * sampleRate / rate)) | 1;
        taps = dsp::taps::rootRaisedCosine<float>(tapCount, rolloff, rate, sampleRate);
        filter = std::make_unique<dsp::filter::FIR<dsp::complex_t, float>>();
        filter->init(nullptr, taps);
        filter->out.free();
        filter->reset();
        agc = std::make_unique<dsp::loop::FastAGC<dsp::complex_t>>();
        agc->init(nullptr, 1, 1000000, 0.0001);
        agc->out.free();
        double samplesPerSymbol = sampleRate / rate;
        if (mode == 1 || mode == 2) { branches.push_back(std::make_unique<Branch<2>>(samplesPerSymbol)); }
        if (mode == 1 || mode == 3 || mode >= 5) { branches.push_back(std::make_unique<Branch<4>>(samplesPerSymbol)); }
        if (mode == 1 || mode == 4) { branches.push_back(std::make_unique<Branch<8>>(samplesPerSymbol)); }
        selectedBranch = mode == 1 ? 1 : 0;
        activeRate = rate;
    }

    int mode = 0;
    double sampleRate = 0;
    double symbolRate = 0;
    double rolloff = 0.35;
    double activeRate = 0;
    bool automaticSymbols = false;
    bool frequencyOutput = false;
    double sinceClassification = 0;
    size_t selectedBranch = 0;
    dsp::tap<float> taps{};
    std::unique_ptr<dsp::filter::FIR<dsp::complex_t, float>> filter;
    std::unique_ptr<dsp::loop::FastAGC<dsp::complex_t>> agc;
    std::unique_ptr<SymbolRateEstimator> estimator;
    std::unique_ptr<constellation::FskDetector> fsk;
    std::unique_ptr<constellation::BroadcastDetector> broadcast;
    std::vector<std::unique_ptr<BranchBase>> branches;
    std::vector<dsp::complex_t> filtered;
    std::vector<dsp::complex_t> aligned;
    constellation::Detection detection;
};
