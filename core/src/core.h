#pragma once
#include <config.h>
#include <module.h>
#include <module_com.h>
#include "command_args.h"

namespace core {
    SDRPP_EXPORT ConfigManager configManager;
    SDRPP_EXPORT ModuleManager moduleManager;
    SDRPP_EXPORT ModuleComManager modComManager;
    SDRPP_EXPORT CommandArgsParser args;
    SDRPP_EXPORT bool isSessionLocked();
    SDRPP_EXPORT void setSessionLocked(bool locked);
    enum class ScreenOrientation { Automatic, Portrait, Landscape };
    SDRPP_EXPORT ScreenOrientation getScreenOrientation();
    SDRPP_EXPORT bool setScreenOrientation(ScreenOrientation orientation);
    SDRPP_EXPORT bool isTuningLocked();
    SDRPP_EXPORT void retainTuningLock();
    SDRPP_EXPORT void releaseTuningLock();
    SDRPP_EXPORT Event<int> onFrame;
    SDRPP_EXPORT void reportRecordingError(const std::string& message);
    SDRPP_EXPORT std::string recordingError();

    void setInputSampleRate(double samplerate);
};

int sdrpp_main(int argc, char* argv[]);
