#pragma once
#include <vector>
#include <stdint.h>
#include <string>

namespace backend {
    struct DevVIDPID {
        uint16_t vid;
        uint16_t pid;
    };

    extern const std::vector<DevVIDPID> AIRSPY_VIDPIDS;
    extern const std::vector<DevVIDPID> AIRSPYHF_VIDPIDS;
    extern const std::vector<DevVIDPID> HACKRF_VIDPIDS;
    extern const std::vector<DevVIDPID> HYDRASDR_VIDPIDS;
    extern const std::vector<DevVIDPID> RTL_SDR_VIDPIDS;

    int getDeviceFD(int& vid, int& pid, const std::vector<DevVIDPID>& allowedVidPids);
    void setSessionLocked(bool locked);
    void chooseRecordingDirectory();
    std::string recordingDirectoryResult();
    bool canWriteRecordingDirectory(const std::string& uri);
    int openRecordingDocument(const std::string& tree, const std::string& filename);
    std::string recordingError();
}
