/*
 * ReaderPage.cpp
 * -------------------------------------------------------------------
 * 阅读器页面实现 —— 整个应用中最复杂的页面。
 *
 * 关键设计：
 *   - 构造时从 App::get() 拿到渲染器指针（假设 App 有 get_mupdf() / get_text()）
 *   - m_currentPage 是唯一的 "页码真相源"
 *   - draw_content 里按 m_useMupdf 分发：
 *       * MuPDF：render_page_pixmap → 上传 OpenGL tex → nvg_image
 *       * TextRenderer：get_page_lines → 逐行 nvgText
 *   - 菜单 overlay 有 3s 自动隐藏（m_lastActivity 计时）
 *   - TOC 用 MuPDFRenderer::get_toc()；Text 格式没 TOC，显示"无目录"
 *   - 设置弹窗快速改 fontSize / theme / readMode，即时生效
 *   - B 返回书架时调 persist_progress() 存盘
 *
 * Switch 上 OpenGL 纹理管理用一个小 LRU 缓存（4 个槽），避免每页都上传。
 * 如果 Borealis 的 nvgCreateImageGL 不可用，退化为 "画纯文本占位" 路径。
 *
 * 编码：UTF-8
 * -------------------------------------------------------------------
 */

#include "ReaderPage.hpp"
#include "App.hpp"

#include "../storage/ProgressStore.hpp"

#if __has_include(<borealis.hpp>)
#include <borealis.hpp>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mjnexus {

/* ============================================================
 * 小工具：format → 是否 MuPDF 路径
 * ============================================================ */

bool ReaderPage::is_mupdf_format(int fmt) {
    switch ((RenderFormat)fmt) {
        case RenderFormat::PDF:
        case RenderFormat::EPUB:
        case RenderFormat::XPS:
        case RenderFormat::CBZ:
        case RenderFormat::CBR:
        case RenderFormat::CBT:
        case RenderFormat::CB7:
            return true;
        case RenderFormat::MOBI:
        case RenderFormat::TXT:
        case RenderFormat::MD:
            return false;
        default:
            return false;
    }
}

/* ============================================================
 * 构造 —— 关键初始化
 *
 * 从 App::get() 拿：
 *   - get_current_book() → m_format / totalPages
 *   - get_mupdf() / get_text() → 渲染器（public getter，假设 App 提供）
 *   - settings → m_fontSize / m_readMode / m_darkTheme
 *   - ProgressStore → 上次读的 pageIndex
 * ============================================================ */

ReaderPage::ReaderPage()
    : m_app(App::get())
    , m_currentPage(0)
    , m_totalPages(0)
    , m_format((int)RenderFormat::UNKNOWN)
    , m_useMupdf(false)
    , m_showMenu(true)
    , m_showToc(false)
    , m_showSettings(false)
    , m_showProgress(false)
    , m_lastPressed(brls::ControllerButton::BUTTON_NONE)
    , m_lastActivity(std::chrono::steady_clock::now())
    , m_fontSize(16)
    , m_readMode((int)ReadMode::PORTRAIT)
    , m_darkTheme(false)
    , m_tocFocus(0)
{
    // 1) 取当前书
    const BookInfo* cur = m_app.get_current_book();
    if (cur == nullptr || !m_app.is_book_loaded()) {
        // 这种情况不应该发生，但防御式处理：直接返回，让上层 popView
        std::fprintf(stderr, "[mjnexus] ReaderPage: no book loaded!\n");
        return;
    }

    m_format = cur->format;
    m_useMupdf = is_mupdf_format(m_format);

    // 2) 总页数
    //    - MuPDF：App::get_mupdf()->get_total_pages()
    //    - Text ：App::get_text()->get_total_pages()
    //    - 两者都失败：回退到 BookInfo.totalPages
    m_totalPages = cur->totalPages;
    if (m_useMupdf) {
        auto* r = App::get_mupdf();
        if (r && r->is_open() && r->get_total_pages() > 0) {
            m_totalPages = r->get_total_pages();
        }
        // 读 TOC
        if (r && r->is_open()) {
            auto toc = r->get_toc();
            m_toc.reserve(toc.size());
            for (auto& e : toc) {
                TocEntry t;
                t.title = e.first;
                t.pageIndex = r->map_toc_page_to_index(e.second);
                m_toc.push_back(t);
            }
        }
    } else {
        auto* r = App::get_text();
        if (r && r->is_open() && r->get_total_pages() > 0) {
            m_totalPages = r->get_total_pages();
        }
    }

    if (m_totalPages <= 0) {
        m_totalPages = 1;   // 至少一页，避免除 0
    }

    // 3) 设置（App 持有 copy，我们也缓存一份方便 UI 改）
    const AppSettings& s = m_app.get_settings();
    m_fontSize   = s.fontSize;
    m_readMode   = s.defaultReadMode;
    m_darkTheme  = (s.theme == (int)Theme::DARK);

    // 如果用当前书自己的 readMode（优先用 per-book 设置）
    if (cur->readMode >= 0 && cur->readMode < 4) {
        m_readMode = cur->readMode;
    }

    // 同步到渲染器（字号 / 主题 变化会触发重排）
    if (!m_useMupdf) {
        auto* tr = App::get_text();
        if (tr && tr->is_open()) {
            tr->set_font_size(m_fontSize);
            tr->set_theme(m_darkTheme);
        }
        m_totalPages = App::get_text()->get_total_pages();
    }

    // 4) 读上次进度
    auto prog = ProgressStore::instance().get(cur->filePath);
    if (prog.has_value()) {
        m_currentPage = prog.value().pageIndex;
        if (m_currentPage < 0) m_currentPage = 0;
        if (m_currentPage >= m_totalPages) m_currentPage = m_totalPages - 1;
    }

    // 5) 如果是双页横屏模式，确保 m_currentPage 是偶数（左页优先）
    if (m_readMode == (int)ReadMode::SPREAD_TWO_PAGE && m_useMupdf) {
        if (m_currentPage % 2 == 1) m_currentPage -= 1;
    }

    std::fprintf(stdout, "[mjnexus] ReaderPage: fmt=%d total=%d startPage=%d\n",
                 m_format, m_totalPages, m_currentPage);
}

