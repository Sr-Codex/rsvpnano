#pragma once

#include "library/CoverStore.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "reader/ReadingLoop.h"
#include "library/BookLibrary.h"
#include "ui/screens/Screens.h"
#include "ui/Layouts.h"

class StorageManager;

namespace screens {

    struct LibraryItem {
        const BookLibrary::Entry* book = nullptr;
        std::string chapter;
        uint8_t progress = 0;
    };

    class LibraryScreen {
    public:
        Action draw(ui::Context& ui, const std::vector<LibraryItem>& items, uint32_t nowMs, Screen& screen);
        void reset();
        void invalidate();
        bool needsItems(const StorageManager& storage) const;
        const std::vector<LibraryItem>& items(StorageManager& storage, const IndexedBookStore& bookStore,
                                              const ReadingSession& session);
        size_t selectedIndex() const {
            return selectedIndex_;
        }
        // Shelf order (indexes into items): book being read first, then newest added.
        const std::vector<size_t>& shelf() const {
            return shelf_;
        }
        // Preview/test hook: shelf order and the book being read without touching the card.
        void setShelf(std::vector<size_t> order, std::string readingPath) {
            shelf_ = std::move(order);
            shelfReadingPath_ = std::move(readingPath);
        }
        // Cover thumbnails (Lume shelf). The store is owned by the App; null disables covers.
        void setCoverStore(covers::CoverStore* store) {
            coverStore_ = store;
        }
        bool hasPendingCovers() const {
            return !pendingCovers_.empty();
        }
        // Next book whose thumbnail must be built (main thread hands it to the background job).
        std::string takePendingCover() {
            if (pendingCovers_.empty())
                return {};
            std::string path = std::move(pendingCovers_.back());
            pendingCovers_.pop_back();
            return path;
        }

    private:
        int32_t centeredOffset(const std::vector<LibraryItem>& items, size_t index, int16_t viewportWidth) const;
        int32_t clampOffset(const std::vector<LibraryItem>& items, int32_t offset, int16_t viewportWidth) const;
        size_t nearest(const std::vector<LibraryItem>& items, int32_t offset, int16_t x, int16_t viewportX) const;
        size_t spineAt(const std::vector<LibraryItem>& items, int32_t offset, const ui::Rect& viewport, uint16_t x,
                       uint16_t y) const;
        int16_t spineHeight(const LibraryItem& item, size_t index) const;
        uint32_t signature(const std::vector<LibraryItem>& items, size_t current, size_t first,
                           size_t pastLast) const;
        bool dragging_ = false;
        bool moved_ = false;
        uint16_t startX_ = 0;
        uint16_t startY_ = 0;
        int32_t startOffset_ = 0;
        int32_t offset_ = 0;
        size_t selectedIndex_ = 0;
        std::vector<LibraryItem> items_;
        std::vector<std::string> knownOrder_; // contents of /library/.order after reconcile
        std::vector<size_t> shelf_;
        std::string shelfReadingPath_;
        size_t shelfPage_ = 0;
        void updateShelf(StorageManager& storage, const ReadingSession& session, bool reloadOrderFile);
        Action drawShelf(ui::Context& ui, const std::vector<LibraryItem>& items, uint32_t nowMs, Screen& screen);
        covers::CoverStore* coverStore_ = nullptr;
        std::vector<std::string> pendingCovers_; // stack: last = built first
        int optionsFor_ = -1;                    // item index whose long-press sheet is open
        int pressedSlot_ = -1;
        bool itemsValid_ = false;
        uint32_t itemsRevision_ = 0;
        ui::CarouselGesture carouselGesture_;
    };

} // namespace screens
