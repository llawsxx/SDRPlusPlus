#include "../../misc_modules/constellation/src/samples.h"
#include "../../misc_modules/constellation/src/classifier.h"
#include "../../misc_modules/constellation/src/symbol_rate.h"
#include "../../misc_modules/constellation/src/fsk.h"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <dsp/multirate/channel_rate.h>
#ifdef __ANDROID__
#include "../../misc_modules/constellation/src/digital_recovery.h"
#include <dsp/taps/root_raised_cosine.h>
#endif

static void require(bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

static std::vector<dsp::complex_t> symbols(constellation::Modulation modulation, int count, double noise = 0.002) {
    auto reference = constellation::reference(modulation);
    std::mt19937 generator(128);
    std::uniform_int_distribution<int> choose(0, static_cast<int>(reference.size()) - 1);
    std::normal_distribution<float> disturbance(0, static_cast<float>(noise));
    std::vector<dsp::complex_t> result;
    for (int index = 0; index < count; ++index) {
        auto point = reference[choose(generator)];
        float real = static_cast<float>(point.re * std::cos(0.27) - point.im * std::sin(0.27));
        float imag = static_cast<float>(point.re * std::sin(0.27) + point.im * std::cos(0.27));
        result.push_back({real + disturbance(generator), imag + disturbance(generator)});
    }
    return result;
}

static void sampleChecks() {
    ConstellationSamples buffer;
    std::vector<dsp::complex_t> input(5000);
    for (size_t index = 0; index < input.size(); ++index) { input[index] = {static_cast<float>(index), 0}; }
    buffer.push(input.data(), static_cast<int>(input.size()));
    auto result = buffer.snapshot(4096, false, false, 1, 0);
    require(result.size() == 4096 && result.front().re == 904 && result.back().re == 4999, "Sample history is not bounded/ordered");
    buffer.setFrozen(true);
    dsp::complex_t point{7, 0};
    buffer.push(&point, 1);
    require(buffer.snapshot(1, false, false, 1, 0).front().re == 4999, "Freeze did not hold the plot");
    buffer.clear();
    require(buffer.snapshot(4096, false, false, 1, 0).empty(), "Clear kept old points");
    buffer.setFrozen(false);
    point = {2, 0};
    buffer.push(&point, 1);
    result = buffer.snapshot(1, true, false, 1, 90);
    require(std::abs(result[0].re) < 1e-6 && std::abs(result[0].im - 1) < 1e-6, "RMS normalization / phase rotation failed");
    result = buffer.snapshot(1, true, true, 1, 0);
    require(result[0].re == 0 && result[0].im == 0, "Removing the mean produced nonzero/nonfinite points");
    point = {std::numeric_limits<float>::quiet_NaN(), 0};
    buffer.push(&point, 1);
    require(buffer.snapshot(4096, false, false, 1, 0).size() == 1, "Nonfinite samples entered the plot");
}

static void classifierChecks() {
    for (int candidate = 1; candidate <= 6; ++candidate) {
        auto modulation = static_cast<constellation::Modulation>(candidate);
        auto result = constellation::classify(symbols(modulation, 4096));
        std::cout << "Candidate " << constellation::label(modulation) << ": " << constellation::label(result.modulation) << " score=" << result.confidence << " residual=" << result.evm << std::endl;
        require(result.modulation == modulation, std::string("Wrong modulation candidate for ") + constellation::label(modulation) + ": " + constellation::label(result.modulation));
        require(result.confidence > 0.6, "Clean symbols had low candidate score");
    }
    std::mt19937 generator(123);
    std::normal_distribution<float> noise(0, 1);
    std::vector<dsp::complex_t> input(4096);
    for (auto& point : input) { point = {noise(generator), noise(generator)}; }
    require(constellation::classify(input).modulation == constellation::Modulation::Unknown, "Noise was given a modulation label");
    for (auto& point : input) { point = {1, 0}; }
    require(constellation::classify(input).modulation == constellation::Modulation::Unknown, "A tone was given a modulation label");
    for (size_t index = 0; index < input.size(); ++index) {
        double angle = 2 * 3.14159265358979323846 * index / input.size();
        input[index] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
    }
    require(constellation::classify(input).modulation == constellation::Modulation::Unknown, "An unsynchronized ring was given a modulation label");
    require(constellation::classify(symbols(constellation::Modulation::QPSK, 100)).modulation == constellation::Modulation::Unknown, "Insufficient samples were classified");
}

static void estimatorChecks() {
    SymbolRateEstimator estimator(48000);
    std::vector<dsp::complex_t> input(200000);
    std::mt19937 generator(122);
    std::normal_distribution<float> noise(0, 0.01f);
    for (size_t index = 0; index < input.size(); ++index) {
        double magnitude = std::sqrt(1 + 0.4 * std::cos(2 * 3.14159265358979323846 * 11973 * index / 48000));
        input[index] = {static_cast<float>(magnitude) + noise(generator), noise(generator)};
    }
    for (size_t start = 0; start < input.size(); start += 1024) {
        estimator.push(input.data() + start, static_cast<int>(std::min<size_t>(1024, input.size() - start)));
    }
    require(std::abs(estimator.symbolRate() / 11973 - 1) < 0.001, "Non-bin-centered periodic symbol-rate feature was not recovered");
    SymbolRateEstimator noSignal(48000);
    std::normal_distribution<float> gaussian(0, 1);
    for (auto& point : input) { point = {gaussian(generator), gaussian(generator)}; }
    noSignal.push(input.data(), static_cast<int>(input.size()));
    require(noSignal.symbolRate() == 0, "Random noise was given a symbol rate");
    for (auto& point : input) { point = {1, 0}; }
    noSignal.push(input.data(), static_cast<int>(input.size()));
    require(noSignal.symbolRate() == 0, "Constant-envelope tone was given a symbol rate");
}

static std::vector<dsp::complex_t> fskWaveform(double rate = 11973, int levels = 2) {
    std::mt19937 generator(811);
    std::uniform_int_distribution<int> choose(0, levels - 1);
    std::vector<int> data(40000);
    for (auto& symbol : data) { symbol = choose(generator); }
    std::vector<dsp::complex_t> output(static_cast<size_t>(data.size() * 48000 / rate));
    double phase = 0.3;
    double shaped = 0;
    for (size_t index = 0; index < output.size(); ++index) {
        double level = 2.0 * data[std::min(data.size() - 1, static_cast<size_t>(index * rate / 48000))] / (levels - 1) - 1;
        shaped += 0.85 * (level - shaped);
        phase += 2 * 3.14159265358979323846 * (731 + shaped * rate * 0.4) / 48000;
        output[index] = {static_cast<float>(0.05 * std::cos(phase)), static_cast<float>(0.05 * std::sin(phase))};
    }
    return output;
}

static void fskChecks() {
    auto input = fskWaveform();
    for (double manualRate : {0.0, 11973.0}) {
        constellation::FskDetector detector(48000, manualRate);
        size_t outputCount = 0;
        for (size_t start = 0; start < input.size(); start += 1024) {
            detector.push(input.data() + start, static_cast<int>(std::min<size_t>(1024, input.size() - start)));
            outputCount += detector.points.size();
        }
        const auto& result = detector.result;
        std::cout << "FSK " << (manualRate ? "manual" : "automatic") << ": " << result.symbolRate << " baud, spread=" << result.spread << ", coherence=" << result.coherence << std::endl;
        require(std::abs(result.symbolRate / 11973 - 1) < 0.001, "FSK transition clock was not recovered");
        require(std::abs(result.offset - 731) < 200, "FSK carrier offset was not removed");
        require(outputCount > 1024, "FSK frequency levels were not emitted");
    }
    std::vector<dsp::complex_t> window(input.begin(), input.begin() + 32768);
    require(constellation::analyzeFsk(window, 48000, 10000).symbolRate == 0, "FSK ignored an incompatible manual symbol rate");
    auto fourLevels = fskWaveform(11973, 4);
    window.assign(fourLevels.begin(), fourLevels.begin() + 32768);
    require(constellation::analyzeFsk(window, 48000).symbolRate == 0, "4-FSK was incorrectly labeled binary FSK");
    for (auto& point : window) { point = {1, 0}; }
    require(constellation::analyzeFsk(window, 48000).symbolRate == 0, "A carrier was labeled FSK");
    double phase = 0;
    for (size_t index = 0; index < window.size(); ++index) {
        phase += 0.7 * std::sin(2 * 3.14159265358979323846 * index * 1973 / 48000);
        window[index] = {static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase))};
    }
    require(constellation::analyzeFsk(window, 48000).symbolRate == 0, "A periodic analog FM tone was labeled FSK");
    std::mt19937 generator(812);
    std::normal_distribution<float> noise(0, 1);
    for (auto& point : window) { point = {noise(generator), noise(generator)}; }
    require(constellation::analyzeFsk(window, 48000).symbolRate == 0, "Noise was labeled FSK");
    window[10].re = std::numeric_limits<float>::quiet_NaN();
    require(constellation::analyzeFsk(window, 48000).symbolRate == 0, "Invalid IQ was labeled FSK");
}