ReaderPage::~ReaderPage() {
    // 析构时再存一次进度，确保不会丢
    persist_progress();
}

/* ============================================================
 * 辅助方法
 * ============================================================ */

void ReaderPage::clamp_page() {
    if (m_totalPages <= 0) { m_currentPage = 0; return; }
    if (m_currentPage < 0) m_currentPage = 0;
    if (m_currentPage >= m_totalPages) m_currentPage = m_totalPages - 1;
}

void ReaderPage::change_page(int delta) {
    // SPREAD 模式：一次跳 2 页
    int step = delta;
    if (m_readMode == (int)ReadMode::SPREAD_TWO_PAGE && m_useMupdf) {
        step = delta * 2;
    }
    m_currentPage += step;
    clamp_page();
    touch_activity();
}

void ReaderPage::touch_activity() {
    m_lastActivity = std::chrono::steady_clock::now();
}

void ReaderPage::recalc_total_pages() {
    if (!m_useMupdf) {
        auto* tr = App::get_text();
        if (tr && tr->is_open()) {
            tr->set_font_size(m_fontSize);    // 触发重排
            tr->set_theme(m_darkTheme);
            m_totalPages = tr->get_total_pages();
            if (m_currentPage >= m_totalPages) m_currentPage = m_totalPages - 1;
            if (m_currentPage < 0) m_currentPage = 0;
        }
    }
    // MuPDF 路径总页数固定
}

void ReaderPage::persist_progress() {
    if (!m_app.is_book_loaded()) return;

    int pct = 0;
    if (m_totalPages > 0) {
        pct = (int)((m_currentPage * 100.0f) / (m_totalPages - 1));
    }

    const BookInfo* cur = m_app.get_current_book();
    if (cur) {
        ProgressStore::instance().update(
            cur->filePath, m_currentPage, (float)pct, m_readMode);
    }

    m_app.save_progress(m_currentPage, (float)pct);
}

/* ============================================================
 * get_mupdf_page_texture —— 把 MuPDF pixmap 转 OpenGL texture + nvg_image
 *
 * 流程：
 *   1) 看 cache 里有没有同一页同一 scale 的纹理
 *   2) 没有则调 renderer->get_page_pixmap → fz_pixmap → glTexImage2D
 *   3) 用 nvgCreateImageGL 或 glGenTextures 上传
 *   4) 把结果缓存在 m_texCache[] 里
 *
 * 注意：Switch libnx 上 OpenGL ES 是 2.0，需要自己封装。
 * 如果没有现成的 nvgCreateImageGL，退化为 "在 draw_content 里用占位"。
 * 这里我们假设 Borealis/自制的 NVG 里有这个 API。
 * ============================================================ */

