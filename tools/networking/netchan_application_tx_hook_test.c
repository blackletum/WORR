/* Deterministic integration tests for the dormant post-assembly TX hook. */

#include "shared/shared.h"
#include "common/net/chan.h"
#include "common/cvar.h"
#include "common/zone.h"
#include "system/system.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "netchan_application_tx_hook_test:%d: %s\n", \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

enum {
    TEST_MAX_COPIES = 8,
    TEST_MAX_APPLICATION = 512,
    TEST_QPORT = 0x5a,
};

typedef struct test_send_capture_s {
    bool outcomes[TEST_MAX_COPIES];
    unsigned outcome_count;
    unsigned calls;
    size_t packet_bytes[TEST_MAX_COPIES];
    byte packets[TEST_MAX_COPIES][MAX_PACKETLEN];
} test_send_capture_t;

typedef enum test_prepare_mode_e {
    TEST_PREPARE_BYPASS,
    TEST_PREPARE_VALID,
    TEST_PREPARE_INVALID_ABI,
    TEST_PREPARE_INVALID_RESERVED,
    TEST_PREPARE_INVALID_OVERSIZE,
    TEST_PREPARE_UNKNOWN,
} test_prepare_mode_t;

typedef struct test_hook_s {
    test_prepare_mode_t mode;
    byte candidate[MAX_PACKETLEN_WRITABLE];
    uint32_t candidate_bytes;
    uint64_t token;
    netchan_t *clear_registration_on_prepare;
    bool clear_registration_result;

    unsigned prepare_calls;
    netchan_app_tx_prepare_info_v1_t prepare_info;
    byte legacy[MAX_PACKETLEN_WRITABLE];
    size_t legacy_bytes;
    bool candidate_was_zeroed;

    unsigned completion_calls;
    netchan_app_tx_completion_info_v1_t completion_info;
    byte completed_application[MAX_PACKETLEN_WRITABLE];
    size_t completed_application_bytes;
} test_hook_t;

static test_send_capture_t test_send;

/* Minimal engine service definitions for linking the real chan.c. */
unsigned com_localTime = 1000;
sizebuf_t msg_read;
byte msg_read_buffer[MAX_MSGLEN];

void Com_LPrintf(print_type_t type, const char *fmt, ...)
{
    (void)type;
    (void)fmt;
}

q_noreturn void Com_Error(error_type_t code, const char *fmt, ...)
{
    (void)code;
    (void)fmt;
    abort();
}

size_t Q_vsnprintf(char *dest, size_t size, const char *fmt, va_list argptr)
{
    int result = vsnprintf(dest, size, fmt, argptr);

    return result < 0 ? size : (size_t)result;
}

