/*
 * MuPDFRenderer.cpp
 * -------------------------------------------------------------------
 * 实现 MuPDFRenderer 类。负责 PDF / EPUB / XPS / CBZ / CBR / CBT / CB7
 * 等格式的打开、渲染、文本提取、目录遍历。
 *
 * 编码：UTF-8
 * -------------------------------------------------------------------
 */

#include "mjnexus/MuPDFRenderer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace mjnexus {

/* ================================================================
 * 构造 / 析构
 * ================================================================ */

MuPDFRenderer::MuPDFRenderer()
    : m_ctx(nullptr), m_doc(nullptr), m_totalPages(0) {
    /*
     * MuPDF 的 context 是所有 API 的第一个参数。
     * 第三个参数 FZ_STORE_UNLIMITED 让它自己管理内存缓存上限。
     */
    m_ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!m_ctx) {
        std::fprintf(stderr, "[MuPDFRenderer] fz_new_context 失败，无法创建 MuPDF 上下文\n");
    }
}

MuPDFRenderer::~MuPDFRenderer() {
    /* 析构里自动 close，保证不会漏资源 */
    close();

    if (m_ctx) {
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
    }
}

/* ================================================================
 * 公共 API
 * ================================================================ */

bool MuPDFRenderer::is_open() const {
    return m_ctx != nullptr && m_doc != nullptr;
}

int MuPDFRenderer::get_total_pages() const {
    return m_totalPages;
}

bool MuPDFRenderer::open(const std::string& filePath) {
    if (!m_ctx) {
        std::fprintf(stderr, "[MuPDFRenderer] 上下文尚未创建，无法打开文档\n");
        return false;
    }

    /* 先确保之前打开的文档已关闭 */
    close();

    /* 1) 检测格式 */
    m_format = detect_format(filePath);
    if (m_format.empty()) {
        std::fprintf(stderr, "[MuPDFRenderer] 无法识别格式：%s\n", filePath.c_str());
        return false;
    }

    /* 2) 打开文档。用 fz_try / fz_catch 捕获 MuPDF 内部异常，
     *    因为 MuPDF 在出错时会 longjmp。 */
    bool ok = false;
    fz_try(m_ctx) {
        m_doc = fz_open_document(m_ctx, filePath.c_str());
        if (!m_doc) {
            std::fprintf(stderr, "[MuPDFRenderer] fz_open_document 返回空：%s\n",
                         filePath.c_str());
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "open_document failed");
        }

        /* 3) 读页数 */
        m_totalPages = fz_count_pages(m_ctx, m_doc);

        /* 4) 读元数据 */
        read_metadata();

        /* 5) 同步 BookInfo 的基本字段，便于调用方直接复用 */
        m_info.filePath = filePath;
        /* m_format 是 MuPDF 格式字符串，映射到 RenderFormat 枚举 */
        int rf = (int)RenderFormat::UNKNOWN;
        if      (m_format == "pdf")  rf = (int)RenderFormat::PDF;
        else if (m_format == "epub") rf = (int)RenderFormat::EPUB;
        else if (m_format == "xps")  rf = (int)RenderFormat::XPS;
        else if (m_format == "cbz")  rf = (int)RenderFormat::CBZ;
        else if (m_format == "cbr")  rf = (int)RenderFormat::CBR;
        else if (m_format == "cbt")  rf = (int)RenderFormat::CBT;
        else if (m_format == "cb7")  rf = (int)RenderFormat::CB7;
        m_info.format = rf;
        m_info.totalPages = m_totalPages;

        ok = true;
    }
    fz_catch(m_ctx) {
        std::fprintf(stderr, "[MuPDFRenderer] 打开文档失败：%s — %s\n",
                     filePath.c_str(), fz_caught_message(m_ctx));
        if (m_doc) {
            fz_drop_document(m_ctx, m_doc);
            m_doc = nullptr;
        }
        m_totalPages = 0;
        ok = false;
    }

    return ok;
}

void MuPDFRenderer::close() {
    if (m_doc && m_ctx) {
        fz_drop_document(m_ctx, m_doc);
    }
    m_doc = nullptr;
    m_totalPages = 0;
    m_format.clear();
}