void* ReaderPage::get_mupdf_page_texture(NVGcontext* vg, int pageIndex, float scale) {
    // 1) 查 cache
    for (int i = 0; i < m_texCacheSize; ++i) {
        if (m_texCache[i].pageIndex == pageIndex &&
            std::abs(m_texCache[i].scale - scale) < 0.001f &&
            m_texCache[i].nvgImg != nullptr) {
            return m_texCache[i].nvgImg;
        }
    }

    // 2) 渲染 MuPDF pixmap
    auto* renderer = App::get_mupdf();
    if (!renderer || !renderer->is_open()) return nullptr;

    fz_pixmap* pix = nullptr;
    if (!renderer->get_page_pixmap(pageIndex, &pix, scale)) {
        return nullptr;
    }
    if (!pix) return nullptr;

    // 3) pixmap → nvg_image
    //    fz_pixmap 是 RGBA 或 BGRA；我们假设 fz_enable_device_hints 返回的是 RGBA。
    int pw = pix->w;
    int ph = pix->h;

    // NVG 侧创建 image（按 native）
    // 优先用 nvgCreateImageRGBA；如果不可用，再试其他
    NVGimageFlags flags = 0;
    void* img = nvgCreateImageRGBA(vg, pw, ph, flags, pix->samples);

    // 4) 释放 MuPDF 资源
    fz_drop_pixmap(renderer->get_context(), pix);

    if (!img) {
        std::fprintf(stderr, "[mjnexus] ReaderPage: nvgCreateImageRGBA failed\n");
        return nullptr;
    }

    // 5) 塞进 cache（环形）
    int slot = m_texCacheSize % 4;
    m_texCache[slot].pageIndex = pageIndex;
    m_texCache[slot].scale     = scale;
    m_texCache[slot].nvgImg    = img;
    m_texCache[slot].texId    = 0;  // 由 nvg 内部管理
    m_texCacheSize = std::min(m_texCacheSize + 1, 4);

    return img;
}

/* ============================================================
 * draw_content —— 核心内容渲染
 * ============================================================ */

void ReaderPage::draw_content(NVGcontext* vg) {
    auto& theme = MjnexusTheme::current();

    // 背景填充（整个屏幕）
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)NINTENDO_SWITCH_SCREEN_W, (float)NINTENDO_SWITCH_SCREEN_H);
    nvgFillColor(vg, nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, 255));
    nvgFill(vg);

    if (m_useMupdf) {
        draw_content_mupdf(vg);
    } else {
        draw_content_text(vg);
    }
}

/* ============================================================
 * draw_content_mupdf —— MuPDF 路径
 *
 * 根据 ReadMode 处理：
 *   PORTRAIT     : 单页，scale = min(SW/pageW, SH/pageH)
 *   LANDSCAPE    : 单页（宽优先适配），scale = min(SW/pageW, SH/pageH)
 *   VERTICAL_FIT : 单页，scale 同上
 *   SPREAD_TWO_PAGE : 双页并排；左页 SW/2-80、右页 SW/2-80；scale 各按半屏算
 * ============================================================ */

static float compute_page_scale(int pageW, int pageH, int availW, int availH) {
    if (pageW <= 0 || pageH <= 0) return 1.0f;
    float sx = (float)availW / (float)pageW;
    float sy = (float)availH / (float)pageH;
    return std::min(sx, sy);
}