#ifdef __ANDROID__
static std::vector<dsp::complex_t> waveform(constellation::Modulation modulation) {
    auto input = symbols(modulation, 40000, 0);
    auto taps = dsp::taps::rootRaisedCosine<float>(65, 0.35, 12000, 48000);
    std::vector<dsp::complex_t> output(input.size() * 4 + taps.size);
    for (size_t symbol = 0; symbol < input.size(); ++symbol) {
        for (int tap = 0; tap < taps.size; ++tap) {
            output[symbol * 4 + tap].re += input[symbol].re * taps.taps[tap];
            output[symbol * 4 + tap].im += input[symbol].im * taps.taps[tap];
        }
    }
    dsp::taps::free(taps);
    for (size_t index = 0; index < output.size(); ++index) {
        double angle = 0.2 + index * 0.0005;
        double real = output[index].re;
        double imag = output[index].im;
        output[index] = {static_cast<float>(real * std::cos(angle) - imag * std::sin(angle)), static_cast<float>(real * std::sin(angle) + imag * std::cos(angle))};
    }
    return output;
}

static void recoveryChecks() {
    for (int candidate = 1; candidate <= 6; ++candidate) {
        auto modulation = static_cast<constellation::Modulation>(candidate);
        auto input = waveform(modulation);
        std::vector<dsp::complex_t> window(input.begin() + 4096, input.begin() + 4096 + 32768);
        require(constellation::analyzeFsk(window, 48000).symbolRate == 0, std::string("PSK/QAM was mislabeled FSK: ") + constellation::label(modulation));
        SymbolRateEstimator estimator(48000);
        estimator.push(input.data(), static_cast<int>(input.size()));
        require(std::abs(estimator.symbolRate() / 12000 - 1) < 0.005, std::string("RRC symbol-rate estimate failed: ") + constellation::label(modulation));
        for (bool automatic : {false, true}) {
            ConstellationRecovery recovery;
            recovery.configure(automatic ? 1 : candidate + 1, 48000, 12000, 0.35, automatic);
            int total = 0;
            for (size_t start = 0; start < input.size(); start += 1024) {
                int count = static_cast<int>(std::min<size_t>(1024, input.size() - start));
                int outputCount = 0;
                const auto* output = recovery.process(input.data() + start, count, outputCount);
                for (int index = 0; index < outputCount; ++index) { require(std::isfinite(output[index].re) && std::isfinite(output[index].im), "Recovery emitted nonfinite samples"); }
                if (recovery.isRecovering()) { total += outputCount; }
            }
            auto result = recovery.result();
            std::cout << (automatic ? "Auto " : "Manual ") << constellation::label(modulation) << ": " << constellation::label(result.modulation) << " residual=" << result.evm << " symbols=" << total << std::endl;
            if (!automatic) { require(total > 39000 && total < 41000, "Timing recovery lost symbol rate"); }
            if (automatic) { require(std::abs(recovery.estimatedSymbolRate() / 12000 - 1) < 0.005, "Automatic rate did not settle"); }
            require(result.modulation == modulation, std::string("Carrier/timing recovery did not fit ") + constellation::label(modulation));
        }
    }
    auto input = fskWaveform();
    for (int mode : {1, 8}) {
        for (bool automatic : {false, true}) {
            ConstellationRecovery recovery;
            recovery.configure(mode, 48000, 11973, 0.35, automatic);
            for (size_t start = 0; start < input.size(); start += 1024) {
                int outputCount = 0;
                recovery.process(input.data() + start, static_cast<int>(std::min<size_t>(1024, input.size() - start)), outputCount);
            }
            std::cout << "Integrated FSK mode=" << mode << " auto=" << automatic << " candidate=" << constellation::label(recovery.result().modulation) << std::endl;
            require(recovery.result().modulation == constellation::Modulation::FSK2 && recovery.isFrequencyOutput(), "Integrated FSK branch did not detect binary FSK");
            require(std::abs(recovery.estimatedSymbolRate() / 11973 - 1) < 0.001, "Integrated FSK timing used an incorrect rate");
        }
    }
}
#endif

int main() {
    try {
        for (double inputRate : {2400000.0, 4000000.0, 10000000.0, 2441406.25}) {
            for (double bandwidth : {1.0, 12345.0, 251003.0, 1050010.0, 2345678.0, inputRate}) {
                double minimumRate = bandwidth * 1.25;
                double rate = dsp::multirate::channelSampleRate(inputRate, minimumRate, 8192);
                double ratio = inputRate / rate;
                require(rate <= inputRate && rate >= std::min(inputRate, minimumRate), "Efficient analysis rate lost bandwidth");
                if (minimumRate < inputRate / 16384) {
                    require(rate == std::ceil(minimumRate), "Very narrow channels used an oversized channel filter");
                }
                else { require(ratio <= 8192 && std::abs(std::log2(ratio) - std::round(std::log2(ratio))) < 1e-10, "Channel rate generated a large rational filter bank"); }
            }
        }
        sampleChecks();
        classifierChecks();
        estimatorChecks();
        fskChecks();
#ifdef __ANDROID__
        recoveryChecks();
#endif
        std::cout << "PASS: IQ history/display, PSK/QAM recovery, binary FSK detection/timing, manual rates and unknown rejection" << std::endl;
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << std::endl;
        return 1;
    }
}
