#include "library/LibraryOrder.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace library_order {

std::vector<std::string> parse(std::string_view text) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty() && seen.emplace(line).second) out.emplace_back(line);
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return out;
}

std::string serialize(const std::vector<std::string>& paths) {
    std::string out;
    for (const auto& path : paths) out.append(path).push_back('\n');
    return out;
}

std::vector<std::string> reconcile(const std::vector<std::string>& stored, const std::vector<std::string>& current,
                                   bool* changed) {
    const std::unordered_set<std::string_view> present(current.begin(), current.end());
    std::vector<std::string> out;
    out.reserve(current.size());
    std::unordered_set<std::string_view> kept;
    for (const auto& path : stored)
        if (present.contains(path) && kept.emplace(path).second) out.push_back(path);
    for (const auto& path : current)
        if (!kept.contains(path)) { out.push_back(path); kept.emplace(out.back()); }
    if (changed) *changed = out != stored;
    return out;
}

std::vector<size_t> shelf(const std::vector<std::string>& reconciled, const std::vector<std::string>& current,
                          std::string_view readingPath) {
    std::unordered_map<std::string_view, size_t> index;
    for (size_t i = 0; i < current.size(); ++i) index.emplace(current[i], i);
    std::vector<size_t> out;
    out.reserve(current.size());
    if (const auto it = index.find(readingPath); !readingPath.empty() && it != index.end()) out.push_back(it->second);
    for (auto it = reconciled.rbegin(); it != reconciled.rend(); ++it) {
        const auto found = index.find(*it);
        if (found == index.end() || found->first == readingPath) continue;
        out.push_back(found->second);
    }
    // Anything not covered by `reconciled` (should not happen) goes last, in card order.
    if (out.size() < current.size()) {
        std::vector<bool> used(current.size(), false);
        for (const size_t i : out) used[i] = true;
        for (size_t i = 0; i < current.size(); ++i) if (!used[i]) out.push_back(i);
    }
    return out;
}

} // namespace library_order
