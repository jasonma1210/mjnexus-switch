/*
 * SettingsPage.hpp
 * -------------------------------------------------------------------
 * 设置页 —— 列出所有 AppSettings 字段，用 brls::ListItem + Slider + Toggle
 * 呈现，保存到 App::apply_settings() + SettingsStore::save()。
 *
 * 输入：
 *   B → 返回 HomePage（自动保存）
 *   D-Pad / 摇杆 → 在列表项之间移动
 *   A / X → 进入当前项的编辑（弹出 mini panel 或直接 +/-）
 *
 * 编码：UTF-8
 * -------------------------------------------------------------------
 */

#ifndef MJNEXUS_SETTINGS_PAGE_HPP
#define MJNEXUS_SETTINGS_PAGE_HPP

#if __has_include(<borealis.hpp>)
#include <borealis.hpp>
#define MJNEXUS_HAS_BOREALIS 1
#else
#define MJNEXUS_HAS_BOREALIS 0
#endif

#include "../../include/mjnexus/Config.hpp"

#include <string>

namespace mjnexus {

class SettingsPage :
#if MJNEXUS_HAS_BOREALIS
    public brls::View
#else
    public
#endif
{
public:
    SettingsPage();
    ~SettingsPage() override;

#if MJNEXUS_HAS_BOREALIS
    void draw(NVGcontext* vg, float x, float y) override;
    void update(brls::View* view, brls::ControllerButton button, bool pressed) override;
#endif

private:
    /* ============================================================
     * 设置项枚举 —— 决定 m_focusIndex 映射到哪个字段
     * ============================================================ */
    enum class SettingId : int {
        THEME = 0,
        FONT_SIZE,
        DEFAULT_READ_MODE,
        SORT_BY,
        NATURAL_SORT,
        ABOUT,
        COUNT
    };

    /* 当前焦点（对应 SettingId 的 int 值） */
    int m_focusIndex;

    /* 是否处于编辑子模式（某些项需要左右键调 +/-，不是简单 toggle） */
    bool m_editing;

    /* 从 App 拉到本地的 "正在编辑副本" —— 保存时才 commit */
    AppSettings m_workSettings;

    /* 应用版本号（静态常量） */
    static constexpr const char* APP_VERSION = "0.1.0";
    static constexpr const char* APP_NAME    = "MJ Reader for Switch";

    /* 应用设置（commit 到 App + SettingsStore） */
    void commit_settings();

    /* 画每一项（传入行号、高度、是否选中） */
    void draw_item_row(NVGcontext* vg, float y, SettingId id, bool selected);

    /* 辅助：排序字段 label 数组 */
    static const char* sort_label(int sortBy);
    static const char* read_mode_label(int mode);
};

} /* namespace mjnexus */

#endif /* MJNEXUS_SETTINGS_PAGE_HPP */
