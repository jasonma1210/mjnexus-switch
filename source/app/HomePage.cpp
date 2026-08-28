/*
 * HomePage.cpp
 * -------------------------------------------------------------------
 * 书架主界面实现。
 *
 * 关键流程：
 *   构造时 → refresh_library() → 扫描 → 生成缺失封面 → 加载进度 → 排序
 *   draw() → 填背景 → 顶栏 → 近阅读 → 全书籍 → 空占位
 *   update() → A 开书 / X 设置 / Y 刷新 / D-Pad 移动焦点
 *
 * Switch 上没有异常 / RTTI，所有错误走 if-else + stderr。
 * 依赖：
 *   - source/app/App.hpp
 *   - source/storage/BookLibrary.hpp
 *   - source/storage/ProgressStore.hpp
 *   - borealis.hpp
 * -------------------------------------------------------------------
 */

#include "HomePage.hpp"
#include "App.hpp"
#include "ReaderPage.hpp"
#include "SettingsPage.hpp"

#include "../storage/BookLibrary.hpp"
#include "../storage/ProgressStore.hpp"

#if __has_include(<borealis.hpp>)
#include <borealis.hpp>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace mjnexus {

/* ============================================================
 * 国际化字典（静态，两种语言）
 * ============================================================ */

static const char* i18n_zh[][2] = {
    {"app_title",       "MJ Reader"},
    {"recent_title",    "最近阅读"},
    {"all_title",       "全部书籍"},
    {"empty_hint",      "把 .epub / .pdf / .cbz / .txt 文件放到\n/switch/mjnexus/books/ 目录下"},
    {"press_y_refresh", "按 Y 键刷新书架"},
    {"section_recent",  "最近阅读"},
    {"section_all",     "全部书籍"},
    {"back",            "返回"},
    {"settings",        "设置"},
    {"open",            "打开"},
    {nullptr, nullptr}
};

static const char* i18n_en[][2] = {
    {"app_title",       "MJ Reader"},
    {"recent_title",    "Recently Read"},
    {"all_title",       "All Books"},
    {"empty_hint",      "Drop .epub / .pdf / .cbz / .txt files into\n/switch/mjnexus/books/"},
    {"press_y_refresh", "Press Y to refresh"},
    {"section_recent",  "Recent"},
    {"section_all",     "Library"},
    {"back",            "Back"},
    {"settings",        "Settings"},
    {"open",            "Open"},
    {nullptr, nullptr}
};

const char* HomePage::get_i18n_value(const std::string& key) {
    // 用 App::get().get_settings().language 决定查哪张表；
    // 取不到时回退 zh-CN。
    bool wantEn = false;
    // App::get 是 function-local static 单例（C++11 保证线程安全），
    // 在 main() 里 initialize() 后即稳定可用，不会抛异常 ——
    // Switch 编译参数 -fno-exceptions，严禁 try/catch。
    {
        const AppSettings& s = App::get().get_settings();
        wantEn = (s.language == "en" || s.language == "en-US");
    }

    const char* (*table)[2] = wantEn ? i18n_en : i18n_zh;
    for (int i = 0; table[i][0] != nullptr; ++i) {
        if (key == table[i][0]) return table[i][1];
    }
    return key.c_str();
}

/* ============================================================
 * 构造 / 析构
 * ============================================================ */

HomePage::HomePage() {
    refresh_library();
}

HomePage::~HomePage() = default;

/* ============================================================
 * refresh_library —— 扫描、封面、排序、进度回填
 * ============================================================ */

