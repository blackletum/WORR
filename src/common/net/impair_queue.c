/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/impair_queue.h"

#include <limits.h>
#include <string.h>

static bool key_less(const net_impair_queue_t *queue,
                     uint16_t left_id,
                     uint16_t right_id)
{
    const net_impair_queue_key_t *left = &queue->keys[left_id];
    const net_impair_queue_key_t *right = &queue->keys[right_id];
    if (left->release_ms != right->release_ms)
        return left->release_ms < right->release_ms;
    return left->order < right->order;
}

static void heap_push(net_impair_queue_t *queue, uint16_t id)
{
    unsigned index = queue->heap_count++;
    while (index) {
        const unsigned parent = (index - 1u) / 2u;
        const uint16_t parent_id = queue->heap[parent];
        if (!key_less(queue, id, parent_id))
            break;
        queue->heap[index] = parent_id;
        index = parent;
    }
    queue->heap[index] = id;
    if (queue->heap_count > queue->high_water)
        queue->high_water = queue->heap_count;
}

void NetImpairQueue_Init(net_impair_queue_t *queue)
{
    memset(queue, 0, sizeof(*queue));
    queue->free_count = NET_IMPAIR_QUEUE_CAPACITY;
    for (unsigned i = 0; i < NET_IMPAIR_QUEUE_CAPACITY; ++i)
        queue->free_ids[i] =
            (uint16_t)(NET_IMPAIR_QUEUE_CAPACITY - i - 1u);
}

bool NetImpairQueue_Reserve(net_impair_queue_t *queue,
                            unsigned active_limit,
                            uint64_t release_ms,
                            uint16_t *id)
{
    if (active_limit > NET_IMPAIR_QUEUE_CAPACITY)
        active_limit = NET_IMPAIR_QUEUE_CAPACITY;
    if (!active_limit || queue->heap_count >= active_limit ||
        !queue->free_count) {
        queue->overflow_count++;
        return false;
    }

    *id = queue->free_ids[--queue->free_count];
    queue->keys[*id].release_ms = release_ms;
    queue->keys[*id].order = queue->next_order++;
    heap_push(queue, *id);
    return true;
}

bool NetImpairQueue_Peek(const net_impair_queue_t *queue,
                         uint16_t *id,
                         uint64_t *release_ms)
{
    if (!queue->heap_count)
        return false;
    *id = queue->heap[0];
    if (release_ms)
        *release_ms = queue->keys[*id].release_ms;
    return true;
}

bool NetImpairQueue_Pop(net_impair_queue_t *queue, uint16_t *id)
{
    if (!queue->heap_count)
        return false;

    *id = queue->heap[0];
    const uint16_t replacement = queue->heap[--queue->heap_count];
    if (queue->heap_count) {
        unsigned index = 0;
        while (true) {
            const unsigned left = index * 2u + 1u;
            if (left >= queue->heap_count)
                break;
            const unsigned right = left + 1u;
            unsigned child = left;
            if (right < queue->heap_count &&
                key_less(queue, queue->heap[right], queue->heap[left])) {
                child = right;
            }
            if (!key_less(queue, queue->heap[child], replacement))
                break;
            queue->heap[index] = queue->heap[child];
            index = child;
        }
        queue->heap[index] = replacement;
    }
    return true;
}

void NetImpairQueue_Release(net_impair_queue_t *queue, uint16_t id)
{
    if (id >= NET_IMPAIR_QUEUE_CAPACITY ||
        queue->free_count >= NET_IMPAIR_QUEUE_CAPACITY)
        return;
    queue->free_ids[queue->free_count++] = id;
}

uint64_t NetImpairClock_Extend(net_impair_clock_t *clock, uint32_t now)
{
    if (!clock->initialized) {
        clock->initialized = true;
        clock->last = now;
    } else if (now < clock->last &&
               clock->last - now > UINT32_MAX / 2u) {
        clock->epoch += UINT64_C(1) << 32u;
    }
    clock->last = now;
    return clock->epoch + now;
}
