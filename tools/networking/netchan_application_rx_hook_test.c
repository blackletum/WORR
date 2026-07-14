/* Deterministic integration tests for the dormant post-admission RX hook. */

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
            fprintf(stderr, "netchan_application_rx_hook_test:%d: %s\n", \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

#define TEST_REL_BIT UINT32_C(0x80000000)
#define TEST_FRG_BIT UINT32_C(0x40000000)

enum {
    TEST_MAX_APPLICATION = 512,
    TEST_QPORT = 0x5a,
};

typedef enum test_receive_mode_e {
    TEST_RX_BYPASS,
    TEST_RX_EXPOSE,
    TEST_RX_REJECT,
    TEST_RX_INVALID_ABI,
    TEST_RX_INVALID_SIZE,
    TEST_RX_INVALID_RESERVED,
    TEST_RX_INVALID_OVERSIZE,
    TEST_RX_UNKNOWN,
    TEST_RX_REJECT_REASSEMBLED,
} test_receive_mode_t;

typedef struct test_hook_s {
    test_receive_mode_t mode;
    uint32_t legacy_bytes;
    bool scribble_descriptor;
    netchan_t *clear_registration_on_receive;
    bool clear_registration_result;

    unsigned calls;
    netchan_app_rx_info_v1_t info;
    netchan_app_rx_output_v1_t initial_output;
    sizebuf_t descriptor_at_entry;
    byte application[MAX_MSGLEN];
    uint32_t copied_application_bytes;
} test_hook_t;

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
    (void)sock;
    (void)data;
    (void)len;
    (void)to;
    return true;
}

void MSG_BeginReading(void)
{
    msg_read.readcount = 0;
    msg_read.bits_buf = 0;
    msg_read.bits_left = 0;
}

static bool msg_can_read(uint32_t bytes)
{
    if (msg_read.readcount > msg_read.cursize ||
        bytes > msg_read.cursize - msg_read.readcount) {
        msg_read.readcount = msg_read.cursize + 1;
        return false;
    }
    return true;
}

int MSG_ReadByte(void)
{
    if (!msg_can_read(1))
        return -1;
    return msg_read.data[msg_read.readcount++];
}

int MSG_ReadShort(void)
{
    uint16_t value;

    if (!msg_can_read(2))
        return -1;
    value = (uint16_t)msg_read.data[msg_read.readcount] |
            (uint16_t)((uint16_t)msg_read.data[msg_read.readcount + 1] << 8);
    msg_read.readcount += 2;
    return (int)(int16_t)value;
}

int MSG_ReadWord(void)
{
    uint16_t value;

    if (!msg_can_read(2))
        return -1;
    value = (uint16_t)msg_read.data[msg_read.readcount] |
            (uint16_t)((uint16_t)msg_read.data[msg_read.readcount + 1] << 8);
    msg_read.readcount += 2;
    return value;
}

int MSG_ReadLong(void)
{
    uint32_t value;

    if (!msg_can_read(4))
        return -1;
    value = (uint32_t)msg_read.data[msg_read.readcount] |
            ((uint32_t)msg_read.data[msg_read.readcount + 1] << 8) |
            ((uint32_t)msg_read.data[msg_read.readcount + 2] << 16) |
            ((uint32_t)msg_read.data[msg_read.readcount + 3] << 24);
    msg_read.readcount += 4;
    return (int)value;
}

static void write_u16(byte *out, uint16_t value)
{
    out[0] = (byte)value;
    out[1] = (byte)(value >> 8);
}

static void write_u32(byte *out, uint32_t value)
{
    out[0] = (byte)value;
    out[1] = (byte)(value >> 8);
    out[2] = (byte)(value >> 16);
    out[3] = (byte)(value >> 24);
}