char *va(const char *format, ...)
{
    static char buffer[128];
    va_list argptr;

    va_start(argptr, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    return buffer;
}

unsigned Sys_Milliseconds(void)
{
    return com_localTime;
}

cvar_t *Cvar_Get(const char *var_name, const char *value, int flags)
{
    static cvar_t value_stub;

    (void)var_name;
    (void)value;
    (void)flags;
    memset(&value_stub, 0, sizeof(value_stub));
    return &value_stub;
}

int Cvar_ClampInteger(cvar_t *var, int min_value, int max_value)
{
    if (var->integer < min_value)
        var->integer = min_value;
    if (var->integer > max_value)
        var->integer = max_value;
    return var->integer;
}

void *Z_TagMalloc(size_t size, memtag_t tag)
{
    (void)tag;
    return malloc(size);
}

void *Z_Malloc(size_t size)
{
    return malloc(size);
}

void *Z_Realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void Z_Free(void *ptr)
{
    free(ptr);
}

const char *NET_AdrToString(const netadr_t *address)
{
    (void)address;
    return "test-address";
}

bool NET_SendPacket(netsrc_t sock, const void *data, size_t len,
                    const netadr_t *to)
{
    const unsigned call = test_send.calls++;

    (void)sock;
    (void)to;
    if (call < TEST_MAX_COPIES) {
        test_send.packet_bytes[call] = len;
        if (len <= sizeof(test_send.packets[call]))
            memcpy(test_send.packets[call], data, len);
    }
    return call < test_send.outcome_count ? test_send.outcomes[call] : true;
}

void MSG_BeginReading(void)
{
}

int MSG_ReadByte(void)
{
    return -1;
}

int MSG_ReadShort(void)
{
    return -1;
}

int MSG_ReadWord(void)
{
    return -1;
}

int MSG_ReadLong(void)
{
    return -1;
}

static void reset_send(const bool *outcomes, unsigned count)
{
    memset(&test_send, 0, sizeof(test_send));
    if (outcomes && count) {
        memcpy(test_send.outcomes, outcomes, count * sizeof(outcomes[0]));
        test_send.outcome_count = count;
    }
}

static void init_channel(netchan_t *chan, netchan_type_t type,
                         netsrc_t sock, int qport, unsigned maxpacketlen)
{
    netadr_t address;

    memset(chan, 0, sizeof(*chan));
    memset(&address, 0, sizeof(address));
    Netchan_Setup(chan, sock, type, &address, qport, maxpacketlen, 0);
}

static size_t application_offset(const netchan_t *chan)
{
    return 8u + (chan->sock == NS_CLIENT && chan->qport ? 1u : 0u);
}

static netchan_app_tx_prepare_result_t test_prepare(
    void *opaque,
    const netchan_app_tx_prepare_info_v1_t *info,
    const byte *legacy_application,
    byte *candidate_application,
    netchan_app_tx_prepare_output_v1_t *output)
{
    test_hook_t *hook = opaque;

    hook->prepare_calls++;
    hook->prepare_info = *info;
    hook->legacy_bytes = info->legacy_application_bytes;
    if (hook->legacy_bytes)
        memcpy(hook->legacy, legacy_application, hook->legacy_bytes);

    hook->candidate_was_zeroed = true;
    for (uint32_t i = 0; i < info->max_application_bytes; i++) {
        if (candidate_application[i] != 0) {
            hook->candidate_was_zeroed = false;
            break;
        }
    }

    output->token = hook->token;
    if (hook->candidate_bytes)
        memcpy(candidate_application, hook->candidate,
               hook->candidate_bytes <= info->max_application_bytes
                   ? hook->candidate_bytes
                   : info->max_application_bytes);
    output->application_bytes = hook->candidate_bytes;

    if (hook->clear_registration_on_prepare) {
        hook->clear_registration_result = Netchan_SetApplicationTxHook(
            hook->clear_registration_on_prepare, NULL, NULL, NULL);
    }

    switch (hook->mode) {
    case TEST_PREPARE_BYPASS:
        output->reserved0 = UINT32_C(0xfeedface);
        return NETCHAN_APP_TX_PREPARE_BYPASS;
    case TEST_PREPARE_VALID:
        return NETCHAN_APP_TX_PREPARE_PREPARED;
    case TEST_PREPARE_INVALID_ABI:
        output->abi_version = NETCHAN_APP_TX_HOOK_ABI_V1 + 1;
        return NETCHAN_APP_TX_PREPARE_PREPARED;
    case TEST_PREPARE_INVALID_RESERVED:
        output->reserved0 = 1;
        return NETCHAN_APP_TX_PREPARE_PREPARED;
    case TEST_PREPARE_INVALID_OVERSIZE:
        output->application_bytes = info->max_application_bytes + 1;
        return NETCHAN_APP_TX_PREPARE_PREPARED;
    case TEST_PREPARE_UNKNOWN:
        return (netchan_app_tx_prepare_result_t)99;
    }

    return (netchan_app_tx_prepare_result_t)99;
}

static void test_completion(
    void *opaque,
    const netchan_app_tx_completion_info_v1_t *info,
    const byte *application)
{
    test_hook_t *hook = opaque;

    hook->completion_calls++;
    hook->completion_info = *info;
    hook->completed_application_bytes = info->application_bytes;
    if (info->application_bytes)
        memcpy(hook->completed_application, application,
               info->application_bytes);
}

static int test_default_and_bypass_are_byte_identical(void)
{
    static const byte unreliable[] = { 0x10, 0x20, 0x30, 0x40 };
    static const byte reliable[] = { 0xa1, 0xa2, 0xa3 };
    static const byte combined[] = {
        0xa1, 0xa2, 0xa3, 0x10, 0x20, 0x30, 0x40
    };
    static const bool accepted[] = { true, true };
    netchan_t chan;
    test_hook_t hook;
    byte baseline[MAX_PACKETLEN];
    size_t baseline_bytes;
    size_t offset;

    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, TEST_QPORT,
                 TEST_MAX_APPLICATION);
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, sizeof(unreliable), unreliable, 1) == 13);
    CHECK(test_send.calls == 1);
    offset = application_offset(&chan);
    CHECK(offset == 9 && test_send.packet_bytes[0] == offset + sizeof(unreliable));
    CHECK(test_send.packets[0][8] == TEST_QPORT);
    CHECK(memcmp(test_send.packets[0] + offset, unreliable,
                 sizeof(unreliable)) == 0);
    Netchan_Close(&chan);

    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, TEST_QPORT,
                 TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, reliable, sizeof(reliable));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, sizeof(unreliable), unreliable, 1) ==
          (int)(9 + sizeof(combined)));
    baseline_bytes = test_send.packet_bytes[0];
    memcpy(baseline, test_send.packets[0], baseline_bytes);
    Netchan_Close(&chan);

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_BYPASS;
    hook.candidate_bytes = 5;
    memset(hook.candidate, 0xee, hook.candidate_bytes);
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, TEST_QPORT,
                 TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, reliable, sizeof(reliable));
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 2);
    CHECK(Netchan_Transmit(&chan, sizeof(unreliable), unreliable, 2) ==
          (int)((9 + sizeof(combined)) * 2));
    CHECK(hook.prepare_calls == 1 && hook.completion_calls == 0);
    CHECK(hook.candidate_was_zeroed);
    CHECK(hook.prepare_info.abi_version == NETCHAN_APP_TX_HOOK_ABI_V1 &&
          hook.prepare_info.struct_size == sizeof(hook.prepare_info));
    CHECK(hook.prepare_info.outgoing_sequence == 1 &&
          hook.prepare_info.max_application_bytes == TEST_MAX_APPLICATION &&
          hook.prepare_info.reliable_bytes == sizeof(reliable) &&
          hook.prepare_info.unreliable_bytes == sizeof(unreliable) &&
          hook.prepare_info.legacy_application_bytes == sizeof(combined) &&
          hook.prepare_info.packet_copies == 2);
    CHECK(hook.legacy_bytes == sizeof(combined) &&
          memcmp(hook.legacy, combined, sizeof(combined)) == 0);
    CHECK(test_send.calls == 2 &&
          test_send.packet_bytes[0] == 9 + sizeof(combined) &&
          test_send.packet_bytes[1] == test_send.packet_bytes[0]);
    CHECK(memcmp(test_send.packets[0] + 9, combined, sizeof(combined)) == 0 &&
          memcmp(test_send.packets[1], test_send.packets[0],
                 test_send.packet_bytes[0]) == 0 &&
          test_send.packet_bytes[0] == baseline_bytes &&
          memcmp(test_send.packets[0], baseline, baseline_bytes) == 0);
    Netchan_Close(&chan);
    return 0;
}