void HomePage::refresh_library() {
    // 1) 扫描
    BookLibrary lib;
    m_books = lib.scan_books(SD_CARD_ROOT "books/");

    // 2) 对每本书生成缺失的封面
    for (size_t i = 0; i < m_books.size(); ++i) {
        if (m_books[i].coverPath.empty()) {
            // 统一命名：books/<md5>_cover.png。这里简单用 filePath 的 hash 前 16 位。
            // 为避免引入 hash 依赖，直接拼一个 "cover_<basename>.png" 占位路径，
            // 由 BookLibrary::generate_cover_png 内部处理真实写入。
            std::string out = std::string(SD_CARD_ROOT) + "covers/";
            // 确保目录存在（libnx 需要 mkdir）
            // BookLibrary 内部会 stat + mkdir
            if (lib.generate_cover_png(m_books[i].filePath, out + "cover_" + std::to_string(i) + ".png")) {
                m_books[i].coverPath = out + "cover_" + std::to_string(i) + ".png";
            }
        }
    }

    // 3) 从 ProgressStore 回填 lastReadTimestamp / lastReadPage 等
    auto progMap = ProgressStore::instance().load();
    for (auto& b : m_books) {
        auto it = progMap.find(b.filePath);
        if (it != progMap.end()) {
            b.lastReadPage       = it->second.pageIndex;
            b.lastReadPercentage = it->second.percentage;
            b.lastReadTimestamp  = it->second.timestamp;
            b.readMode           = it->second.readMode;
        }
    }

    // 4) 排序
    apply_sort();

    // 5) 挑出最近阅读
    m_recent = lib.list_recently_read(m_books, 4);

    // 6) 聚焦复位
    m_focusIndex = 0;
    m_currentSection = m_recent.empty() ? "all" : "recent";
    m_lastBookCount = m_books.size();

    std::fprintf(stdout, "[mjnexus] HomePage: %zu books scanned, %zu recent\n",
                 m_books.size(), m_recent.size());
}

/* ============================================================
 * apply_sort —— 根据 App::get().get_settings().sortBy 排序
 * ============================================================ */

void HomePage::apply_sort() {
    BookLibrary lib;
    const AppSettings& s = App::get().get_settings();

    const char* field = "name";
    switch (s.sortBy) {
        case AppSettings::SORT_RECENT: field = "recent"; break;
        case AppSettings::SORT_ADDED:  field = "added";  break;
        default:                       field = "name";   break;
    }
    lib.natural_sort(m_books, field, s.naturalSort);
}

/* ============================================================
 * resolve_section / clamp_focus —— 焦点辅助
 * ============================================================ */

std::string HomePage::resolve_section(int index) const {
    if (m_recent.empty()) return "all";
    if (index < (int)m_recent.size()) return "recent";
    return "all";
}

void HomePage::clamp_focus() {
    int total = (int)m_recent.size() + (int)m_books.size();
    if (total == 0) {
        m_focusIndex = 0;
        return;
    }
    if (m_focusIndex < 0) m_focusIndex = 0;
    if (m_focusIndex >= total) m_focusIndex = total - 1;
    m_currentSection = resolve_section(m_focusIndex);
}

/* ============================================================
 * draw_top_bar —— 顶部栏（120px 高）
 * ============================================================ */

void HomePage::draw_top_bar(NVGcontext* vg, float /*y*/) {
    const float bar_h = 120.0f;
    auto& theme = MjnexusTheme::current();

    // 背景矩形：SW=1920 x 120
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)NINTENDO_SWITCH_SCREEN_W, bar_h);
    nvgFillColor(vg, nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, theme.color_bg.a));
    nvgFill(vg);

    // 底部细分割线（border 色）
    nvgBeginPath(vg);
    nvgRect(vg, 0, bar_h - 1.0f, (float)NINTENDO_SWITCH_SCREEN_W, 1.0f);
    nvgFillColor(vg, nvgRGBA(theme.color_border.r, theme.color_border.g, theme.color_border.b, theme.color_border.a));
    nvgFill(vg);

    // 标题文字
    const char* title = get_i18n_value("app_title");
    nvgFontSize(vg, 48.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, theme.color_ink.a));
    nvgText(vg, 40.0f, bar_h * 0.5f, title, nullptr);

    // 右侧：提示 X=设置 Y=刷新（muted 灰）
    const char* hint = get_i18n_value("settings");
    nvgFontSize(vg, 22.0f);
    nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, theme.color_muted.a));
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, (float)NINTENDO_SWITCH_SCREEN_W - 40.0f, bar_h * 0.5f, hint, nullptr);
}

/* ============================================================
 * draw_recent_section —— 最近阅读（2x2 封面卡片）
 *
 * 布局（home y 方向坐标）：
 *   顶栏 120px + 标题 60px = 180px 开始
 *   4 卡片：左列 x=40, 右列 x=SW/2+20；上下各 340px 高、宽 ~440px
 * ============================================================ */

