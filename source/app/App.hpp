#pragma once

#include "../../include/mjnexus/Config.hpp"
#include "../../include/mjnexus/Theme.hpp"

#include <memory>
#include <string>

namespace mjnexus {

// ============================================================
// 前置声明：渲染器
//   MuPDFRenderer —— 负责 PDF / EPUB / XPS / CBZ / CBR / CBT / CB7 的渲染
//   TextRenderer  —— 负责 TXT / MD / MOBI（文本流）的渲染
//
//   它们的实现不在本次任务范围内，App 只持有指针并在
//   不同格式间做分发；如果该 Renderer 不存在，load_book 会返回 false。
// ============================================================
class MuPDFRenderer;
class TextRenderer;

// ============================================================
// App —— 应用运行时单例
//
// 生命周期：
//   main() 构造 → 加载设置 + 扫描进度 → 打开 HomePage → run() → 析构保存
//
// 职责：
//   - 持有当前打开的书籍元数据（BookInfo）
//   - 持有两个渲染器实例，按格式分发
//   - 对外暴露 theme / fontSize / readMode / settings 等读写接口
//   - save_progress() 把当前进度写入 ProgressStore
// ============================================================
class App {
public:
    // 单例
    static App& get();

    // 禁止拷贝 / 移动
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // 初始化：构造后调用一次，加载设置和进度
    void initialize();

    // 加载一本书；返回是否成功；内部根据格式分发到对应渲染器
    bool load_book(const std::string& filePath);

    // 关闭当前书籍并释放渲染器资源
    void close_book();

    // 只读访问：无锁（Switch 上通常单核，UI 线程独占访问）
    const BookInfo* get_current_book() const;
    bool            is_book_loaded() const;

    // 把当前书籍的阅读进度写回磁盘（异步 / 同步由 ProgressStore 决定）
    void save_progress(int pageIndex, float percentage);

    // ======== 设置相关 ========
    const AppSettings& get_settings() const;
    void apply_settings(const AppSettings& newSettings);

    Theme get_theme() const;
    void  switch_theme(Theme t);

    int  get_font_size() const;
    void set_font_size(int size);   // 10-24 之外被夹紧

    int  get_read_mode() const;
    void set_read_mode(int mode);

    // ======== 渲染器访问（供 ReaderPage 等 UI 层调用）========
    MuPDFRenderer* get_mupdf();
    TextRenderer*  get_text();

private:
    App();
    ~App();

    AppSettings                  m_settings;
    BookInfo                     m_currentBook;
    std::unique_ptr<MuPDFRenderer> m_mupdf;
    std::unique_ptr<TextRenderer>  m_text;
    bool                         m_loaded = false;
};

} // namespace mjnexus
