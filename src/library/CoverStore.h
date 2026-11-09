#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "library/CoverImage.h"

// Where covers live on the card and how their thumbnails are cached.
//   /library/books/Book.rsvp
//   /library/books/Book.cover.jpg | .jpeg | .png   source cover (uploaded, extracted or replaced by hand)
//   /library/books/Book.rcov                       96x142 RGB565 thumbnail cache
// File access is injected so the logic runs in host tests and on the device.
namespace covers {

struct Io {
    std::function<std::optional<std::vector<uint8_t>>(const std::string& path, size_t maxBytes)> read;
    std::function<bool(const std::string& path, std::span<const uint8_t> bytes)> write; // atomic on device
    std::function<bool(const std::string& path)> remove;
    std::function<std::optional<size_t>(const std::string& path)> size; // nullopt = missing
};

[[nodiscard]] std::string stem(std::string_view bookPath);          // path without extension
[[nodiscard]] std::string cachePath(std::string_view bookPath);     // Book.rcov
[[nodiscard]] std::vector<std::string> sourceCandidates(std::string_view bookPath);
[[nodiscard]] bool isCoverFileName(std::string_view name);          // *.cover.jpg/.jpeg/.png or *.rcov

class CoverStore {
public:
    explicit CoverStore(Io io, size_t memoryThumbnails = 12) : io_(std::move(io)), capacity_(memoryThumbnails) {}

    // First existing source cover for the book, if any.
    [[nodiscard]] std::optional<std::string> sourceFor(std::string_view bookPath) const;
    // True when a source exists and the cache is missing or stale (source size changed).
    [[nodiscard]] bool needsThumbnail(std::string_view bookPath) const;
    // Decode the source and write the cache. Drops any in-memory copy.
    std::expected<void, std::string> generate(std::string_view bookPath);
    // Cached thumbnail (memory LRU, then card). nullptr when absent/stale/corrupt.
    [[nodiscard]] std::shared_ptr<const Thumbnail> thumbnail(std::string_view bookPath);
    // Store a new source cover (replaces any other format) and invalidate the cache.
    std::expected<void, std::string> replaceSource(std::string_view bookPath, Format format,
                                                   std::span<const uint8_t> bytes);
    // Remove sources and cache (book deleted).
    void removeFor(std::string_view bookPath);
    void forget(std::string_view bookPath); // drop memory copy only
    void clearMemory() { memory_.clear(); }

private:
    struct Entry { std::string path; std::shared_ptr<const Thumbnail> thumb; };
    Io io_;
    size_t capacity_;
    std::list<Entry> memory_; // most recent first
};

} // namespace covers
