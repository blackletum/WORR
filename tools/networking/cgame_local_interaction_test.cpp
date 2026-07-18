/*
 * Cgame-side shadow test: canonical records may create only a local request
 * transaction. No authority, collision, damage, attachment, or presentation
 * interface exists in this test seam.
 */
#include "cg_local_interaction.hpp"
#include "shared/local_interaction_abi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            std::fprintf(stderr, "%s:%d: %s\\n", __FILE__, __LINE__,      \
                         #expression);                                      \
            return EXIT_FAILURE;                                             \
        }                                                                   \
    } while (0)

namespace {

worr_cgame_command_record_entry_v1 source_entries[3];
bool corrupt_range;
bool exact_history_missing;

worr_command_record_v1 make_record(std::uint32_t sequence, bool hook_held)
{
    worr_command_record_v1 record{};
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id = {41, sequence};
    record.sample_time_us = static_cast<std::uint64_t>(sequence) * 16000u;
    record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    record.command.struct_size = sizeof(record.command);
    record.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    record.command.duration_ms = 16;
    if (hook_held)
        record.command.buttons =
            WORR_LOCAL_INTERACTION_HOOK_BUTTON | (1u << 0);
    record.render_watermark.struct_size = sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    return record;
}

std::uint32_t resolve_by_id(
    worr_command_id_v1 command_id,
    worr_cgame_command_record_entry_v1 *entry_out)
{
    if (!entry_out || !Worr_CommandIdValidV1(command_id, false))
        return WORR_CGAME_COMMAND_RECORD_INVALID_ARGUMENT;
    if (!exact_history_missing) {
        for (const auto &entry : source_entries) {
            if (entry.command.command_id.epoch == command_id.epoch &&
                entry.command.command_id.sequence == command_id.sequence) {
                *entry_out = entry;
                return WORR_CGAME_COMMAND_RECORD_OK;
            }
        }
    }
    return WORR_CGAME_COMMAND_RECORD_HISTORY_MISSING;
}

std::uint32_t resolve(std::uint32_t first_legacy_sequence,
                      std::uint32_t command_count,
                      worr_cgame_command_record_range_v1 *range_out)
{
    worr_cgame_command_record_range_v1 output{};
    if (!range_out)
        return WORR_CGAME_COMMAND_RECORD_INVALID_ARGUMENT;
    output.struct_size = sizeof(output);
    output.api_version = WORR_CGAME_COMMAND_RECORD_API_VERSION;
    output.flags = WORR_CGAME_COMMAND_RECORD_CANONICAL;
    output.first_legacy_sequence = first_legacy_sequence;
    for (std::uint32_t index = 0; index < command_count; ++index) {
        bool found = false;
        for (const auto &entry : source_entries) {
            if (entry.legacy_sequence == first_legacy_sequence + index) {
                output.commands[index] = entry;
                found = true;
                break;
            }
        }
        if (!found) {
            output.result = WORR_CGAME_COMMAND_RECORD_HISTORY_MISSING;
            *range_out = output;
            return output.result;
        }
    }
    output.command_count = command_count;
    if (corrupt_range && command_count > 1)
        output.commands[1].command.command_id.sequence = 9;
    output.result = WORR_CGAME_COMMAND_RECORD_OK;
    *range_out = output;
    return output.result;
}

worr_cgame_prediction_input_range_v1 prediction_range()
{
    worr_cgame_prediction_input_range_v1 range{};
    range.struct_size = sizeof(range);
    range.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    range.result = WORR_CGAME_PREDICTION_INPUT_OK;
    range.source = WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    range.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    range.authoritative_legacy_sequence = 100;
    range.current_legacy_sequence = 102;
    range.command_count = 2;
    for (std::uint32_t index = 0; index < range.command_count; ++index) {
        range.commands[index].legacy_sequence = 101 + index;
        range.commands[index].command_id = source_entries[index].command.command_id;
        range.commands[index].command = source_entries[index].command.command;
    }
    return range;
}

worr_cgame_prediction_input_range_v1 expired_prediction_range()
{
    worr_cgame_prediction_input_range_v1 range{};
    range.struct_size = sizeof(range);
    range.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    range.result = WORR_CGAME_PREDICTION_INPUT_OK;
    range.source = WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    range.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    range.authoritative_legacy_sequence = 102;
    range.current_legacy_sequence = 103;
    range.command_count = 1;
    range.commands[0].legacy_sequence = source_entries[2].legacy_sequence;
    range.commands[0].command_id = source_entries[2].command.command_id;
    range.commands[0].command = source_entries[2].command.command;
    return range;
}

worr_local_interaction_authority_receipt_v1 authority_receipt(bool active)
{
    worr_local_interaction_state_v1 initial{};
    worr_local_interaction_intent_v1 intent{};
    worr_local_interaction_transaction_v1 transaction{};
    worr_local_interaction_authority_receipt_v1 receipt{};
    if (!Worr_LocalInteractionStateInitV1(&initial, 41))
        std::abort();
    intent.struct_size = sizeof(intent);
    intent.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
    intent.flags = WORR_LOCAL_INTERACTION_INTENT_HOOK_HELD;
    if (!Worr_LocalInteractionBuildAuthoritativeHookV1(
            &initial, &source_entries[0].command, &intent, active,
            &transaction) ||
        !Worr_LocalInteractionAuthorityReceiptBuildV1(&transaction,
                                                       &receipt)) {
        std::abort();
    }
    return receipt;
}

worr_local_action_shadow_authority_receipt_v1 action_shadow_receipt(
    std::size_t command_index, bool server_packet_watermark = false)
{
    worr_local_action_observation_state_v1 before{};
    before.struct_size = sizeof(before);
    before.schema_version = WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION;
    before.flags = WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ALIVE |
                   WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ELIGIBLE;
    before.phase = WORR_LOCAL_ACTION_OBSERVATION_READY;
    before.active_weapon_id = 9;
    before.presentation_frame = 7;
    before.presentation_rate = 10;
    auto after = before;
    after.presentation_frame = 8;
    worr_local_action_observation_record_v1 observation{};
    worr_local_action_shadow_v1 shadow{};
    worr_local_action_shadow_authority_receipt_v1 receipt{};
    auto command = source_entries[command_index].command;
    if (server_packet_watermark) {
        command.render_watermark.provenance =
            WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED;
        command.render_watermark.source_server_tick = 100;
        command.render_watermark.tick_interval_us = 16000;
        command.render_watermark.source_server_time_us = 1600000;
        command.render_watermark.rendered_server_time_us = 1600000;
    }
    if (!Worr_LocalActionObservationBuildV1(
            static_cast<std::uint32_t>(command_index),
            &command, &before, &after,
            &observation) ||
        !Worr_LocalActionShadowBuildV1(WORR_LOCAL_ACTION_CATALOG_BLASTER,
                                        &observation, &shadow) ||
        !Worr_LocalActionShadowAuthorityReceiptBuildV1(&shadow, &receipt)) {
        std::abort();
    }
    return receipt;
}

} // namespace

