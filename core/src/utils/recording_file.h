#pragma once
#include <cstdio>
#include <fstream>
#include <string>

namespace recording {
    class File {
    public:
        File() = default;
        File(const File&) = delete;
        File& operator=(const File&) = delete;
        ~File() { close(); }
        bool open(const std::string& path);
        bool is_open() const { return handle != nullptr; }
        void close();
        void write(const char* data, size_t length);
        std::streampos tellp();
        void seekp(std::streampos position);
        bool flush();
        const std::string& error() const { return lastError; }
        void fail(const std::string& message);
    private:
        FILE* handle = nullptr;
        std::string lastError;
    };
}
