#pragma once

#include <kungfu/common/hash.h>
#include <kungfu/common/types.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kungfu::yijinjing::io {

enum class category : uint8_t { MD = 0, TD = 1, STRATEGY = 2, SYSTEM = 3 };
enum class mode : uint8_t { LIVE = 0, DATA = 1, REPLAY = 2, BACKTEST = 3 };
enum class layout : uint8_t { JOURNAL = 0, NN = 1, LOG = 2 };

inline const char* category_name(category c) {
    static const char* names[] = {"md", "td", "strategy", "system"};
    return names[static_cast<int>(c)];
}

inline const char* mode_name(mode m) {
    static const char* names[] = {"live", "data", "replay", "backtest"};
    return names[static_cast<int>(m)];
}

inline const char* layout_name(layout l) {
    static const char* names[] = {"journal", "nn", "log"};
    return names[static_cast<int>(l)];
}

struct location {
    category cat;
    std::string group;
    std::string name;
    mode m;
    uint32_t uid;

    std::string uname() const {
        return std::string(category_name(cat)) + "/" + group + "/" + name + "/" + mode_name(m);
    }

    static std::shared_ptr<location> make(category c, const std::string& group,
                                           const std::string& name, mode m) {
        auto loc = std::make_shared<location>();
        loc->cat = c;
        loc->group = group;
        loc->name = name;
        loc->m = m;
        loc->uid = common::hash_str_32(loc->uname());
        return loc;
    }
};

using location_ptr = std::shared_ptr<location>;

class Locator {
public:
    explicit Locator(const std::string& root_path);

    std::string layout_dir(const location_ptr& loc, layout l) const;
    std::string journal_path(const location_ptr& loc, uint32_t dest_uid, uint32_t page_id) const;
    std::string nn_path(const location_ptr& loc, const std::string& protocol) const;
    std::vector<uint32_t> list_page_ids(const location_ptr& loc, uint32_t dest_uid) const;
    void ensure_dir(const location_ptr& loc, layout l) const;

    const std::string& root() const { return root_; }

private:
    std::string root_;
};

} // namespace kungfu::yijinjing::io
