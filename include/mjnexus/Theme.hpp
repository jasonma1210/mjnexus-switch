#pragma once
#include <borealis.hpp>
#include <nanovg.h>
#include <string>

namespace mjnexus
{
    // 颜色 key 常量
    namespace ColorKeys
    {
        constexpr const char* BG = "bgColor";
        constexpr const char* TEXT = "textColor";
        constexpr const char* ACCENT = "accentColor";
        constexpr const char* LIST_SEP = "listItemSeparatorColor";
        constexpr const char* SIDEBAR_BG = "sidebarColor";
        constexpr const char* SIDEBAR_TEXT = "sidebarTextColor";
        constexpr const char* LIST_BG = "listItemBgColor";
        constexpr const char* LIST_SEL = "listItemSelectedColor";
        constexpr const char* LIST_SEL_TEXT = "listItemSelectedTextColor";
        constexpr const char* HIGHLIGHT_BG = "highlightBackgroundColor";
        constexpr const char* HIGHLIGHT_TEXT = "highlightTextColor";
        constexpr const char* DESC = "descriptionColor";
    }

    inline NVGcolor darken(NVGcolor c, float factor) {
        return nvgRGBAf(c.r * factor, c.g * factor, c.b * factor, c.a);
    }

    inline NVGcolor lighten(NVGcolor c, float factor) {
        return nvgRGBAf(1.0f - (1.0f - c.r) * factor,
                        1.0f - (1.0f - c.g) * factor,
                        1.0f - (1.0f - c.b) * factor,
                        c.a);
    }

    inline NVGcolor getBgColor() {
        return brls::Application::getTheme() ? brls::Application::getTheme()->getColor(ColorKeys::BG) : nvgRGBA(20, 20, 20, 255);
    }
}
