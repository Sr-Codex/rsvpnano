#pragma once
#include "FreeRTOS.h"
struct SimTask;
using TaskHandle_t = SimTask*;
using TaskFunction_t = void (*)(void*);
BaseType_t xTaskCreate(TaskFunction_t entry, const char* name, uint32_t stackBytes, void* parameter,
                       UBaseType_t priority, TaskHandle_t* handle);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char* name, uint32_t stackBytes, void* parameter,
                                   UBaseType_t priority, TaskHandle_t* handle, BaseType_t core);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount();
TickType_t xTaskGetTickCountFromISR();
TaskHandle_t xTaskGetCurrentTaskHandle();
const char* pcTaskGetName(TaskHandle_t task);
BaseType_t xPortGetCoreID();
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t* woken);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
UBaseType_t uxTaskPriorityGet(TaskHandle_t task);
void vTaskPrioritySet(TaskHandle_t task, UBaseType_t priority);
void vTaskSuspendAll();
BaseType_t xTaskResumeAll();