void ReaderPage::draw_content_mupdf(NVGcontext* vg) {
    auto* renderer = App::get_mupdf();
    if (!renderer || !renderer->is_open()) {
        // fallback：渲染器打不开 → 画提示文字
        auto& theme = MjnexusTheme::current();
        nvgFontSize(vg, 40.0f);
        nvgFontFace(vg, "sans");
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
        nvgText(vg, (float)NINTENDO_SWITCH_SCREEN_W * 0.5f,
                     (float)NINTENDO_SWITCH_SCREEN_H * 0.5f,
                     "Renderer not open", nullptr);
        return;
    }

    // 先拿当前页的原始尺寸（先渲染一页拿到 pixmap 的 W/H）
    // 但 MuPDF 没直接 get_page_size 的便捷接口，我们渲染一张 scale=1 的小占位然后取 W/H
    // 更高效做法：直接渲染目标 scale
    // 为了简单，我们先拿到 page pixmap 的尺寸（scale=1 渲染一次，缓存住尺寸），
    // 然后算合适的 scale 再渲染最终版。
    // 但这样会渲染两次 —— 我们换成：先 scale=1 拿尺寸，释放 pixmap，算 scale，再渲染。

    int curPage = m_currentPage;
    if (curPage < 0) curPage = 0;

    auto render_one_page = [&](int pageIndex, float availW, float availH,
                                float outX, float outY, float outW, float outH) {
        // scale=1 拿尺寸
        fz_pixmap* sizePix = nullptr;
        float scale1 = 1.0f;
        renderer->get_page_pixmap(pageIndex, &sizePix, scale1);
        int pw = sizePix ? sizePix->w : 612;
        int ph = sizePix ? sizePix->h : 792;
        if (sizePix) fz_drop_pixmap(renderer->get_context(), sizePix);

        float scale = compute_page_scale(pw, ph, (int)availW, (int)availH);

        // 用缓存拿纹理
        void* img = get_mupdf_page_texture(vg, pageIndex, scale);
        if (!img) {
            // 缓存 miss → 渲染一张 placeholder
            return;
        }

        // 居中显示
        float realW = pw * scale;
        float realH = ph * scale;
        float drawX = outX + (outW - realW) * 0.5f;
        float drawY = outY + (outH - realH) * 0.5f;

        // 画黑色边框 + 页面
        nvgSave(vg);
        nvgTranslate(vg, drawX, drawY);
        // nvgImagePattern 画
        NVGpaint paint = nvgImagePattern(vg, 0.0f, 0.0f, realW, realH,
                                          0.0f, (intptr_t)img, 1.0f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0.0f, 0.0f, realW, realH, 4.0f);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
        nvgRestore(vg);
    };

    switch ((ReadMode)m_readMode) {
        case ReadMode::PORTRAIT:
        case ReadMode::LANDSCAPE:
        case ReadMode::VERTICAL_FIT: {
            render_one_page(curPage,
                NINTENDO_SWITCH_SCREEN_W - 40, NINTENDO_SWITCH_SCREEN_H - 40,
                20.0f, 20.0f,
                (float)NINTENDO_SWITCH_SCREEN_W - 40.0f,
                (float)NINTENDO_SWITCH_SCREEN_H - 40.0f);
            break;
        }
        case ReadMode::SPREAD_TWO_PAGE: {
            // 左页 = curPage，右页 = curPage+1；如果 curPage+1 >= totalPages，左页居中显示
            float halfW = (float)NINTENDO_SWITCH_SCREEN_W * 0.5f;
            render_one_page(curPage,
                halfW - 40.0f, (float)NINTENDO_SWITCH_SCREEN_H - 40.0f,
                20.0f, 20.0f,
                halfW - 40.0f, (float)NINTENDO_SWITCH_SCREEN_H - 40.0f);
            if (curPage + 1 < m_totalPages) {
                render_one_page(curPage + 1,
                    halfW - 40.0f, (float)NINTENDO_SWITCH_SCREEN_H - 40.0f,
                    halfW + 20.0f, 20.0f,
                    halfW - 40.0f, (float)NINTENDO_SWITCH_SCREEN_H - 40.0f);
            }
            break;
        }
        default: break;
    }
}

/* ============================================================
 * draw_content_text —— TextRenderer 路径
 *
 * 直接 nvgText 逐行画。TextRenderer 已经按 SW 排好行，
 * 我们左边留 40px 边距，字号用 m_fontSize，行间距 fontSize*1.5。
 * ============================================================ */

void ReaderPage::draw_content_text(NVGcontext* vg) {
    auto* tr = App::get_text();
    auto& theme = MjnexusTheme::current();

    if (!tr || !tr->is_open()) {
        nvgFontSize(vg, 40.0f);
        nvgFontFace(vg, "sans");
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
        nvgText(vg, (float)NINTENDO_SWITCH_SCREEN_W * 0.5f,
                     (float)NINTENDO_SWITCH_SCREEN_H * 0.5f,
                     "TextRenderer not open", nullptr);
        return;
    }

    int curPage = m_currentPage;
    if (curPage < 0) curPage = 0;

    auto lines = tr->get_page_lines(curPage);

    // 画之前清空背景
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)NINTENDO_SWITCH_SCREEN_W, (float)NINTENDO_SWITCH_SCREEN_H);
    nvgFillColor(vg, nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, 255));
    nvgFill(vg);

    nvgFontSize(vg, (float)m_fontSize);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));

    float lineHeight = (float)m_fontSize * 1.6f;
    float marginX = 40.0f;
    float startY  = 60.0f;  // 顶部留空间给菜单栏（但菜单是 overlay，正常隐藏）

    float y = startY;
    for (const auto& line : lines) {
        // 如果菜单显示，把开始 Y 往下推（留出顶部栏 80px）
        float actualY = (m_showMenu) ? y + 80.0f : y;
        if (actualY > (float)NINTENDO_SWITCH_SCREEN_H - 40.0f) break;  // 超出屏幕

        // 如果行是空白，画一个空行（换行）
        if (line.empty()) {
            y += lineHeight;
            continue;
        }

        nvgText(vg, marginX, actualY, line.c_str(), nullptr);
        y += lineHeight;
    }
}