static void load_packet(uint32_t sequence, uint32_t flags, bool include_qport,
                        uint8_t qport, bool fragment, uint16_t fragment_offset,
                        bool more_fragments, const void *application,
                        size_t application_bytes)
{
    uint32_t cursor = 0;
    uint16_t wire_offset = fragment_offset;

    memset(msg_read_buffer, 0, sizeof(msg_read_buffer));
    write_u32(msg_read_buffer + cursor, sequence | flags);
    cursor += 4;
    write_u32(msg_read_buffer + cursor, 0);
    cursor += 4;
    if (include_qport)
        msg_read_buffer[cursor++] = qport;
    if (fragment) {
        if (more_fragments)
            wire_offset |= UINT16_C(0x8000);
        write_u16(msg_read_buffer + cursor, wire_offset);
        cursor += 2;
    }
    if (application_bytes) {
        memcpy(msg_read_buffer + cursor, application, application_bytes);
        cursor += (uint32_t)application_bytes;
    }
    SZ_InitRead(&msg_read, msg_read_buffer, cursor);
    msg_read.allowunderflow = true;
}

static void init_channel(netchan_t *chan, netchan_type_t type,
                         netsrc_t sock, int qport)
{
    netadr_t address;

    memset(chan, 0, sizeof(*chan));
    memset(&address, 0, sizeof(address));
    Netchan_Setup(chan, sock, type, &address, qport,
                  TEST_MAX_APPLICATION, 0);
}

static netchan_app_rx_result_t test_receive(
    void *opaque,
    const netchan_app_rx_info_v1_t *info,
    const byte *application,
    netchan_app_rx_output_v1_t *output)
{
    static byte scratch[32];
    test_hook_t *hook = opaque;

    hook->calls++;
    hook->info = *info;
    hook->initial_output = *output;
    hook->descriptor_at_entry = msg_read;
    hook->copied_application_bytes = info->application_bytes;
    if (info->application_bytes)
        memcpy(hook->application, application, info->application_bytes);

    if (hook->scribble_descriptor) {
        msg_read.allowoverflow = !msg_read.allowoverflow;
        msg_read.allowunderflow = !msg_read.allowunderflow;
        msg_read.overflowed = !msg_read.overflowed;
        msg_read.growable = !msg_read.growable;
        msg_read.maxsize = 17;
        msg_read.cursize = 11;
        msg_read.readcount = 7;
        msg_read.bits_buf = UINT32_C(0xa5a5a5a5);
        msg_read.bits_left = 13;
        msg_read.data = scratch;
        msg_read.tag = "scribbled";
    }

    output->legacy_bytes = hook->legacy_bytes;
    if (hook->clear_registration_on_receive) {
        hook->clear_registration_result = Netchan_SetApplicationRxHook(
            hook->clear_registration_on_receive, NULL, NULL);
    }
    switch (hook->mode) {
    case TEST_RX_BYPASS:
        output->abi_version++;
        output->reserved0 = 1;
        return NETCHAN_APP_RX_BYPASS;
    case TEST_RX_EXPOSE:
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    case TEST_RX_REJECT:
        return NETCHAN_APP_RX_REJECT;
    case TEST_RX_INVALID_ABI:
        output->abi_version++;
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    case TEST_RX_INVALID_SIZE:
        output->struct_size++;
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    case TEST_RX_INVALID_RESERVED:
        output->reserved0 = 1;
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    case TEST_RX_INVALID_OVERSIZE:
        output->legacy_bytes = info->application_bytes + 1;
        return NETCHAN_APP_RX_EXPOSE_LEGACY;
    case TEST_RX_UNKNOWN:
        return (netchan_app_rx_result_t)99;
    case TEST_RX_REJECT_REASSEMBLED:
        return (info->flags & NETCHAN_APP_RX_FLAG_REASSEMBLED)
                   ? NETCHAN_APP_RX_REJECT
                   : NETCHAN_APP_RX_BYPASS;
    }
    return (netchan_app_rx_result_t)99;
}

static int test_registration_default_and_teardown(void)
{
    static const byte application[] = { 1, 2, 3, 4 };
    netchan_t old_chan;
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    init_channel(&old_chan, NETCHAN_OLD, NS_SERVER, 0);
    CHECK(!Netchan_SetApplicationRxHook(&old_chan, test_receive, &hook));
    CHECK(old_chan.app_rx == NULL && old_chan.app_rx_opaque == NULL);
    Netchan_Close(&old_chan);

    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    load_packet(1, 0, false, 0, false, 0, false,
                application, sizeof(application));
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_APPLICATION_READY);
    CHECK(msg_read.readcount == 8 && msg_read.cursize == 12 &&
          memcmp(msg_read.data + msg_read.readcount, application,
                 sizeof(application)) == 0);
    CHECK(chan.incoming_sequence == 1 && chan.total_received == 1);

    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    CHECK(chan.app_rx == test_receive && chan.app_rx_opaque == &hook);
    CHECK(Netchan_SetApplicationRxHook(&chan, NULL, &hook));
    CHECK(chan.app_rx == NULL && chan.app_rx_opaque == NULL);
    CHECK(!Netchan_SetApplicationRxHook(NULL, test_receive, &hook));
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    Netchan_Close(&chan);
    for (size_t i = 0; i < sizeof(chan); i++)
        CHECK(((const byte *)&chan)[i] == 0);
    return 0;
}

