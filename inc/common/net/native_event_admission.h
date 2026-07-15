/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/event_stream_owner.h"
#include "common/net/native_carrier_ack.h"
#include "common/net/native_codec.h"
#include "shared/cgame_event_runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transactional semantic admission for completed server-originated native
 * event/control DATA (FR-10-T04/T05).
 *
 * The adapter is transport-hook-neutral and owns no storage.  A caller first
 * reassembles DATA into its RX session, then passes the exact MESSAGE_COMPLETE
 * proof here.  This function precomputes RX Commit and ACK-ledger mutation on
 * private copies before invoking the irreversible cgame consumer.  Live
 * transport state is published only after a fresh post-consume status proves
 * the exact descriptor/event is resident.  No cached receipt is accepted.
 */
#define WORR_NATIVE_EVENT_ADMISSION_ABI_VERSION 1u

typedef worr_cgame_event_runtime_result_v1 (
    *worr_native_event_reset_authority_v1)(void *opaque, uint32_t stream_epoch,
                                           uint32_t first_sequence);
typedef worr_cgame_event_runtime_result_v1 (*worr_native_event_submit_batch_v1)(
    void *opaque, const worr_event_record_v1 *records, uint32_t count);
typedef bool (*worr_native_event_get_status_v1)(
    void *opaque, worr_cgame_event_runtime_status_v1 *status_out);

typedef struct worr_native_event_consumer_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t reserved0;
  void *opaque;
  worr_native_event_reset_authority_v1 ResetAuthority;
  worr_native_event_submit_batch_v1 SubmitAuthoritativeBatch;
  worr_native_event_get_status_v1 GetStatus;
} worr_native_event_consumer_v1;

/*
 * The callback table is a trusted, synchronous process-local endpoint.
 * `opaque` must be non-NULL, must not point into any admission argument, and
 * callbacks must not mutate owner/session/slot/arena/ledger state by another
 * alias.  The admission adapter checks direct overlap; the caller owns the
 * stronger no-hidden-alias guarantee.  One event owner must remain bound to
 * the same consumer activation lineage: replacing the endpoint or resetting
 * it out of band requires quiescing admission and advancing/resetting the
 * owning connection state first.  Retired transport banks may release late
 * TX ACKs, but may never invoke semantic DATA or repeat admission.
 */

typedef enum worr_native_event_admission_result_v1_e {
  WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_ACTIVATED = 0,
  WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_DUPLICATE = 1,
  WORR_NATIVE_EVENT_ADMISSION_EVENT_ACCEPTED = 2,
  WORR_NATIVE_EVENT_ADMISSION_EVENT_DUPLICATE = 3,
  WORR_NATIVE_EVENT_ADMISSION_EVENT_DEGRADED = 4,
  WORR_NATIVE_EVENT_ADMISSION_RETRY_UNCOMMITTED = 5,
  WORR_NATIVE_EVENT_ADMISSION_RESYNC_UNCOMMITTED = 6,
  WORR_NATIVE_EVENT_ADMISSION_NOT_READY = 7,
  WORR_NATIVE_EVENT_ADMISSION_WRONG_EPOCH = 8,
  WORR_NATIVE_EVENT_ADMISSION_CONFLICT = 9,
  WORR_NATIVE_EVENT_ADMISSION_UNSUPPORTED = 10,
  WORR_NATIVE_EVENT_ADMISSION_MALFORMED = 11,
  WORR_NATIVE_EVENT_ADMISSION_INVALID_ARGUMENT = 12,
  WORR_NATIVE_EVENT_ADMISSION_INVALID_STATE = 13,
  WORR_NATIVE_EVENT_ADMISSION_NOT_NEGOTIATED = 14,
  WORR_NATIVE_EVENT_ADMISSION_DESCRIPTOR_REPEAT_REVALIDATED = 15,
  WORR_NATIVE_EVENT_ADMISSION_EVENT_REPEAT_REVALIDATED = 16,
} worr_native_event_admission_result_v1;

/*
 * Success commits owner/session/slots/ledger together.  RESYNC_UNCOMMITTED
 * leaves session, slots, and payload storage unchanged, retires every pending
 * event/descriptor ACK receipt while preserving unrelated receipt classes,
 * and may advance/scrub the owner high-water because a consumer callback was
 * invoked and cannot be rolled back.  Every other failure leaves all mutable
 * state byte-identical.
 */
worr_native_event_admission_result_v1
Worr_NativeEventAdmissionCommitCompletedV1(
    worr_event_stream_owner_v1 *owner,
    const worr_native_session_binding_v1 *binding,
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, const void *payload_arena,
    size_t payload_arena_bytes, worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_rx_message_v1 *message,
    const worr_native_event_consumer_v1 *consumer);

/*
 * Observes and revalidates one exact ALREADY_COMMITTED descriptor/event DATA
 * entry before rearming its transport ACK.  Packet and entry identity are
 * decoded internally, matched field-for-field to committed whole-message RX
 * history, and then passed through the base carrier/session acceptor on
 * private session/slot copies.  A caller cannot rearm an ACK by replaying a
 * process-local history object without a newly admitted repeat packet.
 *
 * ACK mutation is first proven on a private ledger copy.  A fresh cgame status
 * must then prove the active descriptor or exact event receipt before that
 * copy is published.  No submit/reset callback is used on success.  Semantic
 * uncertainty publishes no staged transport work, retires all previously
 * authorized event/descriptor ACK receipts, advances the event owner into its
 * resync barrier, and best-effort scrubs the consumer.
 */
worr_native_event_admission_result_v1
Worr_NativeEventAdmissionRevalidateCommittedRepeatV1(
    worr_event_stream_owner_v1 *owner,
    const worr_native_session_binding_v1 *binding,
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, void *payload_arena,
    size_t payload_arena_bytes, uint64_t now_tick, const void *packet,
    size_t packet_bytes, uint16_t entry_index,
    worr_native_carrier_ack_ledger_v1 *ack_ledger,
    const worr_native_event_consumer_v1 *consumer);

#ifdef __cplusplus
}
#endif