bool MuPDFRenderer::get_page_pixmap(int pageIndex, fz_pixmap** outPixmap, float scale) {
    if (!is_open() || !outPixmap) {
        return false;
    }
    if (pageIndex < 0 || pageIndex >= m_totalPages) {
        std::fprintf(stderr, "[MuPDFRenderer] 页码越界：%d / %d\n",
                     pageIndex, m_totalPages);
        return false;
    }

    bool ok = false;
    *outPixmap = nullptr;

    fz_try(m_ctx) {
        /*
         * MuPDF 默认坐标系原点在左上角，单位是 pt(=1/72 英寸)。
         * 创建一个缩放矩阵：水平 scale 倍、垂直 scale 倍。
         * 想按屏幕宽 SW=1920 自适应，外部可以自己算 scale 再传进来。
         */
        fz_matrix transform = fz_scale(scale, scale);

        /*
         * fz_new_pixmap_from_page 会自动：
         *   - fz_load_page
         *   - fz_bound_page 算 bbox
         *   - fz_new_pixmap + fz_new_draw_device + fz_run_page
         *   - 最后 drop_page / drop_device
         * alpha = 0 表示不保留 alpha 通道，节省内存（Switch 显存紧张）。
         */
        fz_pixmap* pix = fz_new_pixmap_from_page(
            m_ctx,
            m_doc,
            pageIndex,
            transform,
            fz_device_rgb(m_ctx),
            0);
        if (!pix) {
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "new_pixmap_from_page 返回空");
        }

        *outPixmap = pix;
        ok = true;
    }
    fz_catch(m_ctx) {
        std::fprintf(stderr, "[MuPDFRenderer] 渲染第 %d 页失败：%s\n",
                     pageIndex, fz_caught_message(m_ctx));
        if (*outPixmap) {
            fz_drop_pixmap(m_ctx, *outPixmap);
            *outPixmap = nullptr;
        }
    }

    return ok;
}

bool MuPDFRenderer::save_page_png(int pageIndex, const std::string& outPath) {
    if (!is_open()) {
        return false;
    }

    fz_pixmap* pix = nullptr;
    if (!get_page_pixmap(pageIndex, &pix, 1.0f)) {
        return false;
    }

    bool ok = false;
    fz_try(m_ctx) {
        fz_save_pixmap_as_png(m_ctx, pix, outPath.c_str());
        ok = true;
    }
    fz_catch(m_ctx) {
        std::fprintf(stderr, "[MuPDFRenderer] 保存 PNG 失败：%s — %s\n",
                     outPath.c_str(), fz_caught_message(m_ctx));
    }

    fz_drop_pixmap(m_ctx, pix);
    return ok;
}

bool MuPDFRenderer::extract_text(int pageIndex, std::string& outText) {
    outText.clear();

    if (!is_open()) {
        return false;
    }
    if (pageIndex < 0 || pageIndex >= m_totalPages) {
        return false;
    }

    /*
     * 图册类格式（CBZ/CBR/CBT/CB7）本质上是图片包，没有文本层。
     * MuPDF 即使能打开也不会返回有用的文本，这里直接返回成功 + 空串，
     * 让上层把它当成"这页没文字"处理。
     */
    if (m_format == "cbz" || m_format == "cbr" ||
        m_format == "cbt" || m_format == "cb7") {
        return true;
    }

    bool ok = false;
    fz_try(m_ctx) {
        fz_page* page = fz_load_page(m_ctx, m_doc, pageIndex);
        if (!page) {
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "load_page 返回空");
        }

        /*
         * fz_new_text_device：创建一个特殊的 device，run_page 时会把页面里
         * 所有可识别的文本片段 append 到它内部的 buffer。
         */
        fz_output* out = fz_new_output_with_path(m_ctx, nullptr, 0);
        fz_device* dev = fz_new_text_device(m_ctx, out);

        fz_cookie cookie = {0};
        fz_matrix transform = fz_identity;
        fz_run_page(m_ctx, page, dev, transform, &cookie);

        /* 把 buffer 拉出来 */
        size_t buf_len = fz_output_write_pos(m_ctx, out);
        if (buf_len > 0) {
            outText.assign(fz_output_as_string(m_ctx, out), buf_len);
        }

        fz_drop_device(m_ctx, dev);
        fz_drop_output(m_ctx, out);
        fz_drop_page(m_ctx, page);

        ok = true;
    }
    fz_catch(m_ctx) {
        std::fprintf(stderr, "[MuPDFRenderer] 提取第 %d 页文本失败：%s\n",
                     pageIndex, fz_caught_message(m_ctx));
    }

    return ok;
}

std::vector<std::pair<std::string, int>> MuPDFRenderer::get_toc() {
    std::vector<std::pair<std::string, int>> result;

    if (!is_open()) {
        return result;
    }

    /* 图册没有目录 */
    if (m_format == "cbz" || m_format == "cbr" ||
        m_format == "cbt" || m_format == "cb7") {
        return result;
    }

    fz_outline* root = nullptr;
    fz_try(m_ctx) {
        root = fz_outline_document(m_ctx, m_doc);
    }
    fz_catch(m_ctx) {
        /* PDF/EPUB 可能根本没目录，静默返回空 */
        std::fprintf(stderr, "[MuPDFRenderer] 读取目录失败：%s\n",
                     fz_caught_message(m_ctx));
        return result;
    }

    if (!root) {
        return result;
    }

    result = walk_outline(root);

    fz_drop_outline(m_ctx, root);
    return result;
}

