#pragma once

#include "Config.hpp"

// Borealis 头文件（Switch 开发环境下这些头位于 libdependencies 或 bor 子模块）
// 本文件会 #include <borealis.hpp>。当外部没有 Borealis 时，我们也提供
// 一个可独立编译的 Fallback 路径：只使用 mjnexus::Color 结构体。
#if __has_include(<borealis.hpp>)
#include <borealis.hpp>
#define MJNEXUS_HAS_BOREALIS 1
#else
#define MJNEXUS_HAS_BOREALIS 0
#endif

namespace mjnexus {

// ============================================================
// MjnexusTheme —— MJNexus 的两套主题（浅色 / 深色）
//
// 设计思路：
//   - 如果 Borealis 可用，则在 brls::Theme 上覆写所有 MJNexus 关心的颜色槽；
//     没覆写的槽（比如 brls::COLOR_HIGHLIGHT）会保留 Borealis 的默认值。
//   - 如果 Borealis 不可用（例如在 PC 上做逻辑单元测试），我们退化为
//     一个简单的 POD 结构体，持有 mjnexus::Color。
// ============================================================
class MjnexusTheme
#if MJNEXUS_HAS_BOREALIS
    : public brls::Theme
#endif
{
public:
    // 六个主色 —— 无论是否有 Borealis，这几个字段始终存在
    Color color_bg;
    Color color_ink;
    Color color_border;
    Color color_sidebar;
    Color color_card;
    Color color_muted;
    Color color_primary_accent;   // 可选，留空时由 UI 层自行决定

    // 构造函数：直接传入 6 种颜色
    MjnexusTheme(Color bg, Color ink, Color border,
                 Color sidebar, Color card, Color muted,
                 Color accent = Color{0, 0, 0, 0});

    // 静态工厂：根据 LightPalette 构造浅色主题
    static MjnexusTheme make_light();

    // 静态工厂：根据 DarkPalette 构造深色主题
    static MjnexusTheme make_dark();

#if MJNEXUS_HAS_BOREALIS
    // 把当前主题应用到 brls::Application 的全局主题槽
    // 内部会做 brls::Color ↔ mjnexus::Color 的逐通道拷贝
    void apply_to_borealis();
#endif

    // 取某套颜色（light / dark）
    static const MjnexusTheme& current();

private:
    // 当用户没有主动切换主题时，默认用浅色
    static Theme s_current_theme;
};

// ============================================================
// 内联实现
// ============================================================

inline MjnexusTheme::MjnexusTheme(Color bg, Color ink, Color border,
                                   Color sidebar, Color card, Color muted,
                                   Color accent)
    : color_bg(bg), color_ink(ink), color_border(border),
      color_sidebar(sidebar), color_card(card), color_muted(muted),
      color_primary_accent(accent) {}

inline MjnexusTheme MjnexusTheme::make_light() {
    return MjnexusTheme(
        LightPalette::bg, LightPalette::ink, LightPalette::border,
        LightPalette::sidebar, LightPalette::card, LightPalette::muted
    );
}

inline MjnexusTheme MjnexusTheme::make_dark() {
    return MjnexusTheme(
        DarkPalette::bg, DarkPalette::ink, DarkPalette::border,
        DarkPalette::sidebar, DarkPalette::card, DarkPalette::muted
    );
}

inline Theme MjnexusTheme::s_current_theme = Theme::LIGHT;

inline const MjnexusTheme& MjnexusTheme::current() {
    static const MjnexusTheme light_cache = make_light();
    static const MjnexusTheme dark_cache  = make_dark();
    return (s_current_theme == Theme::DARK) ? dark_cache : light_cache;
}

#if MJNEXUS_HAS_BOREALIS
inline void MjnexusTheme::apply_to_borealis() {
    // 把 mjnexus::Color 的每个通道拷贝到 brls::Color 对应的槽
    // brls::Color 构造函数签名：Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    auto copy = [](const mjnexus::Color& c) -> brls::Color {
        return brls::Color(c.r, c.g, c.b, c.a);
    };

    // Borealis 的主题槽（borealis 的 brls::Theme 提供以下颜色字段）
    // 如果某字段不存在，会编译失败；Switch 环境下通常都存在
    this->bgColor                  = copy(color_bg);
    this->textColor                = copy(color_ink);
    this->listItemSeparatorColor   = copy(color_border);
    this->sidebarColor             = copy(color_sidebar);
    this->sidebarTextColor         = copy(color_ink);
    this->listItemBgColor          = copy(color_card);
    this->listItemSelectedColor    = copy(color_ink);   // 选中项反色
    this->listItemSelectedTextColor= copy(color_bg);
    this->highlightBackgroundColor = copy(color_card);
    this->highlightTextColor       = copy(color_ink);
    this->descriptionColor         = copy(color_muted);

    // 应用到全局
    brls::Application::setActiveTheme(*this);
}
#endif

} // namespace mjnexus
