#include "../../sink_modules/android_audio_sink/src/main.cpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

static std::atomic<int> writeCalls{0};
static std::atomic<int> writtenFrames{0};
static std::atomic<bool> stopped{false};
static int scenario = 0;

extern "C" {
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder** builder) { *builder = reinterpret_cast<AAudioStreamBuilder*>(1); return AAUDIO_OK; }
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder*, aaudio_direction_t) {}
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder*, aaudio_sharing_mode_t) {}
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder*, int32_t) {}
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder*, int32_t) {}
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder*, aaudio_format_t) {}
void AAudioStreamBuilder_setBufferCapacityInFrames(AAudioStreamBuilder*, int32_t) {}
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder*, AAudioStream_errorCallback, void*) {}
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder*, AAudioStream** stream) { *stream = reinterpret_cast<AAudioStream*>(1); return AAUDIO_OK; }
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder*) { return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestStart(AAudioStream*) { return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestStop(AAudioStream*) { stopped.store(true); return AAUDIO_OK; }
aaudio_result_t AAudioStream_close(AAudioStream*) { return AAUDIO_OK; }
const char* AAudio_convertResultToText(aaudio_result_t) { return "Mock audio result"; }
aaudio_result_t AAudioStream_write(AAudioStream*, const void*, int32_t frames, int64_t) {
    int call = ++writeCalls;
    if (scenario == 1) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!stopped.load() && std::chrono::steady_clock::now() < deadline) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        return AAUDIO_ERROR_INVALID_STATE;
    }
    if (scenario == 2) { return AAUDIO_ERROR_DISCONNECTED; }
    int accepted = call == 1 ? frames / 2 : frames;
    writtenFrames += accepted;
    return accepted;
}
}

static void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

int main() {
    try {
        for (scenario = 0; scenario < 3; ++scenario) {
            core::reportRecordingError("");
            writeCalls = 0;
            writtenFrames = 0;
            stopped = false;
            dsp::stream<dsp::stereo_t> input;
            SinkManager::Stream stream;
            stream.sinkOut = &input;
            AudioSink sink(&stream, "Test output");
            sink.start();
            for (int index = 0; index < 800; ++index) { input.writeBuf[index] = {0.1f, -0.1f}; }
            require(input.swap(800), "Cannot submit audio block");
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < deadline) {
                if (scenario == 0 && writtenFrames == 800) { break; }
                if (scenario == 1 && writeCalls > 0) { break; }
                if (scenario == 2 && !core::recordingError().empty()) { break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            sink.stop();
            require(writeCalls > 0, "Audio worker never ran");
            if (scenario == 0) {
                require(writeCalls == 2 && writtenFrames == 800, "Partial write lost audio frames");
                require(core::recordingError().empty(), "Partial write reported a spurious error");
            } else if (scenario == 1) { require(core::recordingError().empty(), "Intentional stop reported an output failure"); }
            else { require(core::recordingError().find("write failure") != std::string::npos, "Real output failure was silently suppressed"); }
        }
        std::cout << "PASS: actual Android audio worker, partial-write retry, intentional-stop suppression, real-error reporting (mock AAudio, no audio hardware)" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
