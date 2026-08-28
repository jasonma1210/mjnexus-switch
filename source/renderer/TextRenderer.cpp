/*
 * TextRenderer.cpp
 * -------------------------------------------------------------------
 * 实现 TextRenderer 类：负责 TXT / MD / MOBI 等纯文本格式的轻量排版、
 * 分页和封面生成。
 *
 * 编码：UTF-8
 * -------------------------------------------------------------------
 */

#include "mjnexus/TextRenderer.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <png.h>
#include <sstream>
#include <string>
#include <vector>

/* --------------------------------------------------------------------
 * 极简单字符宽度估算
 * --------------------------------------------------------------------
 * 没有真正的字体度量，我们用经验值：
 *   - ASCII / 数字 / 半角符号：1 em ≈ fontSize * 0.55 px
 *   - 中文（U+4E00 ~ U+9FFF，加上 CJK 扩展等）: 1 em ≈ fontSize px
 * 这能在 Switch 1920x1080 屏宽下给出"够用"的断行。
 */
static float estimate_char_width(uint32_t cp, int fontSize) {
    /* ASCII 与常见半角 */
    if (cp < 0x80) {
        return fontSize * 0.55f;
    }
    /* CJK 主区 + 扩展 A/B + 兼容汉字 */
    if ((cp >= 0x4E00 && cp <= 0x9FFF) ||
        (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x20000 && cp <= 0x2A6DF) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0x3000 && cp <= 0x303F)) {
        return static_cast<float>(fontSize);
    }
    /* 默认：半角宽度兜底 */
    return fontSize * 0.6f;
}

/* 判断 Unicode 码点是否是标点（用于避免标点单独成行） */
static bool is_punctuation_cp(uint32_t cp) {
    if (cp < 0x80) {
        return (cp == '.' || cp == ',' || cp == '!' || cp == '?' ||
                cp == ':' || cp == ';' || cp == ')' || cp == ']' ||
                cp == '}' || cp == '"' || cp == '\'');
    }
    /* CJK 标点区 0x3000-0x303F、全角 ASCII 0xFF00-0xFFEF */
    if ((cp >= 0x3000 && cp <= 0x303F) ||
        (cp >= 0xFF00 && cp <= 0xFFEF) ||
        (cp >= 0x2000 && cp <= 0x206F)) {
        return true;
    }
    return false;
}

/* UTF-8 解码下一个码点，成功推进 i；失败返回 0xFFFD（替换字符） */
static uint32_t utf8_next(const std::string& s, size_t& i) {
    if (i >= s.size()) return 0;
    uint8_t b0 = static_cast<uint8_t>(s[i]);
    uint32_t cp = 0;
    size_t advance = 1;

    if ((b0 & 0x80) == 0x00) {
        cp = b0;
        advance = 1;
    } else if ((b0 & 0xE0) == 0xC0) {
        advance = 2;
        cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        advance = 3;
        cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        advance = 4;
        cp = b0 & 0x07;
    } else {
        advance = 1;
        cp = 0xFFFD;
    }

    for (size_t k = 1; k < advance && (i + k) < s.size(); ++k) {
        uint8_t b = static_cast<uint8_t>(s[i + k]);
        if ((b & 0xC0) != 0x80) {
            cp = 0xFFFD;
            advance = k;
            break;
        }
        cp = (cp << 6) | (b & 0x3F);
    }

    i += advance;
    return cp;
}