/* ============================================================
 * draw_menu_overlay —— 顶栏 + 底栏进度条
 *
 * 只在 m_showMenu==true 且没有其他 popup (TOC/Settings) 打开时显示
 * ============================================================ */

void ReaderPage::draw_menu_overlay(NVGcontext* vg) {
    if (!m_showMenu) return;
    if (m_showToc || m_showSettings) return;

    auto& theme = MjnexusTheme::current();
    const BookInfo* cur = m_app.get_current_book();

    // 顶栏：半透明深色（无论深浅主题都用半透明黑，保证不挡内容可读性）
    float barH = 80.0f;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)NINTENDO_SWITCH_SCREEN_W, barH);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 160));
    nvgFill(vg);

    // 书名
    nvgFontSize(vg, 26.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    std::string title = cur ? cur->title : "Unknown";
    if (title.empty()) title = "Unknown";
    nvgText(vg, 40.0f, barH * 0.5f, title.c_str(), nullptr);

    // 页码 / 总页数
    nvgFontSize(vg, 22.0f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    char pagebuf[64];
    std::snprintf(pagebuf, sizeof(pagebuf), "%d / %d",
                  m_currentPage + 1, std::max(1, m_totalPages));
    nvgText(vg, (float)NINTENDO_SWITCH_SCREEN_W - 40.0f, barH * 0.5f, pagebuf, nullptr);

    // 底栏：半透明 + ProgressDual
    float bottomH = 80.0f;
    float bottomY = (float)NINTENDO_SWITCH_SCREEN_H - bottomH;
    nvgBeginPath(vg);
    nvgRect(vg, 0, bottomY, (float)NINTENDO_SWITCH_SCREEN_W, bottomH);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 160));
    nvgFill(vg);

    // 进度条：左侧 100px 开始，右侧 100px 结束，高度 10px，圆角 5
    float trackX = 100.0f;
    float trackY = bottomY + bottomH * 0.5f - 5.0f;
    float trackW = (float)NINTENDO_SWITCH_SCREEN_W - 200.0f;
    float trackH = 10.0f;
    float pct = (m_totalPages > 1)
        ? ((float)m_currentPage / (float)(m_totalPages - 1))
        : 0.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, trackX, trackY, trackW, trackH, 5.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 60));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, trackX, trackY, trackW * pct, trackH, 5.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 220));
    nvgFill(vg);

    // 进度百分比 + 按键提示（在进度条上方小字）
    nvgFontSize(vg, 16.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 200));
    char pctbuf[32];
    std::snprintf(pctbuf, sizeof(pctbuf), "%.0f%%", pct * 100.0f);
    nvgText(vg, (float)NINTENDO_SWITCH_SCREEN_W * 0.5f, trackY - 22.0f, pctbuf, nullptr);

    // 按键提示
    nvgFontSize(vg, 18.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 200));
    nvgText(vg, 40.0f, bottomY + bottomH * 0.5f,
            "B: Back   A: Menu   +/-: Settings   Touch: L/R", nullptr);
}

/* ============================================================
 * draw_settings_popup —— 快速设置弹窗
 *
 * 项：
 *   字号滑块（10-24） —— 左右调 ±1
 *   主题 toggle
 *   阅读模式 4 选 1
 *   返回按钮 B
 *
 * 布局：居中 1000x700 面板
 * ============================================================ */

