#include "library/CoverImage.h"

#include <JPEGDEC.h>
#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace covers {
namespace {

// Accumulates source pixels into the target grid. Source pixels may arrive in
// any order (JPEG MCU blocks or PNG rows); each lands in exactly one cell.
struct Downscaler {
    int cropX = 0, cropY = 0, cropW = 1, cropH = 1;
    // r, g, b, count per cell, one allocation (no exceptions in firmware builds).
    std::unique_ptr<uint32_t[]> sums;
    uint32_t* r = nullptr; uint32_t* g = nullptr; uint32_t* b = nullptr; uint32_t* n = nullptr;

    bool begin(int width, int height) {
        // "Cover" fit: keep the thumbnail aspect ratio, crop the excess, centred.
        const int64_t lhs = int64_t(width) * kThumbHeight, rhs = int64_t(height) * kThumbWidth;
        if (lhs > rhs) { // source is wider: crop left/right
            cropH = height;
            cropW = std::max(1, int(int64_t(height) * kThumbWidth / kThumbHeight));
        } else {         // source is taller: crop top/bottom
            cropW = width;
            cropH = std::max(1, int(int64_t(width) * kThumbHeight / kThumbWidth));
        }
        cropX = (width - cropW) / 2;
        cropY = (height - cropH) / 2;
        sums.reset(new (std::nothrow) uint32_t[kThumbPixels * 4]());
        if (!sums) return false;
        r = sums.get(); g = r + kThumbPixels; b = g + kThumbPixels; n = b + kThumbPixels;
        return true;
    }

    void add(int x, int y, uint16_t pixel) {
        x -= cropX; y -= cropY;
        if (x < 0 || y < 0 || x >= cropW || y >= cropH) return;
        const size_t tx = size_t(int64_t(x) * kThumbWidth / cropW);
        const size_t ty = size_t(int64_t(y) * kThumbHeight / cropH);
        const size_t i = ty * kThumbWidth + tx;
        r[i] += (pixel >> 11) & 31; g[i] += (pixel >> 5) & 63; b[i] += pixel & 31; ++n[i];
    }

