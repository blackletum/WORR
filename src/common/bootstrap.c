#include "common/bootstrap.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shared/platform.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static bool bootstrap_ready_signalled = false;
static com_bootstrap_ready_callback_t bootstrap_ready_callback = NULL;
static void *bootstrap_ready_userdata = NULL;

q_exported void Com_SetBootstrapReadyCallback(com_bootstrap_ready_callback_t callback, void *userdata)
{
    bootstrap_ready_callback = callback;
    bootstrap_ready_userdata = userdata;
    if (callback) {
        bootstrap_ready_signalled = false;
    }
}

void Com_BootstrapSignalReady(void)
{
    if (bootstrap_ready_signalled) {
        return;
    }
    bootstrap_ready_signalled = true;

    if (bootstrap_ready_callback) {
        bootstrap_ready_callback(bootstrap_ready_userdata);
    }

#if defined(_WIN32)
    WCHAR path[MAX_PATH];
    WCHAR token[256];
    DWORD path_len = GetEnvironmentVariableW(L"WORR_BOOTSTRAP_READY_FILE", path, MAX_PATH);
    DWORD token_len = GetEnvironmentVariableW(L"WORR_BOOTSTRAP_READY_TOKEN", token, 256);
    if (!path_len || !token_len || path_len >= MAX_PATH || token_len >= 256) {
        return;
    }

    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    char token_utf8[512];
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, token, -1, token_utf8, sizeof(token_utf8), NULL, NULL);
    if (utf8_len <= 1 || utf8_len >= (int)sizeof(token_utf8)) {
        bool close_ok = CloseHandle(file);
        (void)close_ok;
        DeleteFileW(path);
        return;
    }

    DWORD bytes_to_write = (DWORD)(utf8_len - 1);
    DWORD written = 0;
    bool write_ok = WriteFile(file, token_utf8, bytes_to_write, &written, NULL) && written == bytes_to_write;
    bool close_ok = CloseHandle(file);
    if (!write_ok || !close_ok) {
        DeleteFileW(path);
    }
#else
    const char *path = getenv("WORR_BOOTSTRAP_READY_FILE");
    const char *token = getenv("WORR_BOOTSTRAP_READY_TOKEN");
    if (!path || !*path || !token || !*token) {
        return;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        return;
    }

    size_t token_len = strlen(token);
    bool write_ok = fwrite(token, 1, token_len, file) == token_len;
    bool close_ok = fclose(file) == 0;
    if (!write_ok || !close_ok) {
        remove(path);
    }
#endif

}