static int run_acceptance_case(const bool *outcomes, unsigned accepted_count,
                               uint32_t expected_result)
{
    static const byte legacy[] = { 1, 2, 3 };
    static const byte candidate[] = { 9, 8, 7, 6, 5 };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = UINT64_C(0x1122334455667788);
    hook.candidate_bytes = sizeof(candidate);
    memcpy(hook.candidate, candidate, sizeof(candidate));
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(outcomes, 3);
    CHECK(Netchan_Transmit(&chan, sizeof(legacy), legacy, 3) ==
          (int)((8 + sizeof(candidate)) * 3));
    CHECK(test_send.calls == 3 && hook.prepare_calls == 1 &&
          hook.completion_calls == 1);
    CHECK(hook.completion_info.result == expected_result &&
          hook.completion_info.packet_copies == 3 &&
          hook.completion_info.accepted_copies == accepted_count &&
          hook.completion_info.application_bytes == sizeof(candidate) &&
          hook.completion_info.token == hook.token);
    CHECK(hook.completed_application_bytes == sizeof(candidate) &&
          memcmp(hook.completed_application, candidate, sizeof(candidate)) == 0);
    for (unsigned i = 0; i < 3; i++) {
        CHECK(test_send.packet_bytes[i] == 8 + sizeof(candidate));
        CHECK(memcmp(test_send.packets[i] + 8, candidate,
                     sizeof(candidate)) == 0);
    }
    Netchan_Close(&chan);
    return 0;
}

