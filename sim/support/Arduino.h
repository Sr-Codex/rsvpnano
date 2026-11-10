#pragma once
// Lume Nano simulator: Arduino core shim for the host (real time, real threads).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "esp_attr.h"
#include "pgmspace.h"

using std::max;
using std::min;
using boolean = bool;
using byte = uint8_t;
class __FlashStringHelper;
#define F(s) (s)

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09

namespace sim {
    unsigned long millis();
    unsigned long micros();
    void sleepMs(unsigned long ms);
    // Simulated GPIO levels (buttons are active-low, like the board).
    int gpioRead(int pin);
    void gpioWrite(int pin, int value);
} // namespace sim

inline unsigned long millis() { return sim::millis(); }
inline unsigned long micros() { return sim::micros(); }
inline void delay(unsigned long ms) { sim::sleepMs(ms); }
inline void delayMicroseconds(unsigned int us) { sim::sleepMs(us / 1000); }
inline void yield() { sim::sleepMs(0); }

inline void pinMode(int, int) {}
inline int digitalRead(int pin) { return sim::gpioRead(pin); }
inline void digitalWrite(int pin, int value) { sim::gpioWrite(pin, value); }
inline int analogRead(int) { return 0; }
inline uint32_t analogReadMilliVolts(int) { return 4000; }
inline void analogWrite(int, int) {}
inline void analogWriteResolution(int, int) {}
inline void analogWriteFrequency(int, int) {}
inline void analogReadResolution(int) {}
inline void analogSetPinAttenuation(int, int) {}

inline long random(long maximum) { return maximum <= 0 ? 0 : std::rand() % maximum; }
inline long random(long minimum, long maximum) { return maximum <= minimum ? minimum : minimum + std::rand() % (maximum - minimum); }
inline void randomSeed(unsigned long seed) { std::srand(static_cast<unsigned>(seed)); }