static int run_bypass_offset_case(netsrc_t sock, int qport,
                                  bool include_qport, uint32_t flags,
                                  uint32_t expected_offset,
                                  uint32_t expected_hook_flags)
{
    static const byte application[] = { 0x10, 0x20, 0x30, 0x40 };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_RX_BYPASS;
    init_channel(&chan, NETCHAN_NEW, sock, qport);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    load_packet(1, flags, include_qport, (uint8_t)qport, false, 0, false,
                application, sizeof(application));
    com_localTime++;
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_APPLICATION_READY);
    CHECK(hook.calls == 1);
    CHECK(hook.info.abi_version == NETCHAN_APP_RX_HOOK_ABI_V1 &&
          hook.info.struct_size == sizeof(hook.info) &&
          hook.info.incoming_sequence == 1 &&
          hook.info.message_bytes == expected_offset + sizeof(application) &&
          hook.info.application_offset == expected_offset &&
          hook.info.application_bytes == sizeof(application) &&
          hook.info.flags == expected_hook_flags && hook.info.reserved0 == 0);
    CHECK(hook.initial_output.abi_version == NETCHAN_APP_RX_HOOK_ABI_V1 &&
          hook.initial_output.struct_size == sizeof(hook.initial_output) &&
          hook.initial_output.legacy_bytes == 0 &&
          hook.initial_output.reserved0 == 0);
    CHECK(hook.copied_application_bytes == sizeof(application) &&
          memcmp(hook.application, application, sizeof(application)) == 0);
    CHECK(memcmp(&msg_read, &hook.descriptor_at_entry, sizeof(msg_read)) == 0);
    CHECK(chan.last_received == com_localTime && chan.total_received == 1);
    if (flags & TEST_REL_BIT) {
        CHECK(chan.reliable_ack_pending &&
              chan.incoming_reliable_sequence == true);
    }
    Netchan_Close(&chan);
    return 0;
}

static int test_exact_offsets_flags_and_descriptor_restore(void)
{
    CHECK(run_bypass_offset_case(NS_CLIENT, 0, false, 0, 8, 0) == 0);
    CHECK(run_bypass_offset_case(NS_SERVER, 0, false, 0, 8, 0) == 0);
    CHECK(run_bypass_offset_case(
              NS_SERVER, TEST_QPORT, true, TEST_REL_BIT, 9,
              NETCHAN_APP_RX_FLAG_RELIABLE) == 0);
    return 0;
}

static int run_expose_case(uint32_t legacy_bytes)
{
    static const byte application[] = {
        0xa1, 0xa2, 0xa3, 0xb1, 0xb2, 0xb3, 0xb4
    };
    netchan_t chan;
    test_hook_t hook;
    sizebuf_t expected;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_RX_EXPOSE;
    hook.legacy_bytes = legacy_bytes;
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    load_packet(1, 0, false, 0, false, 0, false,
                application, sizeof(application));
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_APPLICATION_READY);
    expected = hook.descriptor_at_entry;
    expected.cursize = expected.readcount + legacy_bytes;
    CHECK(memcmp(&msg_read, &expected, sizeof(msg_read)) == 0);
    CHECK(msg_read.readcount == 8 && msg_read.cursize == 8 + legacy_bytes);
    CHECK(memcmp(msg_read.data + msg_read.readcount, application,
                 legacy_bytes) == 0);
    CHECK(memcmp(msg_read_buffer + 8, application, sizeof(application)) == 0);
    Netchan_Close(&chan);
    return 0;
}

