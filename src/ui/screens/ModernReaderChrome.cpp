#include "ui/screens/ModernReaderChrome.h"
#include "ui/FormaSuaveGfx.h"
#include "ui/screens/OnboardingCopy.h"

namespace screens::modern {
    ReaderControl readerControlAt(uint16_t x, uint16_t y, const settings::ReadingSettings& config,
                                  bool reading, bool canRead, bool focusActive) {
        if (focusActive && ui::contains(readerControlRect(ReaderControl::Focus),x,y)) return ReaderControl::Focus;
        if (canRead && settings::visible(config.chapterVisibility,reading)
            && ui::contains(readerControlRect(ReaderControl::Chapter),x,y)) return ReaderControl::Chapter;
        if (settings::visible(config.progressVisibility,reading)
            && ui::contains(readerControlRect(ReaderControl::Footer),x,y)) return ReaderControl::Footer;
        return ReaderControl::None;
    }

    void readerChrome(ui::Context& ui, const readerLayout::Chrome& view,
                      const settings::ReadingSettings& config, const Board::Power::BatteryState& battery) {
        using namespace ui::themes;
        ui.label(readerControlRect(ReaderControl::Chapter), settings::visible(config.chapterVisibility,view.reading)
                 ? view.chapter : std::string_view{},11,Muted,ui::TextAlign::Start,1,view.locale);
        ui.label(readerControlRect(ReaderControl::Focus),ui.focusBadge(),12,Accent,ui::TextAlign::Center);
        ui.battery({514,0,116,36},battery.status.percent,battery.charging,
                   settings::visible(config.batteryLabelVisibility,view.reading)?view.batteryLabel:std::string_view{},
                   settings::visible(config.batteryIconVisibility,view.reading));
        ui.label({124,136,260,34},!view.scrubText.empty()?std::string{view.scrubText}
                 :view.reading&&view.pausePending?std::string{ui.text(UiText::Pause)}+"…":std::string{},11,
                 view.scrubText.empty()?Muted:Accent);
        // Rewind indicator (hold-left): small left-pointing triangle drawn with primitives + "voltando".
        const ui::Rect badge{12,136,108,34};
        if(ui.redraw(badge,view.backward?1u:0u)){
            Arduino_GFX& gfx=ui.gfx();
            gfx.fillRect(badge.x,badge.y,badge.w,badge.h,ui.color(Background));
            if(view.backward){
                const int16_t cy=badge.y+badge.h/2;
                ui::forma::triangulo(gfx,badge.x+2,cy,badge.x+14,cy-7,badge.x+14,cy+7,ui.color(Accent),ui.color(Background));
                ui::forma::triangulo(gfx,badge.x+12,cy,badge.x+24,cy-7,badge.x+24,cy+7,ui.color(Accent),ui.color(Background));
            }
        }
        ui.label({40,136,84,34},view.backward?"voltando":"",11,Accent,ui::TextAlign::Start);
        const bool progress=settings::visible(config.progressVisibility,view.reading);
        ui.label(readerControlRect(ReaderControl::Footer),progress?view.footer:std::string_view{},11,Muted,ui::TextAlign::Right);
        ui.progress({0,170,640,2},progress?view.percent:0);
    }
}
