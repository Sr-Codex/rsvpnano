#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Shelf order for the Lume cover library (no reliable clock on the device):
// `/library/.order` keeps book paths in the order they first appeared. New
// books are appended; removed books are dropped. The shelf shows the book being
// read first, then the most recently added.
namespace library_order {

// Parse/serialise the order file: one path per line, blank lines ignored.
[[nodiscard]] std::vector<std::string> parse(std::string_view text);
[[nodiscard]] std::string serialize(const std::vector<std::string>& paths);

// Reconcile the stored order with the paths currently on the card.
// Known paths keep their relative order, unknown ones are appended in `current`
// order, vanished ones are removed. `changed` tells whether to rewrite the file.
[[nodiscard]] std::vector<std::string> reconcile(const std::vector<std::string>& stored,
                                                 const std::vector<std::string>& current, bool* changed = nullptr);

// Display order as indexes into `current`: `readingPath` first (if present),
// then newest-added first.
[[nodiscard]] std::vector<size_t> shelf(const std::vector<std::string>& reconciled,
                                        const std::vector<std::string>& current, std::string_view readingPath);

} // namespace library_order
