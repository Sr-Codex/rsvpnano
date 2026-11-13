#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "library/BookMetadata.h"
#include "reader/ReadingMarks.h"
#include "reader/ReadingState.h"
#include "library/IndexedBookStore.h"

struct ReadingSession {
    BookMetadata metadata;
    size_t lastSavedWordIndex = static_cast<size_t>(-1);
    size_t lastMirroredWordIndex = static_cast<size_t>(-1); // word already on the card (.rstate.toml)
    uint32_t lastMirrorMs = 0;                              // last card write attempt (ReadingProgress::mirror)
    uint32_t lastAdvanceMs = 0;
    uint32_t lastSaveMs = 0;
    std::string currentWord;
    std::span<const std::string> words;
    const IndexedBookStore* bookStore = nullptr;
    bool playing = false;
    uint8_t displayWordCount = 1;
    size_t displayWordIndex = static_cast<size_t>(-1);
    reading::State state;
    reading::ResumeTracker resumeTracker;
    std::vector<reading::Bookmark> bookmarks; // persisted in the .rmarks sidecar
    bool resumeDirty = false;                 // sidecar needs a mirror()
    bool bookmarksDirty = false;              // .rmarks needs a write

    std::string_view localeAt(size_t index) const {
        return state.overrides.locale && !state.overrides.locale->empty()
                   ? std::string_view{*state.overrides.locale} : metadata.localeAt(index);
    }

    bool stored() const {
        return bookStore != nullptr && bookStore->isOpen();
    }
    std::string_view sourcePath() const {
        return stored() ? bookStore->sourcePath() : std::string_view{};
    }
};
