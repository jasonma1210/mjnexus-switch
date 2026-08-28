#include "HomePage.hpp"

namespace mjnexus
{
    HomePage::HomePage() : brls::Box()
    {
        brls::Logger::info("HomePage created");
    }

    void HomePage::draw(NVGcontext* vg, float x, float y, float width, float height,
                        brls::Style style, brls::FrameContext* ctx)
    {
        brls::Box::draw(vg, x, y, width, height, style, ctx);

        // 背景（固定深色，避免 getTheme 问题）
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBA(20, 20, 20, 255));
        nvgFill(vg);

        // 标题
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgFontSize(vg, 48);
        nvgFontFace(vg, "standard");
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, x + width / 2, y + height / 2 - 60, "MJ Reader", nullptr);

        nvgFontSize(vg, 20);
        nvgFillColor(vg, nvgRGBA(180, 180, 180, 255));
        nvgText(vg, x + width / 2, y + height / 2 - 15, "Nintendo Switch Homebrew", nullptr);
        nvgText(vg, x + width / 2, y + height / 2 + 20, "Borealis moonlight_wiliwili", nullptr);
        nvgText(vg, x + width / 2, y + height / 2 + 55, "Build skeleton - v2", nullptr);

        brls::Logger::info("HomePage draw at {:.0f}x{:.0f}", width, height);
    }
}