static int test_prefix_trim_zero_partial_and_full(void)
{
    CHECK(run_expose_case(0) == 0);
    CHECK(run_expose_case(3) == 0);
    CHECK(run_expose_case(7) == 0);
    return 0;
}

static int test_admitted_zero_application(void)
{
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_RX_EXPOSE;
    hook.legacy_bytes = 0;
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    load_packet(1, 0, false, 0, false, 0, false, NULL, 0);
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_APPLICATION_READY);
    CHECK(hook.calls == 1 && hook.info.message_bytes == 8 &&
          hook.info.application_offset == 8 &&
          hook.info.application_bytes == 0 &&
          hook.copied_application_bytes == 0);
    CHECK(msg_read.readcount == 8 && msg_read.cursize == 8 &&
          chan.incoming_sequence == 1 && chan.total_received == 1);
    Netchan_Close(&chan);
    return 0;
}

static int test_truncated_headers_never_invoke_hook(void)
{
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_RX_BYPASS;

    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    memset(msg_read_buffer, 0, 7);
    SZ_InitRead(&msg_read, msg_read_buffer, 7);
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_NO_APPLICATION);
    CHECK(hook.calls == 0 && chan.incoming_sequence == 0 &&
          chan.total_received == 0);
    Netchan_Close(&chan);

    init_channel(&chan, NETCHAN_NEW, NS_SERVER, TEST_QPORT);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    memset(msg_read_buffer, 0, 8);
    write_u32(msg_read_buffer, 1);
    SZ_InitRead(&msg_read, msg_read_buffer, 8);
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_NO_APPLICATION);
    CHECK(hook.calls == 0 && chan.incoming_sequence == 0 &&
          chan.total_received == 0);
    Netchan_Close(&chan);

    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    memset(msg_read_buffer, 0, 8);
    write_u32(msg_read_buffer, 1 | TEST_FRG_BIT);
    SZ_InitRead(&msg_read, msg_read_buffer, 8);
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_NO_APPLICATION);
    CHECK(hook.calls == 0 && chan.incoming_sequence == 0 &&
          chan.total_received == 0);
    Netchan_Close(&chan);
    return 0;
}

static int test_old_process_ex_mapping(void)
{
    static const byte application[] = { 0x71, 0x72 };
    netchan_t chan;

    init_channel(&chan, NETCHAN_OLD, NS_CLIENT, 0);
    load_packet(1, 0, false, 0, false, 0, false,
                application, sizeof(application));
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_APPLICATION_READY);
    CHECK(msg_read.readcount == 8 && msg_read.cursize == 10 &&
          memcmp(msg_read.data + msg_read.readcount, application,
                 sizeof(application)) == 0);
    CHECK(chan.incoming_sequence == 1 && chan.total_received == 1);
    Netchan_Close(&chan);

    init_channel(&chan, NETCHAN_OLD, NS_CLIENT, 0);
    memset(msg_read_buffer, 0, 7);
    SZ_InitRead(&msg_read, msg_read_buffer, 7);
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_NO_APPLICATION);
    CHECK(chan.incoming_sequence == 0 && chan.total_received == 0);
    Netchan_Close(&chan);
    return 0;
}

static int test_self_unregistration_keeps_current_result(void)
{
    static const byte first[] = { 0x81, 0x82, 0x83, 0x84 };
    static const byte second[] = { 0x91, 0x92 };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_RX_EXPOSE;
    hook.legacy_bytes = 2;
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    hook.clear_registration_on_receive = &chan;
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    load_packet(1, 0, false, 0, false, 0, false,
                first, sizeof(first));
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_APPLICATION_READY);
    CHECK(hook.clear_registration_result && hook.calls == 1 &&
          msg_read.readcount == 8 && msg_read.cursize == 10 &&
          memcmp(msg_read.data + msg_read.readcount, first, 2) == 0);
    CHECK(chan.app_rx == NULL && chan.app_rx_opaque == NULL);

    load_packet(2, 0, false, 0, false, 0, false,
                second, sizeof(second));
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_APPLICATION_READY);
    CHECK(hook.calls == 1 && msg_read.readcount == 8 &&
          msg_read.cursize == 8 + sizeof(second) &&
          memcmp(msg_read.data + msg_read.readcount, second,
                 sizeof(second)) == 0);
    Netchan_Close(&chan);
    return 0;
}

