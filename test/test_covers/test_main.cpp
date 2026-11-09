#include <unity.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "library/CoverImage.h"
#include "library/CoverStore.h"
#include "library/LibraryOrder.h"
#include "conversion/epub/EpubPackage.h"

#include <map>

void setUp() {}
void tearDown() {}

namespace {
const std::filesystem::path kDir = "test/fixtures/covers";

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(in.good(), path.c_str());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Mean absolute difference per 8-bit channel between two RGB565 thumbnails.
double meanDiff(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b) {
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const int ar = (a[i] >> 11) * 255 / 31, ag = ((a[i] >> 5) & 63) * 255 / 63, ab = (a[i] & 31) * 255 / 31;
        const int br = (b[i] >> 11) * 255 / 31, bg = ((b[i] >> 5) & 63) * 255 / 63, bb = (b[i] & 31) * 255 / 31;
        sum += std::abs(ar - br) + std::abs(ag - bg) + std::abs(ab - bb);
    }
    return sum / (3.0 * double(a.size()));
}

covers::Thumbnail decodeFixture(const char* name, covers::DecodeInfo* info = nullptr) {
    const auto bytes = readFile(kDir / name);
    auto thumb = covers::decodeThumbnail(bytes, info);
    TEST_ASSERT_TRUE_MESSAGE(thumb.has_value(), thumb ? name : thumb.error().c_str());
    TEST_ASSERT_EQUAL(covers::kThumbWidth, thumb->width);
    TEST_ASSERT_EQUAL(covers::kThumbHeight, thumb->height);
    TEST_ASSERT_EQUAL(covers::kThumbPixels, thumb->pixels.size());
    return *thumb;
}

void expectCloseToReference(const char* image, const char* reference, double tolerance) {
    covers::DecodeInfo info;
    const auto thumb = decodeFixture(image, &info);
    const auto ref = covers::decodeRcov(readFile(kDir / reference));
    TEST_ASSERT_TRUE(ref.has_value());
    const double diff = meanDiff(thumb.pixels, ref->pixels);
    std::printf("  %-24s %4dx%-4d scale 1/%d%s -> mean diff %.2f (tol %.1f)\n", image, info.sourceWidth,
                info.sourceHeight, info.scale, info.progressive ? " progressive" : "", diff, tolerance);
    TEST_ASSERT_TRUE_MESSAGE(diff <= tolerance, image);
}
} // namespace

void test_sniff_formats() {
    TEST_ASSERT_EQUAL(int(covers::Format::Jpeg), int(covers::sniff(readFile(kDir / "dracula-baseline.jpg"))));
    TEST_ASSERT_EQUAL(int(covers::Format::Png), int(covers::sniff(readFile(kDir / "dracula.png"))));
    const std::vector<uint8_t> junk{'h', 'e', 'l', 'l', 'o'};
    TEST_ASSERT_EQUAL(int(covers::Format::Unknown), int(covers::sniff(junk)));
    TEST_ASSERT_FALSE(covers::decodeThumbnail(junk).has_value());
    TEST_ASSERT_FALSE(covers::decodeThumbnail({}).has_value());
}

