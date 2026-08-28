#include "HomePage.hpp"

namespace mjnexus
{
    HomePage::HomePage() : brls::Box()
    {
        // 首页最小骨架
    }

    void HomePage::draw(NVGcontext* vg, float x, float y, float width, float height,
                        brls::Style style, brls::FrameContext* ctx)
    {
        brls::Box::draw(vg, x, y, width, height, style, ctx);

        // 临时占位：画个背景色
        NVGcolor bg = brls::Application::getTheme()
            ? brls::Application::getTheme()->getColor("bgColor")
            : nvgRGBA(20, 20, 20, 255);

        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, bg);
        nvgFill(vg);

        // 临时标题
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgFontSize(vg, 40);
        nvgFontFace(vg, "standard");
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, x + width / 2, y + height / 2 - 40, "MJ Reader", nullptr);

        nvgFontSize(vg, 22);
        nvgText(vg, x + width / 2, y + height / 2 + 10, "Borealis moonlight_wiliwili", nullptr);
        nvgText(vg, x + width / 2, y + height / 2 + 40, "Press A to continue...", nullptr);
    }
}
