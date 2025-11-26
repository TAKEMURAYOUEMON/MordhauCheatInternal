#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

#include <windows.h>

// MinHook Error Codes.
typedef enum MH_STATUS
{
    MH_UNKNOWN = -1,
    MH_OK = 0,
    MH_ERROR_ALREADY_INITIALIZED,
    MH_ERROR_NOT_INITIALIZED,
    MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,
    MH_ERROR_ENABLED,
    MH_ERROR_DISABLED,
    MH_ERROR_NOT_EXECUTABLE,
    MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,
    MH_ERROR_MEMORY_PROTECT,
    MH_ERROR_MODULE_NOT_FOUND,
    MH_ERROR_FUNCTION_NOT_FOUND
} MH_STATUS;

// MinHook API.
MH_STATUS WINAPI MH_Initialize(VOID);
MH_STATUS WINAPI MH_Uninitialize(VOID);

MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID *ppOriginal);
MH_STATUS WINAPI MH_CreateHookApi(LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour, LPVOID *ppOriginal);
MH_STATUS WINAPI MH_CreateHookApiEx(LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour, LPVOID *ppOriginal, LPVOID *ppTarget);
MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget);
MH_STATUS WINAPI MH_DisableHook(LPVOID pTarget);
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID pTarget);
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID pTarget);
MH_STATUS WINAPI MH_ApplyQueued(VOID);

const char * WINAPI MH_StatusToString(MH_STATUS status);

#if defined(__cplusplus)
}
#endif