// Independent reference: Pillow BOX filter over the same centred crop (tools/make_cover_fixtures.py).
void test_baseline_jpeg_matches_reference() { expectCloseToReference("dracula-baseline.jpg", "dracula-baseline.ref.rcov", 6.0); }
void test_png_matches_reference() { expectCloseToReference("dracula.png", "dracula.ref.rcov", 4.0); }
void test_png_variants_match_reference() { // sanity vs Pillow BOX filter (different resampler)
    for (const char* n : {"dracula-palette", "dracula-pal4bit", "dracula-gray", "dracula-rgba", "dracula-16bit"})
        expectCloseToReference((std::string{n} + ".png").c_str(), (std::string{n} + ".ref.rcov").c_str(), 6.0);
}
// PNG is lossless: an independent Python port of the exact algorithm must match BIT FOR BIT.
void test_png_variants_are_bit_exact() {
    for (const char* n : {"dracula", "dracula-palette", "dracula-pal4bit", "dracula-gray", "dracula-rgba", "dracula-16bit"}) {
        const auto thumb = decodeFixture((std::string{n} + ".png").c_str());
        const auto exact = covers::decodeRcov(readFile(kDir / (std::string{n} + ".exact.rcov")));
        TEST_ASSERT_TRUE(exact.has_value());
        size_t differing = 0;
        for (size_t i = 0; i < thumb.pixels.size(); ++i) differing += thumb.pixels[i] != exact->pixels[i];
        std::printf("  %-22s bit-exact pixels differing: %zu\n", n, differing);
        TEST_ASSERT_EQUAL_MESSAGE(0, differing, n);
    }
}
void test_interlaced_png_is_rejected_cleanly() {
    const auto r = covers::decodeThumbnail(readFile(kDir / "dracula-interlaced.png"));
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL_STRING("interlaced png not supported", r.error().c_str());
}
void test_truncated_inputs_fail_without_crash() {
    for (const char* name : {"dracula-baseline.jpg", "dracula.png"}) {
        auto bytes = readFile(kDir / name);
        for (const size_t keep : {size_t(10), bytes.size() / 2, bytes.size() - 20}) {
            std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + long(keep));
            (void)covers::decodeThumbnail(cut); // must return (ok or error), never crash
        }
    }
}
void test_landscape_cover_is_cropped_centred() { expectCloseToReference("dracula-wide.jpg", "dracula-wide.ref.rcov", 6.0); }
// Progressive JPEG: JPEGDEC decodes only the DC scan (1/8) -> blurrier but must still work.
void test_progressive_jpeg_degrades_gracefully() { expectCloseToReference("dracula-progressive.jpg", "dracula-progressive.ref.rcov", 20.0); }
// Smaller than the thumbnail: gaps filled from neighbours, never an error.
void test_tiny_cover_is_upscaled() {
    expectCloseToReference("dracula-tiny.png", "dracula-tiny.ref.rcov", 12.0);
    expectCloseToReference("dracula-tiny.jpg", "dracula-tiny.ref.rcov", 16.0);
}

void test_rcov_round_trip_and_validation() {
    const auto thumb = decodeFixture("dracula-baseline.jpg");
    const uint32_t hash = covers::sourceHash(readFile(kDir / "dracula-baseline.jpg"));
    const auto bytes = covers::encodeRcov(thumb, hash);
    TEST_ASSERT_EQUAL(covers::kRcovHeaderBytes + covers::kThumbPixels * 2, bytes.size());
    uint32_t back = 0;
    const auto decoded = covers::decodeRcov(bytes, &back);
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_HEX32(hash, back);
    TEST_ASSERT_TRUE(decoded->pixels == thumb.pixels);
    auto truncated = bytes; truncated.pop_back();
    TEST_ASSERT_FALSE(covers::decodeRcov(truncated).has_value());
    auto badMagic = bytes; badMagic[0] ^= 0xFF;
    TEST_ASSERT_FALSE(covers::decodeRcov(badMagic).has_value());
    // Hash changes when the source changes (cache invalidation).
    auto other = readFile(kDir / "dracula-baseline.jpg"); other.back() ^= 1;
    TEST_ASSERT_NOT_EQUAL(hash, covers::sourceHash(other));
}

