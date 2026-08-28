#include "App.hpp"

#include "../storage/SettingsStore.hpp"
#include "../storage/ProgressStore.hpp"

// 项目里已经提供了 MuPDFRenderer / TextRenderer 的头文件和实现，
// 直接 include 进来用就行
#include "mjnexus/MuPDFRenderer.hpp"
#include "mjnexus/TextRenderer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#if __has_include(<borealis.hpp>)
#include <borealis.hpp>
#define MJNEXUS_HAS_BOREALIS 1
#else
#define MJNEXUS_HAS_BOREALIS 0
#endif

namespace mjnexus {

// ============================================================
// 单例
// ============================================================

App& App::get() {
    static App inst;
    return inst;
}

App::App() = default;
App::~App() = default;

// ============================================================
// initialize —— 启动时加载设置 + 应用主题
// ============================================================

void App::initialize() {
    m_settings = SettingsStore::instance().load();
    switch_theme((Theme)m_settings.theme);
}

// ============================================================
// load_book —— 根据文件扩展名分发到对应渲染器
//
// 流程：
//   1) stat 文件 + 扩展名校验
//   2) 填充 BookInfo 基础字段（文件名、size、format 枚举）
//   3) 从 ProgressStore 回填 lastReadPage / lastReadPercentage / readMode
//   4) 按格式走 MuPDFRenderer 或 TextRenderer 的 open()
//   5) 渲染器 open() 成功后，把 renderer 里的 title / author 回填到 BookInfo
//
// 返回 false 的情况：
//   - stat 失败（文件不存在）
//   - 扩展名不在 SUPPORTED_EXTENSIONS 里
//   - 渲染器 open() 返回 false
// ============================================================

bool App::load_book(const std::string& filePath) {
    close_book();

    struct stat st {};
    if (stat(filePath.c_str(), &st) != 0) {
        std::fprintf(stderr, "[mjnexus] file not found: %s\n", filePath.c_str());
        return false;
    }

    int fmt = ext_to_format(filePath);
    if (fmt == (int)RenderFormat::UNKNOWN) {
        std::fprintf(stderr, "[mjnexus] unsupported format for: %s\n", filePath.c_str());
        return false;
    }

    m_currentBook.filePath = filePath;
    m_currentBook.format   = fmt;
    m_currentBook.fileSize = (uint64_t)st.st_size;
    m_currentBook.id       = filePath;

    // 从文件名推断 title（去掉扩展名）—— 渲染器 open() 之后会尝试覆盖
    auto base = filePath.find_last_of('/');
    std::string name = (base == std::string::npos) ? filePath : filePath.substr(base + 1);
    auto dot = name.rfind('.');
    m_currentBook.title = (dot != std::string::npos) ? name.substr(0, dot) : name;

    // 从 ProgressStore 回填已有的阅读进度
    auto progress = ProgressStore::instance().get(filePath);
    if (progress.has_value()) {
        m_currentBook.lastReadPage       = progress->pageIndex;
        m_currentBook.lastReadPercentage = progress->percentage;
        m_currentBook.readMode           = progress->readMode;
        m_currentBook.lastReadTimestamp  = progress->timestamp;
    } else {
        m_currentBook.readMode = m_settings.defaultReadMode;
    }

    RenderFormat rf = (RenderFormat)fmt;
    bool needs_mupdf = (rf == RenderFormat::PDF ||
                        rf == RenderFormat::EPUB ||
                        rf == RenderFormat::XPS ||
                        rf == RenderFormat::CBZ ||
                        rf == RenderFormat::CBR ||
                        rf == RenderFormat::CBT ||
                        rf == RenderFormat::CB7);

    bool needs_text = (rf == RenderFormat::TXT ||
                       rf == RenderFormat::MD ||
                       rf == RenderFormat::MOBI);

    if (needs_mupdf) {
        m_mupdf = std::make_unique<MuPDFRenderer>();
        if (!m_mupdf->open(filePath)) {
            m_mupdf.reset();
            return false;
        }
        // 用 MuPDF 解析出来的元数据覆盖我们猜的 title
        const auto& info = m_mupdf->get_book_info();
        if (!info.title.empty())  m_currentBook.title  = info.title;
        if (!info.author.empty()) m_currentBook.author  = info.author;
        m_currentBook.totalPages = m_mupdf->get_total_pages();
        m_loaded = true;
        return true;
    }

    if (needs_text) {
        m_text = std::make_unique<TextRenderer>();
        if (!m_text->open(filePath)) {
            m_text.reset();
            return false;
        }
        m_text->set_font_size(m_settings.fontSize);
        m_text->set_theme((Theme)m_settings.theme == Theme::DARK);
        m_currentBook.title  = m_text->get_title();
        m_currentBook.author = m_text->get_author();
        m_currentBook.totalPages = m_text->get_total_pages();
        m_loaded = true;
        return true;
    }

    return false;
}

// ============================================================
// close_book —— 释放渲染器 + 保存进度
// ============================================================

void App::close_book() {
    if (m_loaded) {
        save_progress(m_currentBook.lastReadPage,
                      m_currentBook.lastReadPercentage);
    }
    m_mupdf.reset();
    m_text.reset();
    m_currentBook = BookInfo{};
    m_loaded = false;
}

// ============================================================
// get_current_book / is_book_loaded
// ============================================================

const BookInfo* App::get_current_book() const {
    return m_loaded ? &m_currentBook : nullptr;
}

bool App::is_book_loaded() const {
    return m_loaded;
}

// ============================================================
// save_progress —— UI 层在翻页时调用，把最新页码 / 百分比持久化
// ============================================================

void App::save_progress(int pageIndex, float percentage) {
    if (!m_loaded) return;

    m_currentBook.lastReadPage       = pageIndex;
    m_currentBook.lastReadPercentage = percentage;
    m_currentBook.lastReadTimestamp  = now_unix_seconds();

    ProgressStore::instance().update(
        m_currentBook.filePath,
        pageIndex, percentage, m_currentBook.readMode);
}

// ============================================================
// 设置访问
// ============================================================

const AppSettings& App::get_settings() const {
    return m_settings;
}

void App::apply_settings(const AppSettings& newSettings) {
    m_settings = newSettings;
    m_settings.fontSize = std::clamp(m_settings.fontSize, 10, 24);
    if (m_settings.theme < 0 || m_settings.theme > 1)
        m_settings.theme = (int)Theme::LIGHT;

    SettingsStore::instance().save(m_settings);
    switch_theme((Theme)m_settings.theme);

    // 如果正在看一本书，TextRenderer 需要同步字号 / 主题
    if (m_text) {
        m_text->set_font_size(m_settings.fontSize);
        m_text->set_theme((Theme)m_settings.theme == Theme::DARK);
    }
}

Theme App::get_theme() const {
    return (Theme)m_settings.theme;
}

void App::switch_theme(Theme t) {
    m_settings.theme = (int)t;

#if MJNEXUS_HAS_BOREALIS
    auto theme = (t == Theme::DARK)
        ? MjnexusTheme::make_dark()
        : MjnexusTheme::make_light();
    theme.apply_to_borealis();
#endif

    // 正在用 TextRenderer 读书的话，也要通知它重排
    if (m_text) {
        m_text->set_theme(t == Theme::DARK);
    }

    SettingsStore::instance().save(m_settings);
}

int App::get_font_size() const {
    return m_settings.fontSize;
}

void App::set_font_size(int size) {
    m_settings.fontSize = std::clamp(size, 10, 24);
    SettingsStore::instance().save(m_settings);

    if (m_text) m_text->set_font_size(m_settings.fontSize);
}

int App::get_read_mode() const {
    return m_currentBook.readMode;
}

void App::set_read_mode(int mode) {
    m_currentBook.readMode = mode;
    m_settings.defaultReadMode = mode;
    SettingsStore::instance().save(m_settings);
}

// ============================================================
// 渲染器访问器（供 ReaderPage 等 UI 层读取）
// 注意：返回裸指针，指向 App 持有的 unique_ptr 内部对象。
// App 生命周期内安全；App 析构后悬空 —— 但 App 是单例，
// UI 层 View 在 App 存活期内才存在，所以 OK。
// ============================================================

MuPDFRenderer* App::get_mupdf() {
    return m_mupdf.get();
}

TextRenderer* App::get_text() {
    return m_text.get();
}

} // namespace mjnexus
