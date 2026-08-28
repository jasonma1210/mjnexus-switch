/*
 * HomePage.hpp
 * -------------------------------------------------------------------
 * 书架主界面 —— 继承自 brls::View，以 Borealis 电视范式呈现
 *   - 顶部标题栏 "MJ Reader"
 *   - "最近阅读" 分区（封面卡片网格）
 *   - "全部书籍" 分区（brls::ListItem 列表 + 封面缩略图）
 *   - 无书占位提示
 *
 * 输入映射：
 *   A → 打开选中的书 → 推入 ReaderPage
 *   B → 无（首页）
 *   X → 推入 SettingsPage
 *   Y → 重新扫描书架
 *   D-Pad / 摇杆 → 在分区内部移动焦点
 *
 * 编码：UTF-8
 * 作者：mjnexus-switch 项目
 * -------------------------------------------------------------------
 */

#ifndef MJNEXUS_HOME_PAGE_HPP
#define MJNEXUS_HOME_PAGE_HPP

#if __has_include(<borealis.hpp>)
#include <borealis.hpp>
#define MJNEXUS_HAS_BOREALIS 1
#else
#define MJNEXUS_HAS_BOREALIS 0
#endif

#include "../../include/mjnexus/Config.hpp"

#include <string>
#include <vector>

namespace mjnexus {

class HomePage :
#if MJNEXUS_HAS_BOREALIS
    public brls::View
#else
    public
#endif
{
public:
    /* 构造：立即扫描书架、加载进度、根据设置排序。
     * Switch 上阻塞扫描通常 < 1 秒（几百本书以内）。 */
    HomePage();
    ~HomePage() override;

    /* ============================================================
     * brls::View 虚函数
     * ============================================================ */
#if MJNEXUS_HAS_BOREALIS
    void draw(NVGcontext* vg, float x, float y) override;
    void update(brls::View* view, brls::ControllerButton button, bool pressed) override;
#endif

    /* ============================================================
     * 国际化（静态 map，zh-CN / en 两套）
     * ============================================================ */
    static const char* get_i18n_value(const std::string& key);

private:
    /* ============================================================
     * 私有数据
     * ============================================================ */

    /* 扫描到的全部书籍 */
    std::vector<BookInfo> m_books;

    /* 最近阅读的书籍子集（lastReadTimestamp 降序） */
    std::vector<BookInfo> m_recent;

    /* 当前整体焦点：
     *   0 .. m_recent.size()-1        → recent 封面卡片
     *   m_recent.size() .. m_all-1     → all 列表项
     *   m_all                          → "无书" 占位 / 无效
     */
    int m_focusIndex = 0;

    /* 当前高亮分区："recent" 或 "all" —— 绘制时用来决定
     * 分区标题的强调色 + D-Pad 左右切换时跳转 */
    std::string m_currentSection = "recent";

    /* 上一次的书籍总数 —— 用来判断扫描是否触发了 UI 刷新 */
    size_t m_lastBookCount = 0;

    /* ============================================================
     * 私有辅助方法
     * ============================================================ */

    /* 重跑 BookLibrary::scan_books + 应用排序 + 刷新 recent 子集
     * 构造函数和 Y 键都会调用 */
    void refresh_library();

    /* 根据 App::get().settings.sortBy 对 m_books 排序 */
    void apply_sort();

    /* 绘制顶部栏（标题 + 主题色适配） */
    void draw_top_bar(NVGcontext* vg, float y);

    /* 绘制 "最近阅读" 分区：
     *   标题 "最近阅读" + 4 个封面卡片（2x2 网格）
     *   卡片 = 封面图 + 书名 */
    void draw_recent_section(NVGcontext* vg, float x, float y, float w, float h);

    /* 绘制 "全部书籍" 分区：
     *   标题 "全部书籍" + brls::ListItem 列表
     *   列表项：封面 thumbnail 左侧 + "书名 - 作者" 右侧 */
    void draw_all_section(NVGcontext* vg, float x, float y, float w, float h);

    /* 无书占位：显示提示文本 + 图标占位 */
    void draw_empty_state(NVGcontext* vg);

    /* 判断 m_focusIndex 落在哪个分区（返回 "recent" 或 "all"） */
    std::string resolve_section(int index) const;

    /* 把 m_focusIndex 夹到合法范围，避免溢出 */
    void clamp_focus();
};

} /* namespace mjnexus */

#endif /* MJNEXUS_HOME_PAGE_HPP */
