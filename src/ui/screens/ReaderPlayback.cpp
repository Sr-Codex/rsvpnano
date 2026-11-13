#include "ui/screens/ReaderScreen.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace screens {
    void ReaderScreen::toggle(Preferences& preferences, uint32_t nowMs) {
        if (pageBrowsing_) return;
        if (session.playing)
            requestPause(preferences, nowMs);
        else
            start(nowMs, true);
    }

    void ReaderScreen::update(Preferences& preferences, uint32_t nowMs) {
        // Browsing owns the page font. Preparing a chunk here would replace its
        // cached strike with the RSVP size on every paused browser frame.
        if (pageBrowsing_) return;
        if (wpmFeedbackUntilMs_ > 0 && nowMs >= wpmFeedbackUntilMs_) {
            wpmFeedbackUntilMs_ = 0;
        }
        if (scrubNoticeUntilMs_ != 0 && static_cast<int32_t>(scrubNoticeUntilMs_ - nowMs) <= 0) {
            scrubNoticeUntilMs_ = 0; // the "‹ 1 frase" after a swipe goes away (and never comes back on a wrap)
            if (!modernScrub_) scrubText_.clear();
        }
        prepareChunk();
        if (shouldFinishPause(nowMs)) {
            finishPause(preferences, nowMs);
            return;
        }
        prefetchNextWord(nowMs);
        nowMs = millis();
        {
            // "Onde parei": count real reading (forward steps / play time); see reader/ReadingMarks.h.
            const uint32_t dt = nowMs - lastTickMs_;
            lastTickMs_ = nowMs;
            if (backward_ || modernScrub_)
                session.resumeTracker.lastWord = session.state.wordIndex; // rewind/scrub is not "reading"
            else if (reading::tick(session.state.resume, session.resumeTracker, session.state.wordIndex, dt,
                              session.playing))
                session.resumeDirty = true;
        }
        if (backward_) {
            // Hold-left rewind: word by word at the current WPM (x2 after 2 s, x3 after 4 s). No sentence-end stop.
            ReadingLoop::updateBackward(session, settings_, nowMs,
                                        reading::backwardSpeedPercent(nowMs - backwardStartMs_));
            return;
        }
        if (!session.playing) {
            reorientUntilMs_ = 0;
            return;
        }
        if (reorientUntilMs_ != 0) {
            // Just jumped back a sentence with the words running: the eye is still coming back from the corner
            // of the screen. The first word waits, then gets its full time.
            if (static_cast<int32_t>(reorientUntilMs_ - nowMs) > 0) {
                session.lastAdvanceMs = nowMs;
                return;
            }
            reorientUntilMs_ = 0;
        }
#if defined(RSVP_BENCHMARK_MODE)
        // Reading benchmarks load indexed fixtures but intentionally exclude persistence I/O.
        ReadingLoop::update(session, settings_, nowMs, true, pauseAtSentenceEndRequested_);
#else
        const size_t previousIndex = session.state.wordIndex;
        if (ReadingLoop::update(session, settings_, nowMs, true, pauseAtSentenceEndRequested_)) {
            ReadingProgress::saveChapterTransition(session, preferences, store, previousIndex, session.state.wordIndex,
                                                   nowMs);
        }
#endif
        if (!session.playing && pauseAtSentenceEndRequested_)
            finishPause(preferences, nowMs);
    }

    void ReaderScreen::stopBackward(Preferences& preferences, uint32_t nowMs) {
        if (!backward_) return;
        backward_ = false;
        ReadingProgress::save(session, preferences, true, nowMs);
    }

    void ReaderScreen::rewindSentenceSwipe(Preferences& preferences, uint32_t nowMs) {
        constexpr uint32_t kReorientMs = 350;
        constexpr uint32_t kNoticeMs = 700;
        if (pageBrowsing_ || backward_ || modernScrub_ || ReadingLoop::wordCount(session) == 0)
            return;
        const bool playing = session.playing; // false when the pause asked earlier landed during the flick
        const size_t from = session.state.wordIndex;
        // The reader reacts a few words late, so a swipe just after a sentence began replays the one before.
        // Not a jump: the "onde parei" is not touched.
        ReadingLoop::rewindSentence(session, ReadingLoop::kRewindMarginWhilePlaying);
        ESP_LOGI("reader", "rewind sentence %u -> %u (%s)", static_cast<unsigned int>(from),
                 static_cast<unsigned int>(session.state.wordIndex), playing ? "playing" : "paused");
        if (session.state.wordIndex == from) return; // already at the first word of the book: nothing to show
        scrubText_ = "‹ 1 frase";
        scrubNoticeUntilMs_ = nowMs + kNoticeMs;
        session.lastAdvanceMs = nowMs;
        if (playing)
            reorientUntilMs_ = nowMs + kReorientMs; // the position reaches the card at the next pause
        else
            ReadingProgress::save(session, preferences, true, nowMs);
    }

    void ReaderScreen::start(uint32_t nowMs, bool locked) {
        pagePreview_ = false;
        pageBrowsing_ = false;
        backward_ = false;
        playLocked_ = locked;
        pauseAtSentenceEndRequested_ = false;
        wpmFeedbackUntilMs_ = 0;
        prepareChunk();
        ReadingLoop::start(session, nowMs);
    }

    void ReaderScreen::requestPause(Preferences& preferences, uint32_t nowMs) {
        if (!session.playing)
            return;
        playLocked_ = false;
        if (settings_.pauseMode == settings::PauseMode::instant) {
            finishPause(preferences, nowMs);
            return;
        }
        pauseAtSentenceEndRequested_ = true;
        prepareChunk();
        if (shouldFinishPause(nowMs)) {
            finishPause(preferences, nowMs);
        }
    }

    bool ReaderScreen::shouldFinishPause(uint32_t nowMs) const {
        if (!session.playing || !pauseAtSentenceEndRequested_)
            return false;
        const uint32_t durationMs = ReadingLoop::currentWordDurationMs(session, settings_);
        return durationMs > 0 && ReadingLoop::elapsedInCurrentWordMs(session, nowMs) >= durationMs
            && (ReadingLoop::currentWordEndsSentence(session) || ReadingLoop::atEnd(session));
    }

    void ReaderScreen::finishPause(Preferences& preferences, uint32_t nowMs) {
        ReadingLoop::pause(session);
        pauseAtSentenceEndRequested_ = false;
        playLocked_ = false;
        ReadingProgress::save(session, preferences, true, nowMs);
    }

} // namespace screens
