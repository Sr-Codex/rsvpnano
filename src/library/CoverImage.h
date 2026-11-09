#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

// Book cover thumbnails for the Lume library grid.
// A source cover (JPEG or PNG) is decoded once, cropped to fill the thumbnail
// ("cover" fit, centred), box-filtered down and stored as an `.rcov` cache next
// to the book. The library screen only ever blits the cached RGB565 pixels.
namespace covers {

inline constexpr int kThumbWidth = 96;
inline constexpr int kThumbHeight = 142;
inline constexpr size_t kThumbPixels = size_t(kThumbWidth) * kThumbHeight;
inline constexpr uint32_t kRcovMagic = 0x31564352; // "RCV1" little endian
inline constexpr size_t kRcovHeaderBytes = 16;
inline constexpr size_t kMaxSourceBytes = 4U * 1024U * 1024U;
inline constexpr size_t kMaxUploadBytes = 2U * 1024U * 1024U; // companion PUT limit (body held in PSRAM)

enum class Format : uint8_t { Unknown, Jpeg, Png };

struct Thumbnail {
    int width = kThumbWidth;
    int height = kThumbHeight;
    std::vector<uint16_t> pixels; // RGB565, native little endian, row-major
};

struct DecodeInfo {
    Format format = Format::Unknown;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int scale = 1;          // JPEG decode scale actually used (1, 2, 4 or 8)
    bool progressive = false;
};

[[nodiscard]] Format sniff(std::span<const uint8_t> data);
[[nodiscard]] uint32_t sourceHash(std::span<const uint8_t> data); // FNV-1a 32

// Decode a JPEG/PNG cover into a kThumbWidth x kThumbHeight RGB565 thumbnail.
[[nodiscard]] std::expected<Thumbnail, std::string> decodeThumbnail(std::span<const uint8_t> data,
                                                                    DecodeInfo* info = nullptr);

// `.rcov` cache: 16-byte header (magic, width, height, source hash, source byte size) + pixels (LE RGB565).
[[nodiscard]] std::vector<uint8_t> encodeRcov(const Thumbnail& thumb, uint32_t hash, uint32_t sourceBytes = 0);
[[nodiscard]] std::expected<Thumbnail, std::string> decodeRcov(std::span<const uint8_t> data,
                                                               uint32_t* hash = nullptr);

} // namespace covers
