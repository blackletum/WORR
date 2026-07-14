/* Standalone FR-10-T10 callback-scope authority tests. */

#include "server/command_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(stderr, "command_context_test:%d: %s\n", __LINE__,    \
                    #expression);                                           \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    } while (0)

static worr_authoritative_command_context_v1 make_context(void)
{
    worr_authoritative_command_context_v1 context;
    worr_command_record_v1 *command;
    worr_rewind_snapshot_time_v1 *snapshot;
    worr_rewind_mapping_proof_v1 *proof;
    memset(&context, 0, sizeof(context));
    context.struct_size = sizeof(context);
    context.schema_version = WORR_COMMAND_CONTEXT_API_VERSION;
    context.client_index = 3;

    command = &context.command;
    command->struct_size = sizeof(*command);
    command->schema_version = WORR_COMMAND_ABI_VERSION;
    command->command_id.epoch = 7;
    command->command_id.sequence = 11;
    command->sample_time_us = UINT64_C(110000);
    command->movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    command->command.struct_size = sizeof(command->command);
    command->command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command->command.duration_ms = 10;
    command->render_watermark.struct_size =
        sizeof(command->render_watermark);
    command->render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    command->render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
    command->render_watermark.source_server_tick = 90;
    command->render_watermark.tick_interval_us = 10000;
    command->render_watermark.source_server_time_us = UINT64_C(900000);
    command->render_watermark.rendered_server_time_us = UINT64_C(900000);

    snapshot = &context.current_snapshot;
    snapshot->struct_size = sizeof(*snapshot);
    snapshot->schema_version = WORR_REWIND_ABI_VERSION;
    snapshot->tick_interval_us = 10000;
    snapshot->snapshot_id.epoch = 4;
    snapshot->snapshot_id.sequence = 101;
    snapshot->server_tick = 100;
    snapshot->server_time_us = UINT64_C(1000000);
    snapshot->consumed_command.cursor.epoch = command->command_id.epoch;
    snapshot->consumed_command.cursor.contiguous_sequence =
        command->command_id.sequence;
    snapshot->consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;

    proof = &context.mapping_proof;
    proof->struct_size = sizeof(*proof);
    proof->schema_version = WORR_REWIND_ABI_VERSION;
    proof->flags = WORR_REWIND_MAPPING_AUTHENTICATED_TIMELINE;
    proof->command_id = command->command_id;
    proof->source_snapshot_id.epoch = snapshot->snapshot_id.epoch;
    proof->source_snapshot_id.sequence = 91;
    proof->source_server_tick =
        command->render_watermark.source_server_tick;
    proof->tick_interval_us = command->render_watermark.tick_interval_us;
    proof->watermark_provenance =
        command->render_watermark.provenance;
    proof->watermark_flags = command->render_watermark.flags;
    proof->source_server_time_us =
        command->render_watermark.source_server_time_us;
    proof->rendered_server_time_us =
        command->render_watermark.rendered_server_time_us;
    proof->mapped_server_time_us =
        command->render_watermark.rendered_server_time_us;
    return context;
}

static void test_scope_lifecycle(void)
{
    const worr_command_context_import_v1 *import =
        SV_CommandContextImportV1();
    worr_authoritative_command_context_v1 context = make_context();
    worr_authoritative_command_context_v1 copy;

    CHECK(import != NULL);
    CHECK(import->struct_size == sizeof(*import));
    CHECK(import->api_version == WORR_COMMAND_CONTEXT_API_VERSION);
    CHECK(import->GetCurrent != NULL && import->GetScopeState != NULL);

    SV_CommandContextReset();
    memset(&copy, 0xa5, sizeof(copy));
    CHECK(import->GetScopeState() ==
          WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY);
    CHECK(!import->GetCurrent(&copy));
    CHECK(!import->GetCurrent(NULL));

    CHECK(SV_CommandContextBeginRejected());
    CHECK(import->GetScopeState() ==
          WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_REJECTED);
    CHECK(!import->GetCurrent(&copy));
    CHECK(!SV_CommandContextBeginRejected());
    CHECK(!SV_CommandContextBegin(&context));
    SV_CommandContextEnd();

    CHECK(SV_CommandContextBegin(&context));
    CHECK(import->GetScopeState() ==
          WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID);
    CHECK(import->GetCurrent(&copy));
    CHECK(memcmp(&copy, &context, sizeof(copy)) == 0);
    copy.client_index = UINT32_MAX;
    CHECK(import->GetCurrent(&copy));
    CHECK(copy.client_index == context.client_index);
    CHECK(!SV_CommandContextBeginRejected());
    CHECK(!SV_CommandContextBegin(&context));
    SV_CommandContextEnd();
    CHECK(import->GetScopeState() ==
          WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY);
    CHECK(!import->GetCurrent(&copy));
}

static void test_context_binding(void)
{
    worr_authoritative_command_context_v1 context = make_context();

    SV_CommandContextReset();
    CHECK(SV_CommandContextBegin(&context));
    SV_CommandContextEnd();

    context.reserved0 = 1;
    CHECK(!SV_CommandContextBegin(&context));
    context = make_context();
    context.current_snapshot.consumed_command.cursor.contiguous_sequence--;
    CHECK(!SV_CommandContextBegin(&context));
    context = make_context();
    context.mapping_proof.command_id.sequence++;
    CHECK(!SV_CommandContextBegin(&context));
    context = make_context();
    context.mapping_proof.source_snapshot_id.epoch++;
    CHECK(!SV_CommandContextBegin(&context));
    context = make_context();
    context.mapping_proof.rendered_server_time_us++;
    CHECK(!SV_CommandContextBegin(&context));
    context = make_context();
    context.current_snapshot.tick_interval_us++;
    CHECK(!SV_CommandContextBegin(&context));
    CHECK(SV_CommandContextBeginRejected());
    SV_CommandContextReset();
}

int main(void)
{
    test_scope_lifecycle();
    test_context_binding();
    puts("command_context_test: ok");
    return 0;
}