template<typename T, typename L, typename H>
constexpr T constrain(T value, L low, H high) {
    return value < low ? low : (value > high ? high : value);
}
inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
    return inMax == inMin ? outMin : (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

class String {
    std::string s_;

public:
    String() = default;
    String(const char* cs) : s_(cs ? cs : "") {}
    String(char c) : s_(1, c) {}
    String(const std::string& s) : s_(s) {}
    String(int value) : s_(std::to_string(value)) {}
    String(unsigned int value) : s_(std::to_string(value)) {}
    String(long value) : s_(std::to_string(value)) {}
    String(unsigned long value) : s_(std::to_string(value)) {}
    String(long long value) : s_(std::to_string(value)) {}
    String(unsigned long long value) : s_(std::to_string(value)) {}
    String(float value, unsigned int decimals = 2) : String(static_cast<double>(value), decimals) {}
    String(double value, unsigned int decimals = 2) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", static_cast<int>(decimals), value);
        s_ = buffer;
    }
    String(const String&) = default;
    String(String&&) = default;
    String& operator=(const String&) = default;
    String& operator=(String&&) = default;
    String& operator=(const char* cs) {
        s_ = cs ? cs : "";
        return *this;
    }

    size_t length() const { return s_.size(); }
    bool isEmpty() const { return s_.empty(); }
    char operator[](size_t i) const { return s_[i]; }
    char& operator[](size_t i) { return s_[i]; }
    char charAt(size_t i) const { return i < s_.size() ? s_[i] : 0; }
    String& operator+=(char c) { s_ += c; return *this; }
    String& operator+=(const char* cs) { if (cs) s_ += cs; return *this; }
    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String& operator+=(int v) { s_ += std::to_string(v); return *this; }
    String& operator+=(unsigned v) { s_ += std::to_string(v); return *this; }
    bool concat(const char* cs) { if (cs) s_ += cs; return true; }
    bool concat(const String& o) { s_ += o.s_; return true; }
    bool concat(char c) { s_ += c; return true; }
    friend String operator+(String lhs, const String& rhs) { lhs.s_ += rhs.s_; return lhs; }
    friend String operator+(String lhs, const char* rhs) { if (rhs) lhs.s_ += rhs; return lhs; }
    friend String operator+(String lhs, char rhs) { lhs.s_ += rhs; return lhs; }
    friend String operator+(const char* lhs, const String& rhs) { String out(lhs); out.s_ += rhs.s_; return out; }
    bool operator==(const char* cs) const { return s_ == (cs ? cs : ""); }
    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator!=(const char* cs) const { return !(*this == cs); }
    bool operator!=(const String& o) const { return !(*this == o); }
    bool operator<(const String& o) const { return s_ < o.s_; }
    bool equals(const String& o) const { return s_ == o.s_; }
    bool equalsIgnoreCase(const String& o) const {
        if (s_.size() != o.s_.size()) return false;
        for (size_t i = 0; i < s_.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(s_[i])) != std::tolower(static_cast<unsigned char>(o.s_[i]))) return false;
        return true;
    }
    void reserve(size_t n) { s_.reserve(n); }
    bool startsWith(const char* prefix) const { return prefix && s_.rfind(prefix, 0) == 0; }
    bool startsWith(const String& prefix) const { return startsWith(prefix.c_str()); }
    bool endsWith(const char* suffix) const {
        if (!suffix) return false;
        const size_t sl = std::strlen(suffix);
        return sl <= s_.size() && s_.compare(s_.size() - sl, sl, suffix) == 0;
    }
    bool endsWith(const String& suffix) const { return endsWith(suffix.c_str()); }
    int indexOf(char c, unsigned int from = 0) const { const auto p = s_.find(c, from); return p == std::string::npos ? -1 : int(p); }
    int indexOf(const char* n, unsigned int from = 0) const { if (!n) return -1; const auto p = s_.find(n, from); return p == std::string::npos ? -1 : int(p); }
    int indexOf(const String& n, unsigned int from = 0) const { return indexOf(n.c_str(), from); }
    int lastIndexOf(char c) const { const auto p = s_.rfind(c); return p == std::string::npos ? -1 : int(p); }
    String substring(unsigned int b) const { return b >= s_.size() ? String() : String(s_.substr(b)); }
    String substring(unsigned int b, unsigned int e) const {
        if (b > e) std::swap(b, e);
        if (b >= s_.size()) return String();
        e = std::min(e, static_cast<unsigned int>(s_.size()));
        return String(s_.substr(b, e - b));
    }
    void replace(const char* find, const char* repl) {
        if (!find || !*find) return;
        const std::string f(find), r(repl ? repl : "");
        for (size_t pos = 0; (pos = s_.find(f, pos)) != std::string::npos; pos += r.size()) s_.replace(pos, f.size(), r);
    }
    void replace(const String& f, const String& r) { replace(f.c_str(), r.c_str()); }
    void remove(unsigned int index, unsigned int count) { if (index < s_.size()) s_.erase(index, count); }
    void remove(unsigned int index) { if (index < s_.size()) s_.erase(index); }
    void trim() {
        size_t b = 0, e = s_.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s_[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s_[e - 1]))) --e;
        s_ = s_.substr(b, e - b);
    }
    void toLowerCase() { for (char& c : s_) c = char(std::tolower(static_cast<unsigned char>(c))); }
    void toUpperCase() { for (char& c : s_) c = char(std::toupper(static_cast<unsigned char>(c))); }
    long toInt() const { return std::strtol(s_.c_str(), nullptr, 10); }
    float toFloat() const { return std::strtof(s_.c_str(), nullptr); }
    const char* c_str() const { return s_.c_str(); }
    const std::string& str() const { return s_; }
    operator std::string() const { return s_; }
};

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t value) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        for (size_t i = 0; i < size; ++i) n += write(buffer[i]);
        return n;
    }
    size_t write(const char* text) { return text ? write(reinterpret_cast<const uint8_t*>(text), std::strlen(text)) : 0; }
    size_t write(const char* buffer, size_t size) { return write(reinterpret_cast<const uint8_t*>(buffer), size); }
    size_t print(const char* text) { return write(text); }
    size_t print(const String& text) { return write(text.c_str()); }
    size_t print(const std::string& text) { return write(text.c_str()); }
    size_t print(char c) { return write(static_cast<uint8_t>(c)); }
    size_t print(int v) { return printf("%d", v); }
    size_t print(unsigned v) { return printf("%u", v); }
    size_t print(long v) { return printf("%ld", v); }
    size_t print(unsigned long v) { return printf("%lu", v); }
    size_t print(long long v) { return printf("%lld", v); }
    size_t print(unsigned long long v) { return printf("%llu", v); }
    size_t print(double v, int digits = 2) { return printf("%.*f", digits, v); }
    template<typename T>
    size_t println(const T& value) { const size_t n = print(value); return n + println(); }
    size_t println() { return write("\n"); }
    size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3))) {
        char buffer[512];
        va_list args;
        va_start(args, format);
        const int n = std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        if (n <= 0) return 0;
        return write(reinterpret_cast<const uint8_t*>(buffer), std::min<size_t>(n, sizeof(buffer) - 1));
    }
};

