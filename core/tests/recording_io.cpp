#include "../src/utils/riff.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

static uint32_t readSize(const std::string& filename, size_t offset) {
    std::ifstream input(filename, std::ios::binary);
    input.seekg(offset);
    uint32_t value = 0;
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    require(input.good(), "Cannot read chunk size");
    return value;
}

int main(int argumentCount, char** arguments) {
    try {
        require(argumentCount == 2, "Pass a test output directory");
        std::filesystem::path output(arguments[1]);
        std::filesystem::create_directories(output);
        std::string filename = (output / "checkpoint.wav").string();
        riff::Writer writer;
        require(!writer.open((output / "missing" / "invalid.wav").string(), "WAVE"), "Invalid path was accepted");
        require(!writer.error().empty(), "Open failure was silent");
        require(writer.open(filename, "WAVE"), "Cannot open valid output after an error");
        require(writer.error().empty(), "Open did not clear old error");
        std::array<uint8_t, 16> format{};
        writer.beginChunk("fmt ");
        writer.write(format.data(), format.size());
        writer.endChunk();
        writer.beginChunk("data");
        std::array<uint8_t, 128> samples{};
        writer.write(samples.data(), samples.size());
        require(writer.flush(), "Checkpoint failed");
        require(readSize(filename, 4) == 36 + samples.size(), "RIFF checkpoint size is wrong");
        require(readSize(filename, 40) == samples.size(), "Data checkpoint size is wrong");
        writer.write(samples.data(), samples.size());
        require(writer.flush(), "Second checkpoint failed");
        require(readSize(filename, 4) == 36 + 2 * samples.size(), "Repeated checkpoint double-counted chunks");
        require(readSize(filename, 40) == 2 * samples.size(), "Second data size is wrong");
        writer.endChunk();
        writer.close();
        require(writer.error().empty(), "Close failed");
        require(std::filesystem::file_size(filename) == 44 + 2 * samples.size(), "Unexpected recording size");
        require(readSize(filename, 4) == 36 + 2 * samples.size(), "Final RIFF size is wrong");

        require(writer.open((output / "failure.wav").string(), "WAVE"), "Cannot open failure test file");
        writer.beginChunk("data");
        writer.fail("simulated storage exhaustion");
        writer.write(samples.data(), samples.size());
        require(!writer.flush(), "Write failure was not reported");
        writer.endChunk();
        writer.close();
        require(writer.error() == "simulated storage exhaustion", "Close discarded the write error");
        require(writer.open((output / "recovery.wav").string(), "WAVE"), "Cannot reopen after failed capture");
        writer.close();
        require(writer.error().empty(), "New capture inherited an old error");

        recording::File file;
        require(file.open((output / "seek.bin").string()), "Cannot open seek test file");
        file.seekp(std::streampos(-1));
        require(!file.error().empty(), "Seek failure was silent");
        file.close();
        std::cout << "PASS: open failure, checkpoint headers, append, finalization, write failure, recovery, seek failure\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