    std::expected<Thumbnail, std::string> finish() const {
        Thumbnail out;
        out.pixels.resize(kThumbPixels);
        size_t filled = 0;
        for (size_t i = 0; i < kThumbPixels; ++i) {
            if (n[i] == 0) continue;
            ++filled;
            const uint32_t rr = (r[i] + n[i] / 2) / n[i], gg = (g[i] + n[i] / 2) / n[i], bb = (b[i] + n[i] / 2) / n[i];
            out.pixels[i] = uint16_t((rr << 11) | (gg << 5) | bb);
        }
        if (filled == 0) return std::unexpected(std::string{"cover image has no pixels"});
        // Covers smaller than the thumbnail leave gaps: fill each from its left/upper neighbour.
        for (size_t i = 0; i < kThumbPixels; ++i) {
            if (n[i] != 0) continue;
            const size_t x = i % kThumbWidth;
            out.pixels[i] = x > 0 ? out.pixels[i - 1] : i >= size_t(kThumbWidth) ? out.pixels[i - kThumbWidth] : out.pixels[i];
        }
        if (n[0] == 0) { // first cell had no sample: take the first real one
            for (size_t i = 0; i < kThumbPixels; ++i) if (n[i]) { out.pixels[0] = out.pixels[i]; break; }
            for (size_t i = 1; i < size_t(kThumbWidth); ++i) if (!n[i]) out.pixels[i] = out.pixels[i - 1];
        }
        return out;
    }
};

int onJpegBlock(JPEGDRAW* draw) {
    auto* scaler = static_cast<Downscaler*>(draw->pUser);
    for (int row = 0; row < draw->iHeight; ++row)
        for (int col = 0; col < draw->iWidthUsed; ++col)
            scaler->add(draw->x + col, draw->y + row, draw->pPixels[row * draw->iWidth + col]);
    return 1;
}

// Largest JPEG scale (1/1..1/8) that still leaves at least one source pixel per thumbnail cell.
int pickScale(int width, int height) {
    for (const int s : {8, 4, 2}) {
        const int w = width / s, h = height / s;
        if (int64_t(w) * kThumbHeight >= int64_t(h) * kThumbWidth ? h >= kThumbHeight : w >= kThumbWidth) return s;
    }
    return 1;
}

std::expected<Thumbnail, std::string> decodeJpeg(std::span<const uint8_t> data, DecodeInfo* info) {
    // JPEGDEC's state is several KB: keep it off the (small) task stack.
    auto jpeg = std::unique_ptr<JPEGDEC>(new (std::nothrow) JPEGDEC());
    if (!jpeg) return std::unexpected(std::string{"out of memory (jpeg)"});
    if (!jpeg->openRAM(const_cast<uint8_t*>(data.data()), int(data.size()), onJpegBlock))
        return std::unexpected("jpeg open failed: " + std::to_string(jpeg->getLastError()));
    const int width = jpeg->getWidth(), height = jpeg->getHeight();
    const bool progressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
    // Progressive files: JPEGDEC decodes only the first (DC) scan, i.e. a 1/8 image.
    const int scale = progressive ? 8 : pickScale(width, height);
    if (info) { info->sourceWidth = width; info->sourceHeight = height; info->scale = scale; info->progressive = progressive; }
    Downscaler scaler;
    if (!scaler.begin(width / scale, height / scale)) return std::unexpected(std::string{"out of memory (thumb)"});
    jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
    jpeg->setUserPointer(&scaler);
    const int option = scale == 8 ? JPEG_SCALE_EIGHTH : scale == 4 ? JPEG_SCALE_QUARTER : scale == 2 ? JPEG_SCALE_HALF : 0;
    const int ok = jpeg->decode(0, 0, option);
    const int error = jpeg->getLastError();
    jpeg->close();
    if (!ok) return std::unexpected("jpeg decode failed: " + std::to_string(error));
    return scaler.finish();
}

// PNG reader on the project's zlib (PNGdec bundles a modified zlib whose symbols
// clash with it). Streams IDAT through inflate one scanline at a time: memory is
// two rows, never the whole image. Non-interlaced only (Adam7 is rare for covers).
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

std::expected<Thumbnail, std::string> decodePng(std::span<const uint8_t> data, DecodeInfo* info) {
    const uint8_t* p = data.data();
    const size_t size = data.size();
    size_t at = 8;
    uint32_t width = 0, height = 0;
    uint8_t depth = 0, color = 0, interlace = 0;
    uint16_t palette[256]{};
    int channels = 0;
    // IHDR must come first.
    if (size < at + 25 || std::memcmp(p + at + 4, "IHDR", 4) != 0) return std::unexpected(std::string{"png missing IHDR"});
    width = be32(p + at + 8); height = be32(p + at + 12); depth = p[at + 16]; color = p[at + 17]; interlace = p[at + 20];
    if (info) { info->sourceWidth = int(width); info->sourceHeight = int(height); info->scale = 1; }
    if (width == 0 || height == 0 || width > 8192 || height > 8192) return std::unexpected(std::string{"png size out of range"});
    if (interlace != 0) return std::unexpected(std::string{"interlaced png not supported"});
    switch (color) {
    case 0: channels = 1; break;            // gray
    case 2: channels = 3; break;            // RGB
    case 3: channels = 1; break;            // palette
    case 4: channels = 2; break;            // gray + alpha
    case 6: channels = 4; break;            // RGBA
    default: return std::unexpected(std::string{"png colour type not supported"});
    }
    if (!(depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) || (depth < 8 && color != 0 && color != 3)
        || (depth == 16 && color == 3))
        return std::unexpected(std::string{"png bit depth not supported"});
    const size_t bitsPerPixel = size_t(channels) * depth;
    const size_t rowBytes = (size_t(width) * bitsPerPixel + 7) / 8;
    const size_t bpp = std::max<size_t>(1, bitsPerPixel / 8); // filter byte distance
    std::unique_ptr<uint8_t[]> rows(new (std::nothrow) uint8_t[2 * (rowBytes + 1)]());
    if (!rows) return std::unexpected(std::string{"out of memory (png rows)"});
    uint8_t* cur = rows.get();
    uint8_t* prev = cur + rowBytes + 1;
    std::memset(prev, 0, rowBytes + 1);

    Downscaler scaler;
    if (!scaler.begin(int(width), int(height))) return std::unexpected(std::string{"out of memory (thumb)"});

    z_stream z{};
    if (inflateInit(&z) != Z_OK) return std::unexpected(std::string{"png inflate init failed"});
    struct Closer { z_stream* z; ~Closer() { inflateEnd(z); } } closer{&z};
    uint32_t y = 0;
    size_t filled = 0; // bytes of the current scanline (filter byte + data)
    const auto sample = [&](const uint8_t* row, size_t x, int ch) -> int { // 0..255
        if (depth == 8) return row[x * channels + ch];
        if (depth == 16) return row[(x * channels + ch) * 2];
        const size_t bit = x * depth;
        const int v = (row[bit / 8] >> (8 - depth - int(bit % 8))) & ((1 << depth) - 1);
        return color == 3 ? v : v * 255 / ((1 << depth) - 1);
    };
    const auto emitRow = [&]() -> bool {
        uint8_t* row = cur + 1;
        const uint8_t* up = prev + 1;
        const uint8_t filter = cur[0];
        for (size_t i = 0; i < rowBytes; ++i) {
            const int left = i >= bpp ? row[i - bpp] : 0, above = up[i], diag = i >= bpp ? up[i - bpp] : 0;
            switch (filter) {
            case 0: break;
            case 1: row[i] = uint8_t(row[i] + left); break;
            case 2: row[i] = uint8_t(row[i] + above); break;
            case 3: row[i] = uint8_t(row[i] + ((left + above) >> 1)); break;
            case 4: row[i] = uint8_t(row[i] + paeth(left, above, diag)); break;
            default: return false;
            }
        }
        for (size_t x = 0; x < width; ++x) {
            int r, g, b, a = 255;
            if (color == 3) { const uint16_t c = palette[sample(row, x, 0) & 255]; scaler.add(int(x), int(y), c); continue; }
            if (channels <= 2) { r = g = b = sample(row, x, 0); if (channels == 2) a = sample(row, x, 1); }
            else { r = sample(row, x, 0); g = sample(row, x, 1); b = sample(row, x, 2); if (channels == 4) a = sample(row, x, 3); }
            if (a < 255) { r = (r * a + 255 * (255 - a)) / 255; g = (g * a + 255 * (255 - a)) / 255; b = (b * a + 255 * (255 - a)) / 255; } // over white
            scaler.add(int(x), int(y), uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)));
        }
        std::swap(cur, prev);
        return true;
    };

    for (at = 8; at + 12 <= size && y < height;) {
        const uint32_t length = be32(p + at);
        if (length > size - at - 12) return std::unexpected(std::string{"png truncated chunk"});
        const uint8_t* type = p + at + 4;
        const uint8_t* body = p + at + 8;
        if (std::memcmp(type, "PLTE", 4) == 0) {
            for (uint32_t i = 0; i < length / 3 && i < 256; ++i)
                palette[i] = uint16_t((body[i * 3] >> 3) << 11 | (body[i * 3 + 1] >> 2) << 5 | (body[i * 3 + 2] >> 3));
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            z.next_in = const_cast<Bytef*>(body);
            z.avail_in = length;
            while (z.avail_in > 0 && y < height) {
                z.next_out = cur + filled;
                z.avail_out = uInt(rowBytes + 1 - filled);
                const int rc = inflate(&z, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END) return std::unexpected(std::string{"png inflate failed"});
                filled = rowBytes + 1 - z.avail_out;
                if (filled == rowBytes + 1) {
                    if (!emitRow()) return std::unexpected(std::string{"png bad filter"});
                    ++y; filled = 0;
                }
                if (rc == Z_STREAM_END) break;
            }
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += size_t(length) + 12;
    }
    if (y < height) return std::unexpected(std::string{"png truncated image"});
    return scaler.finish();
}

