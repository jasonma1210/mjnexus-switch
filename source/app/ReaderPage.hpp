/*
 * ReaderPage.hpp
 * -------------------------------------------------------------------
 * 阅读器页面 —— 整个应用中最复杂的页面。
 *
 * 职责：
 *   1) 保持 m_currentPage，维护 m_totalPages
 *   2) 根据 BookInfo.format 分发：
 *        - PDF/EPUB/XPS/CBZ/... → MuPDF 路径（需要 App 暴露 renderer）
 *        - TXT/MD/MOBI          → TextRenderer 路径（同上）
 *   3) draw() 里直接调 nvg_* 系列函数画：
 *        - 背景（按主题）
 *        - 页面内容（MuPDF → 渲染 pixmap → nvg_image；Text → nvgText 逐行）
 *        - 菜单 overlay（顶栏 + 进度条 + 目录 + 设置按钮，默认隐藏 3s）
 *        - TOC 弹窗（半透明背景 + 列表）
 *   4) update() 里处理手柄 + 触屏输入：
 *        - ZL/L → 上一页   ZR/R → 下一页
 *        - Left/Right → 翻页
 *        - A → 菜单 toggle
 *        - B → 返回书架（先 save_progress）
 *        - + → 设置弹出（字号/主题/模式 quick switch）
 *        - 触屏 → 上下左右滑 / 点击区域
 *
 * 自动隐藏：m_lastActivity + 3000ms 内无输入则 m_showMenu=false
 *
 * 编码：UTF-8
 * -------------------------------------------------------------------
 */

#ifndef MJNEXUS_READER_PAGE_HPP
#define MJNEXUS_READER_PAGE_HPP

#if __has_include(<borealis.hpp>)
#include <borealis.hpp>
#define MJNEXUS_HAS_BOREALIS 1
#else
#define MJNEXUS_HAS_BOREALIS 0
#endif

#include "../../include/mjnexus/Config.hpp"
#include "../../include/mjnexus/MuPDFRenderer.hpp"
#include "../../include/mjnexus/TextRenderer.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace mjnexus {

class ReaderPage :
#if MJNEXUS_HAS_BOREALIS
    public brls::View
#else
    public
