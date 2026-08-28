#include "SettingsPage.hpp"

namespace mjnexus
{
    SettingsPage::SettingsPage() : brls::Box() {}

    void SettingsPage::draw(NVGcontext* vg, float x, float y, float width, float height,
                            brls::Style style, brls::FrameContext* ctx)
    {
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgFontSize(vg, 32);
        nvgFontFace(vg, "standard");
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, x + width / 2, y + height / 2, "Settings (TODO)", nullptr);
    }
}
