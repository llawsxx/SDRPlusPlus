#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <signal_path/sink.h>
#include <dsp/buffer/packer.h>
#include <utils/flog.h>
#include <config.h>
#include <utils/optionlist.h>
#include <aaudio/AAudio.h>
#include <core.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "audio_sink",
    /* Description:     */ "Android audio sink module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class AudioSink : SinkManager::Sink {
public:
    AudioSink(SinkManager::Stream* stream, std::string streamName) {
        _stream = stream;
        _streamName = streamName;

        packer.init(_stream->sinkOut, 512);

        // TODO: Add choice? I don't think anyone cares on android...
        sampleRate = 48000;
        _stream->setSampleRate(sampleRate);
    }

    ~AudioSink() {
    }

    void start() {
        if (running) {
            return;
        }
        doStart();
        running = true;
    }

    void stop() {
        if (!running) {
            return;
        }
        doStop();
        running = false;
    }

    void menuHandler() {
        // Draw menu here
    }

private:
    void doStart() {
        // Create stream builder
        AAudioStreamBuilder *builder = nullptr;
        aaudio_result_t result = AAudio_createStreamBuilder(&builder);
        outputErrorReported = false;
        if (result != AAUDIO_OK || !builder) {
            core::reportRecordingError(_streamName + ": cannot create audio output; recording streams remain active");
            packer.start();
            workerThread = std::thread(&AudioSink::worker, this);
            return;
        }

        // Set stream options
        bufferSize = round(sampleRate / 60.0);
        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setSampleRate(builder, sampleRate);
        AAudioStreamBuilder_setChannelCount(builder, 2);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setBufferCapacityInFrames(builder, bufferSize);
        AAudioStreamBuilder_setErrorCallback(builder, errorCallback, this);
        packer.setSampleCount(bufferSize);
        
        // Open the stream
        result = AAudioStreamBuilder_openStream(builder, &stream);
        if (result != AAUDIO_OK || !stream) {
            stream = nullptr;
            core::reportRecordingError(_streamName + ": audio output unavailable: " + AAudio_convertResultToText(result));
        }

        // Stream stream and packer
        packer.start();
        if (stream) {
            result = AAudioStream_requestStart(stream);
            if (result != AAUDIO_OK) {
                core::reportRecordingError(_streamName + ": cannot start audio output: " + AAudio_convertResultToText(result));
                AAudioStream_close(stream);
                stream = nullptr;
            }
        }

        // We no longer need the builder
        AAudioStreamBuilder_delete(builder);

        // Start worker thread
        workerThread = std::thread(&AudioSink::worker, this);
    }

    void doStop() {
        packer.stop();
        packer.out.stopReader();
        if (stream) { AAudioStream_requestStop(stream); }
        if (workerThread.joinable()) { workerThread.join(); }
        if (stream) { AAudioStream_close(stream); stream = nullptr; }
        packer.out.clearReadStop();
    }

    void worker() {
        while (true) {
            int count = packer.out.read();
            if (count < 0) { return; }
            if (stream) {
                aaudio_result_t written = AAudioStream_write(stream, packer.out.readBuf, count, 100000000);
                if (written != count && !outputErrorReported) {
                    outputErrorReported = true;
                    core::reportRecordingError(_streamName + ": audio output underrun or write failure; check output device");
                }
            }
            packer.out.flush();
        }
    }

    static void errorCallback(AAudioStream *stream, void *userData, aaudio_result_t error){
        // detect an audio device detached and restart the stream
        if (error == AAUDIO_ERROR_DISCONNECTED){
            std::thread thr(&AudioSink::restart, (AudioSink*)userData);
            thr.detach();
        }
    }

    void restart() {
        if (running) { doStop(); }
        if (running) { doStart(); }
    }

    std::thread workerThread;

    AAudioStream *stream = NULL;
    bool outputErrorReported = false;
    SinkManager::Stream* _stream;
    dsp::buffer::Packer<dsp::stereo_t> packer;

    std::string _streamName;
    double sampleRate;
    int bufferSize;

    bool running = false;
};

class AudioSinkModule : public ModuleManager::Instance {
public:
    AudioSinkModule(std::string name) {
        this->name = name;
        provider.create = create_sink;
        provider.ctx = this;

        sigpath::sinkManager.registerSinkProvider("Audio", provider);
    }

    ~AudioSinkModule() {
        // Unregister sink, this will automatically stop and delete all instances of the audio sink
        sigpath::sinkManager.unregisterSinkProvider("Audio");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static SinkManager::Sink* create_sink(SinkManager::Stream* stream, std::string streamName, void* ctx) {
        return (SinkManager::Sink*)(new AudioSink(stream, streamName));
    }

    std::string name;
    bool enabled = true;
    SinkManager::SinkProvider provider;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/audio_sink_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    AudioSinkModule* instance = new AudioSinkModule(name);
    return instance;
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (AudioSinkModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
