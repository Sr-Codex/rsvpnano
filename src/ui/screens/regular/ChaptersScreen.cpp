#include "ui/screens/ChaptersScreen.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "ui/screens/ScreenCommon.h"
#include "ui/screens/ModernScreens.h"

namespace screens {

    namespace {

        constexpr int16_t kHeaderHeight = 32;
        constexpr int16_t kRowStep = 30;
        constexpr int16_t kDragThreshold = 6;

        int16_t rowCenter(ui::Rect viewport, int row, int offset) {
            const int raw = row * kRowStep + offset;
            const int magnitude = std::min(std::abs(raw), static_cast<int>(viewport.h));
            return viewport.y + viewport.h / 2 + raw * (2 * viewport.h - magnitude) / (2 * viewport.h);
        }

        constexpr int16_t rowHeight(bool centered) {
            return centered ? 28 : 18;
        }

        bool rowVisible(ui::Rect viewport, int y, int height) {
            return y - height / 2 >= viewport.y && y + height / 2 <= viewport.y + viewport.h;
        }

    } // namespace

    Action ChaptersScreen::draw(ui::Context& ui, std::span<const ChapterMarker> chapters, ReadingSession& reader,
                                const settings::ReadingSettings& settings, uint32_t nowMs, Screen& screen) {
        (void) nowMs;
        if (const Action action = detail::navigation(ui, Screen::Chapters, screen); action != Action::None)
            return action;

        const ui::Rect content = detail::tabContent(ui);
        if (!ui.modernPresentation() && ui.button({content.x, content.y, 64, kHeaderHeight}, "<<")) {
            screen = Screen::Read;
            dragging_ = false;
            return Action::None;
        }

        const auto nextChapter = std::upper_bound(chapters.begin(), chapters.end(), reader.state.wordIndex,
            [](size_t word, const ChapterMarker& chapter) { return word < chapter.wordIndex; });
        const size_t readingIndex = nextChapter == chapters.begin() ? 0 : static_cast<size_t>(nextChapter - chapters.begin() - 1);

        if (source_.data() != chapters.data() || source_.size() != chapters.size()) {
            source_ = chapters;
            centeredIndex_ = readingIndex;
            offset_ = 0;
            dragging_ = false;
        }

        if (ui.modernPresentation())
            return drawModern(ui, chapters, reader, settings, readingIndex, screen);

        char position[24];
        std::snprintf(position, sizeof(position), "%u / %u",
                      static_cast<unsigned>(chapters.empty() ? 0 : centeredIndex_ + 1),
                      static_cast<unsigned>(chapters.size()));
        const int16_t positionWidth = std::min<int16_t>(84, content.w / 4);
        ui.label({static_cast<int16_t>(content.x + 68), content.y,
                  static_cast<int16_t>(std::max<int16_t>(0, content.w - positionWidth - 72)), kHeaderHeight},
                 ui.text(UiText::Chapters), 2, ui::themes::ColorRole::Foreground, ui::TextAlign::Center);
        ui.label({static_cast<int16_t>(content.x + content.w - positionWidth), content.y, positionWidth, kHeaderHeight},
                 position, 1, ui::themes::ColorRole::Muted, ui::TextAlign::Right);

        const ui::Rect viewport{content.x, static_cast<int16_t>(content.y + kHeaderHeight + 4), content.w,
                                static_cast<int16_t>(content.h - kHeaderHeight - 4)};
        if (viewport.h <= 0)
            return Action::None;
        if (chapters.empty()) {
            if (ui.button(viewport, ui.text(UiText::StartReading))) {
                ReadingLoop::jumpTo(reader, 0);
                return Action::Resume;
            }
            return Action::None;
        }

        const ui::Touch* touch = ui.touch();
        if (touch != nullptr && ui::hasTouch(*touch, ui::TouchStart) && ui::contains(viewport, touch->x, touch->y)) {
            dragging_ = true;
            moved_ = false;
            dragStartIndex_ = centeredIndex_;
            dragStartY_ = touch->y;
        }
        if (dragging_ && touch != nullptr
            && (ui::hasTouch(*touch, ui::TouchMove) || ui::hasTouch(*touch, ui::TouchRelease))) {
            const int delta = static_cast<int>(touch->y) - dragStartY_;
            moved_ = moved_ || std::abs(delta) > kDragThreshold;
            if (moved_) {
                // Displacement owns the selection: holding still never advances chapters,
                // and sample rate cannot change the distance travelled.
                const int direction = settings.chapterScrollReversed ? -1 : 1;
                const int64_t position = std::clamp<int64_t>(
                    static_cast<int64_t>(dragStartIndex_) * kRowStep - direction * delta,
                    0, static_cast<int64_t>(chapters.size() - 1) * kRowStep);
                centeredIndex_ = static_cast<size_t>((position + kRowStep / 2) / kRowStep);
                offset_ = static_cast<int16_t>(static_cast<int64_t>(centeredIndex_) * kRowStep - position);
            }
        }

        if (dragging_ && touch != nullptr && ui::hasTouch(*touch, ui::TouchRelease)) {
            dragging_ = false;
            if (!moved_ && ui::hasTouch(*touch, ui::TouchTap) && ui::contains(viewport, touch->x, touch->y)) {
                const size_t first = centeredIndex_ > 4 ? centeredIndex_ - 4 : 0;
                const size_t last = std::min(chapters.size(), centeredIndex_ + 5);
                size_t tappedIndex = chapters.size();
                int closestDistance = kRowStep / 2 + 1;
                for (size_t index = first; index < last; ++index) {
                    const int y = rowCenter(viewport, static_cast<int>(index) - static_cast<int>(centeredIndex_), offset_);
                    if (!rowVisible(viewport, y, rowHeight(index == centeredIndex_)))
                        continue;
                    const int distance = std::abs(y - touch->y);
                    if (distance < closestDistance) {
                        closestDistance = distance;
                        tappedIndex = index;
                    }
                }
                if (tappedIndex != chapters.size()) {
                    centeredIndex_ = tappedIndex;
                    offset_ = 0;
                    ReadingLoop::jumpTo(reader, chapters[centeredIndex_].wordIndex);
                    return Action::Resume;
                }
            }
            // Snap to the same nearest chapter used for highlighting during the drag.
            offset_ = 0;
        }

        uint32_t state = ui::Context::combine(static_cast<uint32_t>(chapters.size()), centeredIndex_);
        state = ui::Context::combine(state, static_cast<uint16_t>(offset_));
        state = ui::Context::combine(state, static_cast<uint32_t>(readingIndex));
        const size_t first = centeredIndex_ > 4 ? centeredIndex_ - 4 : 0;
        const size_t last = std::min(chapters.size(), centeredIndex_ + 5);
        const int16_t centerY = static_cast<int16_t>(viewport.y + viewport.h / 2);
        for (size_t index = first; index < last; ++index)
            state =
                ui::Context::signature(chapters[index].title,
                                       ui::Context::combine(state, static_cast<uint32_t>(chapters[index].wordIndex)));

        if (ui.redraw(viewport, state)) {
            Arduino_GFX& gfx = ui.gfx();
            const int16_t halfHeight = std::max<int16_t>(1, viewport.h / 2);
            const int16_t maximumWidth = std::max<int16_t>(40, static_cast<int16_t>(viewport.w - 20));
            const uint16_t background = ui.color(ui::themes::ColorRole::Background);
            for (size_t index = first; index < last; ++index) {
                const int16_t y = rowCenter(viewport, static_cast<int>(index) - static_cast<int>(centeredIndex_), offset_);
                const int curved = y - centerY;
                const bool centered = index == centeredIndex_;
                const uint8_t alpha =
                    centered ? 255 : static_cast<uint8_t>(std::max(48, 220 - std::abs(curved) * 172 / halfHeight));
                const int16_t height = rowHeight(centered);
                const int16_t width = centered ? maximumWidth
                                               : static_cast<int16_t>(maximumWidth
                                                                      - std::min<int>(std::abs(curved), halfHeight)
                                                                            * (maximumWidth / 3) / halfHeight);
                const int16_t x = static_cast<int16_t>(viewport.x + (viewport.w - width) / 2);
                const int16_t top = static_cast<int16_t>(y - height / 2);
                if (!rowVisible(viewport, y, height))
                    continue;
                const int16_t right = static_cast<int16_t>(x + width - 1);
                const int16_t notch = std::min<int16_t>(10, height / 2);
                const uint16_t surface = centered ? ui.color(ui::themes::ColorRole::SurfaceActive)
                                                  : ui.blend(ui::themes::ColorRole::SurfaceMuted, alpha);
                const uint16_t outline = centered ? ui.color(ui::themes::ColorRole::Outline)
                                                  : ui.blend(ui::themes::ColorRole::Outline, alpha);
                gfx.fillRoundRect(x, top, width, height, 5, surface);
                if (index == readingIndex) {
                    const int16_t tailLeft = static_cast<int16_t>(right - notch - 6);
                    gfx.fillRect(tailLeft, top, static_cast<int16_t>(right - tailLeft + 1), height,
                                 centered ? ui.color(ui::themes::ColorRole::Accent)
                                          : ui.blend(ui::themes::ColorRole::Accent, alpha));
                }
                gfx.drawRoundRect(x, top, width, height, 5, outline);
                gfx.fillTriangle(static_cast<int16_t>(right - notch), y, right, top, right,
                                 static_cast<int16_t>(top + height - 1), background);
                gfx.drawLine(static_cast<int16_t>(right - notch), y, right, top, outline);
                gfx.drawLine(static_cast<int16_t>(right - notch), y, right, static_cast<int16_t>(top + height - 1),
                             outline);
                if (centered)
                    gfx.fillRect(static_cast<int16_t>(x + 4), static_cast<int16_t>(top + 3), 3,
                                 static_cast<int16_t>(height - 6), ui.color(ui::themes::ColorRole::Accent));

                char fallback[24];
                const std::string_view chapter = ui.text(UiText::Chapter);
                std::snprintf(fallback, sizeof(fallback), "%.*s %u", static_cast<int>(chapter.size()), chapter.data(),
                              static_cast<unsigned>(index + 1));
                const std::string_view title = chapters[index].title.empty() ? std::string_view{fallback}
                                                                             : std::string_view{chapters[index].title};
                ui.drawText({static_cast<int16_t>(x + 10), top, static_cast<int16_t>(width - notch - 24), height},
                            title, centered ? 2 : 1, ui.blend(ui::themes::ColorRole::Foreground, alpha),
                            ui::TextAlign::Center, 1, reader.metadata.localeAt(chapters[index].wordIndex));
            }
        }
        return Action::None;
    }

