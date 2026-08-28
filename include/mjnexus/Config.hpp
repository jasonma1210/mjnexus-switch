#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <chrono>

namespace mjnexus {

// ============================================================
// Switch 屏幕物理分辨率（1920x1080 是主机模式；
// 掌机模式实际渲染为 1280x720，Borealis 会自动做缩放）
// ============================================================
constexpr int NINTENDO_SWITCH_SCREEN_W = 1920;
constexpr int NINTENDO_SWITCH_SCREEN_H = 1080;

// SD 卡上应用数据根目录（libnx 标准路径）
constexpr const char* SD_CARD_ROOT = "/switch/mjnexus/";

// 支持的文件扩展名（小写，带点号）
constexpr std::array<const char*, 10> SUPPORTED_EXTENSIONS = {
    ".pdf", ".epub", ".xps", ".cbz", ".cbr", ".cbt", ".cb7", ".mobi", ".txt", ".md"
};

// ============================================================
// 渲染格式枚举
// ============================================================
enum class RenderFormat : int {
    PDF  = 0,
    EPUB = 1,
    XPS  = 2,
    CBZ  = 3,
    CBR  = 4,
    CBT  = 5,
    CB7  = 6,
    MOBI = 7,
    TXT  = 8,
    MD   = 9,
    UNKNOWN = -1
};

// ============================================================
// 阅读模式枚举
// ============================================================
enum class ReadMode : int {
    PORTRAIT     = 0,   // 竖屏单列，整页自上而下列
    LANDSCAPE    = 1,   // 横屏单页（整页等比缩放）
    VERTICAL_FIT = 2,   // 竖屏滚动到底，宽度自适应
    SPREAD_TWO_PAGE = 3 // 横屏双页并排（跨页模式）
};

// ============================================================
// 主题枚举
// ============================================================
enum class Theme : int {
    LIGHT = 0,
    DARK  = 1
};

// ============================================================
// MJNexus 色板 —— 使用简单的 8bit RGBA 四通道
// 不依赖 Borealis 头文件，Config.hpp 保持纯净
// ============================================================
struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    constexpr Color() = default;
    constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
};

// 浅色主题色板
struct LightPalette {
    static constexpr Color bg      = {0xff, 0xff, 0xff, 0xff};
    static constexpr Color ink     = {0x00, 0x00, 0x00, 0xff};
    static constexpr Color border  = {0xe6, 0xe6, 0xe6, 0xff};
    static constexpr Color sidebar = {0xf5, 0xf5, 0xf5, 0xff};
    static constexpr Color card    = {0xff, 0xff, 0xff, 0xff};
    static constexpr Color muted   = {0x66, 0x66, 0x66, 0xff};
};

// 深色主题色板
struct DarkPalette {
    static constexpr Color bg      = {0x17, 0x17, 0x17, 0xff};
    static constexpr Color ink     = {0xd2, 0xd3, 0xda, 0xff};
    static constexpr Color border  = {0x38, 0x38, 0x38, 0xff};
    static constexpr Color sidebar = {0x26, 0x26, 0x26, 0xff};
    static constexpr Color card    = {0x1f, 0x1f, 0x1f, 0xff};
    static constexpr Color muted   = {0x88, 0x88, 0x88, 0xff};
};

// ============================================================
// 书籍元数据（来自 library.json + 文件扫描）
// ============================================================
struct BookInfo {
    std::string id;                  // 唯一 ID，默认用 filePath
    std::string filePath;            // SD 卡上的绝对路径
    std::string title;               // 书名
    std::string author;              // 作者
    uint64_t    fileSize      = 0;   // 文件字节数
    int         format        = (int)RenderFormat::UNKNOWN;
    int         totalPages    = 0;   // 总页数（异步获取，扫描时可能为 0）
    std::string coverPath;           // 封面 PNG 缓存路径
    int64_t     addedTimestamp   = 0; // 加入书架时间戳（Unix 秒）
    int64_t     lastReadTimestamp = 0;
    int         lastReadPage      = 0;
    float       lastReadPercentage = 0.0f;
    int         readMode          = (int)ReadMode::PORTRAIT;
};

// ============================================================
// 全局应用设置
// ============================================================
struct AppSettings {
    int         theme            = (int)Theme::LIGHT;
    int         fontSize         = 16;   // 10-24，默认 16
    int         defaultReadMode  = (int)ReadMode::PORTRAIT;
    int         sortBy           = 0;    // 0=name 1=recent 2=added
    std::string language         = "zh-CN";
    bool        naturalSort      = true;

    // 排序字段常量（供 sort_books 使用）
    static constexpr int SORT_NAME   = 0;
    static constexpr int SORT_RECENT = 1;
    static constexpr int SORT_ADDED  = 2;
};

// ============================================================
// 辅助工具函数（全部为小函数，直接 inline 在这里）
// ============================================================

inline std::string format_ext_to_mime(const std::string& ext) {
    // 输入期望是小写、带点号的扩展名，如 ".pdf"
    if (ext == ".pdf")  return "application/pdf";
    if (ext == ".epub") return "application/epub+zip";
    if (ext == ".xps")  return "application/vnd.ms-package.xps";
    if (ext == ".cbz")  return "application/x-cbz";
    if (ext == ".cbr")  return "application/x-cbr";
    if (ext == ".cbt")  return "application/x-cbt";
    if (ext == ".cb7")  return "application/x-cb7";
    if (ext == ".mobi") return "application/x-mobipocket-ebook";
    if (ext == ".txt")  return "text/plain";
    if (ext == ".md")   return "text/markdown";
    return "application/octet-stream";
}

inline std::string mime_to_format_str(int fmt) {
    switch ((RenderFormat)fmt) {
        case RenderFormat::PDF:  return "pdf";
        case RenderFormat::EPUB: return "epub";
        case RenderFormat::XPS:  return "xps";
        case RenderFormat::CBZ:  return "cbz";
        case RenderFormat::CBR:  return "cbr";
        case RenderFormat::CBT:  return "cbt";
        case RenderFormat::CB7:  return "cb7";
        case RenderFormat::MOBI: return "mobi";
        case RenderFormat::TXT:  return "txt";
        case RenderFormat::MD:   return "md";
        default: return "unknown";
    }
}

inline std::string read_mode_str(int mode) {
    switch ((ReadMode)mode) {
        case ReadMode::PORTRAIT:        return "portrait";
        case ReadMode::LANDSCAPE:       return "landscape";
        case ReadMode::VERTICAL_FIT:    return "vertical_fit";
        case ReadMode::SPREAD_TWO_PAGE: return "spread_two_page";
        default: return "portrait";
    }
}

// 取当前 Unix 时间戳（秒），inline 便于直接使用
inline int64_t now_unix_seconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace mjnexus
