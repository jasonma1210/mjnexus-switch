#include "ReaderPage.hpp"

namespace mjnexus
{
    ReaderPage::ReaderPage(const std::string& path) : brls::Box(), bookPath(path) {}

    void ReaderPage::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx)
    {
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgFontSize(vg, 32);
        nvgFontFace(vg, "standard");
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        std::string title = "Reader: " + (bookPath.empty() ? "(no book)" : bookPath);
        nvgText(vg, x + width / 2, y + height / 2, title.c_str(), nullptr);
    }
}