int main()
{
    source_entries[0].legacy_sequence = 101;
    source_entries[0].command = make_record(1, true);
    source_entries[1].legacy_sequence = 102;
    source_entries[1].command = make_record(2, true);
    source_entries[2].legacy_sequence = 103;
    source_entries[2].command = make_record(3, true);

    const worr_cgame_command_record_import_v1 import = {
        sizeof(import), WORR_CGAME_COMMAND_RECORD_API_VERSION, resolve};
    const worr_cgame_command_record_import_v2 import_v2 = {
        sizeof(import_v2), WORR_CGAME_COMMAND_RECORD_API_VERSION_V2,
        resolve_by_id};
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionPredict(prediction_range());

    cg_local_interaction_shadow_status_v1 status{};
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.prediction_passes == 1);
    CHECK(status.transactions == 2);
    CHECK(status.pending_requests == 1);
    CHECK(status.unavailable_ranges == 0);
    CHECK(status.invalid_ranges == 0);

    const auto confirmed = authority_receipt(true);
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&confirmed) ==
          cg_local_interaction_receipt_result_v1::hook_confirmed);
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&confirmed) ==
          cg_local_interaction_receipt_result_v1::duplicate);
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.authority_receipts == 1);
    CHECK(status.authority_duplicates == 1);
    CHECK(status.corrections_confirmed == 1);
    CHECK(status.requires_resync == 0);

    corrupt_range = true;
    CG_LocalInteractionPredict(prediction_range());
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.prediction_passes == 1);
    CHECK(status.transactions == 2);
    CHECK(status.invalid_ranges == 1);

    corrupt_range = false;
    CG_LocalInteractionSetImport(&import);
    const auto rejected = authority_receipt(false);
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&rejected) ==
          cg_local_interaction_receipt_result_v1::accepted_unmatched);
    CG_LocalInteractionPredict(prediction_range());
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.authority_unmatched == 1);
    CHECK(status.corrections_rejected == 1);
    CHECK(status.requires_resync == 0);

    /* A receipt-first pair may wait for its prediction, but not silently
     * outlive the retained canonical history that could prove that pair. */
    CG_LocalInteractionSetImport(&import);
    const auto orphaned = authority_receipt(false);
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&orphaned) ==
          cg_local_interaction_receipt_result_v1::accepted_unmatched);
    CG_LocalInteractionPredict(expired_prediction_range());
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.authority_unmatched == 1);
    CHECK(status.authority_expirations == 1);
    CHECK(status.requires_resync == 1);
    CHECK(CG_LocalInteractionRequiresResync());

    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionPredict(prediction_range());
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&confirmed) ==
          cg_local_interaction_receipt_result_v1::hook_confirmed);
    CHECK(CG_LocalInteractionSubmitAuthorityReceipt(&rejected) ==
          cg_local_interaction_receipt_result_v1::conflict);
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.authority_conflicts == 1);
    CHECK(status.requires_resync == 1);

    CG_LocalInteractionSetImport(nullptr);
    CG_LocalInteractionPredict(prediction_range());
    CG_LocalInteractionGetStatus(&status);
    CHECK(status.prediction_passes == 0);
    CHECK(status.transactions == 0);

    CG_LocalInteractionSetImport(&import);
    CG_LocalActionShadowObserveCommands(prediction_range());
    cg_local_action_shadow_status_v1 action_status{};
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.observation_passes == 1);
    CHECK(action_status.canonical_commands == 2);
    const auto action_receipt = action_shadow_receipt(0);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&action_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&action_receipt) ==
          cg_local_action_shadow_receipt_result_v1::duplicate);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.authority_receipts == 1);
    CHECK(action_status.authority_duplicates == 1);
    CHECK(action_status.command_matches == 1);
    CHECK(action_status.requires_resync == 0);

    CG_LocalInteractionSetImport(&import);
    auto early_action_receipt = action_shadow_receipt(0);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&early_action_receipt) ==
          cg_local_action_shadow_receipt_result_v1::accepted_unmatched);
    CG_LocalActionShadowObserveCommands(prediction_range());
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.authority_unmatched == 1);
    CHECK(action_status.command_matches == 1);
    CHECK(action_status.requires_resync == 0);

    CG_LocalInteractionSetImport(&import);
    CG_LocalActionShadowObserveCommands(prediction_range());
    auto mismatched_action_receipt = action_shadow_receipt(1);
    mismatched_action_receipt.command_hash ^= UINT64_C(1);
    CHECK(Worr_LocalActionShadowAuthorityReceiptValidateV1(
        &mismatched_action_receipt));
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(
              &mismatched_action_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_mismatch);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.command_mismatches == 1);
    CHECK(action_status.requires_resync == 1);
    CHECK(CG_LocalActionShadowRequiresResync());

    /* Receipt-time lookup survives skipped render/prediction cadence.  The
     * server's later packet-shared watermark is intentionally different from
     * the client's pre-transport NONE watermark; input identity still pairs
     * exactly because render provenance is not command input. */
    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = false;
    const auto exact_receipt = action_shadow_receipt(0, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&exact_receipt) ==
          cg_local_action_shadow_receipt_result_v1::command_matched);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.exact_lookup_attempts == 1);
    CHECK(action_status.exact_lookup_hits == 1);
    CHECK(action_status.exact_lookup_misses == 0);
    CHECK(action_status.canonical_commands == 1);
    CHECK(action_status.command_matches == 1);
    CHECK(action_status.authority_outstanding == 0);
    CHECK(action_status.requires_resync == 0);

    CG_LocalInteractionSetImport(&import);
    CG_LocalInteractionSetImportV2(&import_v2);
    exact_history_missing = true;
    const auto missing_receipt = action_shadow_receipt(1, true);
    CHECK(CG_LocalActionShadowSubmitAuthorityReceipt(&missing_receipt) ==
          cg_local_action_shadow_receipt_result_v1::accepted_unmatched);
    CG_LocalActionShadowGetStatus(&action_status);
    CHECK(action_status.exact_lookup_attempts == 1);
    CHECK(action_status.exact_lookup_hits == 0);
    CHECK(action_status.exact_lookup_misses == 1);
    CHECK(action_status.authority_outstanding == 1);
    CHECK(action_status.requires_resync == 0);
    CG_LocalInteractionSetImportV2(nullptr);
    return EXIT_SUCCESS;
}
