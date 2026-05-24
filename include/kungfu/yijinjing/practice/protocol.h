#pragma once

#include <kungfu/longfist/types.h>
#include <kungfu/yijinjing/io/locator.h>
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace kungfu::yijinjing::practice {

struct RegisterMessage {
    int32_t msg_type = longfist::types::Register::tag;
    int32_t category = 0;
    std::string group;
    std::string name;
    int32_t mode = 0;
    uint32_t uid = 0;
    int32_t pid = 0;
};

inline std::string encode_register(const io::location_ptr& loc, int32_t pid) {
    nlohmann::json j;
    j["msg_type"] = longfist::types::Register::tag;
    j["category"] = static_cast<int>(loc->cat);
    j["group"] = loc->group;
    j["name"] = loc->name;
    j["mode"] = static_cast<int>(loc->m);
    j["uid"] = loc->uid;
    j["pid"] = pid;
    return j.dump();
}

inline RegisterMessage decode_register(const std::string& json_str) {
    auto j = nlohmann::json::parse(json_str);
    RegisterMessage msg;
    msg.msg_type = j.value("msg_type", longfist::types::Register::tag);
    msg.category = j.value("category", 0);
    msg.group = j.value("group", "");
    msg.name = j.value("name", "");
    msg.mode = j.value("mode", 0);
    msg.uid = j.value("uid", static_cast<uint32_t>(0));
    msg.pid = j.value("pid", 0);
    return msg;
}

inline std::string encode_request_write_to(uint32_t source_uid, uint32_t dest_uid) {
    nlohmann::json j;
    j["msg_type"] = longfist::types::RequestWriteTo::tag;
    j["source_uid"] = source_uid;
    j["dest_uid"] = dest_uid;
    return j.dump();
}

inline std::string encode_request_read_from(uint32_t source_uid, uint32_t dest_uid, int64_t from_time) {
    nlohmann::json j;
    j["msg_type"] = longfist::types::RequestReadFrom::tag;
    j["source_uid"] = source_uid;
    j["dest_uid"] = dest_uid;
    j["from_time"] = from_time;
    return j.dump();
}

inline io::location_ptr location_from_register(const RegisterMessage& msg) {
    return io::location::make(
        static_cast<io::category>(msg.category),
        msg.group,
        msg.name,
        static_cast<io::mode>(msg.mode)
    );
}

} // namespace kungfu::yijinjing::practice
