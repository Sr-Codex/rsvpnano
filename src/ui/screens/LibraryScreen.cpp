#include "ui/screens/LibraryScreen.h"

#include <algorithm>

#include "logging/Logger.h"
#include "storage/fs/StoragePaths.h"
#include "library/IndexedBook.h"
#include "library/StorageManager.h"
#include "library/ReadingProgress.h"
#include "library/LibraryOrder.h"
#include "board/BoardStorage.h"
#include "storage/fs/StorageFiles.h"
#include "ui/screens/ScreenCommon.h"

namespace screens {
    void LibraryScreen::reset() {
        carouselGesture_ = {};
        dragging_ = false;
        moved_ = false;
        offset_ = 0;
    }

    void LibraryScreen::invalidate() {
        items_.clear();
        itemsValid_ = false;
    }

    namespace {
        constexpr const char* kOrderPath = "/library/.order";
        constexpr const char* kOrderTemp = "/library/.order.tmp";
        constexpr const char* kOrderBackup = "/library/.order.bak.tmp";
    } // namespace

    void LibraryScreen::updateShelf(StorageManager& storage, const ReadingSession& session, bool reloadOrderFile) {
        std::vector<std::string> paths;
        paths.reserve(storage.books().size());
        for (const auto& book : storage.books()) paths.push_back(book.path);
        if (reloadOrderFile) {
            std::vector<std::string> stored;
            if (const auto text = StorageFiles::readTextFile(Board::Storage::filesystem(), kOrderPath, 256 * 1024))
                stored = library_order::parse(*text);
            bool changed = false;
            knownOrder_ = library_order::reconcile(stored, paths, &changed);
            if (changed) {
                const auto written = StorageFiles::writeFileAtomic(Board::Storage::filesystem(), kOrderPath, kOrderTemp,
                                                                   kOrderBackup, library_order::serialize(knownOrder_));
                if (!written) Logger::failure("library", "write shelf order", kOrderPath, written.error());
            }
        }
        shelfReadingPath_ = session.sourcePath();
        shelf_ = library_order::shelf(knownOrder_, paths, shelfReadingPath_);
    }

    bool LibraryScreen::needsItems(const StorageManager& storage) const {
        return !itemsValid_ || itemsRevision_ != storage.bookRevision() || items_.size() != storage.books().size();
    }

    const std::vector<LibraryItem>& LibraryScreen::items(StorageManager& storage, const IndexedBookStore& bookStore,
                                                         const ReadingSession& session) {
        const size_t bookCount = storage.books().size();
        if (!needsItems(storage)) {
            const int activeIndex = storage.findBook(session.sourcePath());
            if (activeIndex >= 0 && static_cast<size_t>(activeIndex) < items_.size()) {
                LibraryItem& current = items_[static_cast<size_t>(activeIndex)];
                current.progress = ReadingProgress::percent(session.state.wordIndex, ReadingLoop::wordCount(session));
                const auto* chapter = session.metadata.chapterAt(session.state.wordIndex);
                current.chapter = chapter ? chapter->title : std::string{};
            }
            if (shelfReadingPath_ != session.sourcePath()) updateShelf(storage, session, false);
            return items_;
        }

        items_.clear();
        items_.reserve(bookCount);
        for (size_t index = 0; index < bookCount; ++index) {
            const BookLibrary::Entry& book = storage.books()[index];
            LibraryItem item{.book = &book};

            BookMetadata metadata;
            const auto identity = storage.readBookMetadata(index, metadata);
            uint32_t wordIndex = 0;
            bool hasPosition = false;

            if (session.sourcePath() == book.path) {
                wordIndex = static_cast<uint32_t>(session.state.wordIndex);
                item.progress = ReadingProgress::percent(wordIndex, ReadingLoop::wordCount(session));
                if (const ChapterMarker* chapter = session.metadata.chapterAt(wordIndex))
                    item.chapter = chapter->title;
            } else if (identity && identity->wordCount > 0) {
                const auto savedWordIndex = ReadingProgress::readBookStatePosition(book.path, *identity);
                if (savedWordIndex) {
                    hasPosition = true;
                    wordIndex = *savedWordIndex;
                } else if (savedWordIndex.error() != std::errc::no_such_file_or_directory
                           && savedWordIndex.error() != std::errc::state_not_recoverable) {
                    Logger::failure("library", "read progress", StoragePaths::bookStatePathFor(book.path).c_str(),
                                    savedWordIndex.error());
                }
                item.progress = hasPosition ? ReadingProgress::percent(wordIndex, identity->wordCount) : 0;
                if (const ChapterMarker* chapter = metadata.chapterAt(hasPosition ? wordIndex : 0)) {
                    item.chapter = chapter->title;
                }
            } else {
                item.progress = 0;
            }

            items_.push_back(std::move(item));
        }
        updateShelf(storage, session, true);
        pendingCovers_.clear();
        if (coverStore_) {
            coverStore_->clearMemory();
            // Build pages in shelf order: push in reverse so the first visible covers pop first.
            for (auto it = shelf_.rbegin(); it != shelf_.rend(); ++it) {
                const auto& path = storage.books()[*it].path;
                if (coverStore_->needsThumbnail(path)) pendingCovers_.push_back(path);
            }
        }
        itemsValid_ = true;
        itemsRevision_ = storage.bookRevision();
        return items_;
    }

} // namespace screens
