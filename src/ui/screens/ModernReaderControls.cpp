#include "ui/screens/ReaderScreen.h"
#include "ui/screens/ModernReaderChrome.h"

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
            modernScrub_=false;backward_=false;
            modernPressedAtMs_=nowMs;modernPressedPlaying_=session.playing;
            return true;
        }
        const int dx=int(touch->x)-modernTouchX_,dy=int(touch->y)-modernTouchY_;
        modernTouchMoved_|=std::abs(dx)>18 || std::abs(dy)>18;
        const bool released=ui::hasTouch(*touch,ui::TouchRelease);
        if(modernCanvasTouch_){
            // Gestures on the word canvas (y 36..136):
            //  * hold still (<= kScrubSlopPx): right half reads forward, LEFT half (x<320) rewinds (holdToRead only);
            //  * move horizontally beyond kScrubSlopPx: scrub word by word with the finger — ONLY when the touch
            //    started with playback paused (user request lume.5: hold/drag only while paused);
            //  * vertical moves keep cancelling hold (no scrub); tap toggles play. Top/bottom bars are separate controls.
            if(!modernScrub_ && !modernHolding_ && !backward_ && !modernPressedPlaying_ && !session.playing
               && ReadingLoop::wordCount(session)>0 && reading::isScrubMove(dx,dy)){
                modernScrub_=true;scrubStartWord_=session.state.wordIndex;scrubAnchorX_=touch->x;
            }
            if(modernScrub_){
                const size_t target=reading::scrubTarget(scrubStartWord_,int(touch->x)-int(scrubAnchorX_),
                                                         ReadingLoop::wordCount(session));
                if(target!=session.state.wordIndex)ReadingLoop::seekTo(session,target);
                const auto* chapter=session.metadata.chapterAt(session.state.wordIndex);
                const unsigned pct=reading::percentOf(session.state.wordIndex,
                                                      static_cast<uint32_t>(ReadingLoop::wordCount(session)));
                scrubText_=chapter?"Cap. "+std::to_string(chapter-session.metadata.chapters.data()+1)+" · "+std::to_string(pct)+"%"
                                  :std::to_string(pct)+"%";
                if(released){modernScrub_=false;scrubText_.clear();ReadingProgress::save(session,preferences,true,nowMs);cancelModernControls();}
                return true;
            }
            if(modernTouchMoved_ && (modernHolding_||backward_)){
                if(backward_)stopBackward(preferences,nowMs);else finishPause(preferences,nowMs);
                modernHolding_=false;
            }
            if(!released){
                if(!modernTouchMoved_ && settings_.holdToRead && !modernPressedPlaying_ && !modernHolding_ && !backward_
                   && nowMs-modernPressedAtMs_>=350){
                    if(modernTouchX_<320){
                        backward_=true;modernHolding_=true;backwardStartMs_=nowMs;session.lastAdvanceMs=nowMs;
                        ReadingLoop::pause(session);pauseAtSentenceEndRequested_=false;
                    }else{modernHolding_=true;start(nowMs,false);}
                }
                return true;
            }
            const bool holding=modernHolding_,moved=modernTouchMoved_,wasBackward=backward_;cancelModernControls();
            if(wasBackward){ReadingProgress::save(session,preferences,true,nowMs);return true;}
            if(holding){requestPause(preferences,nowMs);return true;}
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
