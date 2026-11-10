#pragma once
#include <cstdio>
#include "esp_err.h"
// Simulator log: INFO and above to stderr (set LUME_SIM_DEBUG=1 for DEBUG).
namespace sim { bool debugLog(); }
#define SIM_LOG(level, tag, format, ...) std::fprintf(stderr, "[%s] %s: " format "\n", level, tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, format, ...) SIM_LOG("E", tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) SIM_LOG("W", tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) SIM_LOG("I", tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) do { if (sim::debugLog()) SIM_LOG("D", tag, format, ##__VA_ARGS__); } while (0)
#define ESP_LOGV(tag, format, ...) ((void)0)
#define ESP_EARLY_LOGE ESP_LOGE
#define ESP_EARLY_LOGW ESP_LOGW
#define ESP_EARLY_LOGI ESP_LOGI
#define ESP_DRAM_LOGE ESP_LOGE
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
inline void esp_log_level_set(const char*, esp_log_level_t) {}