int MuPDFRenderer::map_toc_page_to_index(int tocPage) {
    /*
     * 目前约定目录里的页码是 1-based（PDF 大多数阅读器的习惯），
     * 所以直接 -1 得到 MuPDF 的 pageIndex（0-based）。
     * 如果以后遇到格式差异，只改这里就够了。
     */
    return tocPage - 1;
}

/* ================================================================
 * 私有方法
 * ================================================================ */

std::string MuPDFRenderer::detect_format(const std::string& filePath) {
    /* 1) 先按扩展名匹配（快速路径） */
    std::string ext;
    auto dot = filePath.rfind('.');
    if (dot != std::string::npos) {
        ext = filePath.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }

    /* 扩展名 -> MuPDF 格式字符串。和 Config.hpp 里的支持列表保持一致。 */
    if (ext == "pdf")  return "pdf";
    if (ext == "epub") return "epub";
    if (ext == "xps")  return "xps";
    if (ext == "cbz")  return "cbz";
    if (ext == "cbr")  return "cbr";
    if (ext == "cbt")  return "cbt";
    if (ext == "cb7")  return "cb7";

    /* 2) 扩展名没命中，读前 16 字节做魔数二次确认 */
    std::ifstream fin(filePath, std::ios::binary);
    if (!fin) {
        std::fprintf(stderr, "[MuPDFRenderer] 文件无法打开：%s（errno=%d: %s）\n",
                     filePath.c_str(), errno, std::strerror(errno));
        return {};
    }

    char magic[16] = {0};
    fin.read(magic, 16);
    std::streamsize n = fin.gcount();
    fin.close();

    if (n < 4) {
        return {};
    }

    /* %PDF- */
    if (std::memcmp(magic, "%PDF", 4) == 0) return "pdf";
    /* EPUB 本质是 ZIP，PK\x03\x04 */
    if (static_cast<unsigned char>(magic[0]) == 0x50 &&
        static_cast<unsigned char>(magic[1]) == 0x4B) return "epub";
    /* XPS 也是 ZIP，和 EPUB 同魔数。MuPDF 会自动按扩展名/XPS 内部声明识别，
     * 所以这里返回 "xps" 留给 MuPDF 去做最终判定。 */
    if (ext.empty() || ext == "xps") {
        /* 如果文件同时被识别为 XPS（ZIP 内有 .rels / FixedDocumentSequence），
         * 粗略地把它交给 MuPDF 让它自己 open 时再判定。 */
        return "xps";
    }

    /* 走不到，无法识别 */
    return {};
}

void MuPDFRenderer::read_metadata() {
    if (!m_doc) return;

    const char* title = nullptr;
    const char* author = nullptr;

    fz_try(m_ctx) {
        title = fz_lookup_metadata(m_ctx, m_doc, FZ_META_INFO_TITLE);
        author = fz_lookup_metadata(m_ctx, m_doc, FZ_META_INFO_AUTHOR);
    }
    fz_catch(m_ctx) {
        /* 读元数据失败没关系，继续用文件名兜底 */
        title = nullptr;
        author = nullptr;
    }

    m_info.title = title ? title : "";
    m_info.author = author ? author : "";

    /* 兜底：从文件名取 title */
    if (m_info.title.empty() && !m_info.filePath.empty()) {
        auto slash = m_info.filePath.find_last_of("/\\");
        auto dot   = m_info.filePath.find_last_of('.');
        std::string base = (slash == std::string::npos)
                               ? m_info.filePath
                               : m_info.filePath.substr(slash + 1);
        if (dot != std::string::npos && dot > slash) {
            base = base.substr(0, dot - (slash == std::string::npos ? 0 : slash + 1));
        }
        m_info.title = base;
    }
}

std::vector<std::pair<std::string, int>> MuPDFRenderer::walk_outline(fz_outline* node) {
    std::vector<std::pair<std::string, int>> flat;

    fz_outline* cur = node;
    while (cur) {
        std::string title_str = cur->title ? cur->title : "";
        int pageIndex = -1;

        if (cur->uri && cur->uri[0] != '\0') {
            /*
             * fz_resolve_link：把 outline 里的 link URI 解析成 pageIndex + 坐标。
             * dest 是输出参数（目标区域矩形），对我们没用可以传 nullptr。
             * 有些 PDF 的目录项直接存 "#page=N"，也能被正确解析。
             */
            fz_rect dest;
            fz_try(m_ctx) {
                pageIndex = fz_resolve_link(m_ctx, m_doc, cur->uri, &dest, nullptr);
            }
            fz_catch(m_ctx) {
                pageIndex = -1;
            }
        }

        flat.emplace_back(std::move(title_str), pageIndex);

        /* 递归子节点（多叉树） */
        if (cur->down) {
            auto sub = walk_outline(cur->down);
            flat.insert(flat.end(), sub.begin(), sub.end());
        }

        cur = cur->next;
    }

    return flat;
}

} /* namespace mjnexus */
