#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mjnexus {

// ============================================================
// JsonValue —— 一个极简 JSON 值容器
//
// 实现：
//   - 内部用 std::variant 持有六种基本类型 + object + array
//   - 不做完整 JSON 语法校验，只够读写 settings/progress/library 三种文件
//   - number 统一用 double（读写 int/float 都没问题）
// ============================================================
class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    // ---- 基础构造 ----
    JsonValue();                                       // 默认构造 = null
    JsonValue(std::nullptr_t);
    JsonValue(bool b);
    JsonValue(double d);
    JsonValue(int64_t i);
    JsonValue(const std::string& s);
    JsonValue(std::string&& s);
    JsonValue(const char* s);
    JsonValue(std::vector<JsonValue> arr);
    JsonValue(std::map<std::string, JsonValue> obj);

    // ---- 静态工厂（更明确的语义） ----
    static JsonValue make_object();
    static JsonValue make_array();

    // ---- 类型查询 ----
    Type type() const;
    bool is_null()   const;
    bool is_bool()   const;
    bool is_number() const;
    bool is_string() const;
    bool is_array()  const;
    bool is_object() const;

    // ---- 值提取（不匹配类型会返回零值） ----
    std::string              as_string() const;
    double                   as_number() const;
    bool                     as_bool()   const;
    const std::vector<JsonValue>&       as_array()  const;
    const std::map<std::string, JsonValue>& as_object() const;

    // 非 const 版本（用于填充 object/array）
    std::vector<JsonValue>&       as_array_mut();
    std::map<std::string, JsonValue>& as_object_mut();

    // ---- 便捷访问 ----
    // has(k)：对象中是否存在 key
    bool has(const std::string& key) const;

    // 对 object 的 operator[]：若 key 不存在，会创建一个 null 并返回引用
    JsonValue& operator[](const std::string& key);
    const JsonValue& operator[](const std::string& key) const;

    // 对 array 的 operator[]：越界返回 null
    JsonValue& operator[](size_t idx);
    const JsonValue& operator[](size_t idx) const;

    // 对象 key 数量 / 数组元素数量（其他类型返回 0）
    size_t size() const;

    // ---- 全局静态工具 ----
    // 从文件解析 JSON；失败返回 null 并把错误信息写入 *error（可选）
    static JsonValue parse_file(const std::string& path,
                                std::string* error = nullptr);

    // 把当前值以 2 空格缩进写到文件；失败返回 false
    bool write_file(const std::string& path,
                    std::string* error = nullptr) const;

    // 把当前值序列化为字符串（2 空格缩进）
    std::string to_string(int indent = 0) const;

private:
    struct NullTag {};

    // 内部 variant：
    //   std::monostate      → null
    //   bool                → true/false
    //   double              → number
    //   std::string         → string
    //   std::vector<JsonValue>       → array
    //   std::map<std::string, JsonValue> → object
    std::variant<std::monostate, bool, double,
                 std::string,
                 std::vector<JsonValue>,
                 std::map<std::string, JsonValue>> m_data;

    // 帮助类型（null 的空 struct）
    using ObjectMap = std::map<std::string, JsonValue>;
    using ArrayVec  = std::vector<JsonValue>;
};

} // namespace mjnexus