// Many books: decode cost per cover and cache size scale linearly (numbers printed for the record).
void test_scale_many_books() {
    const auto bytes = readFile(kDir / "dracula-baseline.jpg");
    constexpr int kBooks = 300;
    const auto start = std::chrono::steady_clock::now();
    size_t cacheBytes = 0;
    for (int i = 0; i < kBooks; ++i) {
        const auto thumb = covers::decodeThumbnail(bytes);
        TEST_ASSERT_TRUE(thumb.has_value());
        cacheBytes += covers::encodeRcov(*thumb, uint32_t(i)).size();
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("  %d covers: %.1f ms total on host (%.2f ms/cover), cache %.1f MB on SD, %zu bytes RAM per page of 4\n",
                kBooks, ms, ms / kBooks, cacheBytes / 1048576.0, covers::kThumbPixels * 2 * 4);
    TEST_ASSERT_EQUAL(size_t(kBooks) * (covers::kRcovHeaderBytes + covers::kThumbPixels * 2), cacheBytes);
}

namespace {
struct MemoryCard {
    std::map<std::string, std::vector<uint8_t>> files;
    covers::Io io() {
        return {
            [this](const std::string& p, size_t max) -> std::optional<std::vector<uint8_t>> {
                auto it = files.find(p); if (it == files.end()) return std::nullopt;
                return std::vector<uint8_t>(it->second.begin(), it->second.begin() + long(std::min(max, it->second.size())));
            },
            [this](const std::string& p, std::span<const uint8_t> b) { files[p].assign(b.begin(), b.end()); return true; },
            [this](const std::string& p) { return files.erase(p) > 0; },
            [this](const std::string& p) -> std::optional<size_t> {
                auto it = files.find(p); if (it == files.end()) return std::nullopt; return it->second.size();
            }};
    }
};
} // namespace

void test_cover_paths() {
    TEST_ASSERT_EQUAL_STRING("/library/books/Livro.rcov", covers::cachePath("/library/books/Livro.rsvp").c_str());
    TEST_ASSERT_EQUAL_STRING("/library/books/A.b c", covers::stem("/library/books/A.b c.rsvp").c_str());
    TEST_ASSERT_EQUAL_STRING("/library/books/semext", covers::stem("/library/books/semext").c_str());
    TEST_ASSERT_TRUE(covers::isCoverFileName("Livro.cover.JPG"));
    TEST_ASSERT_TRUE(covers::isCoverFileName("Livro.rcov"));
    TEST_ASSERT_FALSE(covers::isCoverFileName("Livro.rsvp"));
}

void test_cover_store_lifecycle() {
    MemoryCard card;
    covers::CoverStore store(card.io(), 2);
    const std::string book = "/library/books/Livro.rsvp";
    TEST_ASSERT_FALSE(store.needsThumbnail(book));          // no cover at all
    TEST_ASSERT_NULL(store.thumbnail(book).get());
    card.files["/library/books/Livro.cover.jpg"] = readFile(kDir / "dracula-baseline.jpg");
    TEST_ASSERT_TRUE(store.needsThumbnail(book));           // source, no cache
    TEST_ASSERT_NULL(store.thumbnail(book).get());          // never decodes on the UI path
    TEST_ASSERT_TRUE(store.generate(book).has_value());
    TEST_ASSERT_FALSE(store.needsThumbnail(book));
    const auto first = store.thumbnail(book);
    TEST_ASSERT_NOT_NULL(first.get());
    TEST_ASSERT_EQUAL_PTR(first.get(), store.thumbnail(book).get()); // memory hit
    // Replace with a PNG: old source removed, cache invalidated, rebuilt from the new one.
    TEST_ASSERT_TRUE(store.replaceSource(book, covers::Format::Png, readFile(kDir / "dracula.png")).has_value());
    TEST_ASSERT_FALSE(card.files.contains("/library/books/Livro.cover.jpg"));
    TEST_ASSERT_FALSE(card.files.contains("/library/books/Livro.rcov"));
    TEST_ASSERT_TRUE(store.needsThumbnail(book));
    TEST_ASSERT_TRUE(store.generate(book).has_value());
    TEST_ASSERT_NOT_NULL(store.thumbnail(book).get());
    // Hand-replaced file with a different size on the card -> stale cache detected.
    card.files["/library/books/Livro.cover.png"] = readFile(kDir / "dracula-gray.png");
    store.forget(book);
    TEST_ASSERT_TRUE(store.needsThumbnail(book));
    TEST_ASSERT_NULL(store.thumbnail(book).get());
    // Wrong format bytes are refused.
    TEST_ASSERT_FALSE(store.replaceSource(book, covers::Format::Jpeg, readFile(kDir / "dracula.png")).has_value());
    // Corrupt cache is ignored, not trusted.
    TEST_ASSERT_TRUE(store.generate(book).has_value());
    card.files["/library/books/Livro.rcov"].resize(100);
    store.forget(book);
    TEST_ASSERT_TRUE(store.needsThumbnail(book) || store.thumbnail(book) == nullptr);
    // Book removed -> everything removed.
    store.removeFor(book);
    TEST_ASSERT_TRUE(card.files.empty());
}

void test_cover_store_memory_is_bounded() {
    MemoryCard card;
    covers::CoverStore store(card.io(), 2);
    const auto jpg = readFile(kDir / "dracula-baseline.jpg");
    for (const char* b : {"/l/a.rsvp", "/l/b.rsvp", "/l/c.rsvp"}) {
        card.files[covers::stem(b) + ".cover.jpg"] = jpg;
        TEST_ASSERT_TRUE(store.generate(b).has_value());
    }
    const auto a = store.thumbnail("/l/a.rsvp");
    (void)store.thumbnail("/l/b.rsvp");
    (void)store.thumbnail("/l/c.rsvp");                     // evicts a (capacity 2)
    TEST_ASSERT_NOT_EQUAL(a.get(), store.thumbnail("/l/a.rsvp").get()); // reloaded from card
}

void test_library_order() {
    using namespace library_order;
    TEST_ASSERT_EQUAL(2, parse("/a\r\n\n/b\n/a\n").size());
    bool changed = false;
    // First run: no file -> order = card order.
    auto order = reconcile({}, {"/b", "/a"}, &changed);
    TEST_ASSERT_TRUE(changed);
    // New book appended, removed book dropped, known order kept.
    order = reconcile(order, {"/a", "/c", "/b"}, &changed);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL_STRING("/b", order[0].c_str());
    TEST_ASSERT_EQUAL_STRING("/a", order[1].c_str());
    TEST_ASSERT_EQUAL_STRING("/c", order[2].c_str());
    (void)reconcile(order, {"/a", "/c", "/b"}, &changed);
    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_EQUAL_STRING(serialize(order).c_str(), serialize(parse(serialize(order))).c_str());
    // Shelf: reading book first, then newest first.
    const std::vector<std::string> current{"/a", "/c", "/b"};
    auto shelfIdx = shelf(order, current, "/a");
    TEST_ASSERT_EQUAL(3, shelfIdx.size());
    TEST_ASSERT_EQUAL_STRING("/a", current[shelfIdx[0]].c_str()); // reading
    TEST_ASSERT_EQUAL_STRING("/c", current[shelfIdx[1]].c_str()); // newest
    TEST_ASSERT_EQUAL_STRING("/b", current[shelfIdx[2]].c_str()); // oldest
    shelfIdx = shelf(order, current, "");
    TEST_ASSERT_EQUAL_STRING("/c", current[shelfIdx[0]].c_str());
    shelfIdx = shelf(order, current, "/gone");
    TEST_ASSERT_EQUAL(3, shelfIdx.size());
}

void test_epub_cover_detection() {
    using EpubPackage::ManifestItem;
    const std::vector<ManifestItem> items{
        {"c1", "OEBPS/images/front.jpg", "image/jpeg", ""},
        {"img2", "OEBPS/images/map.png", "image/png", ""},
        {"ch1", "OEBPS/ch1.xhtml", "application/xhtml+xml", ""},
        {"cov", "OEBPS/images/art.png", "image/png", "cover-image"},
    };
    // EPUB3 wins.
    TEST_ASSERT_EQUAL_STRING("OEBPS/images/art.png", EpubPackage::findCoverImagePath("<package version='3.0'/>", items).c_str());
    // EPUB2 meta (any attribute order / quotes).
    const std::vector<ManifestItem> epub2{items[0], items[1], items[2]};
    TEST_ASSERT_EQUAL_STRING("OEBPS/images/front.jpg",
        EpubPackage::findCoverImagePath("<metadata><meta content='c1' name=\"cover\"/></metadata>", epub2).c_str());
    TEST_ASSERT_EQUAL_STRING("OEBPS/images/map.png",
        EpubPackage::findCoverImagePath("<meta name=\"cover\" content=\"img2\" />", epub2).c_str());
    // meta pointing to a non-image is ignored; no "cover" hint anywhere -> none.
    TEST_ASSERT_EQUAL_STRING("", EpubPackage::findCoverImagePath("<meta name=\"cover\" content=\"ch1\"/>",
        std::vector<ManifestItem>{items[1], items[2]}).c_str());
    // Fallback by name.
    const std::vector<ManifestItem> named{{"x", "OEBPS/Cover.JPG", "image/jpeg", ""}, items[2]};
    TEST_ASSERT_EQUAL_STRING("OEBPS/Cover.JPG", EpubPackage::findCoverImagePath("<package/>", named).c_str());
    // Name a property that merely contains the word must not match ("cover-image-alt").
    const std::vector<ManifestItem> tricky{{"a", "OEBPS/a.png", "image/png", "cover-image-alt"}};
    TEST_ASSERT_EQUAL_STRING("", EpubPackage::findCoverImagePath("<package/>", tricky).c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sniff_formats);
    RUN_TEST(test_baseline_jpeg_matches_reference);
    RUN_TEST(test_png_matches_reference);
    RUN_TEST(test_png_variants_match_reference);
    RUN_TEST(test_png_variants_are_bit_exact);
    RUN_TEST(test_interlaced_png_is_rejected_cleanly);
    RUN_TEST(test_truncated_inputs_fail_without_crash);
    RUN_TEST(test_landscape_cover_is_cropped_centred);
    RUN_TEST(test_progressive_jpeg_degrades_gracefully);
    RUN_TEST(test_tiny_cover_is_upscaled);
    RUN_TEST(test_rcov_round_trip_and_validation);
    RUN_TEST(test_scale_many_books);
    RUN_TEST(test_cover_paths);
    RUN_TEST(test_cover_store_lifecycle);
    RUN_TEST(test_cover_store_memory_is_bounded);
    RUN_TEST(test_library_order);
    RUN_TEST(test_epub_cover_detection);
    return UNITY_END();
}