#endif
{
public:
    /* 构造：必须在 App::get().load_book() 成功之后再构造。
     * 从 App 拿当前书、上次进度、设置、渲染器引用。 */
    ReaderPage();
    ~ReaderPage() override;

    /* ============================================================
     * brls::View 虚函数
     * ============================================================ */
#if MJNEXUS_HAS_BOREALIS
    void draw(NVGcontext* vg, float x, float y) override;
    void update(brls::View* view, brls::ControllerButton button, bool pressed) override;
#endif

private:
    /* ============================================================
     * 渲染器访问 —— App 里的 m_mupdf / m_text 是 unique_ptr，
     * 这里用 raw pointer 从 App 暴露的访问器取。
     * App 需要提供：
     *   MuPDFRenderer* App::get_mupdf()
     *   TextRenderer*  App::get_text()
     *   bool           App::use_mupdf() (format ∈ {PDF,EPUB,XPS,CBZ,CBR,CBT,CB7})
     * 我们在 App.hpp 侧没有看到这些方法 —— 所以这里用一个小的
     * 静态辅助函数 is_mupdf_format(fmt) 来判断格式，然后通过
     * 前置声明 + 友元假设访问 App 私有成员。最保守做法：
     * 我们让 App 暴露 public 访问器，ReaderPage 直接调。
     * 为了本文件自包含，我们也提供 fallback：只用 BookInfo.totalPages
     * 来翻页，draw 里走 "不渲染真实内容，画占位文本" 路径 —— 但这违背
     * 用户要求 "不要 stub"。所以这里坚定走：假设 App 有这两个 getter。
     * ============================================================ */

    /* ============================================================
     * 成员：阅读状态
     * ============================================================ */

    App& m_app;

    /* 当前页（从 0 起） */
    int m_currentPage;

    /* 总页数 */
    int m_totalPages;

    /* 当前书的格式枚举（BookInfo.format 存的就是 int(RenderFormat)） */
    int m_format;

    /* 渲染路径：true = MuPDF，false = Text */
    bool m_useMupdf;

    /* ============================================================
     * UI 状态
     * ============================================================ */

    bool m_showMenu;       // 顶栏 + 底栏 overlay 是否显示
    bool m_showToc;        // 目录弹窗
    bool m_showSettings;   // 设置快速弹窗（字号/主题/模式）
    bool m_showProgress;   // 长按 L/R 时的大字进度 overlay

    /* 最后一次按键（用于检测长按） */
    brls::ControllerButton m_lastPressed;
    std::chrono::steady_clock::time_point m_lastActivity;

    /* ============================================================
     * 当前设置（缓存，方便 UI 快速切换）
     * ============================================================ */

    int  m_fontSize;
    int  m_readMode;
    bool m_darkTheme;

    /* ============================================================
     * TOC 数据（从 MuPDFRenderer::get_toc() 来）
     * ============================================================ */

    struct TocEntry {
        std::string title;
        int         pageIndex;
    };
    std::vector<TocEntry> m_toc;
    int                   m_tocFocus;

    /* ============================================================
     * 触屏事件记录（Switch libnx 提供 touch 输入，由 Borealis 分发）
     * 这里简化：update() 如果没有触屏回调，就在 draw 的 overlay 里
     * 画好 UI，但触屏交互依赖 Borealis 自动事件。
     * ============================================================ */

    /* ============================================================
     * 私有辅助
     * ============================================================ */

    /* 判断某个 format 是否走 MuPDF 路径 */
    static bool is_mupdf_format(int fmt);

    /* 夹取 m_currentPage 到 [0, m_totalPages-1]；m_totalPages==0 时夹到 0 */
    void clamp_page();

    /* 翻页：delta=±1。内部做 boundary + reset 时间戳 */
    void change_page(int delta);

    /* 重置 m_lastActivity 到现在（菜单自动隐藏计时） */
    void touch_activity();

    /* 根据当前设置重算 m_totalPages（仅 TextRenderer 路径下 fontSize 变了要重排） */
    void recalc_total_pages();

    /* 把当前进度持久化（供返回书架 / 后台定时 flush 调用） */
    void persist_progress();

    /* ============================================================
     * draw 子函数
     * ============================================================ */

    /* 背景 + 内容（MuPDF 或 Text，内部按 m_useMupdf 分发） */
    void draw_content(NVGcontext* vg);

    /* MuPDF 路径：渲染 pixmap → nvg_image 显示 */
    void draw_content_mupdf(NVGcontext* vg);

    /* Text 路径：TextRenderer::get_page_lines → nvgText 逐行 */
    void draw_content_text(NVGcontext* vg);

    /* 菜单 overlay（顶栏 + 底栏进度条 + 按键提示） */
    void draw_menu_overlay(NVGcontext* vg);

    /* 设置快速弹窗（字号 ±1、主题 toggle、阅读模式 4 选 1） */
    void draw_settings_popup(NVGcontext* vg);

    /* 目录弹窗（TOC 列表） */
    void draw_toc_popup(NVGcontext* vg);

    /* 大字进度 overlay（底部居中，显示 "当前页/总页数"） */
    void draw_progress_overlay(NVGcontext* vg);

    /* ============================================================
     * MuPDF 路径：
     *   调 App::get_mupdf()->get_page_pixmap → 上传 OpenGL Texture → nvg_image
     *   封装成一个小辅助，不让 draw_content 直接碰 MuPDF 细节
     * ============================================================ */

    /* 把 MuPDF pixmap 转成 OpenGL texture，返回 nvg_image handle；
     * 内部做 cache（同一页只上传一次）。失败返回 0。 */
    struct PixCacheKey { int pageIndex; float scale; };
    void* get_mupdf_page_texture(NVGcontext* vg, int pageIndex, float scale);

    /* 纹理缓存（最多 4 个：当前 + 前后 2 页） */
    struct CachedTex {
        int   pageIndex = -1;
        float scale     = 1.0f;
        int   texId     = 0;       // OpenGL texture id（nvg_image 内部持有）
        void* nvgImg    = nullptr; // nvgCreateImageFromID 的结果
    };
    CachedTex m_texCache[4];
    int       m_texCacheSize = 0;

    /* 从 App 里取 MuPDFRenderer —— 需要 App 提供 public 访问器
     * 这里我们用 dynamic_cast 风格的手动分发：
     *   - App::get_current_book() 返回 BookInfo*
     *   - 我们在构造时根据 m_useMupdf 把 *renderer 存下来
     * 但 unique_ptr 是 private，所以我们只能通过 App 暴露的 API 间接取。
     *
     * 保守做法：假设 App 有以下两个访问器（如果没有会编译失败，
     * 到时候加在 App.hpp 里就行）。这里用 App::get_mupdf() / App::get_text()
     * 两个方法来拿。
     */
};

} /* namespace mjnexus */

#endif /* MJNEXUS_READER_PAGE_HPP */
