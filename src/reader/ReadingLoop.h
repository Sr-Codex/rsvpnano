#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reader/ReadingSession.h"
#include "settings/SettingsModel.h"

namespace ReadingLoop {

    struct TextParagraph {
        size_t firstWord = 0;
        size_t lastWord = 0;
        std::string text;
        std::vector<size_t> wordOffsets;
    };

    void begin(ReadingSession& session, uint32_t nowMs);
    void start(ReadingSession& session, uint32_t nowMs);
    void pause(ReadingSession& session);
    bool update(ReadingSession& session, const settings::ReadingSettings& settings, uint32_t nowMs,
                bool allowCatchUp = true, bool stopAtSentenceEnd = false);
    void setWords(ReadingSession& session, std::span<const std::string> words, uint32_t nowMs);
    void setBookStore(ReadingSession& session, const IndexedBookStore& store, uint32_t nowMs);
    void seekTo(ReadingSession& session, size_t wordIndex);
    // Position jump that records "onde parei" first (see reader/ReadingMarks.h). Use for every jump.
    void jumpTo(ReadingSession& session, size_t wordIndex);
    // Hold-to-read rewind: steps backwards at the current WPM times speedPercent/100. No sentence-end stop.
    // Returns true when the word changed. Stops at word 0.
    bool updateBackward(ReadingSession& session, const settings::ReadingSettings& settings, uint32_t nowMs,
                        uint32_t speedPercent);
    void seekRelative(ReadingSession& session, size_t baseIndex, int steps);
    bool seekParagraph(ReadingSession& session, int steps);
    // "Voltar uma frase": to the start of the current sentence; when already there (or at most `marginWords` into
    // it), to the start of the sentence before. A paragraph end also closes a sentence. Not a jump: it never
    // touches the "onde parei". While the words are running the reader reacts late, so the reading screen passes
    // kRewindMarginWhilePlaying: a swipe a few words into a new sentence replays the one that was just lost.
    inline constexpr size_t kRewindMarginWhilePlaying = 5;
    void rewindSentence(ReadingSession& session, size_t marginWords = 0);
    // Sentence scrub (drag with the reading paused): the start of the sentence `steps` sentences away from `from`.
    // Back: the first step from the middle of a sentence is its own start, as in rewindSentence. Forward: the start
    // of the next sentence. Stops at the first and last sentences; each step looks at most 400 words away.
    size_t sentenceStartAfterSteps(const ReadingSession& session, size_t from, int steps);
    void adjustWpm(settings::ReadingSettings& settings, int delta);

    std::string_view wordAt(const ReadingSession& session, size_t index);
    size_t wordCount(const ReadingSession& session);
    size_t displayWordCount(const ReadingSession& session);
    uint32_t wordDurationMs(const ReadingSession& session, const settings::ReadingSettings& settings, size_t index);
    std::pair<size_t, size_t> paragraphBoundsAt(const ReadingSession& session, size_t wordIndex);
    TextParagraph paragraphAt(const ReadingSession& session, size_t wordIndex);
    settings::ReadingPacing pacingMode(const ReadingSession& session);
    uint32_t currentWordDurationMs(const ReadingSession& session, const settings::ReadingSettings& settings);
    uint32_t elapsedInCurrentWordMs(const ReadingSession& session, uint32_t nowMs);
    bool currentWordEndsSentence(const ReadingSession& session);
    bool atEnd(const ReadingSession& session);

} // namespace ReadingLoop
