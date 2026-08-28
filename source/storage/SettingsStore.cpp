#include "SettingsStore.hpp"
#include "../utils/Json.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace mjnexus {

// ============================================================
// 路径辅助
// ============================================================

std::string SettingsStore::settings_path() {
    return std::string(SD_CARD_ROOT) + "settings.json";
}

// ============================================================
// 单例
// ============================================================

SettingsStore& SettingsStore::instance() {
    static SettingsStore inst;
    return inst;
}

// ============================================================
// 加载
// ============================================================

AppSettings SettingsStore::load() {
    AppSettings s;

    std::string err;
    JsonValue root = JsonValue::parse_file(settings_path(), &err);
    if (root.is_null()) {
        // 文件不存在或解析失败 → 返回默认
        return s;
    }

    // 字段解析（逐个字段做防御：key 缺失则保留默认值）
    if (root.has("theme"))           s.theme           = (int)root["theme"].as_number();
    if (root.has("fontSize"))        s.fontSize        = (int)root["fontSize"].as_number();
    if (root.has("defaultReadMode")) s.defaultReadMode = (int)root["defaultReadMode"].as_number();
    if (root.has("sortBy"))          s.sortBy          = (int)root["sortBy"].as_number();
    if (root.has("language"))        s.language        = root["language"].as_string();
    if (root.has("naturalSort"))     s.naturalSort     = root["naturalSort"].as_bool();

    // 边界保护
    if (s.theme < 0 || s.theme > 1)              s.theme = (int)Theme::LIGHT;
    if (s.fontSize < 10 || s.fontSize > 24)      s.fontSize = 16;
    if (s.defaultReadMode < 0 || s.defaultReadMode > 3) s.defaultReadMode = (int)ReadMode::PORTRAIT;
    if (s.sortBy < 0 || s.sortBy > 2)            s.sortBy = 0;
    if (s.language != "zh-CN" && s.language != "en") s.language = "zh-CN";

    return s;
}

// ============================================================
// 保存
// ============================================================

bool SettingsStore::save(const AppSettings& settings) {
    JsonValue obj = JsonValue::make_object();
    obj["theme"]           = (int64_t)settings.theme;
    obj["fontSize"]        = (int64_t)settings.fontSize;
    obj["defaultReadMode"] = (int64_t)settings.defaultReadMode;
    obj["sortBy"]          = (int64_t)settings.sortBy;
    obj["language"]        = settings.language;
    obj["naturalSort"]     = settings.naturalSort;

    std::string err;
    return obj.write_file(settings_path(), &err);
}

} // namespace mjnexus
