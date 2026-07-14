#include "shared/local_action_abi.h"

#include <stddef.h>

int main(void)
{
    worr_local_action_transaction_v2 transaction = {0};
    worr_local_action_correction_v2 correction = {0};
    return sizeof(worr_local_action_state_v2) == 72 &&
                   sizeof(worr_local_action_weapon_rule_v2) == 64 &&
                   sizeof(worr_local_action_intent_v2) == 24 &&
                   sizeof(worr_local_action_event_v2) == 48 &&
                   sizeof(transaction) == 1216 &&
                   sizeof(correction) == 64 &&
                   offsetof(worr_local_action_transaction_v2, command) == 88 &&
                   offsetof(worr_local_action_transaction_v2, state_after) ==
                       344 &&
                   offsetof(worr_local_action_transaction_v2, events) == 424
               ? 0
               : 1;
}
