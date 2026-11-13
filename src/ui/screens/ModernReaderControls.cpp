#include "ui/screens/ReaderScreen.h"
#include "ui/screens/ModernReaderChrome.h"
#include <esp_log.h>

namespace screens {
    bool ReaderScreen::handleModernControls(ui::Context& ui,uint32_t nowMs,Preferences& preferences,
                                           settings::SettingsStore& store,Screen& screen) {
        using modern::ReaderControl;
        if(pageBrowsing_ && ui.modernReadingPalette()) return handlePage(ui,nowMs,preferences,store,screen);
        if(!ui.modernReadingPalette() || ui.width()!=640 || ui.height()!=172
           || session.metadata.writingMode==WritingMode::verticalRl){cancelModernControls();return false;}
        const auto* touch=ui.touch();if(!touch)return false;
        if(ui::hasTouch(*touch,ui::TouchCancel)){
            if(modernHolding_)finishPause(preferences,nowMs);
            stopBackward(preferences,nowMs);
            if(modernScrub_)ReadingProgress::save(session,preferences,true,nowMs);
            cancelModernControls();resetTouch();return true;
        }
        if(ui::hasTouch(*touch,ui::TouchStart)){
            resetTouch();lastTapValid_=false;
            modernControl_=static_cast<uint8_t>(modern::readerControlAt(touch->x,touch->y,settings_,session.playing,
                ReadingLoop::wordCount(session)>0,!ui.focusBadge().empty()));
            modernCanvasTouch_=modernControl_==0 && ui::contains({0,36,640,100},touch->x,touch->y);
            modernTouchX_=touch->x;modernTouchY_=touch->y;modernTouchMoved_=false;modernHolding_=false;
            modernScrub_=false;backward_=false;modernHoldStarted_=false;
            modernPressedAtMs_=nowMs;modernPressedPlaying_=session.playing;
            modernMinDx_=0;modernMaxAbsDy_=0;
            return true;
        }
        const int dx=int(touch->x)-modernTouchX_,dy=int(touch->y)-modernTouchY_;
        modernTouchMoved_|=std::abs(dx)>18 || std::abs(dy)>18;
        modernMinDx_=static_cast<int16_t>(std::min<int>(modernMinDx_,dx));
        modernMaxAbsDy_=static_cast<int16_t>(std::max<int>(modernMaxAbsDy_,std::abs(dy)));
        const bool released=ui::hasTouch(*touch,ui::TouchRelease);
        if(modernCanvasTouch_){
            // Gestures on the word canvas (y 36..136):
            //  * hold still (<= kScrubSlopPx): right half reads forward, LEFT half (x<320) rewinds (holdToRead only);
            //  * move horizontally beyond kScrubSlopPx: walk SENTENCE by sentence with the finger (lume.26; the old
            //    word scrub jumped too far) — ONLY when the touch started with playback paused (user request lume.5:
            //    hold/drag only while paused);
            //  * while the words run, a quick flick to the left = "voltar uma frase" and keep reading (lume.26);
            //  * once a hold started, moving the finger does nothing until release (no scrub, no cancel); tap toggles play. Top/bottom bars are separate controls.
            if(!modernScrub_ && !modernHoldStarted_ && !modernHolding_ && !backward_ && !modernPressedPlaying_ && !session.playing
               && ReadingLoop::wordCount(session)>0 && reading::isScrubMove(dx,dy)){
                modernScrub_=true;scrubStartWord_=session.state.wordIndex;scrubAnchorX_=touch->x;scrubShownSteps_=0;
                scrubTakenSteps_=0;scrubBack_.clear();scrubForward_.clear();scrubNoticeUntilMs_=0;scrubText_.clear();
            }
            if(modernScrub_){
                const int steps=reading::scrubSentenceSteps(int(touch->x)-int(scrubAnchorX_));
                if(steps!=scrubShownSteps_ || scrubText_.empty()){
                    scrubShownSteps_=steps;
                    // Sentence starts are found one step at a time and kept for the rest of this drag, so moving the
                    // finger back and forth never searches the same text again.
                    auto& stops=steps<0?scrubBack_:scrubForward_;
                    const size_t wanted=static_cast<size_t>(steps<0?-steps:steps);
                    while(stops.size()<wanted){
                        const size_t from=stops.empty()?scrubStartWord_:stops.back();
                        const size_t next=ReadingLoop::sentenceStartAfterSteps(session,from,steps<0?-1:1);
                        if(next==from)break; // the first or the last sentence of the book
                        stops.push_back(next);
                    }
                    const size_t taken=std::min(wanted,stops.size());
                    scrubTakenSteps_=steps<0?-static_cast<int>(taken):static_cast<int>(taken);
                    const size_t target=taken==0?scrubStartWord_:stops[taken-1];
                    if(target!=session.state.wordIndex)ReadingLoop::seekTo(session,target);
                    const auto* chapter=session.metadata.chapterAt(session.state.wordIndex);
                    const unsigned pct=reading::percentOf(session.state.wordIndex,
                                                          static_cast<uint32_t>(ReadingLoop::wordCount(session)));
                    scrubText_=taken==0?std::string{}
                              :std::string{steps<0?"‹ ":"› "}+std::to_string(taken)+(taken==1?" frase · ":" frases · ");
                    scrubText_+=chapter?"Cap. "+std::to_string(chapter-session.metadata.chapters.data()+1)+" · "+std::to_string(pct)+"%"
                                       :std::to_string(pct)+"%";
                }
                if(released){
                    ESP_LOGI("reader","scrub sentences %d: %u -> %u",scrubTakenSteps_,
                             static_cast<unsigned int>(scrubStartWord_),static_cast<unsigned int>(session.state.wordIndex));
                    modernScrub_=false;scrubText_.clear();ReadingProgress::save(session,preferences,true,nowMs);cancelModernControls();
                }
                return true;
            }
            // Finger drift while holding (reading or rewinding) is ignored: it keeps going until release and never
            // turns into a scrub (user request lume.5: drag only works after pausing).
            if(!released){
                if(!modernTouchMoved_ && settings_.holdToRead && !modernPressedPlaying_ && !modernHolding_ && !backward_
                   && nowMs-modernPressedAtMs_>=350){
                    if(modernTouchX_<320){
                        backward_=true;modernHolding_=true;modernHoldStarted_=true;backwardStartMs_=nowMs;session.lastAdvanceMs=nowMs;
                        ReadingLoop::pause(session);pauseAtSentenceEndRequested_=false;
                    }else{modernHolding_=true;modernHoldStarted_=true;start(nowMs,false);}
                }
                return true;
            }
            const bool holding=modernHolding_,moved=modernTouchMoved_,wasBackward=backward_;cancelModernControls();
            if(wasBackward){ReadingProgress::save(session,preferences,true,nowMs);return true;}
            if(holding){requestPause(preferences,nowMs);return true;}
            // The touch began with the words running; the pause asked earlier may have landed during the flick.
            if(moved && modernPressedPlaying_
               && reading::isRewindSwipe(modernMinDx_,modernMaxAbsDy_,nowMs-modernPressedAtMs_)){
                rewindSentenceSwipe(preferences,nowMs);
                return true;
            }
            if(!moved && (!settings_.holdToRead || modernPressedPlaying_)
               && session.playing==modernPressedPlaying_) toggle(preferences,nowMs);
            return true;
        }
        if(!released)return true;
        const auto control=static_cast<ReaderControl>(modernControl_);modernControl_=0;
        if(modernTouchMoved_ || control==ReaderControl::None
           || control!=modern::readerControlAt(touch->x,touch->y,settings_,session.playing,
                                               ReadingLoop::wordCount(session)>0,!ui.focusBadge().empty())) return true;
        switch(control){
        case ReaderControl::Chapter:finishPause(preferences,nowMs);openLocator(screen);break;
        case ReaderControl::Focus:finishPause(preferences,nowMs);screen=Screen::FocusSession;break;
        case ReaderControl::Footer:
            if(!session.playing){settings_.footerMetric=settings::cycleEnum(settings_.footerMetric);store.acceptChanges();}break;
        default:break;
        }
        return true;
    }
}
