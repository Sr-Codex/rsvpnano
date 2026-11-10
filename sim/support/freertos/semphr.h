#pragma once
#include "FreeRTOS.h"
struct SimSemaphore;
using SemaphoreHandle_t = SimSemaphore*;
SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
SemaphoreHandle_t xSemaphoreCreateBinary();
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t maximum, UBaseType_t initial);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t timeout);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t* woken);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