static int test_prepared_packetdup_completion(void)
{
    static const bool all[] = { true, true, true };
    static const bool mixed[] = { false, true, false };
    static const bool none[] = { false, false, false };

    CHECK(run_acceptance_case(all, 3,
                              NETCHAN_APP_TX_COMPLETION_ACCEPTED) == 0);
    CHECK(run_acceptance_case(mixed, 1,
                              NETCHAN_APP_TX_COMPLETION_ACCEPTED) == 0);
    CHECK(run_acceptance_case(none, 0,
                              NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED) == 0);
    return 0;
}

static int run_invalid_case(test_prepare_mode_t mode)
{
    static const byte legacy[] = { 0x41, 0x42, 0x43, 0x44 };
    static const bool outcomes[] = { false, true };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = mode;
    hook.token = UINT64_C(0xfedcba9876543210);
    hook.candidate_bytes = 6;
    memset(hook.candidate, 0xdd, hook.candidate_bytes);
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(outcomes, 2);
    CHECK(Netchan_Transmit(&chan, sizeof(legacy), legacy, 2) ==
          (int)((8 + sizeof(legacy)) * 2));
    CHECK(test_send.calls == 2 && hook.prepare_calls == 1 &&
          hook.completion_calls == 1);
    CHECK(hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID &&
          hook.completion_info.packet_copies == 2 &&
          hook.completion_info.accepted_copies == 1 &&
          hook.completion_info.application_bytes == sizeof(legacy) &&
          hook.completion_info.token == hook.token);
    CHECK(hook.completed_application_bytes == sizeof(legacy) &&
          memcmp(hook.completed_application, legacy, sizeof(legacy)) == 0);
    CHECK(memcmp(test_send.packets[0] + 8, legacy, sizeof(legacy)) == 0 &&
          memcmp(test_send.packets[1], test_send.packets[0],
                 test_send.packet_bytes[0]) == 0);
    Netchan_Close(&chan);
    return 0;
}

static int test_invalid_prepare_is_transactional(void)
{
    CHECK(run_invalid_case(TEST_PREPARE_INVALID_ABI) == 0);
    CHECK(run_invalid_case(TEST_PREPARE_INVALID_RESERVED) == 0);
    CHECK(run_invalid_case(TEST_PREPARE_INVALID_OVERSIZE) == 0);
    CHECK(run_invalid_case(TEST_PREPARE_UNKNOWN) == 0);
    return 0;
}