class Stream : public Print {
public:
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }
    virtual void flush() {}
    void setTimeout(unsigned long) {}
    size_t readBytes(char* buffer, size_t length) {
        size_t n = 0;
        while (n < length) { const int c = read(); if (c < 0) break; buffer[n++] = char(c); }
        return n;
    }
    size_t readBytes(uint8_t* buffer, size_t length) { return readBytes(reinterpret_cast<char*>(buffer), length); }
};

// USB CDC console: the simulator prints to stdout and never has a companion host attached.
class HWCDC : public Stream {
public:
    void begin(unsigned long = 115200) {}
    void end() {}
    explicit operator bool() const { return true; }
    size_t write(uint8_t value) override { std::fputc(value, stdout); return 1; }
    size_t write(const uint8_t* buffer, size_t size) override { return std::fwrite(buffer, 1, size, stdout); }
    int availableForWrite() { return 4096; }
    void setTxTimeoutMs(uint32_t) {}
    void setRxBufferSize(size_t) {}
    void setTxBufferSize(size_t) {}
    using Print::write;
};
inline HWCDC Serial;
using USBCDC = HWCDC;

class EspClass {
public:
    void restart();
    uint32_t getPsramSize() { return 8u * 1024u * 1024u; }
    uint32_t getFreePsram() { return 6u * 1024u * 1024u; }
    uint32_t getMinFreePsram() { return 6u * 1024u * 1024u; }
    uint32_t getMaxAllocPsram() { return 4u * 1024u * 1024u; }
    uint32_t getHeapSize() { return 320u * 1024u; }
    uint32_t getFreeHeap() { return 200u * 1024u; }
    uint32_t getMinFreeHeap() { return 180u * 1024u; }
    uint32_t getMaxAllocHeap() { return 100u * 1024u; }
    uint32_t getFlashChipSize() { return 16u * 1024u * 1024u; }
    uint32_t getCpuFreqMHz() { return 240; }
    const char* getChipModel() { return "ESP32-S3 (simulated)"; }
    uint8_t getChipRevision() { return 0; }
    uint8_t getChipCores() { return 2; }
    uint64_t getEfuseMac() { return 0x0000A1B2C3D4E5F6ull; }
    const char* getSdkVersion() { return "lume-sim"; }
    uint32_t getSketchSize() { return 3524994; }
    uint32_t getFreeSketchSpace() { return 6553600; }
};
inline EspClass ESP;

inline bool psramFound() { return true; }
inline void* ps_malloc(size_t size) { return std::malloc(size); }
inline void* ps_calloc(size_t n, size_t size) { return std::calloc(n, size); }
inline void* ps_realloc(void* p, size_t size) { return std::realloc(p, size); }

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
