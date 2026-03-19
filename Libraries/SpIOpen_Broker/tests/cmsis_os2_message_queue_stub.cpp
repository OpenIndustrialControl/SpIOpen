#include "cmsis_os2.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

struct TestMessageQueue {
    uint32_t capacity;
    uint32_t msg_size;
    uint32_t count;
    uint32_t head;
    uint32_t tail;
    uint8_t* storage;
    bool owns_storage;
};

}  // namespace

extern "C" {

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size, const osMessageQueueAttr_t* attr) {
    if ((msg_count == 0U) || (msg_size == 0U)) {
        return nullptr;
    }

    auto* queue = new TestMessageQueue{};
    queue->capacity = msg_count;
    queue->msg_size = msg_size;
    queue->count = 0U;
    queue->head = 0U;
    queue->tail = 0U;
    queue->storage = nullptr;
    queue->owns_storage = true;

    const size_t required_size = static_cast<size_t>(msg_count) * static_cast<size_t>(msg_size);
    if ((attr != nullptr) && (attr->mq_mem != nullptr) && (attr->mq_size >= required_size)) {
        queue->storage = static_cast<uint8_t*>(attr->mq_mem);
        queue->owns_storage = false;
    } else {
        queue->storage = new uint8_t[required_size];
        queue->owns_storage = true;
    }

    return static_cast<osMessageQueueId_t>(queue);
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void* msg_ptr, uint8_t msg_prio, uint32_t timeout) {
    (void)msg_prio;
    if ((mq_id == nullptr) || (msg_ptr == nullptr)) {
        return osErrorParameter;
    }

    auto* queue = static_cast<TestMessageQueue*>(mq_id);
    if (queue->count >= queue->capacity) {
        return (timeout == 0U) ? osErrorResource : osErrorTimeout;
    }

    const size_t slot_offset = static_cast<size_t>(queue->tail) * static_cast<size_t>(queue->msg_size);
    std::memcpy(queue->storage + slot_offset, msg_ptr, queue->msg_size);
    queue->tail = (queue->tail + 1U) % queue->capacity;
    ++queue->count;
    return osOK;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id, void* msg_ptr, uint8_t* msg_prio, uint32_t timeout) {
    (void)msg_prio;
    if ((mq_id == nullptr) || (msg_ptr == nullptr)) {
        return osErrorParameter;
    }

    auto* queue = static_cast<TestMessageQueue*>(mq_id);
    if (queue->count == 0U) {
        return (timeout == 0U) ? osErrorResource : osErrorTimeout;
    }

    const size_t slot_offset = static_cast<size_t>(queue->head) * static_cast<size_t>(queue->msg_size);
    std::memcpy(msg_ptr, queue->storage + slot_offset, queue->msg_size);
    queue->head = (queue->head + 1U) % queue->capacity;
    --queue->count;
    return osOK;
}

uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id) {
    if (mq_id == nullptr) {
        return 0U;
    }
    auto* queue = static_cast<TestMessageQueue*>(mq_id);
    return queue->count;
}

osStatus_t osMessageQueueDelete(osMessageQueueId_t mq_id) {
    if (mq_id == nullptr) {
        return osErrorParameter;
    }

    auto* queue = static_cast<TestMessageQueue*>(mq_id);
    if (queue->owns_storage) {
        delete[] queue->storage;
    }
    delete queue;
    return osOK;
}

// ---------------------------------------------------------------------------
// Thread stubs (non-functional: records handle but does not spawn a thread)
// ---------------------------------------------------------------------------

struct TestThread {
    osThreadFunc_t func;
    void* argument;
};

osThreadId_t osThreadNew(osThreadFunc_t func, void* argument, const osThreadAttr_t* attr) {
    if (func == nullptr) {
        return nullptr;
    }
    if ((attr == nullptr) || (attr->stack_size == 0U) || (attr->stack_mem == nullptr) || (attr->cb_mem == nullptr) ||
        (attr->cb_size == 0U)) {
        return nullptr;
    }
    auto* thread = new TestThread{func, argument};
    return static_cast<osThreadId_t>(thread);
}

osStatus_t osThreadTerminate(osThreadId_t thread_id) {
    if (thread_id == nullptr) {
        return osErrorParameter;
    }
    auto* thread = static_cast<TestThread*>(thread_id);
    delete thread;
    return osOK;
}

}  // extern "C"
