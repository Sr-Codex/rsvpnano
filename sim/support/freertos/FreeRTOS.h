#pragma once
// Lume Nano simulator: FreeRTOS on host threads (1 tick = 1 ms, real time).
#include <cstddef>
#include <cstdint>
using BaseType_t = int;
using UBaseType_t = unsigned;
using TickType_t = uint32_t;
using StackType_t = uint8_t;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr TickType_t portMAX_DELAY = 0xFFFFFFFFu;
constexpr TickType_t portTICK_PERIOD_MS = 1;
constexpr int tskNO_AFFINITY = 0x7FFFFFFF;
constexpr UBaseType_t configMAX_PRIORITIES = 25;
#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#define pdTICKS_TO_MS(t) (static_cast<uint32_t>(t))
#define portYIELD_FROM_ISR(...) ((void)0)
#define portYIELD() ((void)0)
#define tskIDLE_PRIORITY 0
struct SimMux { int unused = 0; };
using portMUX_TYPE = SimMux;
#define portMUX_INITIALIZER_UNLOCKED {}
void simEnterCritical();
void simExitCritical();
#define portENTER_CRITICAL(mux) simEnterCritical()
#define portEXIT_CRITICAL(mux) simExitCritical()
#define portENTER_CRITICAL_ISR(mux) simEnterCritical()
#define portEXIT_CRITICAL_ISR(mux) simExitCritical()
#define taskENTER_CRITICAL(mux) simEnterCritical()
#define taskEXIT_CRITICAL(mux) simExitCritical()