void HomePage::draw_recent_section(NVGcontext* vg, float /*x*/, float /*y*/, float /*w*/, float /*h*/) {
    if (m_recent.empty()) return;

    auto& theme = MjnexusTheme::current();

    const float section_y = 180.0f;   // 顶栏 120 + 标题高 60
    const float card_w    = 440.0f;
    const float card_h    = 340.0f;
    const float gap       = 40.0f;
    const float left_x    = 40.0f;
    const float right_x   = left_x + card_w + gap;
    const float top_y     = section_y + 40.0f;
    const float bot_y     = top_y + card_h + gap;

    // 分区标题
    nvgFontSize(vg, 26.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, theme.color_muted.a));
    nvgText(vg, left_x, section_y, get_i18n_value("recent_title"), nullptr);

    // 4 个卡片的位置
    struct Pos { float x; float y; };
    Pos positions[4] = {
        { left_x,  top_y },
        { right_x, top_y },
        { left_x,  bot_y },
        { right_x, bot_y },
    };

    // recent 最多 4 本
    int count = (int)m_recent.size();
    if (count > 4) count = 4;

    // 焦点在 recent 区域内的偏移索引
    int focusRel = -1;
    if (m_currentSection == "recent") focusRel = m_focusIndex;

    for (int i = 0; i < count; ++i) {
        const BookInfo& b = m_recent[i];
        Pos p = positions[i];
        bool selected = (focusRel == i);

        // 卡片背景（card 色 + 边框）
        nvgBeginPath(vg);
        nvgRoundedRect(vg, p.x, p.y, card_w, card_h, 16.0f);
        nvgFillColor(vg, nvgRGBA(theme.color_card.r, theme.color_card.g, theme.color_card.b, theme.color_card.a));
        nvgFill(vg);

        // 选中描边（主强调色或反色边框）
        if (selected) {
            nvgStrokeColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 200));
            nvgStrokeWidth(vg, 3.0f);
            nvgStroke(vg);
        } else {
            nvgStrokeColor(vg, nvgRGBA(theme.color_border.r, theme.color_border.g, theme.color_border.b, 255));
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
        }

        // 封面图：如果 coverPath 存在，尝试用 nvgImagePattern；
        // 封面图像是 Switch libnx 的 fsopen 打开的，这里假设在 draw 里已经有
        // Borealis 的 brls::Image 加载好了。为了不在 HomePage 里持有 brls::Image 成员
        // （避免 include 污染），简化：画一个渐变色块 + 书名。
        {
            // 封面占位色块：左半边 60% 宽度
            float cover_w = card_w * 0.45f;
            float cover_h = card_h - 60.0f;
            float cover_x = p.x + 20.0f;
            float cover_y = p.y + 20.0f;

            // 渐变色（根据 id 稳定选色）
            int hash = 0;
            for (char c : b.id) hash = hash * 31 + (int)(unsigned char)c;
            uint8_t r = (hash & 0xFF);
            uint8_t g = ((hash >> 8) & 0xFF);
            uint8_t bl = ((hash >> 16) & 0xFF);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cover_x, cover_y, cover_w, cover_h, 10.0f);
            nvgFillColor(vg, nvgRGBA(r, g, bl, 200));
            nvgFill(vg);

            // 渐变叠加
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cover_x, cover_y, cover_w, cover_h, 10.0f);
            NVGpaint grad = nvgLinearGradient(vg, cover_x, cover_y, cover_x + cover_w, cover_y + cover_h,
                nvgRGBA(0, 0, 0, 40), nvgRGBA(0, 0, 0, 120));
            nvgFillPaint(vg, grad);
            nvgFill(vg);

            // 书名（右侧）
            nvgFontSize(vg, 24.0f);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
            // 截断到 24 字符以内，避免撑出
            std::string title = b.title.empty() ? "Unknown" : b.title;
            if ((int)title.size() > 28) title = title.substr(0, 27) + "…";
            nvgText(vg, cover_x + cover_w + 20.0f, p.y + 40.0f, title.c_str(), nullptr);

            // 作者（小字）
            nvgFontSize(vg, 18.0f);
            nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
            std::string author = b.author.empty() ? "" : b.author;
            if ((int)author.size() > 24) author = author.substr(0, 23) + "…";
            nvgText(vg, cover_x + cover_w + 20.0f, p.y + 80.0f, author.c_str(), nullptr);
        }
    }
}

