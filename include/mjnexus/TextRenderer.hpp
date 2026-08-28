/*
 * TextRenderer.hpp
 * -------------------------------------------------------------------
 * 负责 TXT / MD / MOBI 等 MuPDF 支持有限或排版效果不理想的纯文本格式。
 * 自己实现一个极简的"段落 → 行 → 页"三段式排版器：
 *
 *     原始文件  →  解析成段落  →  逐段按屏宽断行  →  按每页行数分页
 *
 * 设计取舍：
 *   - 中英文断行规则简单实现：英文按空格断，中文按字符断；
 *     标点不单独成行（遇到标点时提前断到上一行）。
 *   - 不做复杂字体度量，字号换算成"近似像素宽"做简单估计。
 *   - 分页只依赖屏幕宽 SW=1920 与 fontSize / lineHeight，
 *     运行时改字号会自动重新 paginate。
 *
 * 编码：UTF-8
 * -------------------------------------------------------------------
 */

#ifndef MJNEXUS_TEXT_RENDERER_HPP
#define MJNEXUS_TEXT_RENDERER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "mjnexus/Config.hpp"

namespace mjnexus {

class TextRenderer {
public:
    /* ============================================================
     * 生命周期
     * ============================================================ */

    TextRenderer();
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    /* ============================================================
     * 公共 API
     * ============================================================ */

    /* 打开文件：
     *   - 二进制读取
     *   - BOM 检测 / 剥离
     *   - 无 BOM 时尝试 GBK→UTF-8 兜底
     *   - MOBI 文件检测 "BOOKMOBI" 魔数并抽纯文本
     *   - 超过 10MB 的文件截断到前 500KB（Switch 内存宝贵）
     * 成功返回 true；失败输出 stderr。
     */
    bool open(const std::string& filePath);

    /* 释放文件内容，重置所有分页状态。 */
    void close();

    bool is_open() const { return !m_rawContent.empty(); }

    int get_total_pages() const { return m_totalPages; }

    /* 设置字号（钳制到 4~24）。立即触发重排。 */
    void set_font_size(int size);

    /* true 深色 / false 浅色，触发重排。 */
    void set_theme(bool dark);

    /* 返回指定页应显示的"文本行"（按当前 fontSize + SW=1920 排版）。
     * 行已经过断行处理，可以直接逐行画到屏幕上。
     */
    std::vector<std::string> get_page_lines(int pageIndex);

    /* 返回指定页拼接好的完整文本（换行分隔），用于 AI 上下文。 */
    std::string get_page_text(int pageIndex);

    /* 画一张 400x600 的封面 PNG：
     *   - 背景根据 m_darkTheme 选 Config.hpp 里的 bg 颜色
     *   - 标题 + 作者 + "MJ READER" 占位
     *   - 使用 libpng 直接写像素
     * 如果 libpng 在某个平台链接不到，可以退化为 stb_image_write（同一套 lib 都在 Makefile 里）。
     * 这里直接用 libpng，因为 Makefile 已经 -lpng。
     */
    bool export_cover_png(const std::string& outPath);

    /* ============================================================
     * 成员访问器
     * ============================================================ */
    const std::string& get_title()  const { return m_title; }
    const std::string& get_author() const { return m_author; }
    const BookInfo& get_book_info() const { return m_info; }

private:
    /* ============================================================
     * 私有成员
     * ============================================================ */

    /* 原始文件内容（已经是 UTF-8）。 */
    std::string m_rawContent;

    std::string m_title;
    std::string m_author;

    /* 排版后的总行数（字号改变时会重算）。 */
    int m_totalLines;

    /* 每页可容纳的行数 = SH / (fontSize + lineSpacing)。 */
    int m_linesPerPage;

    /* 总页数 = ceil(m_totalLines / m_linesPerPage)。 */
    int m_totalPages;

    /* 按空行切分好的段落。排版是对段落做的。 */
    std::vector<std::string> m_paragraphs;

    int m_fontSize;   /* 默认 16 */
    bool m_darkTheme; /* 默认 false */

    /* MOBI 原始二进制内容，备用（当前没做复杂 MOBI 结构解析）。 */
    std::string m_mobiRaw;

    /* BookInfo：让调用方可以像 MuPDFRenderer 一样拿到。 */
    BookInfo m_info;

    /* 排版缓存：paginate() 填好后，get_page_lines 直接按索引切片。 */
    std::vector<std::string> m_lines;

    /* ============================================================
     * 私有方法
     * ============================================================ */

    /* 根据 m_fontSize + SW=1920 对所有段落重新排版：
     *   - 中文按字符断
     *   - 英文按空格断
     *   - 标点避免单独成行
     * 结果写入 m_lines / m_totalLines / m_linesPerPage / m_totalPages。
     */
    void paginate();

    /* 文件头无 BOM 时尝试用简单启发式检测 GBK 并转码成 UTF-8。
     * 检测：字节高位出现频率 + ASCII 比例。
     * 失败（或判定不是 GBK）时原样返回 raw。
     */
    std::string fix_gbk_utf8(const std::string& raw);

    /* 读取文件前 64 字节，查找 "BOOKMOBI" 魔数。
     * 命中则认为是 MOBI 文件。
     */
    bool detect_mobi_magic(const std::vector<uint8_t>& data);

    /* 简化版 MOBI 文本提取：
     *  - 跳过前 78 字节 header
     *  - 从 offset 16 读 TEXT 部分的 "offset"
     *  - 直接从那个偏移开始，把非控制字节拼成字符串
     * 不做压缩字典解包，不读 PalmDoc 的 bit-packed 流。
     * 能覆盖大多数常见 MOBI（非压缩或压缩但不密集）的可读文本。
     */
    std::string extract_mobi_text(const std::vector<uint8_t>& data);
};

} /* namespace mjnexus */

#endif /* MJNEXUS_TEXT_RENDERER_HPP */
