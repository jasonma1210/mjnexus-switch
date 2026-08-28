#pragma once
#include <borealis.hpp>
#include <nanovg.h>
#include <string>

namespace mjnexus
{
    // 简化版：直接用 brls 主题的 string key
    inline NVGcolor getBgColor() {
        return brls::Theme::getDarkTheme().getColor("bgColor");
    }
    inline NVGcolor getTextColor() {
        return brls::Theme::getDarkTheme().getColor("textColor");
    }
    inline NVGcolor getAccentColor() {
        return brls::Theme::getDarkTheme().getColor("accentColor");
    }
}
