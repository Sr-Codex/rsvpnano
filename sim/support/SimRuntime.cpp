// Lume Nano simulator: host implementations of the Arduino/FreeRTOS/ESP runtime.
#include <Arduino.h>
#include <esp_system.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <pthread.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "SimRuntime.h"

namespace {
    const auto gStart = std::chrono::steady_clock::now();
    std::recursive_mutex gCritical;
    std::mutex gGpioMutex;
    std::map<int, int> gGpio;
} // namespace

namespace sim {
    unsigned long millis() {
        return static_cast<unsigned long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gStart).count());
    }
    unsigned long micros() {
        return static_cast<unsigned long>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - gStart).count());
    }
    int gpioRead(int pin) {
        std::lock_guard lock(gGpioMutex);
        const auto it = gGpio.find(pin);
        return it == gGpio.end() ? HIGH : it->second; // pulled-up inputs idle high
    }
    void gpioWrite(int pin, int value) {
        std::lock_guard lock(gGpioMutex);
        gGpio[pin] = value;
    }
    bool debugLog() {
        static const bool enabled = std::getenv("LUME_SIM_DEBUG") != nullptr;
        return enabled;
    }
} // namespace sim

void simEnterCritical() { gCritical.lock(); }
void simExitCritical() { gCritical.unlock(); }

// ---------------------------------------------------------------- tasks
struct SimTask {
    std::string name;
    TaskFunction_t entry = nullptr;
    void* parameter = nullptr;
    UBaseType_t priority = 1;
    std::mutex mutex;
    std::condition_variable notified;
    uint32_t notifications = 0;
    std::atomic<bool> deleteRequested{false};
    bool exited = false; // guarded by mutex; SimTask objects are never freed (tasks are few)
};

namespace {
    thread_local SimTask* tCurrent = nullptr;
    SimTask gMainTask{.name = "loopTask"};

    SimTask* current() { return tCurrent ? tCurrent : &gMainTask; }

    [[noreturn]] void exitCurrentTask() {
        SimTask* task = tCurrent;
        tCurrent = nullptr;
        {
            std::lock_guard lock(task->mutex);
            task->exited = true;
        }
        task->notified.notify_all();
        pthread_exit(nullptr);
    }

    void exitIfDeleted() {
        if (tCurrent && tCurrent->deleteRequested.load())
            exitCurrentTask();
    }
} // namespace

namespace sim {
    void sleepMs(unsigned long ms) {
        exitIfDeleted();
        if (ms == 0)
            std::this_thread::yield();
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        exitIfDeleted();
    }
} // namespace sim

BaseType_t xTaskCreate(TaskFunction_t entry, const char* name, uint32_t, void* parameter, UBaseType_t priority,
                       TaskHandle_t* handle) {
    auto* task = new SimTask;
    task->name = name ? name : "task";
    task->entry = entry;
    task->parameter = parameter;
    task->priority = priority;
    if (handle)
        *handle = task;
    std::thread([task] {
        tCurrent = task;
        pthread_setname_np(task->name.c_str());
        task->entry(task->parameter);
        // FreeRTOS tasks must not return; treat it like vTaskDelete(nullptr).
        if (tCurrent)
            exitCurrentTask();
    }).detach();
    return pdPASS;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char* name, uint32_t stack, void* parameter,
                                   UBaseType_t priority, TaskHandle_t* handle, BaseType_t) {
    return xTaskCreate(entry, name, stack, parameter, priority, handle);
}

void vTaskDelete(TaskHandle_t task) {
    if (task == nullptr || task == tCurrent) {
        if (tCurrent)
            exitCurrentTask();
        return; // main loop never deletes itself
    }
    // Another thread: the task exits at its next delay/notify wait (cooperative). Wait for it so the
    // caller can free queues the task uses, as FreeRTOS guarantees the task no longer runs.
    task->deleteRequested = true;
    task->notified.notify_all();
    std::unique_lock lock(task->mutex);
    task->notified.wait_for(lock, std::chrono::seconds(2), [task] { return task->exited; });
}