void putU16(std::vector<uint8_t>& out, uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); }
void putU32(std::vector<uint8_t>& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); }
uint32_t getU32(std::span<const uint8_t> d, size_t at) { return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24; }
uint16_t getU16(std::span<const uint8_t> d, size_t at) { return uint16_t(d[at] | d[at + 1] << 8); }

} // namespace

Format sniff(std::span<const uint8_t> data) {
    if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return Format::Jpeg;
    static constexpr uint8_t kPng[8]{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (data.size() >= 8 && std::memcmp(data.data(), kPng, 8) == 0) return Format::Png;
    return Format::Unknown;
}

uint32_t sourceHash(std::span<const uint8_t> data) {
    uint32_t hash = 2166136261u;
    for (const uint8_t byte : data) { hash ^= byte; hash *= 16777619u; }
    return hash;
}

std::expected<Thumbnail, std::string> decodeThumbnail(std::span<const uint8_t> data, DecodeInfo* info) {
    if (data.empty() || data.size() > kMaxSourceBytes) return std::unexpected(std::string{"cover size out of range"});
    const Format format = sniff(data);
    if (info) info->format = format;
    if (format == Format::Jpeg) return decodeJpeg(data, info);
    if (format == Format::Png) return decodePng(data, info);
    return std::unexpected(std::string{"unsupported cover format"});
}

std::vector<uint8_t> encodeRcov(const Thumbnail& thumb, uint32_t hash, uint32_t sourceBytes) {
    std::vector<uint8_t> out;
    out.reserve(kRcovHeaderBytes + thumb.pixels.size() * 2);
    putU32(out, kRcovMagic);
    putU16(out, uint16_t(thumb.width));
    putU16(out, uint16_t(thumb.height));
    putU32(out, hash);
    putU32(out, sourceBytes);
    for (const uint16_t px : thumb.pixels) putU16(out, px);
    return out;
}

std::expected<Thumbnail, std::string> decodeRcov(std::span<const uint8_t> data, uint32_t* hash) {
    if (data.size() < kRcovHeaderBytes || getU32(data, 0) != kRcovMagic) return std::unexpected(std::string{"not an rcov file"});
    Thumbnail thumb;
    thumb.width = getU16(data, 4);
    thumb.height = getU16(data, 6);
    if (thumb.width != kThumbWidth || thumb.height != kThumbHeight) return std::unexpected(std::string{"rcov size mismatch"});
    if (data.size() != kRcovHeaderBytes + kThumbPixels * 2) return std::unexpected(std::string{"rcov truncated"});
    if (hash) *hash = getU32(data, 8);
    thumb.pixels.resize(kThumbPixels);
    for (size_t i = 0; i < kThumbPixels; ++i) thumb.pixels[i] = getU16(data, kRcovHeaderBytes + i * 2);
    return thumb;
}

} // namespace covers
