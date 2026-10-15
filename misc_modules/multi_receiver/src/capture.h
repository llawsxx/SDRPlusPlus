#pragma once
#include <utils/wav.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

class ChannelCapture {
public:
    explicit ChannelCapture(uint64_t segmentBytes = 1024ULL * 1024 * 1024) : segmentLimit(segmentBytes) {}
    ~ChannelCapture() { stop(); }

    bool start(const std::string& filename, uint64_t sampleRate, bool iq) {
        stop();
        {
            std::lock_guard<std::mutex> lock(mutex);
            error.clear();
            queue.clear();
            queuedBytes = 0;
            stopping = false;
        }
        basePath = filename;
        part = 0;
        written.store(0);
        segments.store(0);
        bytesPerFrame = iq ? 8 : 4;
        writer.setChannels(2);
        writer.setSamplerate(sampleRate);
        writer.setSampleType(iq ? wav::SAMP_TYPE_FLOAT32 : wav::SAMP_TYPE_INT16);
        if (!openPart()) { return false; }
        lastProgress.store(nowMilliseconds());
        finished.store(false);
        active.store(true);
        worker = std::thread(&ChannelCapture::run, this);
        return true;
    }

    void requestStop() {
        active.store(false);
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        ready.notify_all();
    }

    void stop() {
        requestStop();
        if (worker.joinable()) { worker.join(); }
        writer.close();
        if (!writer.getError().empty()) { fail(writer.getError()); }
    }

    void enqueue(const float* samples, int count) {
        if (!active.load() || count <= 0) { return; }
        std::lock_guard<std::mutex> lock(mutex);
        if (!active.load() || stopping || !error.empty()) { return; }
        size_t length = static_cast<size_t>(count) * 2;
        if (queuedBytes + length * sizeof(float) > 32 * 1024 * 1024) {
            error = "Recording storage is too slow: queue overflow; capture stopped";
            active.store(false);
        } else {
            queue.emplace_back(samples, samples + length);
            queuedBytes += length * sizeof(float);
        }
        ready.notify_one();
    }

    std::string getError() {
        std::lock_guard<std::mutex> lock(mutex);
        return error;
    }

    uint64_t bytesWritten() const { return written.load(); }
    int fileCount() const { return segments.load(); }
    bool isActive() const { return active.load(); }
    bool isFinished() const { return finished.load(); }
    double secondsWithoutProgress() const { return (nowMilliseconds() - lastProgress.load()) / 1000.0; }

private:
    static int64_t nowMilliseconds() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void fail(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (error.empty()) { error = message; }
        active.store(false);
    }

    bool openPart() {
        std::string filename = basePath + "_part" + std::to_string(++part) + ".wav";
        if (!writer.open(filename)) {
            fail(writer.getError().empty() ? "Cannot create recording file" : writer.getError());
            writer.close();
            return false;
        }
        segments.store(part);
        return true;
    }

    void run() {
        auto lastData = std::chrono::steady_clock::now();
        while (true) {
            std::vector<float> block;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait_for(lock, std::chrono::milliseconds(250), [this] {
                    return stopping || !queue.empty() || !error.empty();
                });
                if (!error.empty() || (stopping && queue.empty())) { break; }
                if (queue.empty()) {
                    if (std::chrono::steady_clock::now() - lastData > std::chrono::seconds(5)) {
                        error = "No samples received for 5 seconds; recording stopped";
                        break;
                    }
                    continue;
                }
                block = std::move(queue.front());
                queue.pop_front();
                queuedBytes -= block.size() * sizeof(float);
            }
            int count = static_cast<int>(block.size() / 2);
            if (writer.getBytesWritten() > 0 && writer.getBytesWritten() + static_cast<uint64_t>(count) * bytesPerFrame > segmentLimit) {
                writer.close();
                if (!writer.getError().empty()) { fail(writer.getError()); break; }
                if (!openPart()) { break; }
            }
            size_t before = writer.getSamplesWritten();
            writer.write(block.data(), count);
            written.fetch_add((writer.getSamplesWritten() - before) * bytesPerFrame);
            if (!writer.getError().empty()) { fail(writer.getError()); break; }
            lastProgress.store(nowMilliseconds());
            lastData = std::chrono::steady_clock::now();
        }
        writer.close();
        if (!writer.getError().empty()) { fail(writer.getError()); }
        active.store(false);
        finished.store(true);
    }

    wav::Writer writer;
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::vector<float>> queue;
    std::thread worker;
    std::atomic<bool> active{false};
    std::atomic<bool> finished{true};
    std::atomic<int64_t> lastProgress{0};
    std::atomic<uint64_t> written{0};
    std::atomic<int> segments{0};
    bool stopping = false;
    size_t queuedBytes = 0;
    int bytesPerFrame = 4;
    int part = 0;
    std::string basePath;
    std::string error;
    uint64_t segmentLimit;
};
