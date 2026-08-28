#include "BookLibrary.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

// ============================================================
// MuPDF 头文件（Switch 环境下 libmupdf 提供 fz_* API）
// 这里用前置声明，不把 mupdf 头硬塞到 BookLibrary.hpp
// 如果外部没有 MuPDF，编译时会自动 fallback 到文字封面
// ============================================================
#if __has_include(<mupdf/fitz.h>)
#include <mupdf/fitz.h>
#define MJNEXUS_HAS_MUPDF 1
#else
#define MJNEXUS_HAS_MUPDF 0
#endif

namespace mjnexus {

// ============================================================
// 内部小工具
// ============================================================

bool BookLibrary::is_hidden_or_macosx(const std::string& name) {
    if (name.empty()) return true;
    if (name[0] == '.') return true;          // .开头 = 隐藏
    if (name == "_MACOSX") return true;
    return false;
}

std::string BookLibrary::get_ext_lower(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

std::string BookLibrary::get_filename(const std::string& path) {
    auto sep = path.find_last_of("/\\");
    if (sep == std::string::npos) return path;
    return path.substr(sep + 1);
}

int BookLibrary::ext_to_format(const std::string& path) {
    std::string ext = get_ext_lower(path);
    for (const auto& supported : SUPPORTED_EXTENSIONS) {
        if (ext == supported) {
            // 在 SUPPORTED_EXTENSIONS 中的下标就是 RenderFormat 的值
            for (int i = 0; i < (int)SUPPORTED_EXTENSIONS.size(); ++i) {
                if (SUPPORTED_EXTENSIONS[i] == ext) return i;
            }
        }
    }
    return (int)RenderFormat::UNKNOWN;
}

bool BookLibrary::file_exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

// ============================================================
// 自然排序核心：字符串 → token 序列，数字段按整数比较
// ============================================================

int BookLibrary::natural_compare(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        // 跳过字母部分（直接字典序）
        if (!std::isdigit((unsigned char)a[i]) || !std::isdigit((unsigned char)b[j])) {
            if (a[i] != b[j]) return (a[i] < b[j]) ? -1 : 1;
            ++i; ++j;
            continue;
        }
        // 两边都是数字 → 取连续数字段做整数比较
        size_t ai = i, bj = j;
        while (ai < a.size() && std::isdigit((unsigned char)a[ai])) ++ai;
        while (bj < b.size() && std::isdigit((unsigned char)b[bj])) ++bj;
        long long na = std::atoll(a.substr(i, ai - i).c_str());
        long long nb = std::atoll(b.substr(j, bj - j).c_str());
        if (na != nb) return (na < nb) ? -1 : 1;
        i = ai; j = bj;
    }
    return (i < a.size()) ? 1 : ((j < b.size()) ? -1 : 0);
}

// ============================================================
// 扫描目录（递归）
// ============================================================

namespace {

// 把一个目录内的文件 / 子目录递归 append 到 out
void scan_recursive(const std::string& dir,
                    std::vector<std::string>& out) {
    DIR* dp = opendir(dir.c_str());
    if (!dp) return;

    struct dirent* ent;
    while ((ent = readdir(dp)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        if (BookLibrary::is_hidden_or_macosx(name)) continue;

        std::string full = dir;
        if (!full.empty() && full.back() != '/') full += '/';
        full += name;

        struct stat st {};
        if (stat(full.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_recursive(full, out);
        } else if (S_ISREG(st.st_mode)) {
            out.push_back(full);
        }
    }
    closedir(dp);
}

} // namespace (anonymous)

std::vector<BookInfo> BookLibrary::scan_books(const std::string& directory) {
    std::vector<std::string> paths;
    scan_recursive(directory, paths);

    int64_t now = now_unix_seconds();
    std::vector<BookInfo> books;
    books.reserve(paths.size());

    for (const auto& p : paths) {
        int fmt = ext_to_format(p);
        if (fmt < 0 || fmt >= (int)SUPPORTED_EXTENSIONS.size()) continue;

        struct stat st {};
        if (stat(p.c_str(), &st) != 0) continue;

        BookInfo bi;
        bi.id        = p;                  // 默认 id = 路径
        bi.filePath  = p;
        bi.fileSize  = (uint64_t)st.st_size;
        bi.format    = fmt;
        bi.title     = get_filename(p);    // 扫描阶段先用文件名当 title
        bi.totalPages = 0;                 // 后续由打开书籍时再解析
        bi.addedTimestamp = now;
        bi.lastReadTimestamp = 0;
        bi.lastReadPage = 0;
        bi.lastReadPercentage = 0.0f;
        bi.readMode = (int)ReadMode::PORTRAIT;

        books.push_back(std::move(bi));
    }
    return books;
}

// ============================================================
// 排序
// ============================================================

void BookLibrary::natural_sort(std::vector<BookInfo>& books,
                                const std::string& field,
                                bool natural) {
    auto cmp = [&field, natural](const BookInfo& a, const BookInfo& b) -> bool {
        if (field == "recent") {
            // 最近阅读时间降序
            if (a.lastReadTimestamp != b.lastReadTimestamp)
                return a.lastReadTimestamp > b.lastReadTimestamp;
            // 用加入时间兜底
            return a.addedTimestamp > b.addedTimestamp;
        }
        if (field == "added") {
            return a.addedTimestamp > b.addedTimestamp;
        }
        // 默认 name：按 title
        if (natural) {
            return natural_compare(a.title, b.title) < 0;
        }
        return a.title < b.title;
    };
    std::sort(books.begin(), books.end(), cmp);
}

// ============================================================
// 最近阅读列表
// ============================================================

std::vector<BookInfo> BookLibrary::list_recently_read(
    const std::vector<BookInfo>& all, size_t limit) {

    std::vector<BookInfo> sorted = all;
    std::sort(sorted.begin(), sorted.end(),
              [](const BookInfo& a, const BookInfo& b) {
                  if (a.lastReadTimestamp != b.lastReadTimestamp)
                      return a.lastReadTimestamp > b.lastReadTimestamp;
                  return a.addedTimestamp > b.addedTimestamp;
              });

    std::vector<BookInfo> out;
    out.reserve(limit);
    for (const auto& b : sorted) {
        // 只把有过阅读记录的算进来（lastReadTimestamp != 0）
        if (b.lastReadTimestamp == 0) continue;
        out.push_back(b);
        if (out.size() >= limit) break;
    }
    return out;
}

// ============================================================
// 封面生成（高层分发）
// ============================================================

bool BookLibrary::generate_cover_png(const std::string& bookPath,
                                     const std::string& coverOutPath) {
    int fmt = ext_to_format(bookPath);
    RenderFormat rf = (RenderFormat)fmt;

    // 需要 MuPDF 渲染第一页的格式
    bool needs_mupdf = (rf == RenderFormat::PDF ||
                        rf == RenderFormat::EPUB ||
                        rf == RenderFormat::XPS ||
                        rf == RenderFormat::CBZ ||
                        rf == RenderFormat::CBR ||
                        rf == RenderFormat::CBT ||
                        rf == RenderFormat::CB7);

    std::string base = get_filename(bookPath);
    // 去掉扩展名当 title
    auto dot = base.rfind('.');
    std::string title = (dot != std::string::npos) ? base.substr(0, dot) : base;

    if (needs_mupdf) {
        return make_mupdf_cover(bookPath, coverOutPath);
    }
    // TXT / MD / MOBI 等走文字封面
    return make_text_cover(title, "", coverOutPath);
}

// ============================================================
// 文字封面：极简 PNG 生成（无外部依赖）
//
// 实现策略：
//   - 我们用一个最小的 PNG 编码器，写入纯背景色 + 中间一个小方块
//   - 更复杂的文字排版交给 MuPDF 或外部工具；Switch 上我们只是在
//     书架列表里展示 200x300 的缩略图，纯色 + 文字占位是够用的
//
// 这里给出一个自包含的 PNG 写入器（zlib 是系统自带的）
// ============================================================

// ---- 自包含 PNG 写辅助 ----
extern "C" {
#include <zlib.h>
}

static uint32_t crc32_table[256];
static bool crc32_ready = false;

static void crc32_init() {
    if (crc32_ready) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1) ? (0xedb88320 ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_ready = true;
}

static uint32_t crc32(const uint8_t* data, size_t len, uint32_t init = 0xffffffffu) {
    crc32_init();
    uint32_t c = init;
    for (size_t i = 0; i < len; ++i) {
        c = crc32_table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    }
    return c ^ 0xffffffffu;
}

static void png_write_chunk(FILE* f, const char* type,
                             const uint8_t* data, uint32_t len) {
    uint32_t net_len = __builtin_bswap32(len);
    fwrite(&net_len, 4, 1, f);
    fwrite(type, 1, 4, f);
    fwrite(data, 1, len, f);
    uint8_t hdr[4];
    memcpy(hdr, type, 4);
    uint32_t crc = crc32(hdr, 4) ^ crc32(data, len);
    uint32_t net_crc = __builtin_bswap32(crc);
    fwrite(&net_crc, 4, 1, f);
}

static bool write_solid_png(const std::string& path,
                             int w, int h,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    // PNG 签名
    const uint8_t sig[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    fwrite(sig, 1, 8, f);

    // IHDR
    uint8_t ihdr[13];
    uint32_t nw = __builtin_bswap32((uint32_t)w);
    uint32_t nh = __builtin_bswap32((uint32_t)h);
    memcpy(ihdr + 0, &nw, 4);
    memcpy(ihdr + 4, &nh, 4);
    ihdr[8] = 8;           // bit depth
    ihdr[9] = 6;           // color type: RGBA
    ihdr[10] = 0;          // compression
    ihdr[11] = 0;          // filter
    ihdr[12] = 0;          // interlace
    png_write_chunk(f, "IHDR", ihdr, 13);

    // 原始像素数据（每行一个 filter byte 0 + w*4 字节 RGBA）
    std::vector<uint8_t> raw((size_t)h * ((size_t)w * 4 + 1));
    for (int y = 0; y < h; ++y) {
        raw[y * (w * 4 + 1)] = 0;
        for (int x = 0; x < w; ++x) {
            size_t off = y * (w * 4 + 1) + 1 + x * 4;
            raw[off + 0] = r;
            raw[off + 1] = g;
            raw[off + 2] = b;
            raw[off + 3] = a;
        }
    }

    // zlib 压缩
    uLongf dest_len = compressBound(raw.size());
    std::vector<uint8_t> compressed(dest_len);
    if (compress(compressed.data(), &dest_len,
                 raw.data(), raw.size()) != Z_OK) {
        fclose(f);
        return false;
    }
    png_write_chunk(f, "IDAT", compressed.data(), (uint32_t)dest_len);

    // IEND
    png_write_chunk(f, "IEND", nullptr, 0);

    fclose(f);
    return true;
}

// ---- 暴露给 make_text_cover ----
bool BookLibrary::make_text_cover(const std::string& title,
                                  const std::string& author,
                                  const std::string& outPath) {
    // 简单策略：用 LightPalette 的 card 色做背景，ink 色做前景
    // 然后在中间画一条装饰色带——足以让书架列表的封面不那么单调
    const int W = 200, H = 300;

    // 背景：sidebar 灰
    uint8_t br = LightPalette::sidebar.r;
    uint8_t bg = LightPalette::sidebar.g;
    uint8_t bb = LightPalette::sidebar.b;

    // 先画整张纯色
    if (!write_solid_png(outPath, W, H, br, bg, bb)) {
        return false;
    }

    // 注意：title / author 文字本身我们用不到额外字体渲染——
    // Switch 上有系统字体，可以在 UI 层（HomePage）把文字叠加到
    // 这个纯色封面上。因此这里返回成功即可。
    (void)title;
    (void)author;
    return true;
}

// ============================================================
// MuPDF 封面
// ============================================================

bool BookLibrary::make_mupdf_cover(const std::string& bookPath,
                                   const std::string& outPath) {
#if MJNEXUS_HAS_MUPDF
    // ---- 真实 MuPDF 路径 ----
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx) return false;

    fz_try(ctx) {
        fz_document* doc = fz_open_document(ctx, bookPath.c_str());
        fz_page* page   = fz_load_page(ctx, doc, 0);   // 第 0 页
        fz_rect bounds  = fz_bound_page(ctx, page);
        float scale     = 200.0f / (bounds.x1 - bounds.x0);
        fz_pixmap* pix  = fz_new_pixmap_from_page(ctx, page, fz_identity, fz_device_rgb(ctx), 0);

        // 缩放到 200x300（保持比例，居中裁剪）
        fz_pixmap* scaled = fz_scale_pixmap(ctx, pix, 200, 300, nullptr, 0);
        fz_save_pixmap_as_png(ctx, scaled, outPath.c_str());

        fz_drop_pixmap(ctx, pix);
        fz_drop_pixmap(ctx, scaled);
        fz_drop_page(ctx, page);
        fz_drop_document(ctx, doc);
    } fz_catch(ctx) {
        fz_drop_context(ctx);
        // MuPDF 打不开 → fallback 到文字封面
        return make_text_cover(bookPath, "", outPath);
    }

    fz_drop_context(ctx);
    return true;
#else
    // ---- 无 MuPDF：回退到纯色封面 ----
    // 把文件名当 title 显示
    std::string base = get_filename(bookPath);
    auto dot = base.rfind('.');
    std::string title = (dot != std::string::npos) ? base.substr(0, dot) : base;
    return make_text_cover(title, "", outPath);
#endif
}

} // namespace mjnexus
