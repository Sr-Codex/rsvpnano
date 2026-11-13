#include "library/ReadingProgress.h"
#include <esp_log.h>
#include "logging/Logger.h"

#include <FS.h>
#include <glaze/toml.hpp>

#include <algorithm>

#include "board/BoardStorage.h"
#include "reader/ReadingLoop.h"
#include "settings/SettingsGlaze.h"
#include "library/StorageManager.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace ReadingProgress {
    using reading::BookIdentity;

    namespace {

        constexpr size_t kMaxBookStateBytes = 2048;
        constexpr uint32_t kSaveIntervalMs = 15000;
        constexpr uint32_t kWordSettledMs = 40; // the current word has been on screen for at least one frame

        bool isValidIdentity(const BookIdentity& identity) {
            return identity.sourceSize > 0 && identity.wordCount > 0;
        }

        bool matches(const reading::StoredState& state, const BookIdentity& identity) {
            return state.identity == identity;
        }

        std::expected<reading::StoredState, std::error_code> readStateFile(const std::string& statePath,
                                                                           const BookIdentity& identity) {
            File file = Board::Storage::filesystem().open(statePath.c_str(), FILE_READ);
            if (!file || file.isDirectory()) {
                if (file)
                    file.close();
                return std::unexpected(std::make_error_code(file ? std::errc::invalid_argument
                                                                 : std::errc::no_such_file_or_directory));
            }
            const size_t size = file.size();
            if (size == 0 || size > kMaxBookStateBytes) {
                file.close();
                return std::unexpected(std::make_error_code(size > kMaxBookStateBytes ? std::errc::value_too_large
                                                                                      : std::errc::invalid_argument));
            }
            std::string input(size, '\0');
            const size_t read = file.read(reinterpret_cast<uint8_t*>(input.data()), input.size());
            file.close();
            if (read != input.size())
                return std::unexpected(std::make_error_code(std::errc::io_error));

            reading::StoredState candidate;
            if (const glz::error_ctx error =
                    glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = false}>(candidate, input)) {
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            }
            if (!matches(candidate, identity))
                return std::unexpected(std::make_error_code(std::errc::state_not_recoverable));
            candidate.reading.wordIndex = std::min(candidate.reading.wordIndex, identity.wordCount - 1);
            return candidate;
        }

        // The sidecar is always replaced through ".tmp" + ".bak" (StorageFiles::writeFileAtomic). When power is cut
        // in the middle, the main file is missing or unreadable: the complete new copy is still in ".tmp" if the cut
        // came between the two renames, and the previous good copy is in ".bak".
        std::expected<reading::StoredState, std::error_code> loadBookState(std::string_view bookPath,
                                                                           const BookIdentity& identity) {
            if (bookPath.empty() || !isValidIdentity(identity))
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));

            const std::string statePath = StoragePaths::bookStatePathFor(bookPath);
            auto state = readStateFile(statePath, identity);
            const bool interrupted = !state
                                  && (state.error() == std::errc::no_such_file_or_directory
                                      || state.error() == std::errc::invalid_argument
                                      || state.error() == std::errc::io_error);
            if (!interrupted)
                return state;
            for (const char* suffix: {".tmp", ".bak"}) {
                const std::string copyPath = statePath + suffix;
                if (!Board::Storage::filesystem().exists(copyPath.c_str()))
                    continue;
                if (auto copy = readStateFile(copyPath, identity)) {
                    ESP_LOGW("storage-progress", "recovered position word=%u from %s",
                             static_cast<unsigned int>(copy->reading.wordIndex), copyPath.c_str());
                    return copy;
                }
            }
            return state;
        }

        bool canReplaceBookState(std::error_code error) {
            return error == std::errc::no_such_file_or_directory || error == std::errc::invalid_argument
                || error == std::errc::value_too_large || error == std::errc::state_not_recoverable;
        }

        std::expected<reading::StoredState, std::error_code> loadWritableBookState(std::string_view bookPath,
                                                                                   const BookIdentity& identity) {
            return loadBookState(bookPath, identity)
                .or_else([](std::error_code error) -> std::expected<reading::StoredState, std::error_code> {
                    if (canReplaceBookState(error))
                        return reading::StoredState{};
                    return std::unexpected(error);
                });
        }

        // Always ".tmp" + rename: opening the sidecar for writing truncates it first, so a power cut in the middle
        // used to leave an empty file (position 0 and the book overrides gone). loadBookState reads the copies back.
        std::expected<void, std::error_code> writeBookState(std::string_view bookPath,
                                                            const reading::StoredState& state) {
            std::string output;
            if (glz::write_toml(state, output))
                return std::unexpected(std::make_error_code(std::errc::io_error));

            const std::string sidecarPath = StoragePaths::bookStatePathFor(bookPath);
            const auto temporary = sidecarPath + ".tmp", backup = sidecarPath + ".bak";
            return StorageFiles::writeFileAtomic(Board::Storage::filesystem(), sidecarPath.c_str(), temporary.c_str(),
                                                  backup.c_str(), output);
        }

        std::expected<void, std::error_code> writeSessionSidecar(const ReadingSession& session,
                                                                 const IndexedBookStore& store) {
            const uint32_t wordCount = store.identity().wordCount;
            if (!session.stored() || wordCount == 0)
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));

            reading::StoredState state{
                .identity = store.identity(),
                .reading = session.state,
            };
            state.reading.wordIndex = std::min(state.reading.wordIndex, wordCount - 1);
            return writeBookState(session.sourcePath(), state);
        }

        std::expected<uint32_t, std::error_code> readSessionSidecar(ReadingSession& session,
                                                                    const IndexedBookStore& store) {
            if (!session.stored())
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            return loadBookState(session.sourcePath(), store.identity())
                .transform([&session](reading::StoredState state) {
                    session.state = std::move(state.reading);
                    return session.state.wordIndex;
                });
        }

    } // namespace

    std::expected<uint32_t, std::error_code> readBookStatePosition(std::string_view bookPath,
                                                                   const BookIdentity& identity) {
        return loadBookState(bookPath, identity).transform([](const reading::StoredState& state) {
            return state.reading.wordIndex;
        });
    }

    std::expected<reading::State, std::error_code> readBookState(std::string_view bookPath,
                                                                 const BookIdentity& identity) {
        return loadBookState(bookPath, identity).transform([](reading::StoredState state) {
            return std::move(state.reading);
        });
    }

    std::expected<void, std::error_code> writeBookStatePosition(std::string_view bookPath, const BookIdentity& identity,
                                                                uint32_t wordIndex) {
        if (bookPath.empty() || !isValidIdentity(identity))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        return loadWritableBookState(bookPath, identity)
            .and_then([&](reading::StoredState state) {
                state.identity = identity;
                state.reading.wordIndex = std::min(wordIndex, identity.wordCount - 1);
                return writeBookState(bookPath, std::move(state));
            })
            .transform([&] {
                ESP_LOGI("storage-progress", "mirrored position word=%u count=%u state=%s",
                         static_cast<unsigned int>(wordIndex), static_cast<unsigned int>(identity.wordCount),
                         StoragePaths::bookStatePathFor(bookPath).c_str());
            });
    }

    std::expected<void, std::error_code> writeBookOverrides(std::string_view bookPath, const BookIdentity& identity,
                                                             const settings::ReadingOverrides& overrides) {
        if (bookPath.empty() || !isValidIdentity(identity))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return loadWritableBookState(bookPath, identity).and_then([&](reading::StoredState state) {
            state.identity = identity;
            state.reading.overrides = overrides;
            return writeBookState(bookPath, state);
        });
    }

    std::expected<void, std::error_code> writeBookLanguageFonts(std::string_view bookPath, const BookIdentity& identity,
                                                                std::vector<settings::LanguageFont> languageFonts) {
        if (bookPath.empty() || !isValidIdentity(identity))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return loadWritableBookState(bookPath, identity).and_then([&](reading::StoredState state) {
            state.identity = identity;
            state.reading.overrides.languageFonts = std::move(languageFonts);
            return writeBookState(bookPath, std::move(state));
        });
    }

    uint8_t percent(uint32_t wordIndex, uint32_t wordCount) {
        if (wordCount <= 1)
            return 0;
        return static_cast<uint8_t>(std::min<uint32_t>((wordIndex * 100UL) / (wordCount - 1), 100));
    }

    void loadBookmarks(ReadingSession& session, const IndexedBookStore& store) {
        session.bookmarks.clear();
        session.bookmarksDirty = false;
        if (!session.stored()) return;
        File file = Board::Storage::filesystem().open(StoragePaths::bookmarksPathFor(session.sourcePath()).c_str(), FILE_READ);
        if (!file || file.isDirectory()) {
            if (file) file.close();
            return;
        }
        const size_t size = file.size();
        if (size == 0 || size > 4096) {
            file.close();
            return;
        }
        std::string input(size, '\0');
        const size_t read = file.read(reinterpret_cast<uint8_t*>(input.data()), input.size());
        file.close();
        if (read != input.size()) return;
        if (auto marks = reading::parseBookmarks(input, store.identity())) session.bookmarks = std::move(*marks);
    }

    void saveBookmarks(ReadingSession& session) {
        session.bookmarksDirty = false;
        if (!session.stored() || session.bookStore == nullptr) return;
        const std::string path = StoragePaths::bookmarksPathFor(session.sourcePath());
        if (session.bookmarks.empty()) {
            Board::Storage::filesystem().remove(path.c_str());
            return;
        }
        const std::string output = reading::serializeBookmarks(session.bookStore->identity(), session.bookmarks);
        const auto temporary = path + ".tmp", backup = path + ".bak";
        auto written = StorageFiles::writeFileAtomic(Board::Storage::filesystem(), path.c_str(), temporary.c_str(),
                                                     backup.c_str(), output);
        if (!written) Logger::failure("storage-progress", "bookmarks", path.c_str(), written.error());
    }

    void save(ReadingSession& session, Preferences& preferences, bool force, uint32_t nowMs) {
        // Resume point / bookmarks changed by a jump or the marker UI: persist right away.
        if (session.stored() && session.resumeDirty && session.bookStore != nullptr) {
            session.resumeDirty = false;
            mirror(session, *session.bookStore);
        }
        if (session.stored() && session.bookmarksDirty) saveBookmarks(session);
        // Position on the card: at every save point and, while reading, about once a minute at a sentence end
        // (reading::positionSaveDue). Before this only the book name was kept below, so a crash, RESET or flat
        // battery threw the reader back to the last chapter start even after pausing.
        if (session.stored() && session.bookStore != nullptr
            && session.state.wordIndex != session.lastMirroredWordIndex) {
            const uint32_t since =
                static_cast<int32_t>(nowMs - session.lastMirrorMs) > 0 ? nowMs - session.lastMirrorMs : 0;
            // While playing, wait until the word has been drawn so the card write eats into its own display time.
            const bool waiting = !force && session.playing && since >= reading::kPositionSaveIntervalMs;
            const bool settled = !waiting || ReadingLoop::elapsedInCurrentWordMs(session, nowMs) >= kWordSettledMs;
            const bool sentenceEnd = waiting && settled && ReadingLoop::currentWordEndsSentence(session);
            if (settled && reading::positionSaveDue(force, session.playing, sentenceEnd, true, since))
                mirror(session, *session.bookStore);
        }
        if (!session.stored() || (!force && nowMs - session.lastSaveMs < kSaveIntervalMs))
            return;
        const size_t wordIndex = session.state.wordIndex;
        if (!force && wordIndex == session.lastSavedWordIndex)
            return;
        session.lastSaveMs = nowMs;
        cache(session, preferences, static_cast<uint32_t>(wordIndex)); // only remembers which book is open
    }

    void cache(ReadingSession& session, Preferences& preferences, uint32_t wordIndex) {
        if (!session.stored())
            return;
        const uint32_t wordCount = session.bookStore->identity().wordCount;
        if (wordCount > 0)
            wordIndex = std::min<uint32_t>(wordIndex, static_cast<uint32_t>(wordCount - 1));
        preferences.putString("book", session.sourcePath().data());
        session.lastSavedWordIndex = wordIndex;
    }

    void mirror(ReadingSession& session, const IndexedBookStore& store) {
        if (!session.stored())
            return;
        auto written = writeSessionSidecar(session, store);
        session.lastMirrorMs = millis(); // also after a failure: never hammer a broken card every frame
        if (!written) {
            Logger::failure("storage-progress", "mirror", StoragePaths::bookStatePathFor(session.sourcePath()).c_str(),
                            written.error());
            return;
        }
        session.lastMirroredWordIndex = session.state.wordIndex;
        ESP_LOGI("storage-progress", "position on card word=%u path=%s",
                 static_cast<unsigned int>(session.state.wordIndex), session.sourcePath().data());
    }

    uint32_t restore(ReadingSession& session, const IndexedBookStore& store) {
        auto wordIndex = readSessionSidecar(session, store);
        if (wordIndex)
            return *wordIndex;
        if (wordIndex.error() != std::errc::no_such_file_or_directory
            && wordIndex.error() != std::errc::state_not_recoverable)
            Logger::failure("storage-progress", "restore", StoragePaths::bookStatePathFor(session.sourcePath()).c_str(),
                            wordIndex.error());
        return kNoSavedWordIndex;
    }

    bool saveChapterTransition(ReadingSession& session, Preferences& preferences, const IndexedBookStore& store,
                               size_t previousWordIndex, size_t currentWordIndex, uint32_t nowMs) {
        if (!session.stored())
            return false;
        const auto chapter = std::ranges::find_if(session.metadata.chapters, [&](const ChapterMarker& candidate) {
            return candidate.wordIndex != 0 && candidate.wordIndex > previousWordIndex
                && candidate.wordIndex <= currentWordIndex;
        });
        if (chapter == session.metadata.chapters.end())
            return false;
        ReadingLoop::seekTo(session, chapter->wordIndex);
        save(session, preferences, true, nowMs); // forced: writes the chapter start to the card
        flush(session, store);                   // one more try if the write inside save failed
        return true;
    }

    void flush(ReadingSession& session, const IndexedBookStore& store) {
        if (!session.stored())
            return;
        if (!session.resumeDirty && session.state.wordIndex == session.lastMirroredWordIndex)
            return;
        session.resumeDirty = false;
        mirror(session, store);
    }

    std::string_view title(const ReadingSession& session, const StorageManager& storage) {
        if (!session.stored())
            return "Demo";
        const int index = storage.findBook(session.sourcePath());
        const BookLibrary::Entry* book = index < 0 ? nullptr : storage.book(static_cast<size_t>(index));
        return book == nullptr ? std::string_view{} : BookLibrary::displayName(*book);
    }

} // namespace ReadingProgress
