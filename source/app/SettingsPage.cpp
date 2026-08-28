/*
 * SettingsPage.cpp
 * -------------------------------------------------------------------
 * 设置页实现。
 *
 * 设计：
 *   - 进入时从 App::get().get_settings() 拉一份 AppSettings 副本
 *     放在 m_workSettings 里，所有修改都在副本上做
 *   - commit_settings() 时才写回 App::apply_settings() + SettingsStore::save()
 *   - 返回（B 键）时自动 commit
 *   - 顶部标题栏 + 列表项 + 按键提示条（和 HomePage 风格一致）
 *
 * 每项的交互：
 *   THEME          : A/X 或 Left/Right toggle（浅色 ↔ 深色）
 *   FONT_SIZE      : Left/Right ±1（夹取 10-24）
 *   DEFAULT_READ_MODE: Left/Right 在 4 种模式间循环
 *   SORT_BY        : Left/Right 在 3 种排序间循环
 *   NATURAL_SORT   : toggle on/off
 *   ABOUT          : 只读，不响应按键
 *
 * 编码：UTF-8
 * -------------------------------------------------------------------
 */

#include "SettingsPage.hpp"
#include "App.hpp"

#include "../storage/SettingsStore.hpp"

#if __has_include(<borealis.hpp>)
#include <borealis.hpp>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mjnexus {

/* ============================================================
 * 静态常量 label
 * ============================================================ */

const char* SettingsPage::sort_label(int sortBy) {
    switch (sortBy) {
        case AppSettings::SORT_NAME:   return "按名称";
        case AppSettings::SORT_RECENT: return "最近阅读";
        case AppSettings::SORT_ADDED:  return "添加时间";
        default: return "按名称";
    }
}

const char* SettingsPage::read_mode_label(int mode) {
    switch ((ReadMode)mode) {
        case ReadMode::PORTRAIT:        return "竖屏";
        case ReadMode::LANDSCAPE:       return "横屏";
        case ReadMode::VERTICAL_FIT:    return "垂直适应";
        case ReadMode::SPREAD_TWO_PAGE: return "双页";
        default: return "竖屏";
    }
}

/* ============================================================
 * 构造 / 析构
 * ============================================================ */

SettingsPage::SettingsPage()
    : m_focusIndex(0)
    , m_editing(false)
    , m_workSettings(App::get().get_settings())
{
}

SettingsPage::~SettingsPage() {
    // 析构时不一定需要 commit（B 键返回已经 commit 了）
    // 但为了健壮，这里再 commit 一次 —— SettingsStore::save 会覆盖
    // 同一份文件，幂等
    commit_settings();
}

/* ============================================================
 * commit_settings —— 把 m_workSettings 写回 App + SettingsStore
 *
 * 也负责：
 *   - 切换主题时通知 App 调 switch_theme
 *   - 切换到 TextRenderer 读书时通知 set_font_size / set_theme
 * ============================================================ */

void SettingsPage::commit_settings() {
    // 夹紧
    m_workSettings.fontSize = std::clamp(m_workSettings.fontSize, 10, 24);
    if (m_workSettings.theme < 0 || m_workSettings.theme > 1)
        m_workSettings.theme = (int)Theme::LIGHT;

    App::get().apply_settings(m_workSettings);
    SettingsStore::instance().save(m_workSettings);

    std::fprintf(stdout, "[mjnexus] SettingsPage: committed theme=%d fontSize=%d mode=%d sort=%d natural=%d\n",
                 m_workSettings.theme, m_workSettings.fontSize,
                 m_workSettings.defaultReadMode, m_workSettings.sortBy,
                 m_workSettings.naturalSort);
}

/* ============================================================
 * draw_item_row —— 画一行设置项
 *
 * 布局：
 *   行高 80px
 *   左侧图标占位（56x56 色块）+ 标题 + 值（右侧）
 *   选中项：反色 + 3px 强调边框
 * ============================================================ */

