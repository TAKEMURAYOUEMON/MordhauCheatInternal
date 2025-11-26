#define _CRT_SECURE_NO_WARNINGS

// Disable SDK warnings
#pragma warning(disable: 4369)
#pragma warning(disable: 4309)
#pragma warning(disable: 4244)
#pragma warning(disable: 4616)
#pragma warning(disable: 2039) 

#include <Windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "MinHook.h"

// SDK Includes
#include "SDK/Engine_classes.hpp"
#include "SDK/Engine_parameters.hpp"
#include "SDK/Engine_structs.hpp"
#include "SDK/Mordhau_classes.hpp"
#include "SDK/Mordhau_parameters.hpp"

// Config
namespace Config {
    bool bEnabled = true;
    bool bDebug = true;
}

// Global Local Player Cache
SDK::AMordhauCharacter* g_LocalChar = nullptr;

// Utilities
void PressParry() {
    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
}

void LOG_INFO(const char* fmt, ...) {
    if (!Config::bDebug) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

// Hook Definitions
typedef void(__fastcall* ProcessEvent_fn)(SDK::UObject*, SDK::UFunction*, void*);
ProcessEvent_fn OriginalProcessEvent = nullptr;

// ProcessEvent Hook to intercept OnHit
void __fastcall ProcessEventHook(SDK::UObject* Caller, SDK::UFunction* Function, void* Parms) {
    // We only care if we are in game and have a local character
    if (!g_LocalChar) {
        return OriginalProcessEvent(Caller, Function, Parms);
    }

    // Identify Function: Mordhau.AdvancedCharacter.OnHit
    static SDK::UFunction* OnHitFunc = nullptr;
    if (!OnHitFunc) {
        OnHitFunc = SDK::AAdvancedCharacter::StaticClass()->GetFunction("AdvancedCharacter", "OnHit");
    }

    // Check if the called function is OnHit
    if (Function == OnHitFunc) {
        // Gate 1: Only if auto-parry enabled
        if (Config::bEnabled) {
            
            // Gate 2: Only if victim is local character
            // 'Caller' in OnHit is the character being hit
            if (Caller == g_LocalChar) {
                
                auto Args = (SDK::Params::AdvancedCharacter_OnHit*)Parms;
                
                // Gate 3: Only if attacker is an AAdvancedCharacter (enemy player)
                if (Args->Actor && Args->Actor->IsA(SDK::AAdvancedCharacter::StaticClass())) {
                    SDK::AAdvancedCharacter* Attacker = (SDK::AAdvancedCharacter*)Args->Actor;

                    // RAGE PARRY - no other checks, just parry
                    PressParry();

                    // Log if debug enabled
                    if (Config::bDebug) {
                        LOG_INFO("[RAGE_PARRY] Auto-parried attack from %s", Attacker->GetName().c_str());
                    }
                }
            }
        }
    }

    // Always call original
    return OriginalProcessEvent(Caller, Function, Parms);
}

// Main Thread
DWORD WINAPI MainThread(LPVOID lpParam) {
    HMODULE hModule = static_cast<HMODULE>(lpParam);
    AllocConsole();
    (void)freopen("CONOUT$", "w", stdout);

    std::cout << "[INFO] Waiting for Game Engine...\n";

    SDK::UWorld* World = nullptr;
    while (!World) {
        World = SDK::UWorld::GetWorld();
        Sleep(100);
    }

    std::cout << "[SUCCESS] Ready! F3(Parry Toggle), F4(Debug Toggle), END(Unload).\n";

    // Initialize MinHook
    if (MH_Initialize() != MH_OK) {
        std::cout << "[ERROR] MH_Initialize failed!\n";
        return 1;
    }

    // Calculate ProcessEvent Address
    // Address = Base + Offset
    uintptr_t BaseAddr = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t ProcessEventAddr = BaseAddr + SDK::Offsets::ProcessEvent;

    // Create Hook
    if (MH_CreateHook((void*)ProcessEventAddr, (LPVOID)ProcessEventHook, (LPVOID*)&OriginalProcessEvent) != MH_OK) {
        std::cout << "[ERROR] MH_CreateHook failed!\n";
        return 1;
    }

    // Enable Hook
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        std::cout << "[ERROR] MH_EnableHook failed!\n";
        return 1;
    }

    std::cout << "[INFO] Hook installed at 0x" << std::hex << ProcessEventAddr << std::dec << "\n";
    std::cout << "[MODE] RAGE AUTOPARRY ACTIVE\n";

    while (true) {
        if (GetAsyncKeyState(VK_END) & 1) break;

        if (GetAsyncKeyState(VK_F3) & 1) {
            Config::bEnabled = !Config::bEnabled;
            std::cout << "[TOGGLE] Auto-Parry: " << (Config::bEnabled ? "ON" : "OFF") << "\n";
        }

        if (GetAsyncKeyState(VK_F4) & 1) {
            Config::bDebug = !Config::bDebug;
            std::cout << "[TOGGLE] Debug Log: " << (Config::bDebug ? "ON" : "OFF") << "\n";
        }

        // Update Local Character Cache safely
        if (World && World->OwningGameInstance && World->OwningGameInstance->LocalPlayers.IsValidIndex(0)) {
            auto LP = World->OwningGameInstance->LocalPlayers[0];
            if (LP && LP->PlayerController && LP->PlayerController->Pawn) {
                // Ensure it is a MordhauCharacter
                if (LP->PlayerController->Pawn->IsA(SDK::AMordhauCharacter::StaticClass())) {
                    g_LocalChar = (SDK::AMordhauCharacter*)LP->PlayerController->Pawn;
                } else {
                    g_LocalChar = nullptr;
                }
            } else {
                g_LocalChar = nullptr;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    FreeConsole();
    FreeLibraryAndExitThread(hModule, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(0, 0, MainThread, hModule, 0, 0);
    }
    return TRUE;
}
