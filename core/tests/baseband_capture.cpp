#include <utils/wav_capture.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static void require(bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

static std::vector<uint8_t> payload(const std::filesystem::path& path, int frameBytes) {
    std::ifstream input(path, std::ios::binary);
    std::array<char, 44> header{};
    input.read(header.data(), header.size());
    require(input.good(), "Cannot read WAV header: " + path.string());
    uint32_t riffBytes = 0;
    uint32_t dataBytes = 0;
    uint16_t alignment = 0;
    std::memcpy(&riffBytes, header.data() + 4, 4);
    std::memcpy(&dataBytes, header.data() + 40, 4);
    std::memcpy(&alignment, header.data() + 32, 2);
    require(std::memcmp(header.data(), "RIFF", 4) == 0 && std::memcmp(header.data() + 8, "WAVE", 4) == 0, "Invalid WAV container");
    std::vector<uint8_t> data{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(dataBytes == data.size() && riffBytes == dataBytes + 36, "WAV sizes are incorrect");
    require(alignment == frameBytes && data.size() % frameBytes == 0, "Split inside an IQ frame");
    return data;
}

static void checkFormat(const std::filesystem::path& output, wav::SampleType type, int sampleBytes) {
    constexpr int totalFrames = 263385;
    constexpr uint64_t segmentBytes = 12347;
    int frameBytes = 2 * sampleBytes;
    std::string name = "format" + std::to_string(static_cast<int>(type));
    std::string prefix = (output / name).string();
    wav::Capture capture(segmentBytes);
    wav::Writer reference(2, 2400000, wav::FORMAT_WAV, type);
    require(reference.open(prefix + "_reference.wav"), "Cannot create reference file");
    require(capture.start(prefix, 2400000, 2, type), "Cannot start capture");
    int nextFrame = 0;
    while (nextFrame < totalFrames) {
        int count = std::min(totalFrames - nextFrame, 1 + (nextFrame * 17 + 31) % 16383);
        std::vector<float> samples(static_cast<size_t>(count) * 2);
        for (int index = 0; index < count; ++index) {
            samples[index * 2] = static_cast<float>((nextFrame + index) % 4093 - 2046) / 4096.0f;
            samples[index * 2 + 1] = -samples[index * 2] + 0.125f;
        }
        reference.write(samples.data(), count);
        capture.enqueue(samples.data(), count);
        std::fill(samples.begin(), samples.end(), 0.0f);
        nextFrame += count;
    }
    reference.close();
    capture.stop();
    require(reference.getError().empty() && capture.getError().empty(), "Writing failed: " + capture.getError());
    require(capture.samplesWritten() == totalFrames, "Cumulative sample count changed during rotation");
    require(capture.bytesWritten() == static_cast<uint64_t>(totalFrames) * frameBytes, "Cumulative byte count is wrong");
    uint64_t partCapacity = segmentBytes / frameBytes * frameBytes;
    int expectedParts = static_cast<int>((capture.bytesWritten() + partCapacity - 1) / partCapacity);
    require(capture.fileCount() == expectedParts, "Unexpected segment count");
    std::vector<uint8_t> combined;
    for (int part = 1; part <= capture.fileCount(); ++part) {
        auto data = payload(prefix + "_part" + std::to_string(part) + ".wav", frameBytes);
        require(!data.empty() && data.size() <= partCapacity, "Invalid segment length");
        if (part < capture.fileCount()) { require(data.size() == partCapacity, "Segment rolled over too early"); }
        combined.insert(combined.end(), data.begin(), data.end());
    }
    require(combined == payload(prefix + "_reference.wav", frameBytes), "Samples were lost, duplicated, reordered or changed at a split");
    require(capture.start(prefix + "_restart", 2400000, 2, type), "Cannot restart capture");
    std::array<float, 6> tail{0.0f, 0.25f, 0.5f, -0.25f, -0.5f, 0.0f};
    capture.enqueue(tail.data(), 3);
    capture.stop();
    require(capture.getError().empty() && capture.samplesWritten() == 3 && capture.fileCount() == 1, "Restart did not reset counters or drain the tail");
    require(payload(prefix + "_restart_part1.wav", frameBytes).size() == 3 * frameBytes, "Stop discarded queued tail samples");
    std::cout << "PASS: " << name << ", " << totalFrames << " frames across " << expectedParts << " segments, byte-exact continuity\n";
}

int main(int argumentCount, char** arguments) {
    try {
        require(argumentCount == 2, "Pass a test output directory");
        std::filesystem::path output(arguments[1]);
        std::filesystem::create_directories(output);
        checkFormat(output, wav::SAMP_TYPE_UINT8, 1);
        checkFormat(output, wav::SAMP_TYPE_INT16, 2);
        checkFormat(output, wav::SAMP_TYPE_INT32, 4);
        checkFormat(output, wav::SAMP_TYPE_FLOAT32, 4);
        wav::Capture failure(64);
        std::string prefix = (output / "open_failure").string();
        std::filesystem::create_directory(prefix + "_part2.wav");
        require(failure.start(prefix, 48000, true), "Cannot start failure test");
        std::vector<float> samples(128, 0.25f);
        failure.enqueue(samples.data(), 64);
        failure.stop();
        require(!failure.getError().empty(), "Segment creation failed silently");
        require(failure.fileCount() == 1 && failure.bytesWritten() == 64, "Failed segment was counted as written");
        require(payload(prefix + "_part1.wav", 8).size() == 64, "Creation failure corrupted the previous segment");
        wav::Capture invalid(0);
        require(!invalid.start((output / "invalid").string(), 48000, true), "Zero-sized segments were accepted");
        require(!invalid.getError().empty(), "Invalid segment limit failed silently");
        std::cout << "PASS: segment-open failure is explicit, previous segment is finalized, invalid limits rejected\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
