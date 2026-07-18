/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server/local_action_shadow_authority.h"

#include "shared/shared.h"

#include <string.h>

#define SV_LOCAL_ACTION_SHADOW_AUTHORITY_CAPACITY 32u

typedef struct local_action_shadow_authority_slot_s {
    worr_local_action_shadow_authority_receipt_v1 receipt;
    uint64_t publish_order;
    uint8_t occupied;
    uint8_t reserved[7];
} local_action_shadow_authority_slot_t;

static local_action_shadow_authority_slot_t
    mailboxes[MAX_CLIENTS][SV_LOCAL_ACTION_SHADOW_AUTHORITY_CAPACITY];
static uint64_t next_publish_order;

static bool command_id_equal(worr_command_id_v1 left,
                             worr_command_id_v1 right)
{
    return left.epoch == right.epoch && left.sequence == right.sequence;
}

static bool publish_receipt(
    uint32_t client_index,
    const worr_local_action_shadow_authority_receipt_v1 *receipt)
{
    uint32_t index;
    int free_index = -1;

    if (client_index >= MAX_CLIENTS || next_publish_order == UINT64_MAX ||
        !Worr_LocalActionShadowAuthorityReceiptValidateV1(receipt)) {
        return false;
    }
    for (index = 0; index < SV_LOCAL_ACTION_SHADOW_AUTHORITY_CAPACITY;
         ++index) {
        local_action_shadow_authority_slot_t *slot =
            &mailboxes[client_index][index];
        if (!slot->occupied) {
            if (free_index < 0)
                free_index = (int)index;
            continue;
        }
        if (!command_id_equal(slot->receipt.command_id, receipt->command_id))
            continue;
        return memcmp(&slot->receipt, receipt, sizeof(*receipt)) == 0;
    }
    if (free_index < 0)
        return false;

    ++next_publish_order;
    mailboxes[client_index][free_index].receipt = *receipt;
    mailboxes[client_index][free_index].publish_order = next_publish_order;
    mailboxes[client_index][free_index].occupied = 1;
    return true;
}

static const worr_local_action_shadow_authority_import_v1 authority_import = {
    sizeof(authority_import),
    WORR_LOCAL_ACTION_SHADOW_AUTHORITY_API_VERSION,
    publish_receipt,
};

void SV_LocalActionShadowAuthorityResetMap(void)
{
    memset(mailboxes, 0, sizeof(mailboxes));
    next_publish_order = 0;
}

void SV_LocalActionShadowAuthorityResetClient(uint32_t client_index)
{
    if (client_index >= MAX_CLIENTS)
        return;
    memset(mailboxes[client_index], 0, sizeof(mailboxes[client_index]));
}

const worr_local_action_shadow_authority_import_v1 *
SV_LocalActionShadowAuthorityImportV1(void)
{
    return &authority_import;
}

static int oldest_receipt_index(uint32_t client_index)
{
    uint32_t index;
    int selected = -1;
    uint64_t selected_order = UINT64_MAX;

    for (index = 0; index < SV_LOCAL_ACTION_SHADOW_AUTHORITY_CAPACITY;
         ++index) {
        local_action_shadow_authority_slot_t *slot =
            &mailboxes[client_index][index];
        if (!slot->occupied || slot->publish_order >= selected_order)
            continue;
        selected = (int)index;
        selected_order = slot->publish_order;
    }
    return selected;
}

bool SV_LocalActionShadowAuthorityPeekNextReceipt(
    uint32_t client_index,
    worr_local_action_shadow_authority_receipt_v1 *receipt_out)
{
    int selected;

    if (client_index >= MAX_CLIENTS || !receipt_out)
        return false;
    selected = oldest_receipt_index(client_index);
    if (selected < 0)
        return false;
    if (!Worr_LocalActionShadowAuthorityReceiptValidateV1(
            &mailboxes[client_index][selected].receipt)) {
        memset(&mailboxes[client_index][selected], 0,
               sizeof(mailboxes[client_index][selected]));
        return false;
    }
    *receipt_out = mailboxes[client_index][selected].receipt;
    return true;
}

bool SV_LocalActionShadowAuthorityConsumeNextReceipt(
    uint32_t client_index,
    const worr_local_action_shadow_authority_receipt_v1 *expected)
{
    int selected;

    if (client_index >= MAX_CLIENTS ||
        !Worr_LocalActionShadowAuthorityReceiptValidateV1(expected)) {
        return false;
    }
    selected = oldest_receipt_index(client_index);
    if (selected < 0 ||
        memcmp(&mailboxes[client_index][selected].receipt, expected,
               sizeof(*expected)) != 0) {
        return false;
    }
    memset(&mailboxes[client_index][selected], 0,
           sizeof(mailboxes[client_index][selected]));
    return true;
}
