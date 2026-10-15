#include "recording_file.h"
#include <cerrno>
#include <cstring>
#ifdef __ANDROID__
#include <android_backend.h>
#include <unistd.h>
#endif

namespace recording {
    void File::fail(const std::string& message) {
        if (lastError.empty()) { lastError = message; }
    }

    bool File::open(const std::string& path) {
        close();
        lastError.clear();
#ifdef __ANDROID__
        if (path.rfind("content://", 0) == 0) {
            size_t separator = path.find_last_of('/');
            int descriptor = backend::openRecordingDocument(path.substr(0, separator), path.substr(separator + 1));
            if (descriptor < 0) {
                fail("Cannot create recording: " + backend::recordingError());
                return false;
            }
            if (lseek(descriptor, 0, SEEK_SET) < 0) {
                fail("Selected provider does not support seekable WAV files; choose local device storage");
                ::close(descriptor);
                return false;
            }
            handle = fdopen(descriptor, "wb+");
            if (!handle) { ::close(descriptor); }
        } else
#endif
        {
            handle = fopen(path.c_str(), "wb+");
        }
        if (!handle) { fail(std::string("Cannot open recording: ") + std::strerror(errno)); }
        return handle != nullptr;
    }

    void File::close() {
        if (!handle) { return; }
        if (fclose(handle) != 0) { fail(std::string("Cannot finalize recording: ") + std::strerror(errno)); }
        handle = nullptr;
    }

    void File::write(const char* data, size_t length) {
        if (!handle || !lastError.empty()) { return; }
        if (fwrite(data, 1, length, handle) != length) {
            fail(std::string("Recording write failed: ") + std::strerror(errno));
        }
    }

    std::streampos File::tellp() {
        if (!handle) { return std::streampos(-1); }
#ifdef _WIN32
        auto position = _ftelli64(handle);
#else
        auto position = ftello(handle);
#endif
        if (position < 0) { fail("Cannot read recording file position"); }
        return std::streampos(position);
    }

    void File::seekp(std::streampos position) {
        if (!handle) { fail("Recording file is closed"); return; }
#ifdef _WIN32
        int result = _fseeki64(handle, static_cast<std::streamoff>(position), SEEK_SET);
#else
        int result = fseeko(handle, static_cast<std::streamoff>(position), SEEK_SET);
#endif
        if (result != 0) { fail("Cannot seek recording file"); }
    }

    bool File::flush() {
        if (!handle) { return false; }
        if (fflush(handle) != 0) { fail(std::string("Recording flush failed: ") + std::strerror(errno)); }
        return lastError.empty();
    }
}
