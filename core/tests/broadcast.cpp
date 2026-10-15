#include "../../misc_modules/constellation/src/broadcast.h"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#ifdef __ANDROID__
#include "../../misc_modules/constellation/src/digital_recovery.h"
#endif

static void require(bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

static std::vector<dsp::complex_t> waveform(const constellation::BroadcastProfile& profile, double seconds, bool hybrid = false, bool nulls = true, double offset = 337) {
    std::mt19937 generator(483);
    std::vector<dsp::complex_t> result;
    size_t wanted = static_cast<size_t>(profile.clock * seconds);
    int symbols = profile.type == constellation::Broadcast::DAB ? (profile.mode == 3 ? 153 : 76) : profile.mode == 1 ? 56 : profile.mode == 2 ? 111 : 61;
    while (result.size() < wanted) {
        if (profile.type == constellation::Broadcast::DAB) {
            for (int index = 0; index < profile.nullLength; ++index) { result.push_back(nulls ? dsp::complex_t{0, 0} : dsp::complex_t{1, 0}); }
        } else {
            int beacon = 130560 - symbols * (profile.useful + profile.guard);
            for (int index = 0; index < beacon; ++index) { result.push_back({generator() & 1 ? 1.0f : -1.0f, generator() & 1 ? 1.0f : -1.0f}); }
        }
        for (int symbol = 0; symbol < symbols; ++symbol) {
            std::vector<std::complex<double>> spectrum(profile.useful);
            for (int carrier = -profile.useful / 2; carrier < profile.useful / 2; ++carrier) {
                double frequency = carrier * profile.clock / profile.useful;
                bool active = profile.type == constellation::Broadcast::DAB ? std::abs(carrier) > 0 && std::abs(carrier) <= profile.useful * 3 / 8 :
                    hybrid ? frequency > 152000 && frequency < 198000 : std::abs(frequency) > 1500 && std::abs(frequency) < 48000;
                if (active) { spectrum[(carrier + profile.useful) % profile.useful] = {generator() & 1 ? 1.0 : -1.0, generator() & 1 ? 1.0 : -1.0}; }
            }
            constellation::fft(spectrum);
            double normalization = std::sqrt(profile.useful);
            for (int index = -profile.guard; index < profile.useful; ++index) {
                const auto& sample = spectrum[(index + profile.useful) % profile.useful];
                result.push_back({static_cast<float>(sample.real() / normalization), static_cast<float>(-sample.imag() / normalization)});
            }
        }
    }
    result.resize(wanted);
    std::normal_distribution<float> noise(0, 0.025f);
    double fmPhase = 0;
    for (size_t index = 0; index < result.size(); ++index) {
        double phase = 2 * 3.14159265358979323846 * offset * index / profile.clock;
        double real = result[index].re * std::cos(phase) - result[index].im * std::sin(phase);
        double imag = result[index].re * std::sin(phase) + result[index].im * std::cos(phase);
        if (hybrid) {
            fmPhase += 2 * 3.14159265358979323846 * 40000 * std::sin(2 * 3.14159265358979323846 * 1137 * index / profile.clock) / profile.clock;
            real += 4 * std::cos(fmPhase);
            imag += 4 * std::sin(fmPhase);
        }
        result[index] = {static_cast<float>(real + noise(generator)), static_cast<float>(imag + noise(generator))};
    }
    return result;
}

static std::vector<dsp::complex_t> resample(const std::vector<dsp::complex_t>& input, double sourceRate, double outputRate) {
    std::vector<dsp::complex_t> output(static_cast<size_t>((input.size() - 1) * outputRate / sourceRate));
    for (size_t index = 0; index < output.size(); ++index) {
        double position = index * sourceRate / outputRate;
        size_t first = static_cast<size_t>(position);
        double fraction = position - first;
        output[index] = {static_cast<float>(input[first].re * (1 - fraction) + input[first + 1].re * fraction),
            static_cast<float>(input[first].im * (1 - fraction) + input[first + 1].im * fraction)};
    }
    return output;
}

static void checkProfiles() {
    for (const auto& profile : constellation::broadcastProfiles()) {
        auto signal = waveform(profile, profile.type == constellation::Broadcast::DAB ? 0.215 : 0.1);
        auto result = profile.type == constellation::Broadcast::DAB ? constellation::analyzeDab(signal, profile.clock) : constellation::analyzeCdr(signal, profile.clock, 120000);
        std::cout << (profile.type == constellation::Broadcast::DAB ? "DAB" : "CDR") << " mode " << profile.mode << ": candidate=" << static_cast<int>(result.type) << "/" << result.mode << " cp=" << result.prefix << " contrast=" << result.contrast << " null=" << result.nullDepth << std::endl;
        require(result.type == profile.type && result.mode == profile.mode, "Wrong broadcast profile");
        if (profile.type == constellation::Broadcast::DAB) {
            auto withoutNull = waveform(profile, 0.215, false, false);
            require(constellation::analyzeDab(withoutNull, profile.clock).type == constellation::Broadcast::Unknown, "DAB accepted without periodic nulls");
        } else {
            auto hybrid = waveform(profile, 0.1, true);
            auto match = constellation::analyzeCdr(hybrid, profile.clock, 500000);
            std::cout << "Hybrid CDR " << profile.mode << ": candidate=" << static_cast<int>(match.type) << "/" << match.mode << " band=" << match.bandOffset << " cp=" << match.prefix << " contrast=" << match.contrast << std::endl;
            require(match.type == constellation::Broadcast::CDR && match.mode == profile.mode && match.bandOffset > 100000, "Central FM prevented CDR sideband detection");
            for (auto& sample : hybrid) { sample.im = -sample.im; }
            match = constellation::analyzeCdr(hybrid, profile.clock, 500000);
            require(match.type == constellation::Broadcast::CDR && match.mode == profile.mode && match.bandOffset < -100000, "Lower CDR sideband was missed");
            auto boundary = waveform(profile, 0.3, true);
            boundary.erase(boundary.begin(), boundary.begin() + static_cast<size_t>(profile.clock * 0.125));
            boundary.resize(static_cast<size_t>(profile.clock * 0.1));
            match = constellation::analyzeCdr(boundary, profile.clock, 500000);
            require(match.type == constellation::Broadcast::CDR && match.mode == profile.mode, "CDR beacon boundary prevented acquisition");
        }
    }
}

static void checkStreaming() {
    for (int profileIndex : {0, 2, 3, 6}) {
        const auto& profile = constellation::broadcastProfiles()[profileIndex];
        double rate = profile.type == constellation::Broadcast::DAB ? 2400000 : 1000000;
        double bandwidth = profile.type == constellation::Broadcast::DAB ? 1700000 : 500000;
        auto input = resample(waveform(profile, 1.25, profile.type == constellation::Broadcast::CDR), profile.clock, rate);
        constellation::BroadcastDetector detector(rate, bandwidth, 1);
        for (size_t start = 0; start < input.size(); start += 8191) {
            detector.push(input.data() + start, static_cast<int>(std::min<size_t>(8191, input.size() - start)));
        }
        require(detector.result.type == profile.type && detector.result.mode == profile.mode, "Streaming/resampled broadcast acquisition failed");
        std::vector<dsp::complex_t> silence(static_cast<size_t>(rate));
        detector.push(silence.data(), static_cast<int>(silence.size()));
        require(detector.result.type == constellation::Broadcast::Unknown, "Lost broadcast candidate stayed latched");
    }
    require(!constellation::BroadcastDetector(1000000, 100000, 10).available(), "DAB accepted a truncated capture");
    require(!constellation::BroadcastDetector(20000000, 10000000, 1).available(), "Unbounded broadcast analysis was enabled");
}

#ifdef __ANDROID__
static void checkProduction() {
    for (int profileIndex : {0, 3}) {
        const auto& profile = constellation::broadcastProfiles()[profileIndex];
        double bandwidth = profile.type == constellation::Broadcast::DAB ? 1700000 : 500000;
        auto input = waveform(profile, 0.9, profile.type == constellation::Broadcast::CDR);
        for (int mode : {1, profile.type == constellation::Broadcast::CDR ? 9 : 10}) {
            ConstellationRecovery recovery;
            recovery.configure(mode, profile.clock, mode == 1 ? 100000 : 1, 0.35, mode == 1, bandwidth);
            for (size_t start = 0; start < input.size(); start += 8192) {
                int count = static_cast<int>(std::min<size_t>(8192, input.size() - start));
                int outputCount = 0;
                recovery.process(input.data() + start, count, outputCount);
            }
            auto expected = profile.type == constellation::Broadcast::CDR ? constellation::Modulation::CDR : constellation::Modulation::DAB;
            require(recovery.result().modulation == expected && recovery.isBroadcastOutput(), "Production auto/explicit broadcast mode failed");
            require(!recovery.isRecovering() && !recovery.isFrequencyOutput() && recovery.estimatedSymbolRate() == 0, "OFDM reported a single-carrier symbol lock/rate");
        }
    }
}
#endif

static void checkRejection() {
    std::mt19937 generator(682);
    std::normal_distribution<float> noise(0, 1);
    std::vector<dsp::complex_t> input(2048000 * 0.215);
    for (auto& sample : input) { sample = {noise(generator), noise(generator)}; }
    require(constellation::analyzeDab(input, 2048000).type == constellation::Broadcast::Unknown, "Noise classified as DAB");
    input.resize(81600);
    require(constellation::analyzeCdr(input, 816000, 500000).type == constellation::Broadcast::Unknown, "Noise classified as CDR");
    for (int kind = 0; kind < 3; ++kind) {
        double phase = 0;
        for (size_t index = 0; index < input.size(); ++index) {
            double frequency = kind == 0 ? 12000 : kind == 1 ? 45000 * std::sin(index * 0.008) : (generator() & 1 ? 45000 : -45000);
            phase += 2 * 3.14159265358979323846 * frequency / 816000;
            input[index] = {static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase))};
        }
        require(constellation::analyzeCdr(input, 816000, 500000).type == constellation::Broadcast::Unknown, "Tone/FM/FSK falsely classified as CDR");
    }
    auto wrong = constellation::broadcastProfiles()[0];
    wrong.guard = 450;
    auto wrongSignal = waveform(wrong, 0.1);
    require(constellation::analyzeCdr(wrongSignal, wrong.clock, 120000).type == constellation::Broadcast::Unknown, "Unrelated OFDM timing classified as CDR");
    constellation::BroadcastDetector detector(816000, 120000, 9);
    input.assign(816000, {std::numeric_limits<float>::quiet_NaN(), 0});
    detector.push(input.data(), static_cast<int>(input.size()));
    require(detector.result.type == constellation::Broadcast::Unknown, "Nonfinite input classified as a broadcast");
}

int main() {
    try {
        checkProfiles();
        checkStreaming();
        checkRejection();
#ifdef __ANDROID__
        checkProduction();
#endif
        std::cout << "PASS: CDR modes 1-3, FM hybrid isolation, DAB modes I-IV, periodic nulls, streaming acquisition/loss and negative controls" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