void ReaderPage::draw_settings_popup(NVGcontext* vg) {
    if (!m_showSettings) return;
    auto& theme = MjnexusTheme::current();

    // 半透明遮罩
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)NINTENDO_SWITCH_SCREEN_W, (float)NINTENDO_SWITCH_SCREEN_H);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 180));
    nvgFill(vg);

    // 面板
    float pw = 900.0f;
    float ph = 600.0f;
    float px = ((float)NINTENDO_SWITCH_SCREEN_W - pw) * 0.5f;
    float py = ((float)NINTENDO_SWITCH_SCREEN_H - ph) * 0.5f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, px, py, pw, ph, 16.0f);
    nvgFillColor(vg, nvgRGBA(theme.color_card.r, theme.color_card.g, theme.color_card.b, 255));
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBA(theme.color_border.r, theme.color_border.g, theme.color_border.b, 255));
    nvgStrokeWidth(vg, 2.0f);
    nvgStroke(vg);

    // 标题
    nvgFontSize(vg, 32.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
    nvgText(vg, px + pw * 0.5f, py + 40.0f, "Reader Settings", nullptr);

    // === 字号 ===
    float rowY = py + 120.0f;
    nvgFontSize(vg, 24.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
    nvgText(vg, px + 60.0f, rowY, "Font Size", nullptr);

    // 字号显示 + +/- 按钮
    char fbuf[32];
    std::snprintf(fbuf, sizeof(fbuf), "%d", m_fontSize);
    nvgFontSize(vg, 28.0f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, px + pw - 200.0f, rowY, fbuf, nullptr);

    // 减号按钮
    nvgFontSize(vg, 32.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, px + pw - 140.0f, rowY, "-", nullptr);
    // 加号按钮
    nvgText(vg, px + pw - 60.0f, rowY, "+", nullptr);

    // === 主题 ===
    rowY += 100.0f;
    nvgFontSize(vg, 24.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
    nvgText(vg, px + 60.0f, rowY, "Theme", nullptr);

    const char* themeLabel = m_darkTheme ? "Dark" : "Light";
    nvgFontSize(vg, 26.0f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, px + pw - 60.0f, rowY, themeLabel, nullptr);

    // === 阅读模式 4 选 1 ===
    rowY += 100.0f;
    nvgFontSize(vg, 24.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
    nvgText(vg, px + 60.0f, rowY, "Read Mode", nullptr);

    const char* modeNames[] = {
        "Portrait", "Landscape", "Vertical Fit", "Spread Two Page"
    };
    const int modeCount = 4;

    // 四个模式按钮（卡片式）
    float cardW = 170.0f;
    float cardH = 60.0f;
    float cardGap = 20.0f;
    float cardX0 = px + 60.0f;
    float cardY = rowY + 30.0f;

    for (int i = 0; i < modeCount; ++i) {
        float cx = cardX0 + (cardW + cardGap) * i;
        bool selected = (i == m_readMode);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, cx, cardY, cardW, cardH, 10.0f);
        if (selected) {
            nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 240));
        } else {
            nvgFillColor(vg, nvgRGBA(theme.color_card.r, theme.color_card.g, theme.color_card.b, 255));
        }
        nvgFill(vg);
        nvgStrokeColor(vg, nvgRGBA(theme.color_border.r, theme.color_border.g, theme.color_border.b, 255));
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);

        nvgFontSize(vg, 20.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, selected
            ? nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, 255)
            : nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
        nvgText(vg, cx + cardW * 0.5f, cardY + cardH * 0.5f, modeNames[i], nullptr);
    }

    // === 按键提示 ===
    nvgFontSize(vg, 18.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
    nvgText(vg, px + pw * 0.5f, py + ph - 40.0f,
            "L/R: Font Size -/+   D-Pad U/D: Theme   L: Prev Mode   R: Next Mode   B: Close", nullptr);
}

/* ============================================================
 * draw_toc_popup —— 目录弹窗
 *
 * 居中 800x800 面板，列表 m_toc[]，选中项高亮
 * 没有 TOC 时显示 "No TOC available for this format"
 * ============================================================ */