static int test_zero_packet_copies_still_completes(void)
{
    static const byte legacy[] = { 0x21, 0x22 };
    static const byte candidate[] = { 0x31, 0x32, 0x33 };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = UINT64_C(0x0102030405060708);
    hook.candidate_bytes = sizeof(candidate);
    memcpy(hook.candidate, candidate, sizeof(candidate));
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(NULL, 0);
    CHECK(Netchan_Transmit(&chan, sizeof(legacy), legacy, 0) == 0);
    CHECK(test_send.calls == 0 && hook.prepare_calls == 1 &&
          hook.completion_calls == 1);
    CHECK(hook.prepare_info.packet_copies == 0 &&
          hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED &&
          hook.completion_info.packet_copies == 0 &&
          hook.completion_info.accepted_copies == 0 &&
          hook.completion_info.application_bytes == sizeof(candidate) &&
          hook.completion_info.token == hook.token);
    CHECK(hook.completed_application_bytes == sizeof(candidate) &&
          memcmp(hook.completed_application, candidate, sizeof(candidate)) == 0);
    Netchan_Close(&chan);
    return 0;
}

static int test_callbacks_are_frozen_for_one_transmit(void)
{
    static const byte legacy[] = { 0x51 };
    static const byte candidate[] = { 0x61, 0x62 };
    static const bool accepted[] = { true };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = UINT64_C(0x8899aabbccddeeff);
    hook.candidate_bytes = sizeof(candidate);
    memcpy(hook.candidate, candidate, sizeof(candidate));
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0, TEST_MAX_APPLICATION);
    hook.clear_registration_on_prepare = &chan;
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, sizeof(legacy), legacy, 1) == 10);
    CHECK(hook.clear_registration_result && hook.prepare_calls == 1 &&
          hook.completion_calls == 1);
    CHECK(hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_ACCEPTED &&
          hook.completion_info.token == hook.token &&
          hook.completed_application_bytes == sizeof(candidate) &&
          memcmp(hook.completed_application, candidate, sizeof(candidate)) == 0);
    CHECK(chan.app_tx_prepare == NULL && chan.app_tx_completion == NULL &&
          chan.app_tx_opaque == NULL);
    Netchan_Close(&chan);
    return 0;
}

static int test_exact_capacity_and_zero_application(void)
{
    static const byte legacy[] = { 0x75 };
    static const bool accepted[] = { true };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = 7;
    hook.candidate_bytes = TEST_MAX_APPLICATION;
    for (size_t i = 0; i < hook.candidate_bytes; i++)
        hook.candidate[i] = (byte)i;
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, sizeof(legacy), legacy, 1) ==
          8 + TEST_MAX_APPLICATION);
    CHECK(test_send.packet_bytes[0] == 8 + TEST_MAX_APPLICATION &&
          memcmp(test_send.packets[0] + 8, hook.candidate,
                 TEST_MAX_APPLICATION) == 0);
    CHECK(hook.completion_info.result ==
              NETCHAN_APP_TX_COMPLETION_ACCEPTED &&
          hook.completion_info.application_bytes == TEST_MAX_APPLICATION);
    Netchan_Close(&chan);

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.token = 8;
    hook.candidate_bytes = 2;
    hook.candidate[0] = 0x91;
    hook.candidate[1] = 0x92;
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, 0, NULL, 1) == 10);
    CHECK(hook.prepare_calls == 1 &&
          hook.prepare_info.reliable_bytes == 0 &&
          hook.prepare_info.unreliable_bytes == 0 &&
          hook.prepare_info.legacy_application_bytes == 0);
    CHECK(hook.legacy_bytes == 0 && hook.completion_calls == 1 &&
          hook.completion_info.application_bytes == 2 &&
          test_send.packet_bytes[0] == 10 &&
          memcmp(test_send.packets[0] + 8, hook.candidate, 2) == 0);
    Netchan_Close(&chan);
    return 0;
}

