#pragma once
#include <borealis.hpp>
#include <nanovg.h>
#include <string>

namespace mjnexus
{
    namespace ColorKeys
    {
        constexpr const char* BG = "bgColor";
        constexpr const char* TEXT = "textColor";
        constexpr const char* ACCENT = "accentColor";
    }

    inline NVGcolor getBgColor() {
        return brls::Theme::getDarkTheme().getColor(BG);
    }

    inline NVGcolor getTextColor() {
        return brls::Theme::getDarkTheme().getColor(TEXT);
    }
}