void SettingsPage::draw_item_row(NVGcontext* vg, float y, SettingId id, bool selected) {
    auto& theme = MjnexusTheme::current();

    const float rowH  = 80.0f;
    const float rowX  = 60.0f;
    const float rowW  = (float)NINTENDO_SWITCH_SCREEN_W - 120.0f;

    // 行背景
    nvgBeginPath(vg);
    nvgRoundedRect(vg, rowX, y, rowW, rowH, 10.0f);
    if (selected) {
        nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 240));
    } else {
        nvgFillColor(vg, nvgRGBA(theme.color_card.r, theme.color_card.g, theme.color_card.b, 255));
    }
    nvgFill(vg);

    // 选中项强调边框
    if (selected) {
        nvgStrokeColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);
    } else {
        nvgBeginPath(vg);
        nvgRect(vg, rowX, y + rowH - 1.0f, rowW, 1.0f);
        nvgFillColor(vg, nvgRGBA(theme.color_border.r, theme.color_border.g, theme.color_border.b, 200));
        nvgFill(vg);
    }

    // 左侧 icon 色块（按 id 用不同颜色）
    float iconX = rowX + 16.0f;
    float iconY = y + 12.0f;
    float iconS = 56.0f;

    uint8_t colorR = 0, colorG = 0, colorB = 0;
    switch (id) {
        case SettingId::THEME:            colorR = 0x33; colorG = 0x66; colorB = 0xcc; break;
        case SettingId::FONT_SIZE:        colorR = 0xcc; colorG = 0x66; colorB = 0x33; break;
        case SettingId::DEFAULT_READ_MODE:colorR = 0x33; colorG = 0xcc; colorB = 0x66; break;
        case SettingId::SORT_BY:          colorR = 0xcc; colorG = 0x33; colorB = 0xcc; break;
        case SettingId::NATURAL_SORT:     colorR = 0xcc; colorG = 0xcc; colorB = 0x33; break;
        case SettingId::ABOUT:            colorR = 0x88; colorG = 0x88; colorB = 0x88; break;
        default: break;
    }
    nvgBeginPath(vg);
    nvgRoundedRect(vg, iconX, iconY, iconS, iconS, 8.0f);
    nvgFillColor(vg, nvgRGBA(colorR, colorG, colorB, 220));
    nvgFill(vg);

    // icon 字母占位
    char iconChar = '?';
    switch (id) {
        case SettingId::THEME:             iconChar = 'T'; break;
        case SettingId::FONT_SIZE:         iconChar = 'F'; break;
        case SettingId::DEFAULT_READ_MODE: iconChar = 'R'; break;
        case SettingId::SORT_BY:           iconChar = 'S'; break;
        case SettingId::NATURAL_SORT:      iconChar = 'N'; break;
        case SettingId::ABOUT:             iconChar = 'i'; break;
        default: break;
    }
    nvgFontSize(vg, 28.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 230));
    char buf[8] = {0};
    buf[0] = iconChar;
    nvgText(vg, iconX + iconS * 0.5f, iconY + iconS * 0.5f, buf, nullptr);

    // 主 label
    const char* title = "";
    const char* value = "";
    std::string valueStr;

    switch (id) {
        case SettingId::THEME: {
            title = "主题";
            value = m_workSettings.theme == (int)Theme::DARK ? "深色" : "浅色";
            break;
        }
        case SettingId::FONT_SIZE: {
            title = "字号";
            char tmp[16];
            std::snprintf(tmp, sizeof(tmp), "%d pt", m_workSettings.fontSize);
            valueStr = tmp;
            value = valueStr.c_str();
            break;
        }
        case SettingId::DEFAULT_READ_MODE: {
            title = "默认阅读模式";
            value = read_mode_label(m_workSettings.defaultReadMode);
            break;
        }
        case SettingId::SORT_BY: {
            title = "排序方式";
            value = sort_label(m_workSettings.sortBy);
            break;
        }
        case SettingId::NATURAL_SORT: {
            title = "自然排序";
            value = m_workSettings.naturalSort ? "开启" : "关闭";
            break;
        }
        case SettingId::ABOUT: {
            title = "关于";
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%s v%s", APP_NAME, APP_VERSION);
            valueStr = tmp;
            value = valueStr.c_str();
            break;
        }
        default: break;
    }

    // 主 label（左侧）
    nvgFontSize(vg, 24.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, selected
        ? nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, 255)
        : nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
    nvgText(vg, iconX + iconS + 24.0f, y + rowH * 0.4f, title, nullptr);

    // 值（右侧）
    nvgFontSize(vg, 22.0f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, selected
        ? nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, 220)
        : nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
    nvgText(vg, rowX + rowW - 24.0f, y + rowH * 0.4f, value, nullptr);
}

/* ============================================================
 * draw —— 主渲染循环
 * ============================================================ */