static int test_reliable_only_and_fragment_bypass(void)
{
    byte oversized[TEST_MAX_APPLICATION + 1];
    static const byte reliable[] = { 0xb1, 0xb2, 0xb3, 0xb4 };
    static const bool accepted[] = { true };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.candidate_bytes = 1;
    hook.candidate[0] = 0xc7;
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0, TEST_MAX_APPLICATION);
    SZ_Write(&chan.message, reliable, sizeof(reliable));
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, 0, NULL, 1) == 9);
    CHECK(hook.prepare_info.reliable_bytes == sizeof(reliable) &&
          hook.prepare_info.unreliable_bytes == 0 &&
          hook.legacy_bytes == sizeof(reliable) &&
          memcmp(hook.legacy, reliable, sizeof(reliable)) == 0);
    CHECK(test_send.packet_bytes[0] == 9 &&
          test_send.packets[0][8] == hook.candidate[0]);
    Netchan_Close(&chan);

    memset(oversized, 0x6d, sizeof(oversized));
    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_PREPARE_VALID;
    hook.candidate_bytes = 1;
    hook.candidate[0] = 0xff;
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, 0, TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &chan, test_prepare, test_completion, &hook));
    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, sizeof(oversized), oversized, 3) ==
          10 + TEST_MAX_APPLICATION);
    CHECK(hook.prepare_calls == 0 && hook.completion_calls == 0);
    CHECK(test_send.calls == 1 && chan.fragment_pending &&
          chan.fragment_out.readcount == TEST_MAX_APPLICATION);

    reset_send(accepted, 1);
    CHECK(Netchan_Transmit(&chan, 0, NULL, 3) == 11);
    CHECK(hook.prepare_calls == 0 && hook.completion_calls == 0 &&
          test_send.calls == 1 && !chan.fragment_pending);
    Netchan_Close(&chan);
    return 0;
}

static int test_registration_and_teardown(void)
{
    netchan_t old_chan;
    netchan_t new_chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    init_channel(&old_chan, NETCHAN_OLD, NS_SERVER, 0,
                 TEST_MAX_APPLICATION);
    CHECK(!Netchan_SetApplicationTxHook(
              &old_chan, test_prepare, test_completion, &hook));
    CHECK(old_chan.app_tx_prepare == NULL &&
          old_chan.app_tx_completion == NULL &&
          old_chan.app_tx_opaque == NULL);
    Netchan_Close(&old_chan);

    init_channel(&new_chan, NETCHAN_NEW, NS_SERVER, 0,
                 TEST_MAX_APPLICATION);
    CHECK(Netchan_SetApplicationTxHook(
              &new_chan, test_prepare, test_completion, &hook));
    CHECK(new_chan.app_tx_prepare == test_prepare &&
          new_chan.app_tx_completion == test_completion &&
          new_chan.app_tx_opaque == &hook);
    CHECK(!Netchan_SetApplicationTxHook(
              &new_chan, test_prepare, NULL, NULL));
    CHECK(new_chan.app_tx_prepare == test_prepare &&
          new_chan.app_tx_completion == test_completion &&
          new_chan.app_tx_opaque == &hook);
    CHECK(Netchan_SetApplicationTxHook(&new_chan, NULL, NULL, &hook));
    CHECK(new_chan.app_tx_prepare == NULL &&
          new_chan.app_tx_completion == NULL &&
          new_chan.app_tx_opaque == NULL);
    CHECK(Netchan_SetApplicationTxHook(
              &new_chan, test_prepare, test_completion, &hook));
    Netchan_Close(&new_chan);
    for (size_t i = 0; i < sizeof(new_chan); i++)
        CHECK(((const byte *)&new_chan)[i] == 0);
    CHECK(!Netchan_SetApplicationTxHook(
              NULL, test_prepare, test_completion, &hook));
    return 0;
}

int main(void)
{
    Netchan_Init();
    if (test_default_and_bypass_are_byte_identical() != 0)
        return 1;
    if (test_prepared_packetdup_completion() != 0)
        return 1;
    if (test_invalid_prepare_is_transactional() != 0)
        return 1;
    if (test_zero_packet_copies_still_completes() != 0)
        return 1;
    if (test_callbacks_are_frozen_for_one_transmit() != 0)
        return 1;
    if (test_exact_capacity_and_zero_application() != 0)
        return 1;
    if (test_reliable_only_and_fragment_bypass() != 0)
        return 1;
    if (test_registration_and_teardown() != 0)
        return 1;

    puts("netchan application TX hook tests passed");
    return 0;
}
