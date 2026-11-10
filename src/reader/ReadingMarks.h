#pragma once

// Lume.5 reader bookmarks: pure logic (no Arduino/FS), covered by native tests.
//
// "Onde parei" (resume point) rule:
//  * Before ANY position jump (chapters, find place, slider, search, bookmark, companion seek) the
//    real current position is saved as the resume point, unless a previous resume point is still
//    "pending" (the user jumped and has not yet read from the new place). So A -> B -> C without
//    reading in between keeps A: the resume point is the latest position that was really read.
//  * The new place settles after kSettleWords words read continuously (forward, small steps) OR
//    kSettleMs of play time. Settling clears the resume point: the live position is the new
//    "onde parei" (like Kindle "most recent read"); the next jump saves it again.
//  * Jumping back exactly to the resume word clears it (the user returned).
//  * Rewinding (hold-left) is NOT a jump and never touches the resume point.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reader/ReadingState.h"

namespace reading {

    inline constexpr uint32_t kSettleWords = 400;
    inline constexpr uint32_t kSettleMs = 120000;
    inline constexpr size_t kMaxBookmarks = 20;
    inline constexpr size_t kSnippetBytes = 28;

    // Runtime-only counters (not persisted).
    struct ResumeTracker {
        uint32_t lastWord = 0;
        uint32_t wordsRead = 0;
        uint32_t msRead = 0;
    };

    uint8_t percentOf(uint32_t wordIndex, uint32_t wordCount);

    // chapterIndexPlusOne: 0 when the book has no chapter at `from`. Returns true when `resume` changed.
    bool noteJump(ResumePoint& resume, ResumeTracker& tracker, uint32_t from, uint32_t to,
                  uint32_t chapterIndexPlusOne, uint32_t wordCount);
    // Call every frame. `dtMs` since the previous call (only counted while playing). True when it settled.
    bool tick(ResumePoint& resume, ResumeTracker& tracker, uint32_t word, uint32_t dtMs, bool playing);
    void resetTracker(ResumeTracker& tracker, uint32_t word);

    struct Bookmark {
        uint32_t wordIndex = 0;
        uint8_t percent = 0;
        std::string snippet;
        bool operator==(const Bookmark&) const = default;
    };
    enum class AddResult : uint8_t { Added, AlreadyMarked, Full };

    // UTF-8 safe truncation with "..." when cut; control characters become spaces.
    std::string makeSnippet(std::string_view text, size_t maxBytes = kSnippetBytes);
    AddResult addBookmark(std::vector<Bookmark>& list, uint32_t wordIndex, uint32_t wordCount, std::string snippet);
    bool removeBookmark(std::vector<Bookmark>& list, size_t index);
    bool removeBookmarkAt(std::vector<Bookmark>& list, uint32_t wordIndex);
    bool hasBookmark(const std::vector<Bookmark>& list, uint32_t wordIndex);

    std::string serializeBookmarks(const BookIdentity& identity, const std::vector<Bookmark>& list);
    // nullopt when malformed or when the identity does not match the book.
    std::optional<std::vector<Bookmark>> parseBookmarks(std::string_view text, const BookIdentity& identity);

    // Rewind (hold-left). Pure step math: how many words to go back for one tick.
    // `chunkWords` is 1 outside chunk mode; result never goes below 0.
    size_t backwardTarget(size_t wordIndex, size_t chunkWords);
    // Speed multiplier in percent for holdMs of continuous rewinding (100, 200 after 2 s, 300 after 4 s).
    uint32_t backwardSpeedPercent(uint32_t holdMs);

    // Horizontal scrub (drag to walk the words). Left-to-right advances, right-to-left goes back.
    // Movement under kScrubSlopPx is a still hold, not a scrub. Beyond it: 1 word per kScrubPxPerWord up to
    // kScrubLinearPx, then 1 extra word every 3 px (distance-based acceleration). Scrub/rewind never
    // change the "onde parei" (they are not jumps).
    inline constexpr int kScrubSlopPx = 8;
    inline constexpr int kScrubPxPerWord = 12;
    inline constexpr int kScrubLinearPx = 96;
    bool isScrubMove(int dx, int dy);              // horizontal and beyond the slop
    int scrubSteps(int dxPixels);                  // signed word steps for a displacement
    size_t scrubTarget(size_t startWord, int dxPixels, size_t wordCount); // clamped to [0, wordCount-1]

} // namespace reading
