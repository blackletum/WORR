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

// Legacy netchan sequences deliberately do not wrap. Connections are renewed
// before their 30/31-bit sequence space is exhausted, so a numerically smaller
// value is always stale rather than implicitly belonging to a new epoch.
bool NetSequence_IsNewer(uint32_t value, uint32_t reference);

typedef enum {
    NET_SEQUENCE_ACK_ACCEPTED,
    NET_SEQUENCE_ACK_STALE,
    NET_SEQUENCE_ACK_FUTURE,
} net_sequence_ack_result_t;

// outgoing_sequence is the next sequence that would be transmitted. Therefore
// the largest acknowledgement that can be valid is outgoing_sequence - 1.
net_sequence_ack_result_t NetSequence_ClassifyAck(
    uint32_t acknowledgement,
    uint32_t previous_acknowledgement,
    uint32_t outgoing_sequence);

bool NetSequence_ReliableAckConsistent(
    uint32_t acknowledgement,
    bool reliable_ack,
    uint32_t previous_acknowledgement,
    bool previous_reliable_ack);

bool NetSequence_NearExhaustion(uint32_t outgoing_sequence,
                                unsigned sequence_bits,
                                uint32_t guard_sequences);

#ifdef __cplusplus
}
#endif