static int run_rejected_case(test_receive_mode_t mode)
{
    static const byte application[] = { 0xc1, 0xc2, 0xc3 };
    netchan_t chan;
    test_hook_t hook;
    unsigned calls_after_first;

    memset(&hook, 0, sizeof(hook));
    hook.mode = mode;
    hook.legacy_bytes = 1;
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    load_packet(1, TEST_REL_BIT, false, 0, false, 0, false,
                application, sizeof(application));
    com_localTime++;
    CHECK(Netchan_ProcessEx(&chan) ==
          NETCHAN_PROCESS_APPLICATION_REJECTED);
    CHECK(hook.calls == 1 && msg_read.readcount == 8 &&
          msg_read.cursize == msg_read.readcount);
    CHECK(memcmp(msg_read_buffer + 8, application, sizeof(application)) == 0);
    CHECK(chan.incoming_sequence == 1 && chan.total_received == 1 &&
          chan.last_received == com_localTime &&
          chan.reliable_ack_pending && chan.incoming_reliable_sequence);

    calls_after_first = hook.calls;
    load_packet(1, TEST_REL_BIT, false, 0, false, 0, false,
                application, sizeof(application));
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_NO_APPLICATION);
    CHECK(hook.calls == calls_after_first && chan.total_received == 1);
    Netchan_Close(&chan);
    return 0;
}

static int test_rejection_and_invalid_output_are_hidden(void)
{
    CHECK(run_rejected_case(TEST_RX_REJECT) == 0);
    CHECK(run_rejected_case(TEST_RX_INVALID_ABI) == 0);
    CHECK(run_rejected_case(TEST_RX_INVALID_SIZE) == 0);
    CHECK(run_rejected_case(TEST_RX_INVALID_RESERVED) == 0);
    CHECK(run_rejected_case(TEST_RX_INVALID_OVERSIZE) == 0);
    CHECK(run_rejected_case(TEST_RX_UNKNOWN) == 0);
    return 0;
}

static int test_descriptor_mutation_is_rejected_and_restored(void)
{
    static const byte application[] = { 0xca, 0xfe, 0xba, 0xbe };
    netchan_t chan;
    test_hook_t hook;
    sizebuf_t expected;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_RX_BYPASS;
    hook.scribble_descriptor = true;
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    load_packet(1, 0, false, 0, false, 0, false,
                application, sizeof(application));
    CHECK(Netchan_ProcessEx(&chan) ==
          NETCHAN_PROCESS_APPLICATION_REJECTED);
    CHECK(hook.calls == 1);
    expected = hook.descriptor_at_entry;
    expected.cursize = expected.readcount;
    CHECK(memcmp(&msg_read, &expected, sizeof(msg_read)) == 0);
    CHECK(msg_read.data == msg_read_buffer && msg_read.readcount == 8 &&
          msg_read.cursize == 8 && msg_read.bits_buf == 0 &&
          msg_read.bits_left == 0);
    CHECK(memcmp(msg_read_buffer + 8, application, sizeof(application)) == 0);
    CHECK(chan.incoming_sequence == 1 && chan.total_received == 1);
    Netchan_Close(&chan);
    return 0;
}

static int test_bool_wrapper_maps_only_ready(void)
{
    static const byte application[] = { 0xd1 };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_RX_REJECT;
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    load_packet(1, 0, false, 0, false, 0, false,
                application, sizeof(application));
    CHECK(!Netchan_Process(&chan));
    Netchan_Close(&chan);

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_RX_BYPASS;
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    load_packet(1, 0, false, 0, false, 0, false,
                application, sizeof(application));
    CHECK(Netchan_Process(&chan));
    Netchan_Close(&chan);
    return 0;
}