void ReaderPage::draw_toc_popup(NVGcontext* vg) {
    if (!m_showToc) return;
    auto& theme = MjnexusTheme::current();

    // 遮罩
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)NINTENDO_SWITCH_SCREEN_W, (float)NINTENDO_SWITCH_SCREEN_H);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 180));
    nvgFill(vg);

    // 面板
    float pw = 800.0f;
    float ph = 900.0f;
    float px = ((float)NINTENDO_SWITCH_SCREEN_W - pw) * 0.5f;
    float py = ((float)NINTENDO_SWITCH_SCREEN_H - ph) * 0.5f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, px, py, pw, ph, 16.0f);
    nvgFillColor(vg, nvgRGBA(theme.color_card.r, theme.color_card.g, theme.color_card.b, 255));
    nvgFill(vg);

    // 标题
    nvgFontSize(vg, 28.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
    nvgText(vg, px + pw * 0.5f, py + 30.0f, "Table of Contents", nullptr);

    if (m_toc.empty()) {
        nvgFontSize(vg, 22.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
        nvgText(vg, px + pw * 0.5f, py + ph * 0.5f,
                "No TOC available for this format", nullptr);
        return;
    }

    // 滚动偏移：让选中项居中
    int total = (int)m_toc.size();
    int visibleCount = 12;
    int firstVisible = 0;
    if (m_tocFocus >= visibleCount / 2) {
        firstVisible = m_tocFocus - visibleCount / 2;
    }
    if (firstVisible + visibleCount > total) {
        firstVisible = std::max(0, total - visibleCount);
    }

    float rowY = py + 80.0f;
    float rowH = 56.0f;
    float rowX = px + 30.0f;
    float rowW = pw - 60.0f;

    for (int i = firstVisible; i < total && i < firstVisible + visibleCount; ++i) {
        const auto& e = m_toc[i];
        bool selected = (i == m_tocFocus);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, rowX, rowY, rowW, rowH, 8.0f);
        if (selected) {
            nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 220));
        } else {
            nvgFillColor(vg, nvgRGBA(theme.color_card.r, theme.color_card.g, theme.color_card.b, 255));
        }
        nvgFill(vg);

        // 标题
        std::string label = e.title.empty() ? "(untitled)" : e.title;
        if ((int)label.size() > 40) label = label.substr(0, 39) + "…";

        nvgFontSize(vg, 20.0f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, selected
            ? nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, 255)
            : nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
        nvgText(vg, rowX + 20.0f, rowY + rowH * 0.5f, label.c_str(), nullptr);

        // 页码（右侧）
        char pg[16];
        std::snprintf(pg, sizeof(pg), "p.%d", e.pageIndex + 1);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, rowX + rowW - 20.0f, rowY + rowH * 0.5f, pg, nullptr);

        rowY += rowH;
    }

    // 按键提示
    nvgFontSize(vg, 16.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
    nvgText(vg, px + pw * 0.5f, py + ph - 30.0f,
            "U/D: Navigate    A: Jump    B: Close", nullptr);
}

/* ============================================================
 * draw_progress_overlay —— 大字进度 overlay（比如长按 L/R 时）
 * ============================================================ */

void ReaderPage::draw_progress_overlay(NVGcontext* vg) {
    if (!m_showProgress) return;
    auto& theme = MjnexusTheme::current();

    float pw = 400.0f;
    float ph = 140.0f;
    float px = ((float)NINTENDO_SWITCH_SCREEN_W - pw) * 0.5f;
    float py = (float)NINTENDO_SWITCH_SCREEN_H - ph - 120.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, px, py, pw, ph, 16.0f);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 200));
    nvgFill(vg);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d / %d",
                  m_currentPage + 1, std::max(1, m_totalPages));

    nvgFontSize(vg, 56.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgText(vg, px + pw * 0.5f, py + ph * 0.5f, buf, nullptr);
}

/* ============================================================
 * draw —— brls::View 主渲染
 * ============================================================ */

void ReaderPage::draw(NVGcontext* vg, float /*x*/, float /*y*/) {
    // 1) 主内容
    draw_content(vg);

    // 2) 如果设置或 TOC 打开，菜单自动隐藏（overlay 自己有半透明遮罩）
    //    先检查自动隐藏计时
    if (m_showMenu && !m_showSettings && !m_showToc) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastActivity).count();
        if (elapsed > 3000) {
            m_showMenu = false;
        }
    }

    // 3) 各种 overlay
    draw_menu_overlay(vg);
    draw_progress_overlay(vg);
    draw_toc_popup(vg);
    draw_settings_popup(vg);
}

/* ============================================================
 * update —— 按键事件
 *
 * 按键优先级：
 *   Settings popup → 响应自己的按键
 *   TOC popup → 响应自己的按键
 *   否则 → 正常翻页 / 菜单 / 返回
 *
 * Switch 的 ControllerButton 按 libnx/brls 规范：
 *   BUTTON_A  / BUTTON_B / BUTTON_X / BUTTON_Y
 *   BUTTON_L  / BUTTON_R / BUTTON_ZL / BUTTON_ZR
 *   BUTTON_LEFT / BUTTON_RIGHT / BUTTON_UP / BUTTON_DOWN
 *   BUTTON_PLUS / BUTTON_MINUS
 * ============================================================ */