/* ============================================================
 * draw_all_section —— 全部书籍列表
 *
 * y 坐标从 180 + 340 + 80 = 600 开始（前两个卡片行 + gap）。
 * 每行高度 ~64px，每屏最多能放 ~7 行（1080 - 600 - 80）。
 * 用一个简单的 "firstVisible / lastVisible" 滚动窗口。
 * ============================================================ */

void HomePage::draw_all_section(NVGcontext* vg, float /*x*/, float /*y*/, float /*w*/, float /*h*/) {
    if (m_books.empty()) return;

    auto& theme = MjnexusTheme::current();

    const float section_y     = 180.0f;    // 和 recent 标题同位置（标题用最近阅读那一行，all 另起一行在 600）
    const float list_title_y  = 600.0f;
    const float row_h         = 64.0f;
    const float row_x         = 40.0f;
    const float row_w         = (float)NINTENDO_SWITCH_SCREEN_W - 80.0f;
    const int   visible_count = 7;

    // 分区标题 "全部书籍"
    nvgFontSize(vg, 26.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
    nvgText(vg, row_x, list_title_y, get_i18n_value("all_title"), nullptr);

    // 计算列表滚动偏移：让选中项尽量居中
    int totalRecent = (int)m_recent.size();
    int focusRel = 0;
    bool onAll = (m_currentSection == "all");
    if (onAll) focusRel = m_focusIndex - totalRecent;
    else focusRel = -1;

    // 滚动窗口（最多放 visible_count 行）
    int firstVisible = 0;
    int lastVisible  = (int)m_books.size() - 1;
    if (onAll && focusRel >= 0) {
        firstVisible = focusRel - visible_count / 2;
        if (firstVisible < 0) firstVisible = 0;
        lastVisible = firstVisible + visible_count - 1;
        if (lastVisible >= (int)m_books.size()) {
            lastVisible = (int)m_books.size() - 1;
            firstVisible = lastVisible - visible_count + 1;
            if (firstVisible < 0) firstVisible = 0;
        }
    }

    float y = list_title_y + 40.0f;
    for (int i = firstVisible; i <= lastVisible; ++i) {
        const BookInfo& b = m_books[i];
        bool selected = onAll && (i == focusRel);

        // 行背景
        nvgBeginPath(vg);
        nvgRoundedRect(vg, row_x, y, row_w, row_h, 8.0f);
        if (selected) {
            nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 240));
        } else {
            nvgFillColor(vg, nvgRGBA(theme.color_card.r, theme.color_card.g, theme.color_card.b, 255));
        }
        nvgFill(vg);

        // 行分割线
        nvgBeginPath(vg);
        nvgRect(vg, row_x, y + row_h - 1.0f, row_w, 1.0f);
        nvgFillColor(vg, nvgRGBA(theme.color_border.r, theme.color_border.g, theme.color_border.b, 255));
        nvgFill(vg);

        // 左封面缩略图：56x56 色块 + 字母占位
        float thumb_x = row_x + 12.0f;
        float thumb_y = y + 4.0f;
        float thumb_s = 56.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, thumb_x, thumb_y, thumb_s, thumb_s, 6.0f);
        int hash = 0;
        for (char c : b.id) hash = hash * 31 + (int)(unsigned char)c;
        nvgFillColor(vg, nvgRGBA((hash & 0xFF), ((hash >> 8) & 0xFF), ((hash >> 16) & 0xFF), 220));
        nvgFill(vg);

        // 书名 + 作者
        nvgFontSize(vg, 22.0f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, selected
            ? nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, 255)
            : nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 255));
        std::string label = b.title;
        if (label.empty()) {
            // 从 filePath 截 basename
            auto pos = b.filePath.find_last_of("/");
            label = (pos == std::string::npos) ? b.filePath : b.filePath.substr(pos + 1);
        }
        if (!b.author.empty()) {
            label += "  -  ";
            label += b.author;
        }
        nvgText(vg, thumb_x + thumb_s + 20.0f, y + row_h * 0.5f, label.c_str(), nullptr);

        // 右：进度百分比（如果有进度）
        if (b.lastReadPercentage > 0.0f) {
            char prog[32];
            std::snprintf(prog, sizeof(prog), "%.0f%%", b.lastReadPercentage);
            nvgFontSize(vg, 18.0f);
            nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, selected
                ? nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, 200)
                : nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
            nvgText(vg, row_x + row_w - 20.0f, y + row_h * 0.5f, prog, nullptr);
        }

        y += row_h;
    }

    // 如果有滚动，画个简单的右侧滑块（高度按 visible_count/total 比例）
    if ((int)m_books.size() > visible_count) {
        float track_x = row_x + row_w + 12.0f;
        float track_h = visible_count * row_h - row_h;
        float slider_h = track_h * ((float)visible_count / (float)m_books.size());
        float slider_y = y - track_h - row_h
            + (track_h - slider_h) * ((float)firstVisible / (float)((int)m_books.size() - visible_count));

        nvgBeginPath(vg);
        nvgRect(vg, track_x, y - track_h - row_h, 6.0f, track_h);
        nvgFillColor(vg, nvgRGBA(theme.color_border.r, theme.color_border.g, theme.color_border.b, 200));
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, track_x, slider_y, 6.0f, slider_h, 3.0f);
        nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));
        nvgFill(vg);
    }
}

