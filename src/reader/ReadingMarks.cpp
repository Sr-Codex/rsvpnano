#include "reader/ReadingMarks.h"

#include <algorithm>
#include <charconv>

namespace reading {

    uint8_t percentOf(uint32_t wordIndex, uint32_t wordCount) {
        if (wordCount <= 1) return 0;
        return static_cast<uint8_t>(std::min<uint64_t>((uint64_t{wordIndex} * 100) / (wordCount - 1), 100));
    }

    void resetTracker(ResumeTracker& tracker, uint32_t word) {
        tracker = {};
        tracker.lastWord = word;
    }

    bool noteJump(ResumePoint& resume, ResumeTracker& tracker, uint32_t from, uint32_t to,
                  uint32_t chapterIndexPlusOne, uint32_t wordCount) {
        if (from == to) return false;
        bool changed = false;
        if (resume.valid && to == resume.wordIndex) {
            resume = {}; // returned to the saved place
            changed = true;
        } else if (!(resume.valid && resume.pending)) {
            resume = {from, chapterIndexPlusOne, percentOf(from, wordCount), true, true};
            changed = true;
        }
        resetTracker(tracker, to);
        return changed;
    }

    bool tick(ResumePoint& resume, ResumeTracker& tracker, uint32_t word, uint32_t dtMs, bool playing) {
        const int64_t delta = int64_t{word} - int64_t{tracker.lastWord};
        tracker.lastWord = word;
        if (!resume.valid || !resume.pending) return false;
        if (delta > 0 && delta <= 16) tracker.wordsRead += static_cast<uint32_t>(delta);
        if (playing) tracker.msRead += std::min<uint32_t>(dtMs, 1000);
        if (tracker.wordsRead >= kSettleWords || tracker.msRead >= kSettleMs) {
            resume = {};
            tracker.wordsRead = tracker.msRead = 0;
            return true;
        }
        return false;
    }

    std::string makeSnippet(std::string_view text, size_t maxBytes) {
        std::string clean;
        bool space = true;
        for (const char c : text) {
            const bool blank = static_cast<unsigned char>(c) < 0x20 || c == ' ';
            if (blank) {
                if (!space) clean += ' ';
                space = true;
            } else {
                clean += c;
                space = false;
            }
        }
        while (!clean.empty() && clean.back() == ' ') clean.pop_back();
        if (clean.size() <= maxBytes) return clean;
        size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(clean[cut]) & 0xC0) == 0x80) --cut;
        clean.resize(cut);
        while (!clean.empty() && clean.back() == ' ') clean.pop_back();
        return clean + "...";
    }

    bool hasBookmark(const std::vector<Bookmark>& list, uint32_t wordIndex) {
        return std::ranges::any_of(list, [&](const Bookmark& b) { return b.wordIndex == wordIndex; });
    }

    AddResult addBookmark(std::vector<Bookmark>& list, uint32_t wordIndex, uint32_t wordCount, std::string snippet) {
        if (hasBookmark(list, wordIndex)) return AddResult::AlreadyMarked;
        if (list.size() >= kMaxBookmarks) return AddResult::Full;
        Bookmark mark{wordIndex, percentOf(wordIndex, wordCount), std::move(snippet)};
        const auto at = std::ranges::upper_bound(list, wordIndex, {}, &Bookmark::wordIndex);
        list.insert(at, std::move(mark));
        return AddResult::Added;
    }

    bool removeBookmark(std::vector<Bookmark>& list, size_t index) {
        if (index >= list.size()) return false;
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    bool removeBookmarkAt(std::vector<Bookmark>& list, uint32_t wordIndex) {
        const auto it = std::ranges::find(list, wordIndex, &Bookmark::wordIndex);
        if (it == list.end()) return false;
        list.erase(it);
        return true;
    }

    std::string serializeBookmarks(const BookIdentity& identity, const std::vector<Bookmark>& list) {
        std::string out = "RMARKS1 " + std::to_string(identity.sourceSize) + ' '
                        + std::to_string(identity.sourceFingerprint) + ' ' + std::to_string(identity.wordCount) + '\n';
        for (const Bookmark& b : list) {
            std::string snippet = b.snippet;
            std::ranges::replace_if(snippet, [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
            out += std::to_string(b.wordIndex) + '\t' + std::to_string(b.percent) + '\t' + snippet + '\n';
        }
        return out;
    }

    namespace {
        bool number(std::string_view text, uint32_t& value) {
            const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
            return r.ec == std::errc{} && r.ptr == text.data() + text.size();
        }
    }

    std::optional<std::vector<Bookmark>> parseBookmarks(std::string_view text, const BookIdentity& identity) {
        const size_t headEnd = text.find('\n');
        if (headEnd == std::string_view::npos) return std::nullopt;
        const std::string expected = serializeBookmarks(identity, {});
        if (text.substr(0, headEnd + 1) != expected) return std::nullopt;
        std::vector<Bookmark> list;
        text.remove_prefix(headEnd + 1);
        while (!text.empty() && list.size() < kMaxBookmarks) {
            const size_t end = text.find('\n');
            std::string_view line = text.substr(0, end);
            text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
            const size_t t1 = line.find('\t');
            const size_t t2 = t1 == std::string_view::npos ? t1 : line.find('\t', t1 + 1);
            if (t2 == std::string_view::npos) continue;
            Bookmark b;
            uint32_t pct = 0;
            if (!number(line.substr(0, t1), b.wordIndex) || !number(line.substr(t1 + 1, t2 - t1 - 1), pct)) continue;
            if (b.wordIndex >= identity.wordCount) continue;
            b.percent = static_cast<uint8_t>(std::min<uint32_t>(pct, 100));
            b.snippet = std::string{line.substr(t2 + 1)};
            if (!hasBookmark(list, b.wordIndex)) list.push_back(std::move(b));
        }
        std::ranges::sort(list, {}, &Bookmark::wordIndex);
        return list;
    }

    size_t backwardTarget(size_t wordIndex, size_t chunkWords) {
        const size_t step = std::max<size_t>(1, chunkWords);
        return wordIndex > step ? wordIndex - step : 0;
    }

    uint32_t backwardSpeedPercent(uint32_t holdMs) {
        return holdMs >= 4000 ? 300 : holdMs >= 2000 ? 200 : 100;
    }

    bool isScrubMove(int dx, int dy) {
        const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        return ax > kScrubSlopPx && ax > ay;
    }

    int scrubSentenceSteps(int dxPixels) {
        const int ax = dxPixels < 0 ? -dxPixels : dxPixels;
        const int steps = (ax + kScrubPxPerSentence / 2) / kScrubPxPerSentence;
        return dxPixels < 0 ? -steps : steps;
    }

    bool isRewindSwipe(int minDx, int maxAbsDy, uint32_t durationMs) {
        return minDx <= -kRewindSwipeMinPx && -minDx > maxAbsDy && durationMs <= kRewindSwipeMaxMs;
    }

    bool positionSaveDue(bool force, bool playing, bool atSentenceEnd, bool changed, uint32_t sinceLastSaveMs) {
        if (!changed) return false;
        if (force) return true;
        if (!playing || sinceLastSaveMs < kPositionSaveIntervalMs) return false;
        return atSentenceEnd || sinceLastSaveMs >= kPositionSaveMaxIntervalMs;
    }

} // namespace reading