void vTaskDelay(TickType_t ticks) { sim::sleepMs(ticks); }
TickType_t xTaskGetTickCount() { return static_cast<TickType_t>(sim::millis()); }
TickType_t xTaskGetTickCountFromISR() { return xTaskGetTickCount(); }
TaskHandle_t xTaskGetCurrentTaskHandle() { return current(); }
const char* pcTaskGetName(TaskHandle_t task) { return (task ? task : current())->name.c_str(); }
BaseType_t xPortGetCoreID() { return tCurrent ? 0 : 1; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
UBaseType_t uxTaskPriorityGet(TaskHandle_t task) { return (task ? task : current())->priority; }
void vTaskPrioritySet(TaskHandle_t task, UBaseType_t priority) { (task ? task : current())->priority = priority; }
void vTaskSuspendAll() { gCritical.lock(); }
BaseType_t xTaskResumeAll() { gCritical.unlock(); return pdFALSE; }

void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t* woken) {
    xTaskNotifyGive(task);
    if (woken)
        *woken = pdFALSE;
}

BaseType_t xTaskNotifyGive(TaskHandle_t task) {
    if (!task)
        return pdFAIL;
    {
        std::lock_guard lock(task->mutex);
        ++task->notifications;
    }
    task->notified.notify_all();
    return pdPASS;
}

uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout) {
    exitIfDeleted();
    SimTask* task = current();
    std::unique_lock lock(task->mutex);
    const auto ready = [task] { return task->notifications != 0 || task->deleteRequested.load(); };
    if (timeout == portMAX_DELAY)
        task->notified.wait(lock, ready);
    else
        task->notified.wait_for(lock, std::chrono::milliseconds(timeout), ready);
    const uint32_t value = task->notifications;
    if (value)
        task->notifications = clear ? 0 : value - 1;
    lock.unlock();
    exitIfDeleted();
    return value;
}

// ---------------------------------------------------------------- queues
struct SimQueue {
    size_t capacity;
    size_t itemSize;
    std::deque<std::vector<uint8_t>> items;
    std::mutex mutex;
    std::condition_variable changed;
};

namespace {
    template<typename Pred>
    bool waitFor(std::unique_lock<std::mutex>& lock, std::condition_variable& cv, TickType_t timeout, Pred pred) {
        if (pred())
            return true;
        if (timeout == 0)
            return false;
        if (timeout == portMAX_DELAY) {
            cv.wait(lock, pred);
            return true;
        }
        return cv.wait_for(lock, std::chrono::milliseconds(timeout), pred);
    }

    BaseType_t send(QueueHandle_t queue, const void* item, TickType_t timeout, bool front) {
        if (!queue)
            return pdFAIL;
        std::unique_lock lock(queue->mutex);
        if (!waitFor(lock, queue->changed, timeout, [queue] { return queue->items.size() < queue->capacity; }))
            return pdFALSE;
        const auto* bytes = static_cast<const uint8_t*>(item);
        std::vector<uint8_t> copy(bytes, bytes + queue->itemSize);
        if (front)
            queue->items.push_front(std::move(copy));
        else
            queue->items.push_back(std::move(copy));
        lock.unlock();
        queue->changed.notify_all();
        return pdTRUE;
    }
} // namespace

