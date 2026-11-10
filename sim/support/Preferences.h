#pragma once
// Lume Nano simulator: Arduino Preferences (NVS) persisted as one file per namespace.
#include <Arduino.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

typedef enum { PT_I8, PT_U8, PT_I16, PT_U16, PT_I32, PT_U32, PT_I64, PT_U64, PT_STR, PT_BLOB, PT_INVALID } PreferenceType;

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false, const char* partition = nullptr);
    void end();
    bool clear();
    bool remove(const char* key);
    bool isKey(const char* key);
    PreferenceType getType(const char* key);
    size_t freeEntries() { return 500; }

    size_t putChar(const char* k, int8_t v) { return putValue(k, PT_I8, &v, sizeof v); }
    size_t putUChar(const char* k, uint8_t v) { return putValue(k, PT_U8, &v, sizeof v); }
    size_t putShort(const char* k, int16_t v) { return putValue(k, PT_I16, &v, sizeof v); }
    size_t putUShort(const char* k, uint16_t v) { return putValue(k, PT_U16, &v, sizeof v); }
    size_t putInt(const char* k, int32_t v) { return putValue(k, PT_I32, &v, sizeof v); }
    size_t putUInt(const char* k, uint32_t v) { return putValue(k, PT_U32, &v, sizeof v); }
    size_t putLong(const char* k, int32_t v) { return putInt(k, v); }
    size_t putULong(const char* k, uint32_t v) { return putUInt(k, v); }
    size_t putLong64(const char* k, int64_t v) { return putValue(k, PT_I64, &v, sizeof v); }
    size_t putULong64(const char* k, uint64_t v) { return putValue(k, PT_U64, &v, sizeof v); }
    size_t putFloat(const char* k, float v) { return putValue(k, PT_BLOB, &v, sizeof v); }
    size_t putDouble(const char* k, double v) { return putValue(k, PT_BLOB, &v, sizeof v); }
    size_t putBool(const char* k, bool v) { return putUChar(k, v ? 1 : 0); }
    size_t putString(const char* k, const char* v);
    size_t putString(const char* k, const String& v) { return putString(k, v.c_str()); }
    size_t putBytes(const char* k, const void* v, size_t len) { return putValue(k, PT_BLOB, v, len); }

    int8_t getChar(const char* k, int8_t d = 0) { return getValue(k, d); }
    uint8_t getUChar(const char* k, uint8_t d = 0) { return getValue(k, d); }
    int16_t getShort(const char* k, int16_t d = 0) { return getValue(k, d); }
    uint16_t getUShort(const char* k, uint16_t d = 0) { return getValue(k, d); }
    int32_t getInt(const char* k, int32_t d = 0) { return getValue(k, d); }
    uint32_t getUInt(const char* k, uint32_t d = 0) { return getValue(k, d); }
    int32_t getLong(const char* k, int32_t d = 0) { return getInt(k, d); }
    uint32_t getULong(const char* k, uint32_t d = 0) { return getUInt(k, d); }
    int64_t getLong64(const char* k, int64_t d = 0) { return getValue(k, d); }
    uint64_t getULong64(const char* k, uint64_t d = 0) { return getValue(k, d); }
    float getFloat(const char* k, float d = 0) { return getValue(k, d); }
    double getDouble(const char* k, double d = 0) { return getValue(k, d); }
    bool getBool(const char* k, bool d = false) { return getUChar(k, d ? 1 : 0) != 0; }
    size_t getString(const char* k, char* value, size_t maxLen);
    String getString(const char* k, const String& d = String());
    size_t getBytesLength(const char* k);
    size_t getBytes(const char* k, void* buf, size_t maxLen);

private:
    struct Entry {
        PreferenceType type = PT_INVALID;
        std::vector<uint8_t> bytes;
    };
    size_t putValue(const char* key, PreferenceType type, const void* data, size_t size);
    template<typename T>
    T getValue(const char* key, T fallback) {
        const Entry* entry = find(key);
        if (!entry || entry->bytes.size() != sizeof(T))
            return fallback;
        T value;
        std::memcpy(&value, entry->bytes.data(), sizeof(T));
        return value;
    }
    const Entry* find(const char* key);
    void load();
    void save();

    std::string namespace_;
    bool open_ = false;
    bool readOnly_ = false;
    std::map<std::string, Entry> entries_;
};