/* ============================================================
 * draw_empty_state —— 没书时的占位
 * ============================================================ */

void HomePage::draw_empty_state(NVGcontext* vg) {
    auto& theme = MjnexusTheme::current();

    // 占位面板：居中 800x400
    float panel_w = 900.0f;
    float panel_h = 420.0f;
    float px = ((float)NINTENDO_SWITCH_SCREEN_W - panel_w) * 0.5f;
    float py = ((float)NINTENDO_SWITCH_SCREEN_H - panel_h) * 0.5f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, px, py, panel_w, panel_h, 24.0f);
    nvgFillColor(vg, nvgRGBA(theme.color_card.r, theme.color_card.g, theme.color_card.b, 255));
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBA(theme.color_border.r, theme.color_border.g, theme.color_border.b, 255));
    nvgStrokeWidth(vg, 2.0f);
    nvgStroke(vg);

    // 大问号
    nvgFontSize(vg, 120.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 180));
    nvgText(vg, px + panel_w * 0.5f, py + panel_h * 0.35f, "?", nullptr);

    // 提示文字
    nvgFontSize(vg, 24.0f);
    nvgFillColor(vg, nvgRGBA(theme.color_ink.r, theme.color_ink.g, theme.color_ink.b, 220));
    nvgText(vg, px + panel_w * 0.5f, py + panel_h * 0.65f, get_i18n_value("empty_hint"), nullptr);

    // Y=刷新 提示
    nvgFontSize(vg, 20.0f);
    nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 200));
    nvgText(vg, px + panel_w * 0.5f, py + panel_h * 0.85f, get_i18n_value("press_y_refresh"), nullptr);
}

/* ============================================================
 * draw —— brls::View 主渲染循环
 * ============================================================ */

void HomePage::draw(NVGcontext* vg, float /*x*/, float /*y*/) {
    auto& theme = MjnexusTheme::current();

    // 1) 清背景
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)NINTENDO_SWITCH_SCREEN_W, (float)NINTENDO_SWITCH_SCREEN_H);
    nvgFillColor(vg, nvgRGBA(theme.color_bg.r, theme.color_bg.g, theme.color_bg.b, theme.color_bg.a));
    nvgFill(vg);

    // 2) 顶栏
    draw_top_bar(vg, 0.0f);

    // 3) 空状态 vs 正常列表
    if (m_books.empty() && m_recent.empty()) {
        draw_empty_state(vg);
        return;
    }

    // 4) 分区绘制
    draw_recent_section(vg, 0.0f, 0.0f, (float)NINTENDO_SWITCH_SCREEN_W, (float)NINTENDO_SWITCH_SCREEN_H);
    draw_all_section(vg, 0.0f, 0.0f, (float)NINTENDO_SWITCH_SCREEN_W, (float)NINTENDO_SWITCH_SCREEN_H);

    // 5) 底部按键提示条（底部 60px）
    nvgBeginPath(vg);
    nvgRect(vg, 0, (float)NINTENDO_SWITCH_SCREEN_H - 60.0f,
            (float)NINTENDO_SWITCH_SCREEN_W, 60.0f);
    nvgFillColor(vg, nvgRGBA(theme.color_sidebar.r, theme.color_sidebar.g, theme.color_sidebar.b, 255));
    nvgFill(vg);

    nvgFontSize(vg, 20.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(theme.color_muted.r, theme.color_muted.g, theme.color_muted.b, 255));

    float bx = 40.0f;
    float by = (float)NINTENDO_SWITCH_SCREEN_H - 30.0f;
    nvgText(vg, bx, by, "A: Open    X: Settings    Y: Refresh    +/-: Section", nullptr);
}

