#include "ProgressStore.hpp"
#include "../utils/Json.hpp"

#include <cstdio>
#include <cstring>

namespace mjnexus {

// ============================================================
// 路径
// ============================================================

std::string ProgressStore::progress_path() {
    return std::string(SD_CARD_ROOT) + "progress.json";
}

// ============================================================
// 单例
// ============================================================

ProgressStore& ProgressStore::instance() {
    static ProgressStore inst;
    return inst;
}

// ============================================================
// 加载
// ============================================================

ProgressStore::ProgressMap ProgressStore::load() {
    ProgressMap result;

    std::string err;
    JsonValue root = JsonValue::parse_file(progress_path(), &err);
    if (root.is_null() || !root.is_object()) {
        m_cache  = result;
        m_loaded = true;
        return result;
    }

    const auto& obj = root.as_object();
    for (const auto& kv : obj) {
        const std::string& path = kv.first;
        const JsonValue&   v    = kv.second;

        BookProgress bp;
        if (v.is_object()) {
            if (v.has("pageIndex"))   bp.pageIndex  = (int)v["pageIndex"].as_number();
            if (v.has("percentage"))  bp.percentage = (float)v["percentage"].as_number();
            if (v.has("readMode"))    bp.readMode   = (int)v["readMode"].as_number();
            if (v.has("timestamp"))   bp.timestamp  = (int64_t)v["timestamp"].as_number();
        }
        result[path] = bp;
    }

    m_cache  = result;
    m_loaded = true;
    return result;
}

// ============================================================
// 保存
// ============================================================

bool ProgressStore::save(const ProgressMap& progress) {
    JsonValue obj = JsonValue::make_object();
    for (const auto& kv : progress) {
        JsonValue entry = JsonValue::make_object();
        entry["pageIndex"]  = (int64_t)kv.second.pageIndex;
        entry["percentage"] = kv.second.percentage;
        entry["readMode"]   = (int64_t)kv.second.readMode;
        entry["timestamp"]  = (int64_t)kv.second.timestamp;
        obj[kv.first] = entry;
    }

    std::string err;
    bool ok = obj.write_file(progress_path(), &err);
    if (ok) {
        m_cache  = progress;
        m_loaded = true;
    }
    return ok;
}

// ============================================================
// 便捷：update
// ============================================================

bool ProgressStore::update(const std::string& filePath,
                           int page, float percentage, int readMode) {
    if (!m_loaded) load();

    BookProgress bp;
    bp.pageIndex  = page;
    bp.percentage = percentage;
    bp.readMode   = readMode;
    bp.timestamp  = now_unix_seconds();

    m_cache[filePath] = bp;
    return save(m_cache);
}

// ============================================================
// 便捷：get
// ============================================================

std::optional<BookProgress> ProgressStore::get(const std::string& filePath) {
    if (!m_loaded) load();

    auto it = m_cache.find(filePath);
    if (it == m_cache.end()) return std::nullopt;
    return it->second;
}

} // namespace mjnexus
