#include "library/CoverStore.h"

#include <algorithm>

namespace covers {
namespace {
constexpr const char* kSourceSuffixes[]{".cover.jpg", ".cover.jpeg", ".cover.png"};

bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
    if (value.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = value[value.size() - suffix.size() + i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

uint32_t storedSourceBytes(std::span<const uint8_t> rcov) {
    return rcov.size() >= kRcovHeaderBytes
               ? uint32_t(rcov[12]) | uint32_t(rcov[13]) << 8 | uint32_t(rcov[14]) << 16 | uint32_t(rcov[15]) << 24
               : 0;
}
} // namespace

std::string stem(std::string_view bookPath) {
    const size_t slash = bookPath.find_last_of('/');
    const size_t dot = bookPath.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return std::string{bookPath};
    return std::string{bookPath.substr(0, dot)};
}

std::string cachePath(std::string_view bookPath) { return stem(bookPath) + ".rcov"; }

std::vector<std::string> sourceCandidates(std::string_view bookPath) {
    const std::string base = stem(bookPath);
    std::vector<std::string> out;
    for (const char* suffix : kSourceSuffixes) out.push_back(base + suffix);
    return out;
}

bool isCoverFileName(std::string_view name) {
    for (const char* suffix : kSourceSuffixes) if (endsWithIgnoreCase(name, suffix)) return true;
    return endsWithIgnoreCase(name, ".rcov");
}

std::optional<std::string> CoverStore::sourceFor(std::string_view bookPath) const {
    for (auto& candidate : sourceCandidates(bookPath))
        if (io_.size(candidate)) return candidate;
    return std::nullopt;
}

bool CoverStore::needsThumbnail(std::string_view bookPath) const {
    const auto source = sourceFor(bookPath);
    if (!source) return false;
    const auto sourceBytes = io_.size(*source);
    const auto header = io_.read(cachePath(bookPath), kRcovHeaderBytes);
    if (!header || header->size() < kRcovHeaderBytes) return true;
    return storedSourceBytes(*header) != uint32_t(sourceBytes.value_or(0));
}

std::expected<void, std::string> CoverStore::generate(std::string_view bookPath) {
    const auto source = sourceFor(bookPath);
    if (!source) return std::unexpected(std::string{"no cover"});
    const auto bytes = io_.read(*source, kMaxSourceBytes);
    if (!bytes) return std::unexpected("cannot read " + *source);
    auto thumb = decodeThumbnail(*bytes);
    if (!thumb) return std::unexpected(thumb.error());
    const auto rcov = encodeRcov(*thumb, sourceHash(*bytes), uint32_t(bytes->size()));
    if (!io_.write(cachePath(bookPath), rcov)) return std::unexpected(std::string{"cannot write cover cache"});
    forget(bookPath);
    return {};
}

std::shared_ptr<const Thumbnail> CoverStore::thumbnail(std::string_view bookPath) {
    const std::string key{bookPath};
    for (auto it = memory_.begin(); it != memory_.end(); ++it) {
        if (it->path != key) continue;
        memory_.splice(memory_.begin(), memory_, it);
        return memory_.front().thumb;
    }
    if (needsThumbnail(bookPath)) return nullptr; // missing or stale: the background job will (re)build it
    const auto bytes = io_.read(cachePath(bookPath), kRcovHeaderBytes + kThumbPixels * 2);
    if (!bytes) return nullptr;
    auto decoded = decodeRcov(*bytes);
    if (!decoded) return nullptr;
    auto shared = std::make_shared<const Thumbnail>(std::move(*decoded));
    memory_.push_front({key, shared});
    while (memory_.size() > capacity_) memory_.pop_back();
    return shared;
}

std::expected<void, std::string> CoverStore::replaceSource(std::string_view bookPath, Format format,
                                                           std::span<const uint8_t> bytes) {
    if (format == Format::Unknown || sniff(bytes) != format) return std::unexpected(std::string{"unsupported cover format"});
    if (bytes.empty() || bytes.size() > kMaxSourceBytes) return std::unexpected(std::string{"cover size out of range"});
    const std::string target = stem(bookPath) + (format == Format::Png ? ".cover.png" : ".cover.jpg");
    if (!io_.write(target, bytes)) return std::unexpected(std::string{"cannot write cover"});
    for (auto& candidate : sourceCandidates(bookPath))
        if (candidate != target && io_.size(candidate)) io_.remove(candidate);
    io_.remove(cachePath(bookPath));
    forget(bookPath);
    return {};
}

void CoverStore::removeFor(std::string_view bookPath) {
    for (auto& candidate : sourceCandidates(bookPath))
        if (io_.size(candidate)) io_.remove(candidate);
    if (io_.size(cachePath(bookPath))) io_.remove(cachePath(bookPath));
    forget(bookPath);
}

void CoverStore::forget(std::string_view bookPath) {
    memory_.remove_if([&](const Entry& e) { return e.path == bookPath; });
}

} // namespace covers
