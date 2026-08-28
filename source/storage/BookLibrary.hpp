#pragma once

#include "../../include/mjnexus/Config.hpp"

#include <string>
#include <vector>

namespace mjnexus {

// ============================================================
// BookLibrary —— 书架扫描 + 排序 + 封面生成
//
// 设计：
//   - 不做单例：扫描可能在后台线程跑，由 App 持有实例更合理
//   - scan_books：递归遍历目录，过滤扩展名、跳过隐藏文件和 _MACOSX
//   - natural_sort：把 "1"、"2"、"11" 按自然顺序排（不是字典序）
//   - generate_cover_png：根据格式走不同渲染路径
//       * PDF/EPUB/CBZ → MuPDF 渲染第一页 → 200x300 PNG
//       * TXT/MD/MOBI  → 文字 canvas（色板背景 + 书名 + 作者）→ PNG
//   - list_recently_read：从 all 中挑出最近读的前 limit 本
// ============================================================
class BookLibrary {
public:
    BookLibrary() = default;
    ~BookLibrary() = default;

    // 递归扫描目录下所有受支持的书籍文件
    std::vector<BookInfo> scan_books(const std::string& directory);

    // 对 vector 做自然排序，field 为 "name" / "recent" / "added"
    void natural_sort(std::vector<BookInfo>& books,
                      const std::string& field,
                      bool natural = true);

    // 生成封面 PNG；失败返回 false
    bool generate_cover_png(const std::string& bookPath,
                            const std::string& coverOutPath);

    // 从 all 中挑出最近阅读的 limit 本（根据 lastReadTimestamp 降序）
    // 需要外部把每本书的 lastReadTimestamp / lastReadPage 填好
    std::vector<BookInfo> list_recently_read(
        const std::vector<BookInfo>& all, size_t limit = 10);

private:
    // 内部小工具
    static bool is_hidden_or_macosx(const std::string& name);
    static int  ext_to_format(const std::string& path);
    static std::string get_filename(const std::string& path);
    static std::string get_ext_lower(const std::string& path);
    static bool file_exists(const std::string& path);

    // 自然排序核心：按数字段拆 token 对比
    static int  natural_compare(const std::string& a, const std::string& b);

    // 文字封面生成（TXT/MD/MOBI 等）
    bool make_text_cover(const std::string& title,
                         const std::string& author,
                         const std::string& outPath);

    // MuPDF 封面生成（PDF/EPUB/CBZ 等）
    bool make_mupdf_cover(const std::string& bookPath,
                          const std::string& outPath);
};

} // namespace mjnexus
