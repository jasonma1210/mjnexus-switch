#pragma once

#include "../../include/mjnexus/Config.hpp"

namespace mjnexus {

// ============================================================
// SettingsStore —— AppSettings 的文件持久化
//
// 设计：
//   - 单例（私有构造 + static &instance()）
//   - 只有两个对外方法：load() / save()
//   - JSON 字段与 AppSettings 成员一一对应；文件不存在返回默认值
//   - 写文件失败静默忽略（Switch 上 SD 卡可能暂时不可用）
// ============================================================
class SettingsStore {
public:
    static SettingsStore& instance();

    // 从 SD 卡加载；失败或文件不存在返回默认 AppSettings
    AppSettings load();

    // 保存到 SD 卡；失败返回 false
    bool save(const AppSettings& settings);

    // settings.json 的绝对路径
    static std::string settings_path();

private:
    SettingsStore() = default;
    ~SettingsStore() = default;
    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;
};

} // namespace mjnexus
