/*
 * MuPDFRenderer.hpp
 * -------------------------------------------------------------------
 * 负责 PDF / EPUB / XPS / CBZ / CBR / CBT / CB7 等格式的渲染。
 * 底层依赖 MuPDF（libmupdf.a），通过 fz_context / fz_document /
 * fz_page / fz_pixmap 完成页面像素解码与元数据 / 目录提取。
 *
 * Switch 项目里，MuPDF 是静态链接的，所以本类直接 include <mupdf/fitz.h>。
 *
 * 作者：mjnexus-switch 项目
 * 编码：UTF-8
 * -------------------------------------------------------------------
 */

#ifndef MJNEXUS_MUPDF_RENDERER_HPP
#define MJNEXUS_MUPDF_RENDERER_HPP

#include <string>
#include <vector>
#include <utility>

#include <mupdf/fitz.h>

#include "mjnexus/Config.hpp"

namespace mjnexus {

class MuPDFRenderer {
public:
    /* ============================================================
     * 生命周期
     * ============================================================ */

    MuPDFRenderer();
    ~MuPDFRenderer();

    /* 禁止拷贝：MuPDF 资源拥有权唯一 */
    MuPDFRenderer(const MuPDFRenderer&) = delete;
    MuPDFRenderer& operator=(const MuPDFRenderer&) = delete;

    /* ============================================================
     * 公共 API
     * ============================================================ */

    /* 打开文档。内部会做：
     *   1) 检测格式（魔数 + 扩展名）
     *   2) fz_open_document 创建 fz_document
     *   3) 读取页数
     *   4) 读取 title / author 元数据（失败则回退到文件名）
     * 返回 true 表示成功。失败原因会输出到 stderr。
     */
    bool open(const std::string& filePath);

    /* 释放所有 MuPDF 资源。如果当前未打开则是 no-op。 */
    void close();

    /* 是否已经持有打开的文档。 */
    bool is_open() const;

    /* 返回总页数；未打开返回 0。 */
    int get_total_pages() const;

    /* 渲染指定页为像素图。
     *   outPixmap   输出的 fz_pixmap*，调用方在用完之后必须 fz_drop_pixmap(ctx, pixmap)。
     *   scale       缩放倍率，1.0f 表示按文档原始 DPI（MuPDF 默认 72）渲染。
     * 返回 false 表示渲染失败，此时 *outPixmap 不会被赋值。
     */
    bool get_page_pixmap(int pageIndex, fz_pixmap** outPixmap, float scale = 1.0f);

    /* 将指定页渲染成 PNG 写入 outPath。常用于生成封面。
     * 内部会临时创建一个 scale=1.0f 的 pixmap 并调用 fz_save_pixmap_as_png。
     */
    bool save_page_png(int pageIndex, const std::string& outPath);

    /* 提取某页纯文本。PDF / EPUB 可用；CBZ / CBR 这类图册返回空字符串。
     * 未来可用于 AI 摘要 / 上下文拼接。
     */
    bool extract_text(int pageIndex, std::string& outText);

    /* 提取整本书的目录，返回 {(title, pageIndex), ...}。
     *   - PDF / EPUB：从 fz_outline_document 递归拿到
     *   - CBZ / CBR：无目录，返回空 vector
     *   - title 可能为空字符串
     */
    std::vector<std::pair<std::string, int>> get_toc();

    /* 将目录里的页码映射到 MuPDF 的真实 pageIndex。
     * 不同格式目录的页码语义不同：
     *   - PDF 目录里的页码通常是 "PDF page number"，和 fz_count_pages 计数一致
     *   - EPUB 可能是章节起始页
     * 当前简化实现：直接返回 tocPage - 1（假设目录里是 1-based 页码）
     * 如果以后发现实际有偏移，在这里集中修补即可。
     */
    int map_toc_page_to_index(int tocPage);

    /* ============================================================
     * 成员访问器（方便外部读元数据）
     * ============================================================ */
    const BookInfo& get_book_info() const { return m_info; }
    const std::string& get_format() const { return m_format; }
    fz_context* get_context() const { return m_ctx; }
    fz_document* get_document() const { return m_doc; }

private:
    /* ============================================================
     * 私有成员
     * ============================================================ */

    /* MuPDF 上下文；构造里创建，close 里销毁。
     * 所有 MuPDF 调用都需要传这个 ctx。 */
    fz_context* m_ctx;

    /* 打开的文档；只在 open() 成功后非空。 */
    fz_document* m_doc;

    /* MuPDF 识别的格式字符串，例如 "pdf" / "epub" / "xps" / "cbz" / "cbr"。 */
    std::string m_format;

    /* 总页数，从 fz_count_pages 来。 */
    int m_totalPages;

    /* 关联的 BookInfo。open() 会尽量填充 title / author / format，
     * 调用方可以复用这个结构塞进数据库。 */
    BookInfo m_info;

    /* ============================================================
     * 私有方法
     * ============================================================ */

    /* 读文件魔数 + 扩展名，返回 MuPDF 能识别的格式字符串。
     *   - 扩展名优先（快速）
     *   - 魔数二次确认（防文件后缀伪造）
     *   - 无法识别时返回空字符串
     */
    std::string detect_format(const std::string& filePath);

    /* 用 fz_lookup_metadata 读 title / author 元数据；
     * 任一字段失败都回退到从文件名截出来的那一份。
     */
    void read_metadata();

    /* 递归遍历 MuPDF 的 fz_outline 树，扁平化返回
     *   {(title, pageIndex), ...}
     * pageIndex 由 fz_resolve_link 解析，不存在则用 -1 占位。
     */
    std::vector<std::pair<std::string, int>> walk_outline(fz_outline* node);
};

} /* namespace mjnexus */

#endif /* MJNEXUS_MUPDF_RENDERER_HPP */
