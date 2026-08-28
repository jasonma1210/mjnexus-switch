#pragma once

#include "../../include/mjnexus/Config.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace mjnexus {

// ============================================================
// BookProgress —— 一本书的阅读进度快照
// ============================================================
struct BookProgress {
    int     pageIndex    = 0;     // 当前页（从 0 起）
    float   percentage   = 0.0f;  // 阅读百分比 0-100
    int     readMode     = 0;     // 当次阅读所用模式
    int64_t timestamp    = 0;     // 最后更新时间戳（Unix 秒）
};

// ============================================================
// ProgressStore —— 所有书籍的阅读进度
//
// - 单例
// - 用 filePath 作为唯一 key（避免额外维护 id 映射表）
// - load() 时返回整个 map，save(map) 时一次性写回
// - 应用启动调 load()，每次 save_progress() 时内存修改 + 定时 flush
// ============================================================
class ProgressStore {
public:
    using ProgressMap = std::unordered_map<std::string /*filePath*/, BookProgress>;

    static ProgressStore& instance();

    // 加载；文件不存在返回空 map
    ProgressMap load();

    // 保存；失败返回 false
    bool save(const ProgressMap& progress);

    // 便捷：更新某本书的进度（同时写回文件）
    bool update(const std::string& filePath,
                int page, float percentage, int readMode);

    // 便捷：查询某本书的进度（不存在返回 nullopt）
    std::optional<BookProgress> get(const std::string& filePath);

    // progress.json 的绝对路径
    static std::string progress_path();

private:
    ProgressStore() = default;
    ~ProgressStore() = default;
    ProgressStore(const ProgressStore&) = delete;
    ProgressStore& operator=(const ProgressStore&) = delete;

    // 缓存：避免每次 get/update 都要重新 parse
    ProgressMap m_cache;
    bool m_loaded = false;
};

} // namespace mjnexus