QueueHandle_t xQueueCreate(size_t capacity, size_t itemSize) {
    auto* queue = new SimQueue;
    queue->capacity = capacity;
    queue->itemSize = itemSize;
    return queue;
}
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t timeout) { return send(q, item, timeout, false); }
BaseType_t xQueueSendToBack(QueueHandle_t q, const void* item, TickType_t timeout) { return send(q, item, timeout, false); }
BaseType_t xQueueSendToFront(QueueHandle_t q, const void* item, TickType_t timeout) { return send(q, item, timeout, true); }
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void* item, BaseType_t* woken) {
    if (woken)
        *woken = pdFALSE;
    return send(q, item, 0, false);
}
BaseType_t xQueueOverwrite(QueueHandle_t q, const void* item) {
    {
        std::lock_guard lock(q->mutex);
        q->items.clear();
    }
    return send(q, item, 0, false);
}
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t timeout) {
    if (!queue)
        return pdFALSE;
    std::unique_lock lock(queue->mutex);
    if (!waitFor(lock, queue->changed, timeout, [queue] { return !queue->items.empty(); }))
        return pdFALSE;
    std::memcpy(item, queue->items.front().data(), queue->itemSize);
    queue->items.pop_front();
    lock.unlock();
    queue->changed.notify_all();
    return pdTRUE;
}
BaseType_t xQueuePeek(QueueHandle_t queue, void* item, TickType_t timeout) {
    std::unique_lock lock(queue->mutex);
    if (!waitFor(lock, queue->changed, timeout, [queue] { return !queue->items.empty(); }))
        return pdFALSE;
    std::memcpy(item, queue->items.front().data(), queue->itemSize);
    return pdTRUE;
}
BaseType_t xQueueReset(QueueHandle_t queue) {
    if (queue) {
        std::lock_guard lock(queue->mutex);
        queue->items.clear();
    }
    if (queue)
        queue->changed.notify_all();
    return pdPASS;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
    std::lock_guard lock(queue->mutex);
    return static_cast<UBaseType_t>(queue->items.size());
}
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t queue) {
    std::lock_guard lock(queue->mutex);
    return static_cast<UBaseType_t>(queue->capacity - queue->items.size());
}
void vQueueDelete(QueueHandle_t queue) { delete queue; }

// ---------------------------------------------------------------- semaphores
struct SimSemaphore {
    bool recursive = false;
    UBaseType_t maximum = 1;
    UBaseType_t count = 0;
    std::thread::id owner;
    UBaseType_t depth = 0;
    std::mutex mutex;
    std::condition_variable changed;
};

SemaphoreHandle_t xSemaphoreCreateMutex() { auto* s = new SimSemaphore; s->count = 1; return s; }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { auto* s = xSemaphoreCreateMutex(); s->recursive = true; return s; }
SemaphoreHandle_t xSemaphoreCreateBinary() { return new SimSemaphore; }
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t maximum, UBaseType_t initial) {
    auto* s = new SimSemaphore;
    s->maximum = maximum;
    s->count = initial;
    return s;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t timeout) {
    std::unique_lock lock(s->mutex);
    if (!waitFor(lock, s->changed, timeout, [s] { return s->count > 0; }))
        return pdFALSE;
    --s->count;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    {
        std::lock_guard lock(s->mutex);
        if (s->count >= s->maximum)
            return pdFALSE;
        ++s->count;
    }
    s->changed.notify_all();
    return pdTRUE;
}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t timeout) {
    {
        std::lock_guard lock(s->mutex);
        if (s->depth > 0 && s->owner == std::this_thread::get_id()) {
            ++s->depth;
            return pdTRUE;
        }
    }
    if (xSemaphoreTake(s, timeout) != pdTRUE)
        return pdFALSE;
    std::lock_guard lock(s->mutex);
    s->owner = std::this_thread::get_id();
    s->depth = 1;
    return pdTRUE;
}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) {
    {
        std::lock_guard lock(s->mutex);
        if (s->depth == 0 || s->owner != std::this_thread::get_id())
            return pdFALSE;
        if (--s->depth > 0)
            return pdTRUE;
        s->owner = {};
    }
    return xSemaphoreGive(s);
}
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t* woken) {
    if (woken)
        *woken = pdFALSE;
    return xSemaphoreGive(s);
}
void vSemaphoreDelete(SemaphoreHandle_t s) { delete s; }

// ---------------------------------------------------------------- restart
void EspClass::restart() { esp_restart(); }
void esp_restart() { sim::requestRestart(); }