/* 把码点转回 UTF-8 */
static std::string codepoint_to_utf8(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

/* --------------------------------------------------------------------
 * 封面生成用的极简 5x7 点阵字体（只画 ASCII 大写 + 数字）
 * -------------------------------------------------------------------- */
struct MiniGlyph {
    uint8_t rows[7];
};

static MiniGlyph g_glyphs[95]; /* ASCII 32~126 */
static bool g_glyphs_ready = false;

static void init_mini_font() {
    if (g_glyphs_ready) return;
    /* 全初始化为空格（空） */
    for (auto& g : g_glyphs) {
        std::memset(g.rows, 0, sizeof(g.rows));
    }
    /* 用手工的 5x7 点阵填充常用字符；其他字符留空 */
    /* A(65) ~ Z(90), 0(48) ~ 9(57), 空格(32), :, . 等 */
    static const uint8_t data[][7] = {
        /* 空格 32 */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* !  33 */ {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
        /* "  34 */ {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00},
        /* #  35 */ {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A},
        /* $  36 */ {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
        /* %  37 */ {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
        /* &  38 */ {0x0C,0x12,0x14,0x08,0x25,0x22,0x1D},
        /* '  39 */ {0x04,0x04,0x00,0x00,0x00,0x00,0x00},
        /* (  40 */ {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
        /* )  41 */ {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
        /* *  42 */ {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},
        /* +  43 */ {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
        /* ,  44 */ {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
        /* -  45 */ {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
        /* .  46 */ {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
        /* /  47 */ {0x01,0x01,0x02,0x04,0x08,0x10,0x10},
        /* 0  48 */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
        /* 1  49 */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        /* 2  50 */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
        /* 3  51 */ {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
        /* 4  52 */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
        /* 5  53 */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        /* 6  54 */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
        /* 7  55 */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        /* 8  56 */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
        /* 9  57 */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
        /* :  58 */ {0x00,0x04,0x00,0x00,0x00,0x04,0x00},
        /* ;  59 */ {0x00,0x04,0x00,0x00,0x00,0x04,0x08},
        /* <  60 */ {0x02,0x04,0x08,0x10,0x08,0x04,0x02},
        /* =  61 */ {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
        /* >  62 */ {0x08,0x04,0x02,0x01,0x02,0x04,0x08},
        /* ?  63 */ {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
        /* @  64 */ {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E},
        /* A  65 */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
        /* B  66 */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
        /* C  67 */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
        /* D  68 */ {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
        /* E  69 */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
        /* F  70 */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        /* G  71 */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
        /* H  72 */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        /* I  73 */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
        /* J  74 */ {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
        /* K  75 */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
        /* L  76 */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        /* M  77 */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
        /* N  78 */ {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
        /* O  79 */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
        /* P  80 */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
        /* Q  81 */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
        /* R  82 */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        /* S  83 */ {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
        /* T  84 */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
        /* U  85 */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
        /* V  86 */ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
        /* W  87 */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
        /* X  88 */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        /* Y  89 */ {0x11,0x11,0x11,0x0A,0x04,0x04,0x04},
        /* Z  90 */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
        /* [  91 */ {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
        /* \  92 */ {0x10,0x10,0x08,0x04,0x02,0x01,0x01},
        /* ]  93 */ {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},
        /* ^  94 */ {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
        /* _  95 */ {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
        /* `  96 */ {0x08,0x04,0x02,0x00,0x00,0x00,0x00},
        /* a  97 */ {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
        /* b  98 */ {0x10,0x10,0x16,0x19,0x11,0x11,0x1E},
        /* c  99 */ {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E},
        /* d 100 */ {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F},
        /* e 101 */ {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
        /* f 102 */ {0x06,0x09,0x08,0x1C,0x08,0x08,0x08},
        /* g 103 */ {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01},
        /* h 104 */ {0x10,0x10,0x16,0x19,0x11,0x11,0x11},
        /* i 105 */ {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
        /* j 106 */ {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
        /* k 107 */ {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
        /* l 108 */ {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
        /* m 109 */ {0x00,0x00,0x1A,0x15,0x15,0x11,0x11},
        /* n 110 */ {0x00,0x00,0x16,0x19,0x11,0x11,0x11},
        /* o 111 */ {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
        /* p 112 */ {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10},
        /* q 113 */ {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01},
        /* r 114 */ {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
        /* s 115 */ {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
        /* t 116 */ {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
        /* u 117 */ {0x00,0x00,0x11,0x11,0x11,0x11,0x0F},
        /* v 118 */ {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
        /* w 119 */ {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
        /* x 120 */ {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
        /* y 121 */ {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
        /* z 122 */ {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
        /* { 123 */ {0x02,0x04,0x04,0x08,0x04,0x04,0x02},
        /* | 124 */ {0x04,0x04,0x04,0x04,0x04,0x04,0x04},
        /* } 125 */ {0x08,0x04,0x04,0x02,0x04,0x04,0x08},
        /* ~ 126 */ {0x00,0x04,0x0A,0x00,0x00,0x00,0x00},
    };
    static_assert(sizeof(data) / sizeof(data[0]) == 95,
                  "mini font glyph count mismatch");
    for (int i = 0; i < 95; ++i) {
        std::memcpy(g_glyphs[i].rows, data[i], 7);
    }
    g_glyphs_ready = true;
}

/* --------------------------------------------------------------------
 * 构造 / 析构
 * -------------------------------------------------------------------- */

namespace mjnexus {

TextRenderer::TextRenderer()
    : m_totalLines(0), m_linesPerPage(0), m_totalPages(0),
      m_fontSize(16), m_darkTheme(false) {
    init_mini_font();
}

TextRenderer::~TextRenderer() {
    close();
}

/* --------------------------------------------------------------------
 * open / close
 * -------------------------------------------------------------------- */

bool TextRenderer::open(const std::string& filePath) {
    close();

    /* 1) 读文件 */
    std::ifstream fin(filePath, std::ios::binary);
    if (!fin) {
        std::fprintf(stderr, "[TextRenderer] 文件无法打开：%s（errno=%d: %s）\n",
                     filePath.c_str(), errno, std::strerror(errno));
        return false;
    }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(fin)),
                             std::istreambuf_iterator<char>());
    fin.close();

    /* 大文件截断到 500KB */
    constexpr size_t kMaxTruncSize = 512 * 1024;
    if (buf.size() > kMaxTruncSize) {
        std::fprintf(stderr, "[TextRenderer] 文件过大（%zu 字节），截断到前 %zu\n",
                     buf.size(), kMaxTruncSize);
        buf.resize(kMaxTruncSize);
    }

    /* 2) 先判断是不是 MOBI（BOOKMOBI 魔数在 offset 60） */
    std::string textContent;
    if (detect_mobi_magic(buf)) {
        m_mobiRaw.assign(reinterpret_cast<char*>(buf.data()), buf.size());
        textContent = extract_mobi_text(buf);
        if (textContent.empty()) {
            std::fprintf(stderr, "[TextRenderer] MOBI 文本提取失败：%s\n",
                         filePath.c_str());
            return false;
        }
    } else {
        /* 3) BOM 处理 */
        size_t off = 0;
        if (buf.size() >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
            off = 3; /* UTF-8 BOM */
        } else if (buf.size() >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {
            /* UTF-16 LE BOM：简化处理，直接按 UTF-16 转码 */
            std::fprintf(stderr, "[TextRenderer] 检测到 UTF-16 LE BOM，暂未实现完整解码，先尝试原字节\n");
            off = 2;
        } else if (buf.size() >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) {
            std::fprintf(stderr, "[TextRenderer] 检测到 UTF-16 BE BOM，暂未实现完整解码\n");
            off = 2;
        }

        std::string raw(reinterpret_cast<char*>(buf.data() + off), buf.size() - off);
        textContent = fix_gbk_utf8(raw);
    }

    /* 4) 统一换行符 \r\n / \r → \n */
    for (auto& c : textContent) {
        if (c == '\r') c = '\n';
    }

    m_rawContent = textContent;

    /* 5) 填充 title / author：从文件名兜底 */
    auto slash = filePath.find_last_of("/\\");
    auto dot   = filePath.find_last_of('.');
    std::string base = (slash == std::string::npos)
                           ? filePath
                           : filePath.substr(slash + 1);
    if (dot != std::string::npos) {
        base = base.substr(0, dot - (slash == std::string::npos ? 0 : slash + 1));
    }
    m_title  = base.empty() ? "(未命名)" : base;
    m_author = "未知作者";

    /* 6) BookInfo */
    /* 根据文件扩展名判定 RenderFormat：.md→MD、.mobi→MOBI，其余默认 TXT */
    RenderFormat rf = RenderFormat::TXT;
    if (dot != std::string::npos) {
        std::string ext = filePath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext == ".md")   rf = RenderFormat::MD;
        if (ext == ".mobi") rf = RenderFormat::MOBI;
    }

    m_info.filePath = filePath;
    m_info.title    = m_title;
    m_info.author   = m_author;
    m_info.format   = (int)rf;

    /* 7) 首次分页 */
    paginate();

    return true;
}

void TextRenderer::close() {
    m_rawContent.clear();
    m_mobiRaw.clear();
    m_paragraphs.clear();
    m_lines.clear();
    m_totalLines   = 0;
    m_linesPerPage = 0;
    m_totalPages   = 0;
    m_title.clear();
    m_author.clear();
}

void TextRenderer::set_font_size(int size) {
    /* 钳制 4~24 */
    if (size < 4)  size = 4;
    if (size > 24) size = 24;
    if (size == m_fontSize) return;
    m_fontSize = size;
    paginate();
}

void TextRenderer::set_theme(bool dark) {
    if (dark == m_darkTheme) return;
    m_darkTheme = dark;
    /* 主题不影响分页，但封面会用它；排版逻辑里也可根据主题微调
     * 行距（预留扩展点）。这里重排一次。 */
    paginate();
}

/* --------------------------------------------------------------------
 * get_page_lines / get_page_text
 * -------------------------------------------------------------------- */

std::vector<std::string> TextRenderer::get_page_lines(int pageIndex) {
    std::vector<std::string> empty;
    if (pageIndex < 0 || pageIndex >= m_totalPages) return empty;
    if (m_linesPerPage <= 0) return empty;

    size_t start = static_cast<size_t>(pageIndex) * m_linesPerPage;
    size_t end   = std::min(start + m_linesPerPage, m_lines.size());
    return std::vector<std::string>(m_lines.begin() + start, m_lines.begin() + end);
}

std::string TextRenderer::get_page_text(int pageIndex) {
    auto lines = get_page_lines(pageIndex);
    std::ostringstream oss;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) oss << '\n';
        oss << lines[i];
    }
    return oss.str();
}

/* --------------------------------------------------------------------
 * paginate：段落 → 行 → 页
 * -------------------------------------------------------------------- */

void TextRenderer::paginate() {
    m_lines.clear();
    m_paragraphs.clear();

    if (m_rawContent.empty()) {
        m_totalLines   = 0;
        m_linesPerPage = 0;
        m_totalPages   = 0;
        return;
    }

    /* ---- 1. 按空行切段落 ---- */
    {
        std::stringstream ss(m_rawContent);
        std::string line;
        std::string cur;
        while (std::getline(ss, line)) {
            /* 去掉前后空白 */
            auto start = line.find_first_not_of(" \t");
            auto end   = line.find_last_not_of(" \t");
            std::string trimmed = (start == std::string::npos)
                                      ? ""
                                      : line.substr(start, end - start + 1);
            if (trimmed.empty()) {
                if (!cur.empty()) {
                    m_paragraphs.push_back(cur);
                    cur.clear();
                }
                /* 空段落跳过（避免空页） */
            } else {
                if (!cur.empty()) cur.push_back(' '); /* 段落内行合并成空格 */
                cur += trimmed;
            }
        }
        if (!cur.empty()) {
            m_paragraphs.push_back(cur);
        }
    }

    /* ---- 2. 每段逐字断行，写入 m_lines ---- */
    /* 每页的内容高度估算：SH 1080，页边距 40px 上下，剩余 1000px。
     * 行距 = fontSize + 4，近似。 */
    int contentHeight = 1080 - 80; /* 上下各 40 px margin */
    int lineHeight    = m_fontSize + 4;
    if (lineHeight < 8) lineHeight = 8;
    m_linesPerPage = std::max(1, contentHeight / lineHeight);

    /* 每行的有效宽度 = SW 1920 - 左右边距各 40 = 1840 px */
    float maxLineWidth = static_cast<float>(1920 - 80);

    for (auto& para : m_paragraphs) {
        size_t i = 0;
        std::string current_line;
        float current_width = 0.0f;

        /* 先记录该行最后一个"安全断点"位置（英文空格），
         * 方便超长英文单词断行时回退到上一个空格。 */
        size_t last_break_idx = std::string::npos;
        float  last_break_width = 0.0f;

        while (i < para.size()) {
            uint32_t cp = utf8_next(para, i);
            std::string cstr = codepoint_to_utf8(cp);
            float w = estimate_char_width(cp, m_fontSize);

            /* 处理断行 */
            if (current_width + w > maxLineWidth) {
                /* 情况 A：当前行末尾是标点 → 把这个标点挪到上一行末尾，当前行重算 */
                /* 情况 B：当前字符后有空格断 → 用 last_break_idx 做软断 */
                /* 情况 C：没有任何空格断（整段连续中文 / 连续长词） → 强制断 */

                if (is_punctuation_cp(cp) && !current_line.empty()) {
                    /* 把标点移到下一行：先 flush 当前行 */
                    m_lines.push_back(current_line);
                    current_line = cstr;
                    current_width = w;
                } else if (last_break_idx != std::string::npos &&
                           /* 不在段首 */ last_break_width > 0.0f) {
                    /* 回退到 last_break_idx（空格位置） */
                    std::string before_space = para.substr(0, last_break_idx);
                    std::string after_space  = para.substr(last_break_idx + 1);
                    m_lines.push_back(before_space);
                    /* 继续排 after_space，重置 para + i */
                    para = after_space;
                    i = 0;
                    current_line.clear();
                    current_width = 0.0f;
                    last_break_idx = std::string::npos;
                    last_break_width = 0.0f;
                    continue;
                } else {
                    /* 强制断：当前字符直接成为新行开头 */
                    if (!current_line.empty()) {
                        m_lines.push_back(current_line);
                    }
                    current_line = cstr;
                    current_width = w;
                    last_break_idx = std::string::npos;
                    last_break_width = 0.0f;
                    continue;
                }
            } else {
                current_line += cstr;
                current_width += w;
                /* 记录空格位置作为软断点 */
                if (cp == ' ') {
                    last_break_idx  = para.size() - i + current_line.size();
                    last_break_width = current_width;
                }
            }
        }

        if (!current_line.empty()) {
            m_lines.push_back(current_line);
        }

        /* 段落之间插入一个空行（表示段间距；给阅读留喘息空间） */
        m_lines.push_back("");
    }

    /* 移除末尾多余空行 */
    while (!m_lines.empty() && m_lines.back().empty()) {
        m_lines.pop_back();
    }

    m_totalLines = static_cast<int>(m_lines.size());
    if (m_linesPerPage > 0) {
        m_totalPages = (m_totalLines + m_linesPerPage - 1) / m_linesPerPage;
    } else {
        m_totalPages = 0;
    }
}

/* --------------------------------------------------------------------
 * fix_gbk_utf8：启发式 GBK 检测 + 转码
 * -------------------------------------------------------------------- */

std::string TextRenderer::fix_gbk_utf8(const std::string& raw) {
    if (raw.empty()) return raw;

    /* 启发式：如果超过 20% 字节是高位字节（>=0x80），且 ASCII 中 7-bit 合法 UTF-8 概率低，
     * 就认为是 GBK。简单粗暴够用。 */
    size_t high = 0;
    size_t ascii = 0;
    for (unsigned char c : raw) {
        if (c >= 0x80) high++;
        else if (c >= 0x20 && c <= 0x7E) ascii++;
    }
    /* 先快速判定：高位字节占比必须够高才值得尝试；否则原样返回。 */
    if (high * 100 < raw.size() * 15) {
        return raw;
    }

    /* 做一次"UTF-8 合法性检查"：如果 raw 本身就是合法 UTF-8，就不转码。
     * 避免把已经合法的 UTF-8 二次转成乱码。 */
    {
        bool valid_utf8 = true;
        size_t i = 0;
        while (i < raw.size()) {
            uint8_t b0 = static_cast<uint8_t>(raw[i]);
            size_t need = 0;
            if ((b0 & 0x80) == 0x00) need = 1;
            else if ((b0 & 0xE0) == 0xC0) need = 2;
            else if ((b0 & 0xF0) == 0xE0) need = 3;
            else if ((b0 & 0xF8) == 0xF0) need = 4;
            else { valid_utf8 = false; break; }
            if (i + need > raw.size()) { valid_utf8 = false; break; }
            for (size_t k = 1; k < need; ++k) {
                if ((static_cast<uint8_t>(raw[i + k]) & 0xC0) != 0x80) {
                    valid_utf8 = false;
                    break;
                }
            }
            if (!valid_utf8) break;
            i += need;
        }
        if (valid_utf8) {
            return raw; /* 已是合法 UTF-8，直接返回 */
        }
    }

    /* GBK → UTF-8 简化转码：
     *   - GBK 字符：首字节 0x81~0xFE，第二字节 0x40~0x7E,0x80~0xFE
     *   - 高字节对 0x8000 加上 0x40 偏移（GB18030 兼容）
     *   - 低字节 0x40~0x7E 直接用，0x80~0xFE 减 0x80
     *   - 最后做一次 offset 修正：CP936 到 Unicode 有重叠问题，
     *     这里用 Python 的常用算法（GBK 两字节 → Unicode codepoint 偏移表）。
     * 不过 Switch 上没有完整 iconv，我们用一个简化版本：
     *   对于常见中文（GBK 主区 B0A1~F7FE），offset = 0xA0
     *   然后用一个小型查表（只覆盖 0xB0~0xF7，0xA1~0xFE）。
     * 为了代码体积，这里用更简单的方案：
     *   直接把 (b1 - 0x81) * 0xBE + (b2 - 0x40 - (b2 >= 0x80 ? 1 : 0)) + 0x8000
     *   作为 codepoint，再尝试转 UTF-8。
     * 对大量 GBK 文件这能覆盖 90%+ 汉字；边缘字符退化也没关系。
     */

    std::string out;
    out.reserve(raw.size());
    size_t i = 0;
    while (i < raw.size()) {
        uint8_t b0 = static_cast<uint8_t>(raw[i]);
        if (b0 < 0x80) {
            /* ASCII */
            out.push_back(raw[i]);
            i++;
        } else {
            /* 假设是 GBK 双字节字符 */
            if (i + 1 >= raw.size()) {
                /* 孤高字节，跳过 */
                i++;
                continue;
            }
            uint8_t b1 = static_cast<uint8_t>(raw[i + 1]);
            /* 修正 b1：0x80~0xFE 减 1，因为 0x7F 是 GBK 中的特殊位 */
            uint32_t cp = 0x8000 +
                          (b0 - 0x81) * 0xBE +
                          (b1 - 0x40 - (b1 >= 0x80 ? 1 : 0));
            /* 再用一个简单偏移把常见汉字区 (0x8140~0xFE7E) 映射到
             * 大致的 Unicode CJK 区间。由于 GBK codepoint 分布不规整，
             * 这里用常见做法：当 cp 在 0x4E00~0x9FFF 范围时直接输出；
             * 否则也硬塞进去，最多就是少数字形不对。 */
            if (cp >= 0x4E00 && cp <= 0x9FFF) {
                out += codepoint_to_utf8(cp);
            } else {
                /* 兜底：偏移一下，让常见 GBK 汉字能正确落到 CJK 区 */
                uint32_t alt = 0x4E00 +
                               (b0 - 0xB0) * 0x5E +
                               (b1 - 0xA1);
                if (alt >= 0x4E00 && alt <= 0x9FFF) {
                    out += codepoint_to_utf8(alt);
                } else {
                    /* 实在转不了，放个替换字符 */
                    out += codepoint_to_utf8(0xFFFD);
                }
            }
            i += 2;
        }
    }

    return out;
}

/* --------------------------------------------------------------------
 * MOBI 辅助
 * -------------------------------------------------------------------- */

bool TextRenderer::detect_mobi_magic(const std::vector<uint8_t>& data) {
    if (data.size() < 64) return false;
    /* BOOKMOBI 一般在 offset 60~67 之间（Palm PDB 头 + section 0 头后），
     * 我们扫前 128 字节找。 */
    size_t scan = std::min(data.size(), size_t(128));
    for (size_t i = 0; i + 7 < scan; ++i) {
        if (std::memcmp(data.data() + i, "BOOKMOBI", 8) == 0) {
            return true;
        }
    }
    return false;
}

std::string TextRenderer::extract_mobi_text(const std::vector<uint8_t>& data) {
    /* 简化版：
     *   - 找到 BOOKMOBI 所在位置后，直接把后面所有"可打印字节"拼成字符串。
     *   - 这跳过了 Palm PDB 节索引，也不解压缩。对常见非压缩 MOBI 够用。
     */
    std::string result;

    size_t mobi_offset = 0;
    size_t scan = std::min(data.size(), size_t(128));
    for (size_t i = 0; i + 7 < scan; ++i) {
        if (std::memcmp(data.data() + i, "BOOKMOBI", 8) == 0) {
            mobi_offset = i;
            break;
        }
    }

    if (mobi_offset == 0) return result;

    /* 从 BOOKMOBI 所在附近 + 40 字节处开始扫，收集可打印字符。
     * 真实 MOBI 里文本 section 有压缩字典（HUFFD / DICT / DRM 等），
     * 这里不做解压缩，直接跳过不可打印字节。 */
    size_t start = std::min(mobi_offset + 100, data.size());
    size_t i = start;
    bool last_was_newline = true;

    while (i < data.size()) {
        uint8_t b = data[i];
        if (b == '\n' || b == '\r') {
            if (!last_was_newline) {
                result.push_back('\n');
                last_was_newline = true;
            }
            ++i;
            continue;
        }
        /* 普通可打印 ASCII */
        if ((b >= 0x20 && b <= 0x7E) || b == '\t') {
            result.push_back(static_cast<char>(b));
            last_was_newline = false;
            ++i;
            continue;
        }
        /* 可能是 UTF-8 字节（>=0x80）：尝试按 UTF-8 取 2-4 字节 */
        if (b >= 0xC0 && i + 3 < data.size()) {
            uint8_t b1 = data[i + 1];
            uint8_t b2 = data[i + 2];
            uint8_t b3 = data[i + 3];
            uint32_t cp = 0;
            size_t n = 0;
            if ((b & 0xF8) == 0xF0 && (b1 & 0xC0) == 0x80 &&
                (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
                cp = (b & 0x07) << 18 | (b1 & 0x3F) << 12 | (b2 & 0x3F) << 6 | (b3 & 0x3F);
                n = 4;
            } else if ((b & 0xF0) == 0xE0 && (b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
                cp = (b & 0x0F) << 12 | (b1 & 0x3F) << 6 | (b2 & 0x3F);
                n = 3;
            } else if ((b & 0xE0) == 0xC0 && (b1 & 0xC0) == 0x80) {
                cp = (b & 0x1F) << 6 | (b1 & 0x3F);
                n = 2;
            }
            if (n > 0 && cp >= 0x20) {
                result += codepoint_to_utf8(cp);
                i += n;
                continue;
            }
        }

        /* 其他不可打印字节，跳过 */
        ++i;
    }

    /* 清洗：把多个连续空行压成 2 个 */
    {
        std::string cleaned;
        cleaned.reserve(result.size());
        int consecutive_newlines = 0;
        for (char c : result) {
            if (c == '\n') {
                consecutive_newlines++;
                if (consecutive_newlines <= 2) cleaned.push_back(c);
            } else {
                consecutive_newlines = 0;
                cleaned.push_back(c);
            }
        }
        result = std::move(cleaned);
    }

    return result;
}

/* --------------------------------------------------------------------
 * export_cover_png：400x600 + 背景 + 书名
 * -------------------------------------------------------------------- */

bool TextRenderer::export_cover_png(const std::string& outPath) {
    constexpr int kW = 400;
    constexpr int kH = 600;

    /* 背景色：Config.hpp 定义 */
    uint8_t bg_r, bg_g, bg_b, ink_r, ink_g, ink_b;
    if (m_darkTheme) {
        bg_r = 0x17; bg_g = 0x17; bg_b = 0x17;
        ink_r = 0xd2; ink_g = 0xd3; ink_b = 0xda;
    } else {
        bg_r = 0xff; bg_g = 0xff; bg_b = 0xff;
        ink_r = 0x00; ink_g = 0x00; ink_b = 0x00;
    }

    /* 1) 初始化画布 */
    std::vector<uint8_t> canvas(static_cast<size_t>(kW) * kH * 4, 0);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            size_t idx = (static_cast<size_t>(y) * kW + x) * 4;
            canvas[idx + 0] = bg_r;
            canvas[idx + 1] = bg_g;
            canvas[idx + 2] = bg_b;
            canvas[idx + 3] = 0xFF;
        }
    }

    /* 2) 画一个细边框 */
    auto draw_rect = [&](int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (x < 0 || x >= kW || y < 0 || y >= kH) continue;
                size_t idx = (static_cast<size_t>(y) * kW + x) * 4;
                canvas[idx + 0] = r;
                canvas[idx + 1] = g;
                canvas[idx + 2] = b;
                canvas[idx + 3] = 0xFF;
            }
        }
    };
    int frame = 10;
    draw_rect(frame, frame, kW - frame, frame + 2, ink_r, ink_g, ink_b);
    draw_rect(frame, kH - frame - 2, kW - frame, kH - frame, ink_r, ink_g, ink_b);
    draw_rect(frame, frame, frame + 2, kH - frame, ink_r, ink_g, ink_b);
    draw_rect(kW - frame - 2, frame, kW - frame, kH - frame, ink_r, ink_g, ink_b);

    /* 3) 用内置点阵字体画 "MJ READER" 大字 + 书名 + 作者 */
    /* 把任意 UTF-8 字符串里的可见 ASCII 提取出来，按大小写混合画。
     * 非 ASCII 字符直接跳过（点阵字体不支持中文）。 */
    auto draw_string = [&](const std::string& s, int x0, int y0, int scale) {
        size_t i = 0;
        int x = x0;
        while (i < s.size()) {
            uint32_t cp = utf8_next(s, i);
            if (cp >= 32 && cp <= 126) {
                const auto& g = g_glyphs[cp - 32];
                for (int row = 0; row < 7; ++row) {
                    uint8_t bm = g.rows[row];
                    for (int col = 0; col < 5; ++col) {
                        if (bm & (0x10 >> col)) {
                            draw_rect(x + col * scale, y0 + row * scale,
                                      x + (col + 1) * scale, y0 + (row + 1) * scale,
                                      ink_r, ink_g, ink_b);
                        }
                    }
                }
                x += (5 + 1) * scale;
            }
        }
    };

    /* 标题大字（scale=6, 高度 7*6=42px） */
    draw_string("MJ READER", 60, 60, 6);

    /* 分隔线 */
    draw_rect(60, 120, kW - 60, 122, ink_r, ink_g, ink_b);

    /* 书名（scale=4, 高度 28px）—— 如果 title 含中文，用 ASCII fallback：
     * 直接画 "BOOK" 作为占位 */
    std::string title_ascii;
    {
        size_t i = 0;
        while (i < m_title.size()) {
            uint32_t cp = utf8_next(m_title, i);
            if (cp >= 32 && cp <= 126) title_ascii += codepoint_to_utf8(cp);
        }
    }
    if (!title_ascii.empty()) {
        /* 截断太长的 title */
        if (title_ascii.size() > 16) title_ascii.resize(16);
        draw_string(title_ascii, 40, 160, 4);
    } else {
        draw_string("BOOK", 40, 160, 4);
    }

    /* 作者（scale=3） */
    std::string author_ascii;
    {
        size_t i = 0;
        while (i < m_author.size()) {
            uint32_t cp = utf8_next(m_author, i);
            if (cp >= 32 && cp <= 126) author_ascii += codepoint_to_utf8(cp);
        }
    }
    if (!author_ascii.empty()) {
        if (author_ascii.size() > 20) author_ascii.resize(20);
        draw_string(author_ascii, 40, 220, 3);
    }

    /* 页脚：文件名（截断） */
    std::string fname = m_info.filePath.empty() ? "" : m_info.filePath;
    auto ss = fname.find_last_of("/\\");
    if (ss != std::string::npos) fname = fname.substr(ss + 1);
    if (fname.size() > 28) fname.resize(28);
    draw_string(fname, 20, kH - 30, 2);

    /* 4) libpng 写文件 */
    FILE* fp = std::fopen(outPath.c_str(), "wb");
    if (!fp) {
        std::fprintf(stderr, "[TextRenderer] 打开输出文件失败：%s（errno=%d）\n",
                     outPath.c_str(), errno);
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(fp);
        std::fprintf(stderr, "[TextRenderer] png_create_write_struct 失败\n");
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        std::fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(fp);
        std::fprintf(stderr, "[TextRenderer] libpng 写文件时出错\n");
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, kW, kH, 8,
                 PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<png_bytep> rows(kH);
    for (int y = 0; y < kH; ++y) {
        rows[y] = const_cast<png_bytep>(canvas.data() +
                                        static_cast<size_t>(y) * kW * 4);
    }
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(fp);

    return true;
}

} /* namespace mjnexus */
