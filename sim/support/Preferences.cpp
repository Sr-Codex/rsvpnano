#include "Preferences.h"

#include <filesystem>
#include <fstream>
#include <mutex>

#include "SimRuntime.h"

// Format per namespace file (<state>/nvs/<namespace>.nvs): repeated
//   key '\0' type(u8) length(u32 LE) bytes
namespace {
    std::recursive_mutex gNvsMutex;

    std::filesystem::path fileFor(const std::string& ns) {
        return std::filesystem::path(sim::stateDirectory()) / "nvs" / (ns + ".nvs");
    }
} // namespace

bool Preferences::begin(const char* name, bool readOnly, const char*) {
    if (!name || !*name || std::strlen(name) > 15)
        return false;
    namespace_ = name;
    readOnly_ = readOnly;
    open_ = true;
    load();
    return true;
}

void Preferences::end() {
    open_ = false;
    entries_.clear();
}

void Preferences::load() {
    std::lock_guard lock(gNvsMutex);
    entries_.clear();
    std::ifstream in(fileFor(namespace_), std::ios::binary);
    while (in) {
        std::string key;
        if (!std::getline(in, key, '\0'))
            break;
        uint8_t type = 0;
        uint32_t length = 0;
        in.read(reinterpret_cast<char*>(&type), 1);
        in.read(reinterpret_cast<char*>(&length), 4);
        if (!in || length > (1u << 24))
            break;
        Entry entry;
        entry.type = static_cast<PreferenceType>(type);
        entry.bytes.resize(length);
        in.read(reinterpret_cast<char*>(entry.bytes.data()), length);
        if (!in)
            break;
        entries_[key] = std::move(entry);
    }
}

void Preferences::save() {
    std::lock_guard lock(gNvsMutex);
    const auto path = fileFor(namespace_);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        for (const auto& [key, entry] : entries_) {
            out.write(key.c_str(), static_cast<std::streamsize>(key.size() + 1));
            const uint8_t type = entry.type;
            const uint32_t length = static_cast<uint32_t>(entry.bytes.size());
            out.write(reinterpret_cast<const char*>(&type), 1);
            out.write(reinterpret_cast<const char*>(&length), 4);
            out.write(reinterpret_cast<const char*>(entry.bytes.data()), length);
        }
    }
    std::filesystem::rename(temporary, path, error);
}

bool Preferences::clear() {
    if (!open_ || readOnly_)
        return false;
    entries_.clear();
    save();
    return true;
}

bool Preferences::remove(const char* key) {
    if (!open_ || readOnly_ || !key)
        return false;
    const bool erased = entries_.erase(key) > 0;
    if (erased)
        save();
    return erased;
}

const Preferences::Entry* Preferences::find(const char* key) {
    if (!open_ || !key)
        return nullptr;
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
}

bool Preferences::isKey(const char* key) { return find(key) != nullptr; }

PreferenceType Preferences::getType(const char* key) {
    const Entry* entry = find(key);
    return entry ? entry->type : PT_INVALID;
}

size_t Preferences::putValue(const char* key, PreferenceType type, const void* data, size_t size) {
    if (!open_ || readOnly_ || !key || std::strlen(key) > 15 || (!data && size))
        return 0;
    Entry entry;
    entry.type = type;
    const auto* bytes = static_cast<const uint8_t*>(data);
    entry.bytes.assign(bytes, bytes + size);
    entries_[key] = std::move(entry);
    save();
    return size == 0 ? 1 : size;
}

size_t Preferences::putString(const char* key, const char* value) {
    const std::string text = value ? value : "";
    const size_t written = putValue(key, PT_STR, text.c_str(), text.size() + 1);
    return written ? text.size() : 0;
}

size_t Preferences::getString(const char* key, char* value, size_t maxLen) {
    const Entry* entry = find(key);
    if (!entry || entry->type != PT_STR)
        return 0;
    if (!value)
        return entry->bytes.size();
    if (entry->bytes.size() > maxLen)
        return 0;
    std::memcpy(value, entry->bytes.data(), entry->bytes.size());
    return entry->bytes.size();
}

String Preferences::getString(const char* key, const String& fallback) {
    const Entry* entry = find(key);
    if (!entry || entry->type != PT_STR || entry->bytes.empty())
        return fallback;
    return String(reinterpret_cast<const char*>(entry->bytes.data()));
}

size_t Preferences::getBytesLength(const char* key) {
    const Entry* entry = find(key);
    return entry ? entry->bytes.size() : 0;
}

size_t Preferences::getBytes(const char* key, void* buffer, size_t maxLen) {
    const Entry* entry = find(key);
    if (!entry || !buffer || entry->bytes.size() > maxLen)
        return 0;
    std::memcpy(buffer, entry->bytes.data(), entry->bytes.size());
    return entry->bytes.size();
}