    // Lume.5 modern layout (640x172): "Voltar para onde parei" on top, chapter rows below, bookmarks panel on
    // the right. Every jump goes through ReadingLoop::jumpTo so "onde parei" is recorded first.
    Action ChaptersScreen::drawModern(ui::Context& ui, std::span<const ChapterMarker> chapters, ReadingSession& reader,
                                      const settings::ReadingSettings& settings, size_t readingIndex, Screen& screen) {
        (void) screen;
        using namespace ui::themes;
        const size_t wordCount = ReadingLoop::wordCount(reader);
        const reading::ResumePoint& resume = reader.state.resume;
        const bool hasResume = resume.valid;
        const std::string_view chapterWord = ui.text(UiText::Chapter);

        auto chapterTitle = [&](size_t index) {
            return index < chapters.size() && !chapters[index].title.empty()
                       ? std::string{chapters[index].title}
                       : std::string{chapterWord} + " " + std::to_string(index + 1);
        };

        // --- "Voltar para onde parei" -------------------------------------------------------------------
        if (hasResume) {
            const ui::Rect rect{16, 48, 452, 40};
            std::string caption = resume.chapter > 0 ? "Cap. " + std::to_string(resume.chapter) + " · "
                                                          + chapterTitle(resume.chapter - 1)
                                                     : std::string{"Início"};
            caption += " · " + std::to_string(resume.percent) + "%";
            if (ui.redraw(rect, ui::Context::signature(caption, resume.wordIndex))) {
                Arduino_GFX& gfx = ui.gfx();
                gfx.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, ui.color(SurfaceActive));
                gfx.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, ui.color(Accent));
                gfx.drawRoundRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 4, ui.color(Accent));
                const int16_t cy = rect.y + rect.h / 2;
                gfx.fillTriangle(rect.x + 10, cy, rect.x + 22, cy - 9, rect.x + 22, cy + 9, ui.color(Accent));
                ui.drawText({static_cast<int16_t>(rect.x + 32), static_cast<int16_t>(rect.y + 2),
                             static_cast<int16_t>(rect.w - 40), 20},
                            "Voltar para onde parei", 14, ui.color(Foreground), ui::TextAlign::Start);
                ui.drawText({static_cast<int16_t>(rect.x + 32), static_cast<int16_t>(rect.y + 21),
                             static_cast<int16_t>(rect.w - 40), 16},
                            caption, 11, ui.color(Accent), ui::TextAlign::Start);
            }
            if (ui.tap(rect)) {
                ReadingLoop::jumpTo(reader, resume.wordIndex); // exact word; clears the resume point
                return Action::Resume;
            }
        }

        // --- chapter rows ---------------------------------------------------------------------------------
        const int16_t top = hasResume ? 92 : 48;
        const size_t rows = hasResume ? 2 : 3;
        const int16_t rowH = hasResume ? 34 : 36, step = hasResume ? 38 : 40;
        const ui::Rect viewport{16, top, 452, static_cast<int16_t>(rows * step)};
        if (chapters.empty()) {
            if (ui::modern::button(ui, {16, 72, 452, 72}, ui.text(UiText::StartReading), ui::modern::Style::Primary)) {
                ReadingLoop::jumpTo(reader, 0);
                return Action::Resume;
            }
        } else {
            using ui::modern::Direction;
            centeredIndex_ = std::min(centeredIndex_, chapters.size() - 1);
            const auto* touch = ui.touch();
            if (touch && ui::hasTouch(*touch, ui::TouchStart) && ui::contains(viewport, touch->x, touch->y)) {
                dragging_ = true;
                moved_ = false;
                dragStartIndex_ = centeredIndex_;
                dragStartY_ = touch->y;
            }
            if (dragging_ && touch && (ui::hasTouch(*touch, ui::TouchMove) || ui::hasTouch(*touch, ui::TouchRelease))) {
                const int delta = static_cast<int>(touch->y) - dragStartY_;
                moved_ = moved_ || std::abs(delta) > kDragThreshold;
                if (moved_) {
                    const int direction = settings.chapterScrollReversed ? -1 : 1;
                    const int64_t position = std::clamp<int64_t>(
                        static_cast<int64_t>(dragStartIndex_) * 38 - direction * delta, 0,
                        static_cast<int64_t>(chapters.size() - 1) * 38);
                    centeredIndex_ = static_cast<size_t>((position + 19) / 38);
                }
                if (ui::hasTouch(*touch, ui::TouchRelease)) dragging_ = false;
            }
            const ui::Rect upRect{420, top, 48, static_cast<int16_t>(viewport.h / 2 - 1)};
            const ui::Rect downRect{420, static_cast<int16_t>(top + viewport.h / 2 + 1), 48,
                                    static_cast<int16_t>(viewport.h / 2 - 1)};
            if (ui::modern::arrow(ui, upRect, Direction::Up, centeredIndex_ > 0)) --centeredIndex_;
            if (ui::modern::arrow(ui, downRect, Direction::Down, centeredIndex_ + 1 < chapters.size())) ++centeredIndex_;

            const size_t first = std::min(hasResume ? centeredIndex_ : (centeredIndex_ == 0 ? 0 : centeredIndex_ - 1),
                                          chapters.size() > rows ? chapters.size() - rows : 0);
            // "parei aqui" marks the chapter of the saved place, or the live reading chapter when none is saved.
            const size_t stoppedIndex = hasResume ? (resume.chapter > 0 ? resume.chapter - 1 : SIZE_MAX) : readingIndex;
            for (size_t row = 0; row < rows && first + row < chapters.size(); ++row) {
                const size_t index = first + row;
                const ui::Rect rect{16, static_cast<int16_t>(top + row * step), 400, rowH};
                const bool selected = index == centeredIndex_;
                const bool stopped = index == stoppedIndex;
                std::string pct = std::to_string(reading::percentOf(chapters[index].wordIndex,
                                                                    static_cast<uint32_t>(wordCount))) + "%";
                if (stopped) pct += " · parei aqui";
                const std::string title = std::to_string(index + 1) + "  " + chapterTitle(index);
                uint32_t state = ui::Context::signature(title, ui::Context::signature(pct));
                state = ui::Context::combine(state, selected);
                if (ui.redraw(rect, state)) {
                    Arduino_GFX& gfx = ui.gfx();
                    gfx.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 4,
                                      ui.color(selected ? SurfaceActive : SurfaceMuted));
                    ui.drawText({static_cast<int16_t>(rect.x + 10), rect.y, static_cast<int16_t>(rect.w - 150), rect.h},
                                title, 14, ui.color(Foreground), ui::TextAlign::Start, 1,
                                reader.metadata.localeAt(chapters[index].wordIndex));
                    ui.drawText({static_cast<int16_t>(rect.x + rect.w - 140), rect.y, 132, rect.h}, pct, 11,
                                ui.color(stopped ? Accent : Muted), ui::TextAlign::Right);
                }
                if (ui.tap(rect) && !moved_) {
                    if (selected) {
                        ReadingLoop::jumpTo(reader, chapters[index].wordIndex);
                        return Action::Resume;
                    }
                    centeredIndex_ = index;
                }
            }
        }

        // --- bookmarks panel ------------------------------------------------------------------------------
        constexpr ui::Rect panel{480, 48, 144, 116};
        auto& marks = reader.bookmarks;
        const int swipe = bmSwipe_.update(ui, {480, 66, 144, 78});
        if (swipe > 0 && bmFirst_ + 3 < marks.size()) ++bmFirst_;
        if (swipe < 0 && bmFirst_ > 0) --bmFirst_;
        bmFirst_ = std::min(bmFirst_, marks.size() > 3 ? marks.size() - 3 : 0);
        {
            uint32_t state = ui::Context::combine(static_cast<uint32_t>(marks.size()), static_cast<uint32_t>(bmFirst_));
            for (const auto& mark : marks) state = ui::Context::combine(state, mark.wordIndex);
            if (ui.redraw(panel, state)) {
                ui.gfx().fillRoundRect(panel.x, panel.y, panel.w, panel.h, 6, ui.color(SurfaceMuted));
                std::string head = "MARCADORES";
                if (marks.size() > 3)
                    head += "  " + std::to_string(bmFirst_ + 1) + "-" + std::to_string(bmFirst_ + 3) + "/"
                          + std::to_string(marks.size());
                ui.drawText({488, 50, 130, 16}, head, 11, ui.color(Muted), ui::TextAlign::Start);
                if (marks.empty())
                    ui.drawText({488, 70, 130, 54}, "Nenhum marcador. Segure um item para apagar.", 11,
                                ui.color(Subtle), ui::TextAlign::Start, 3);
            }
        }
        for (size_t row = 0; row < 3 && bmFirst_ + row < marks.size(); ++row) {
            const size_t index = bmFirst_ + row;
            const reading::Bookmark& mark = marks[index];
            const ui::Rect rect{484, static_cast<int16_t>(68 + row * 25), 136, 24};
            if (ui.redraw(rect, ui::Context::combine(ui::Context::signature(mark.snippet), mark.wordIndex))) {
                Arduino_GFX& gfx = ui.gfx();
                const uint16_t surface = ui.color(SurfaceMuted);
                gfx.fillRect(rect.x, rect.y, rect.w, rect.h, surface);
                // ribbon: bar with a V notch at the bottom
                gfx.fillRect(rect.x + 4, rect.y + 4, 8, 15, ui.color(Accent));
                gfx.fillTriangle(rect.x + 4, rect.y + 19, rect.x + 12, rect.y + 19, rect.x + 8, rect.y + 14, surface);
                ui.drawText({static_cast<int16_t>(rect.x + 18), rect.y, 82, rect.h}, mark.snippet, 11,
                            ui.color(Foreground), ui::TextAlign::Start);
                ui.drawText({static_cast<int16_t>(rect.x + rect.w - 36), rect.y, 34, rect.h},
                            std::to_string(mark.percent) + "%", 11, ui.color(Muted), ui::TextAlign::Right);
            }
            const auto* touch = ui.touch();
            if (touch && ui::hasTouch(*touch, ui::TouchHold) && ui::contains(rect, touch->x, touch->y)) {
                reading::removeBookmark(marks, index); // hold = delete
                reader.bookmarksDirty = true;
                holdHandled_ = true;
                break;
            }
            if (ui.tap(rect) && !holdHandled_) {
                ReadingLoop::jumpTo(reader, mark.wordIndex);
                return Action::Resume;
            }
        }
        if (const auto* touch = ui.touch(); touch && ui::hasTouch(*touch, ui::TouchRelease)) holdHandled_ = false;

        // "+ marcar esta página": marks (or unmarks) the current reading position.
        {
            const ui::Rect rect{484, 144, 136, 20};
            const bool marked = reading::hasBookmark(marks, reader.state.wordIndex);
            const bool full = !marked && marks.size() >= reading::kMaxBookmarks;
            const std::string label = full ? "lista cheia (20)" : marked ? "- desmarcar esta página" : "+ marcar esta página";
            if (ui.redraw(rect, ui::Context::signature(label)))
                ui.drawText(rect, label, 11, ui.color(full ? Muted : Accent), ui::TextAlign::Center);
            if (ui.tap(rect, !full) && ReadingLoop::wordCount(reader) > 0) {
                if (marked) reading::removeBookmarkAt(marks, reader.state.wordIndex);
                else {
                    std::string excerpt;
                    for (size_t i = reader.state.wordIndex; i < wordCount && i < reader.state.wordIndex + 6; ++i) {
                        if (!excerpt.empty()) excerpt += ' ';
                        excerpt += ReadingLoop::wordAt(reader, i);
                    }
                    reading::addBookmark(marks, reader.state.wordIndex, static_cast<uint32_t>(wordCount),
                                         reading::makeSnippet(excerpt));
                }
                reader.bookmarksDirty = true;
            }
        }
        return Action::None;
    }

} // namespace screens
