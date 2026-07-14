/* Compile the local-action audit declarations and layouts as strict C11. */
#include "common/net/local_action_audit.h"

#include <stddef.h>

_Static_assert(sizeof(worr_local_action_audit_slot_v1) == 2528,
               "C audit slot layout mismatch");
_Static_assert(sizeof(worr_local_action_audit_telemetry_v1) == 248,
               "C audit telemetry layout mismatch");
_Static_assert(offsetof(worr_local_action_audit_slot_v1, command_id) == 16,
               "C audit command ID offset mismatch");
_Static_assert(offsetof(worr_local_action_audit_slot_v1,
                        first_arrival_serial) == 24,
               "C audit arrival serial offset mismatch");
_Static_assert(offsetof(worr_local_action_audit_slot_v1, predicted) == 32,
               "C audit predicted offset mismatch");
_Static_assert(offsetof(worr_local_action_audit_slot_v1, authoritative) == 1248,
               "C audit authority offset mismatch");
_Static_assert(offsetof(worr_local_action_audit_slot_v1, correction) == 2464,
               "C audit correction offset mismatch");
_Static_assert(_Generic(&Worr_LocalActionAuditValidateOperationalV2,
                   bool (*)(const worr_local_action_audit_v1 *): 1,
                   default: 0),
               "C operational validator signature mismatch");
_Static_assert(_Generic(&Worr_LocalActionAuditValidateDeepV2,
                   bool (*)(const worr_local_action_audit_v1 *): 1,
                   default: 0),
               "C deep validator signature mismatch");

int main(void) {
  worr_local_action_audit_v1 audit = {0};
  worr_local_action_audit_slot_v1 slot = {0};
  worr_local_action_audit_telemetry_v1 telemetry = {0};
  return sizeof(audit) != 0 && sizeof(slot) == 2528 &&
                 sizeof(telemetry) == 248 &&
                 WORR_LOCAL_ACTION_AUDIT_VERSION == 1 &&
                 WORR_LOCAL_ACTION_AUDIT_OPERATIONAL_API_VERSION == 2 &&
                 WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY == 512
             ? 0
             : 1;
}
