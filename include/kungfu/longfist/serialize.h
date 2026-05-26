#pragma once

#include <kungfu/longfist/types.h>
#include <nlohmann/json.hpp>
#include <boost/hana.hpp>
#include <cstring>
#include <string>
#include <type_traits>

namespace kungfu::longfist {

namespace hana = boost::hana;

template<typename T>
nlohmann::json to_json(const T& obj) {
    nlohmann::json j;
    hana::for_each(hana::accessors<T>(), [&](auto accessor) {
        auto name = hana::to<const char*>(hana::first(accessor));
        const auto& value = hana::second(accessor)(obj);
        using ValueType = std::remove_cvref_t<decltype(value)>;

        if constexpr (is_array_t_v<ValueType>) {
            j[name] = std::string(value.data);
        } else if constexpr (std::is_enum_v<ValueType>) {
            j[name] = static_cast<std::underlying_type_t<ValueType>>(value);
        } else {
            j[name] = value;
        }
    });
    return j;
}

template<typename T>
void from_json(const nlohmann::json& j, T& obj) {
    std::memset(&obj, 0, sizeof(T));
    hana::for_each(hana::accessors<T>(), [&](auto accessor) {
        auto name = hana::to<const char*>(hana::first(accessor));
        if (!j.contains(name)) return;

        auto& field = hana::second(accessor)(obj);
        using FieldType = std::remove_cvref_t<decltype(field)>;

        if constexpr (is_array_t_v<FieldType>) {
            auto s = j[name].template get<std::string>();
            std::strncpy(field.data, s.c_str(), sizeof(field.data) - 1);
            field.data[sizeof(field.data) - 1] = '\0';
        } else if constexpr (std::is_enum_v<FieldType>) {
            field = static_cast<FieldType>(
                j[name].template get<std::underlying_type_t<FieldType>>());
        } else {
            field = j[name].template get<FieldType>();
        }
    });
}

template<typename T>
std::string to_string(const T& obj) {
    return to_json(obj).dump();
}

template<typename T>
T from_string(const std::string& str) {
    T obj{};
    auto j = nlohmann::json::parse(str);
    from_json(j, obj);
    return obj;
}

} // namespace kungfu::longfist
