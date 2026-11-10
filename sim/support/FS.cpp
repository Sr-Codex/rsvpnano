#include "FS.h"

#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace stdfs = std::filesystem;

namespace fs {

    struct SimFileState {
        std::string path;    // device path, e.g. /library/books/a.rsvp
        std::string host;    // host path
        std::string name;    // last path component
        std::FILE* file = nullptr;
        bool directory = false;
        std::vector<std::string> entries; // directory listing (names)
        size_t nextEntry = 0;
        ~SimFileState() {
            if (file)
                std::fclose(file);
        }
    };

    namespace {
        std::string normalize(const char* path) {
            std::string p = path ? path : "/";
            if (p.empty() || p[0] != '/')
                p.insert(p.begin(), '/');
            while (p.size() > 1 && p.back() == '/')
                p.pop_back();
            return p;
        }

        std::string baseName(const std::string& path) {
            const auto slash = path.rfind('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        std::shared_ptr<SimFileState> openState(const std::string& device, const std::string& host, const char* mode,
                                                bool create) {
            std::error_code error;
            auto state = std::make_shared<SimFileState>();
            state->path = device;
            state->host = host;
            state->name = baseName(device);
            const std::string m = mode ? mode : "r";
            if (stdfs::is_directory(host, error)) {
                state->directory = true;
                for (const auto& entry : stdfs::directory_iterator(host, error)) {
                    const auto name = entry.path().filename().string();
                    if (name == ".DS_Store")
                        continue;
                    state->entries.push_back(name);
                }
                std::sort(state->entries.begin(), state->entries.end());
                return state;
            }
            if (m[0] == 'r' && !stdfs::exists(host, error))
                return nullptr;
            if (m[0] != 'r' || create)
                stdfs::create_directories(stdfs::path(host).parent_path(), error);
            std::string hostMode = m;
            if (hostMode.find('b') == std::string::npos)
                hostMode += 'b';
            if (hostMode == "wb")
                hostMode = "w+b"; // Arduino FILE_WRITE files are readable after writing
            if (hostMode == "ab")
                hostMode = "a+b";
            state->file = std::fopen(host.c_str(), hostMode.c_str());
            if (!state->file)
                return nullptr;
            return state;
        }
    } // namespace

    std::string FS::hostPath(const char* path) const { return root_ + normalize(path); }

    File FS::open(const char* path, const char* mode, bool create) {
        const auto device = normalize(path);
        auto state = openState(device, hostPath(device.c_str()), mode, create);
        return state ? File(state) : File();
    }

    bool FS::exists(const char* path) {
        std::error_code error;
        return stdfs::exists(hostPath(path), error);
    }

    bool FS::remove(const char* path) {
        std::error_code error;
        const auto host = hostPath(path);
        return !stdfs::is_directory(host, error) && stdfs::remove(host, error);
    }

    bool FS::rename(const char* from, const char* to) {
        std::error_code error;
        const auto target = hostPath(to);
        if (stdfs::exists(target, error))
            return false; // FAT/VFS rename does not overwrite
        stdfs::rename(hostPath(from), target, error);
        return !error;
    }

    bool FS::mkdir(const char* path) {
        std::error_code error;
        const auto host = hostPath(path);
        if (stdfs::is_directory(host, error))
            return true;
        return stdfs::create_directory(host, error);
    }

    bool FS::rmdir(const char* path) {
        std::error_code error;
        const auto host = hostPath(path);
        return stdfs::is_directory(host, error) && stdfs::is_empty(host, error) && stdfs::remove(host, error);
    }

    size_t File::write(uint8_t value) { return write(&value, 1); }

    size_t File::write(const uint8_t* buffer, size_t size) {
        if (!state_ || !state_->file || !buffer)
            return 0;
        return std::fwrite(buffer, 1, size, state_->file);
    }

    int File::available() {
        if (!state_ || !state_->file)
            return 0;
        const long here = std::ftell(state_->file);
        return here < 0 ? 0 : static_cast<int>(size() - static_cast<size_t>(here));
    }

    int File::read() {
        uint8_t value = 0;
        return read(&value, 1) == 1 ? value : -1;
    }

    int File::peek() {
        if (!state_ || !state_->file)
            return -1;
        const int c = std::fgetc(state_->file);
        if (c != EOF)
            std::ungetc(c, state_->file);
        return c == EOF ? -1 : c;
    }

    void File::flush() {
        if (state_ && state_->file)
            std::fflush(state_->file);
    }

    size_t File::read(uint8_t* buffer, size_t size) {
        if (!state_ || !state_->file || !buffer)
            return 0;
        return std::fread(buffer, 1, size, state_->file);
    }

    bool File::seek(uint32_t position, SeekMode mode) {
        if (!state_ || !state_->file)
            return false;
        const int whence = mode == SeekCur ? SEEK_CUR : (mode == SeekEnd ? SEEK_END : SEEK_SET);
        if (mode == SeekSet && position > size())
            return false;
        return std::fseek(state_->file, static_cast<long>(position), whence) == 0;
    }

    size_t File::position() const {
        if (!state_ || !state_->file)
            return 0;
        const long here = std::ftell(state_->file);
        return here < 0 ? 0 : static_cast<size_t>(here);
    }

    size_t File::size() const {
        if (!state_ || !state_->file)
            return 0;
        std::fflush(state_->file);
        struct stat info {};
        return fstat(fileno(state_->file), &info) == 0 ? static_cast<size_t>(info.st_size) : 0;
    }

    void File::close() { state_.reset(); }

    File::operator bool() const { return state_ != nullptr; }

    time_t File::getLastWrite() {
        struct stat info {};
        return state_ && stat(state_->host.c_str(), &info) == 0 ? info.st_mtime : 0;
    }

    const char* File::path() const { return state_ ? state_->path.c_str() : ""; }
    const char* File::name() const { return state_ ? state_->name.c_str() : ""; }
    bool File::isDirectory() const { return state_ && state_->directory; }

    bool File::seekDir(long position) {
        if (!isDirectory() || position < 0)
            return false;
        state_->nextEntry = static_cast<size_t>(position);
        return true;
    }

    File File::openNextFile(const char* mode) {
        if (!isDirectory() || state_->nextEntry >= state_->entries.size())
            return File();
        const auto& entry = state_->entries[state_->nextEntry++];
        const std::string device = (state_->path == "/" ? "" : state_->path) + "/" + entry;
        const std::string host = state_->host + "/" + entry;
        auto next = openState(device, host, mode, false);
        return next ? File(next) : openNextFile(mode);
    }

    String File::getNextFileName() {
        bool directory = false;
        return getNextFileName(&directory);
    }

    String File::getNextFileName(bool* directory) {
        if (!isDirectory() || state_->nextEntry >= state_->entries.size())
            return String();
        const auto& entry = state_->entries[state_->nextEntry++];
        std::error_code error;
        if (directory)
            *directory = stdfs::is_directory(state_->host + "/" + entry, error);
        return String(((state_->path == "/" ? "" : state_->path) + "/" + entry).c_str());
    }

    void File::rewindDirectory() {
        if (isDirectory())
            state_->nextEntry = 0;
    }

    String File::readString() {
        std::string out;
        uint8_t buffer[512];
        size_t n = 0;
        while ((n = read(buffer, sizeof(buffer))) > 0)
            out.append(reinterpret_cast<const char*>(buffer), n);
        return String(out);
    }

    String File::readStringUntil(char terminator) {
        std::string out;
        for (int c = read(); c >= 0 && c != terminator; c = read())
            out.push_back(static_cast<char>(c));
        return String(out);
    }

} // namespace fs