/* ============================================================
 * update —— brls::View 按键事件
 *
 * Borealis 的 update 签名通常是：
 *   void update(brls::View* view, brls::ControllerButton button, bool pressed)
 * 我们把它当作 "按键按下瞬间" 触发（pressed=true）。
 * 如果 Borealis 的实际签名不同（比如用 Event），保持 conservative
 * 假设 puch 语义。
 * ============================================================ */

void HomePage::update(brls::View* view, brls::ControllerButton button, bool pressed) {
    if (!pressed) return;

    // 辅助 lambda：判断 A / B / X / Y
    auto isKey = [&](brls::ControllerButton b) { return b == button; };

    // 辅助：按 selected index 拿到 BookInfo*
    auto get_selected = [&]() -> const BookInfo* {
        if (m_currentSection == "recent") {
            if (m_focusIndex >= 0 && m_focusIndex < (int)m_recent.size())
                return &m_recent[m_focusIndex];
        } else {
            int rel = m_focusIndex - (int)m_recent.size();
            if (rel >= 0 && rel < (int)m_books.size())
                return &m_books[rel];
        }
        return nullptr;
    };

    if (isKey(brls::ControllerButton::BUTTON_A)) {
        const BookInfo* b = get_selected();
        if (!b) return;
        std::fprintf(stdout, "[mjnexus] HomePage A: open %s\n", b->filePath.c_str());
        if (App::get().load_book(b->filePath)) {
            ReaderPage* reader = new ReaderPage();
            brls::Application::pushView(reader, brls::ViewStack::Default);
        } else {
            std::fprintf(stderr, "[mjnexus] load_book failed: %s\n", b->filePath.c_str());
        }
        return;
    }

    if (isKey(brls::ControllerButton::BUTTON_X)) {
        std::fprintf(stdout, "[mjnexus] HomePage X: settings\n");
        SettingsPage* sp = new SettingsPage();
        brls::Application::pushView(sp, brls::ViewStack::Default);
        return;
    }

    if (isKey(brls::ControllerButton::BUTTON_Y)) {
        std::fprintf(stdout, "[mjnexus] HomePage Y: refresh\n");
        refresh_library();
        return;
    }

    // D-Pad Left/Right：在 recent 和 all 分区之间跳
    if (isKey(brls::ControllerButton::BUTTON_LEFT)) {
        if (m_currentSection == "all" && !m_recent.empty()) {
            m_currentSection = "recent";
            m_focusIndex = std::min(m_focusIndex, (int)m_recent.size() - 1);
        }
        clamp_focus();
        return;
    }
    if (isKey(brls::ControllerButton::BUTTON_RIGHT)) {
        if (m_currentSection == "recent" && !m_books.empty()) {
            m_currentSection = "all";
            m_focusIndex = (int)m_recent.size();
        }
        clamp_focus();
        return;
    }

    // D-Pad Up：上移（在当前分区里）
    if (isKey(brls::ControllerButton::BUTTON_UP)) {
        if (m_currentSection == "recent") {
            m_focusIndex -= 2;  // 2x2 网格里，上移一行（跨两个卡片）
            if (m_focusIndex < 0) m_focusIndex = (int)m_recent.size() - 1;
        } else {
            m_focusIndex -= 1;
            if (m_focusIndex < (int)m_recent.size())
                m_focusIndex = (int)m_recent.size();
        }
        clamp_focus();
        return;
    }

    // D-Pad Down：下移
    if (isKey(brls::ControllerButton::BUTTON_DOWN)) {
        if (m_currentSection == "recent") {
            m_focusIndex += 2;
            if (m_focusIndex >= (int)m_recent.size()) {
                if (!m_books.empty()) {
                    m_currentSection = "all";
                    m_focusIndex = (int)m_recent.size();
                } else {
                    m_focusIndex = (int)m_recent.size() - 1;
                }
            }
        } else {
            m_focusIndex += 1;
            if (m_focusIndex >= (int)m_recent.size() + (int)m_books.size()) {
                // 循环回到分区顶部
                m_focusIndex = (int)m_recent.size();
            }
        }
        clamp_focus();
        return;
    }
}

} /* namespace mjnexus */
