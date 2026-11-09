#include "library/CoverStoreSd.h"

#include <FS.h>

#include "board/BoardStorage.h"
#include "storage/fs/StorageFiles.h"

namespace covers {

Io sdIo() {
    Io io;
    io.read = [](const std::string& path, size_t maxBytes) -> std::optional<std::vector<uint8_t>> {
        File file = Board::Storage::filesystem().open(path.c_str(), FILE_READ);
        if (!file || file.isDirectory()) {
            if (file) file.close();
            return std::nullopt;
        }
        const size_t bytes = std::min(static_cast<size_t>(file.size()), maxBytes);
        std::vector<uint8_t> out(bytes);
        const size_t got = bytes ? file.read(out.data(), bytes) : 0;
        file.close();
        if (got != bytes) return std::nullopt;
        return out;
    };
    io.write = [](const std::string& path, std::span<const uint8_t> bytes) {
        // Temporary names end in ".tmp" so the storage check sweeps leftovers after a power cut.
        const std::string temp = path + ".tmp", backup = path + ".bak.tmp";
        return StorageFiles::writeFileAtomic(Board::Storage::filesystem(), path.c_str(), temp.c_str(), backup.c_str(),
                                             std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()})
            .has_value();
    };
    io.remove = [](const std::string& path) { return Board::Storage::filesystem().remove(path.c_str()); };
    io.size = [](const std::string& path) -> std::optional<size_t> {
        File file = Board::Storage::filesystem().open(path.c_str(), FILE_READ);
        if (!file || file.isDirectory()) {
            if (file) file.close();
            return std::nullopt;
        }
        const size_t bytes = static_cast<size_t>(file.size());
        file.close();
        return bytes;
    };
    return io;
}

} // namespace covers
