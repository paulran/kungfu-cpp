#include <kungfu/yijinjing/io/locator.h>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <regex>

namespace kungfu::yijinjing::io {

namespace fs = std::filesystem;

Locator::Locator(const std::string& root_path) : root_(root_path) {}

std::string Locator::layout_dir(const location_ptr& loc, layout l) const {
    fs::path dir = fs::path(root_) / layout_name(l) / category_name(loc->cat) /
                   loc->group / loc->name / mode_name(loc->m);
    return dir.string();
}

std::string Locator::journal_path(const location_ptr& loc, uint32_t dest_uid, uint32_t page_id) const {
    std::ostringstream filename;
    filename << std::hex << std::setfill('0') << std::setw(8) << dest_uid
             << "." << std::dec << page_id << ".journal";

    fs::path path = fs::path(layout_dir(loc, layout::JOURNAL)) / filename.str();
    return path.string();
}

std::string Locator::nn_path(const location_ptr& loc, const std::string& protocol) const {
#ifdef _WIN32
    // NNG IPC on Windows uses named pipes; build a flat pipe name from location components
    std::string pipe_name = "ipc:///kungfu/" +
        std::string(category_name(loc->cat)) + "/" +
        loc->group + "/" + loc->name + "/" +
        std::string(mode_name(loc->m)) + "/" + protocol;
    return pipe_name;
#else
    fs::path dir = fs::path(layout_dir(loc, layout::NN));
    fs::create_directories(dir);
    fs::path path = dir / (protocol + ".ipc");
    return "ipc://" + path.string();
#endif
}

std::vector<uint32_t> Locator::list_page_ids(const location_ptr& loc, uint32_t dest_uid) const {
    std::vector<uint32_t> ids;
    std::string dir = layout_dir(loc, layout::JOURNAL);

    if (!fs::exists(dir)) return ids;

    std::ostringstream prefix;
    prefix << std::hex << std::setfill('0') << std::setw(8) << dest_uid << ".";
    std::string prefix_str = prefix.str();

    for (auto& entry : fs::directory_iterator(dir)) {
        std::string name = entry.path().filename().string();
        if (name.starts_with(prefix_str) && name.ends_with(".journal")) {
            auto dot1 = name.find('.');
            auto dot2 = name.find('.', dot1 + 1);
            if (dot1 != std::string::npos && dot2 != std::string::npos) {
                std::string id_str = name.substr(dot1 + 1, dot2 - dot1 - 1);
                ids.push_back(static_cast<uint32_t>(std::stoul(id_str)));
            }
        }
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}

void Locator::ensure_dir(const location_ptr& loc, layout l) const {
    fs::create_directories(layout_dir(loc, l));
}

} // namespace kungfu::yijinjing::io
