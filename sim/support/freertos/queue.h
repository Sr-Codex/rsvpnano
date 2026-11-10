#pragma once
#include "FreeRTOS.h"
struct SimQueue;
using QueueHandle_t = SimQueue*;
QueueHandle_t xQueueCreate(size_t capacity, size_t itemSize);
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t timeout);
BaseType_t xQueueSendToBack(QueueHandle_t queue, const void* item, TickType_t timeout);
BaseType_t xQueueSendToFront(QueueHandle_t queue, const void* item, TickType_t timeout);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void* item);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void* item, BaseType_t* woken);
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t timeout);
BaseType_t xQueuePeek(QueueHandle_t queue, void* item, TickType_t timeout);
BaseType_t xQueueReset(QueueHandle_t queue);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);