void SettingsPage::draw(NVGcontext* vg, float /*x*/, float /*y*/) {
    auto& theme = MjnexusTheme::current();

    // 1) 背景
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)NINTENDO_SWITCH_SCREEN_W, (float)NINTENDO_SWITCH_SCREEN_H);
    nvgFillColor(vg, nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, 255));
    nvgFill(vg);

    // 2) 顶栏（120px）
    const float barH = 120.0f;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)NINTENDO_SWITCH_SCREEN_W, barH);
    nvgFillColor(vg, nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, 255));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, 0, barH - 1.0f, (float)NINTENDO_SWITCH_SCREEN_W, 1.0f);
    nvgFillColor(vg, nvgRGBA(theme.color_border.r, theme.color_border.g, theme.color_border.b, 255));
    nvgFill(vg);

    nvgFontSize(vg, 44.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
    nvgText(vg, 40.0f, barH * 0.5f, "设置", nullptr);

    // 3) 列表
    SettingId ids[] = {
        SettingId::THEME,
        SettingId::FONT_SIZE,
        SettingId::DEFAULT_READ_MODE,
        SettingId::SORT_BY,
        SettingId::NATURAL_SORT,
        SettingId::ABOUT,
    };
    int count = (int)(sizeof(ids) / sizeof(ids[0]));

    const float startY = barH + 40.0f;
    const float rowH   = 80.0f;
    const float rowGap = 12.0f;

    for (int i = 0; i < count; ++i) {
        bool selected = (i == m_focusIndex);
        draw_item_row(vg, startY + i * (rowH + rowGap), ids[i], selected);
    }

    // 4) 按键提示底栏
    const float bottomH = 60.0f;
    const float bottomY = (float)NINTENDO_SWITCH_SCREEN_H - bottomH;
    nvgBeginPath(vg);
    nvgRect(vg, 0, bottomY, (float)NINTENDO_SWITCH_SCREEN_W, bottomH);
    nvgFillColor(vg, nvgRGBA(theme.color_sidebar.r, theme.color_sidebar.g, theme.color_sidebar.b, 255));
    nvgFill(vg);

    nvgFontSize(vg, 20.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
    nvgText(vg, 40.0f, bottomY + bottomH * 0.5f,
            "A: Edit    L/R: +/-    D-Pad U/D: Navigate    B: Save & Back", nullptr);
}

/* ============================================================
 * update —— 按键事件
 *
 * 编辑模式：某些项（字号）在 Left/Right 按下时直接 +/-
 * 其他项：Left/Right 循环切换
 * B 键 → commit + popView
 * ============================================================ */

void SettingsPage::update(brls::View* /*view*/, brls::ControllerButton button, bool pressed) {
    if (!pressed) return;

    int maxCount = (int)SettingId::COUNT;

    switch (button) {
        case brls::ControllerButton::BUTTON_UP:
            m_focusIndex -= 1;
            if (m_focusIndex < 0) m_focusIndex = maxCount - 1;
            return;

        case brls::ControllerButton::BUTTON_DOWN:
            m_focusIndex += 1;
            if (m_focusIndex >= maxCount) m_focusIndex = 0;
            return;

        case brls::ControllerButton::BUTTON_LEFT:
        case brls::ControllerButton::BUTTON_RIGHT: {
            bool inc = (button == brls::ControllerButton::BUTTON_RIGHT);
            auto id = (SettingId)m_focusIndex;
            switch (id) {
                case SettingId::THEME: {
                    m_workSettings.theme = (inc)
                        ? (int)Theme::DARK : (int)Theme::LIGHT;
                    break;
                }
                case SettingId::FONT_SIZE: {
                    if (inc) m_workSettings.fontSize += 1;
                    else     m_workSettings.fontSize -= 1;
                    m_workSettings.fontSize = std::clamp(m_workSettings.fontSize, 10, 24);
                    break;
                }
                case SettingId::DEFAULT_READ_MODE: {
                    if (inc) m_workSettings.defaultReadMode =
                        (m_workSettings.defaultReadMode + 1) % 4;
                    else     m_workSettings.defaultReadMode =
                        (m_workSettings.defaultReadMode + 3) % 4;
                    break;
                }
                case SettingId::SORT_BY: {
                    if (inc) m_workSettings.sortBy =
                        (m_workSettings.sortBy + 1) % 3;
                    else     m_workSettings.sortBy =
                        (m_workSettings.sortBy + 2) % 3;
                    break;
                }
                case SettingId::NATURAL_SORT: {
                    m_workSettings.naturalSort = !m_workSettings.naturalSort;
                    break;
                }
                case SettingId::ABOUT:
                    // 只读，忽略
                    break;
                default: break;
            }
            return;
        }

        case brls::ControllerButton::BUTTON_A: {
            // A 键行为：对于 toggle 类项（theme / natural sort），toggle
            // 对于 step 类项（font size / read mode / sort），等价于 Right 一次
            auto id = (SettingId)m_focusIndex;
            switch (id) {
                case SettingId::THEME:
                    m_workSettings.theme = (m_workSettings.theme == (int)Theme::LIGHT)
                        ? (int)Theme::DARK : (int)Theme::LIGHT;
                    break;
                case SettingId::NATURAL_SORT:
                    m_workSettings.naturalSort = !m_workSettings.naturalSort;
                    break;
                case SettingId::FONT_SIZE:
                    m_workSettings.fontSize = std::clamp(m_workSettings.fontSize + 1, 10, 24);
                    break;
                case SettingId::DEFAULT_READ_MODE:
                    m_workSettings.defaultReadMode =
                        (m_workSettings.defaultReadMode + 1) % 4;
                    break;
                case SettingId::SORT_BY:
                    m_workSettings.sortBy = (m_workSettings.sortBy + 1) % 3;
                    break;
                case SettingId::ABOUT:
                    break;
                default: break;
            }
            return;
        }

        case brls::ControllerButton::BUTTON_B: {
            commit_settings();
            brls::Application::popView();
            return;
        }

        default:
            break;
    }
}

} /* namespace mjnexus */