static int test_final_fragment_invokes_once_and_trims(void)
{
    static const byte first[] = { 0xe1, 0xe2, 0xe3 };
    static const byte second[] = { 0xe4, 0xe5 };
    static const byte assembled[] = { 0xe1, 0xe2, 0xe3, 0xe4, 0xe5 };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_RX_EXPOSE;
    hook.legacy_bytes = 3;
    init_channel(&chan, NETCHAN_NEW, NS_SERVER, TEST_QPORT);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));

    load_packet(1, TEST_REL_BIT | TEST_FRG_BIT, true, TEST_QPORT,
                true, 0, true, first, sizeof(first));
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_NO_APPLICATION);
    CHECK(hook.calls == 0 && chan.incoming_sequence == 0 &&
          chan.fragment_in.cursize == sizeof(first));

    load_packet(1, TEST_REL_BIT | TEST_FRG_BIT, true, TEST_QPORT,
                true, sizeof(first), false, second, sizeof(second));
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_APPLICATION_READY);
    CHECK(hook.calls == 1 &&
          hook.info.message_bytes == sizeof(assembled) &&
          hook.info.application_offset == 0 &&
          hook.info.application_bytes == sizeof(assembled) &&
          hook.info.flags == (NETCHAN_APP_RX_FLAG_RELIABLE |
                              NETCHAN_APP_RX_FLAG_REASSEMBLED));
    CHECK(hook.copied_application_bytes == sizeof(assembled) &&
          memcmp(hook.application, assembled, sizeof(assembled)) == 0);
    CHECK(msg_read.data == hook.descriptor_at_entry.data &&
          msg_read.readcount == hook.descriptor_at_entry.readcount &&
          msg_read.bits_buf == hook.descriptor_at_entry.bits_buf &&
          msg_read.bits_left == hook.descriptor_at_entry.bits_left);
    CHECK(msg_read.readcount == 0 && msg_read.cursize == 3 &&
          memcmp(msg_read.data, assembled, 3) == 0 &&
          memcmp(msg_read_buffer, assembled, sizeof(assembled)) == 0 &&
          chan.fragment_in.cursize == 0 && chan.incoming_sequence == 1 &&
          chan.incoming_reliable_sequence && chan.total_received == 1);
    Netchan_Close(&chan);
    return 0;
}

static int test_reassembled_candidate_can_be_rejected(void)
{
    static const byte first[] = { 0xf1, 0xf2 };
    static const byte second[] = { 0xf3 };
    netchan_t chan;
    test_hook_t hook;

    memset(&hook, 0, sizeof(hook));
    hook.mode = TEST_RX_REJECT_REASSEMBLED;
    init_channel(&chan, NETCHAN_NEW, NS_CLIENT, 0);
    CHECK(Netchan_SetApplicationRxHook(&chan, test_receive, &hook));
    load_packet(1, TEST_FRG_BIT, false, 0, true, 0, true,
                first, sizeof(first));
    CHECK(Netchan_ProcessEx(&chan) == NETCHAN_PROCESS_NO_APPLICATION);
    CHECK(hook.calls == 0);
    load_packet(1, TEST_FRG_BIT, false, 0, true, sizeof(first), false,
                second, sizeof(second));
    CHECK(Netchan_ProcessEx(&chan) ==
          NETCHAN_PROCESS_APPLICATION_REJECTED);
    CHECK(hook.calls == 1 && msg_read.readcount == 0 &&
          msg_read.cursize == 0 && chan.incoming_sequence == 1 &&
          chan.total_received == 1);
    Netchan_Close(&chan);
    return 0;
}

int main(void)
{
    Netchan_Init();
    if (test_registration_default_and_teardown() != 0)
        return 1;
    if (test_exact_offsets_flags_and_descriptor_restore() != 0)
        return 1;
    if (test_prefix_trim_zero_partial_and_full() != 0)
        return 1;
    if (test_admitted_zero_application() != 0)
        return 1;
    if (test_truncated_headers_never_invoke_hook() != 0)
        return 1;
    if (test_old_process_ex_mapping() != 0)
        return 1;
    if (test_self_unregistration_keeps_current_result() != 0)
        return 1;
    if (test_rejection_and_invalid_output_are_hidden() != 0)
        return 1;
    if (test_descriptor_mutation_is_rejected_and_restored() != 0)
        return 1;
    if (test_bool_wrapper_maps_only_ready() != 0)
        return 1;
    if (test_final_fragment_invokes_once_and_trims() != 0)
        return 1;
    if (test_reassembled_candidate_can_be_rejected() != 0)
        return 1;

    puts("netchan application RX hook tests passed");
    return 0;
}
