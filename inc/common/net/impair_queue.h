/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_IMPAIR_QUEUE_CAPACITY 1024

typedef struct {
    uint64_t release_ms;
    uint64_t order;
} net_impair_queue_key_t;

typedef struct {
    net_impair_queue_key_t keys[NET_IMPAIR_QUEUE_CAPACITY];
    uint16_t heap[NET_IMPAIR_QUEUE_CAPACITY];
    uint16_t free_ids[NET_IMPAIR_QUEUE_CAPACITY];
    unsigned heap_count;
    unsigned free_count;
    unsigned high_water;
    uint64_t next_order;
    uint64_t overflow_count;
} net_impair_queue_t;

void NetImpairQueue_Init(net_impair_queue_t *queue);
bool NetImpairQueue_Reserve(net_impair_queue_t *queue,
                            unsigned active_limit,
                            uint64_t release_ms,
                            uint16_t *id);
bool NetImpairQueue_Peek(const net_impair_queue_t *queue,
                         uint16_t *id,
                         uint64_t *release_ms);
bool NetImpairQueue_Pop(net_impair_queue_t *queue, uint16_t *id);
void NetImpairQueue_Release(net_impair_queue_t *queue, uint16_t id);

typedef struct {
    uint64_t epoch;
    uint32_t last;
    bool initialized;
} net_impair_clock_t;

uint64_t NetImpairClock_Extend(net_impair_clock_t *clock, uint32_t now);

#ifdef __cplusplus
}
#endif
