/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/sequence.h"

#include <limits.h>

bool NetSequence_IsNewer(uint32_t value, uint32_t reference)
{
    return value > reference;
}

net_sequence_ack_result_t NetSequence_ClassifyAck(
    uint32_t acknowledgement,
    uint32_t previous_acknowledgement,
    uint32_t outgoing_sequence)
{
    if (acknowledgement < previous_acknowledgement)
        return NET_SEQUENCE_ACK_STALE;
    if (acknowledgement >= outgoing_sequence)
        return NET_SEQUENCE_ACK_FUTURE;
    return NET_SEQUENCE_ACK_ACCEPTED;
}

bool NetSequence_ReliableAckConsistent(
    uint32_t acknowledgement,
    bool reliable_ack,
    uint32_t previous_acknowledgement,
    bool previous_reliable_ack)
{
    return acknowledgement != previous_acknowledgement ||
           reliable_ack == previous_reliable_ack;
}

bool NetSequence_NearExhaustion(uint32_t outgoing_sequence,
                                unsigned sequence_bits,
                                uint32_t guard_sequences)
{
    if (!sequence_bits || sequence_bits >= 32u)
        return true;

    const uint32_t end = UINT32_C(1) << sequence_bits;
    if (guard_sequences >= end)
        return true;
    return outgoing_sequence >= end - guard_sequences;
}