void ReaderPage::update(brls::View* /*view*/, brls::ControllerButton button, bool pressed) {
    if (!pressed) {
        // 长按释放：如果 showProgress 且没按翻页键了，延迟关闭
        m_lastPressed = button;
        return;
    }

    // ==========================================================
    // 优先处理 Settings popup 的按键
    // ==========================================================
    if (m_showSettings) {
        if (button == brls::ControllerButton::BUTTON_B) {
            // B: 应用设置 + 关闭
            m_app.set_font_size(m_fontSize);
            Theme t = m_darkTheme ? Theme::DARK : Theme::LIGHT;
            m_app.switch_theme(t);

            AppSettings s = m_app.get_settings();
            s.fontSize   = m_fontSize;
            s.theme      = (int)t;
            m_app.apply_settings(s);

            recalc_total_pages();
            m_showSettings = false;
            touch_activity();
            return;
        }
        // Left/Right: 字号 ±1
        if (button == brls::ControllerButton::BUTTON_LEFT) {
            m_fontSize -= 1;
            if (m_fontSize < 10) m_fontSize = 10;
            if (!m_useMupdf) recalc_total_pages();
            touch_activity();
            return;
        }
        if (button == brls::ControllerButton::BUTTON_RIGHT) {
            m_fontSize += 1;
            if (m_fontSize > 24) m_fontSize = 24;
            if (!m_useMupdf) recalc_total_pages();
            touch_activity();
            return;
        }
        // Up/Down: 主题 toggle
        if (button == brls::ControllerButton::BUTTON_UP ||
            button == brls::ControllerButton::BUTTON_DOWN) {
            m_darkTheme = !m_darkTheme;
            recalc_total_pages();
            touch_activity();
            return;
        }
        // L/R: 阅读模式切换（R=下一个）
        if (button == brls::ControllerButton::BUTTON_R ||
            button == brls::ControllerButton::BUTTON_X) {
            m_readMode = (m_readMode + 1) % 4;
            touch_activity();
            return;
        }
        if (button == brls::ControllerButton::BUTTON_L ||
            button == brls::ControllerButton::BUTTON_Y) {
            m_readMode = (m_readMode + 3) % 4;
            touch_activity();
            return;
        }
        return;
    }

    // ==========================================================
    // 处理 TOC popup 的按键
    // ==========================================================
    if (m_showToc) {
        if (button == brls::ControllerButton::BUTTON_B) {
            m_showToc = false;
            touch_activity();
            return;
        }
        if (button == brls::ControllerButton::BUTTON_UP) {
            if (!m_toc.empty()) {
                m_tocFocus -= 1;
                if (m_tocFocus < 0) m_tocFocus = (int)m_toc.size() - 1;
            }
            touch_activity();
            return;
        }
        if (button == brls::ControllerButton::BUTTON_DOWN) {
            if (!m_toc.empty()) {
                m_tocFocus += 1;
                if (m_tocFocus >= (int)m_toc.size()) m_tocFocus = 0;
            }
            touch_activity();
            return;
        }
        if (button == brls::ControllerButton::BUTTON_A) {
            if (!m_toc.empty() && m_tocFocus >= 0 && m_tocFocus < (int)m_toc.size()) {
                m_currentPage = m_toc[m_tocFocus].pageIndex;
                clamp_page();
                m_showToc = false;
                touch_activity();
            }
            return;
        }
        return;
    }

    // ==========================================================
    // 正常阅读模式：翻页 / 菜单 / 返回 / 设置 / TOC
    // ==========================================================
    touch_activity();

    switch (button) {
        case brls::ControllerButton::BUTTON_ZL:
        case brls::ControllerButton::BUTTON_L:
        case brls::ControllerButton::BUTTON_LEFT:
            change_page(-1);
            m_showProgress = true;
            return;

        case brls::ControllerButton::BUTTON_ZR:
        case brls::ControllerButton::BUTTON_R:
        case brls::ControllerButton::BUTTON_RIGHT:
            change_page(1);
            m_showProgress = true;
            return;

        case brls::ControllerButton::BUTTON_A:
            // 菜单 toggle
            m_showMenu = !m_showMenu;
            touch_activity();
            return;

        case brls::ControllerButton::BUTTON_B:
            // 返回书架
            persist_progress();
            m_app.close_book();
            brls::Application::popView();
            return;

        case brls::ControllerButton::BUTTON_PLUS:
            // 设置 popup
            m_showSettings = true;
            touch_activity();
            return;

        case brls::ControllerButton::BUTTON_MINUS:
            // TOC popup（仅 MuPDF 路径有意义）
            if (m_useMupdf) {
                m_showToc = true;
                m_tocFocus = 0;
                // 找到和当前页最接近的目录项做焦点
                int best = 0;
                int bestDist = 99999;
                for (int i = 0; i < (int)m_toc.size(); ++i) {
                    int d = std::abs(m_toc[i].pageIndex - m_currentPage);
                    if (d < bestDist) { bestDist = d; best = i; }
                }
                m_tocFocus = best;
            }
            touch_activity();
            return;

        default:
            break;
    }
}

} /* namespace mjnexus */
