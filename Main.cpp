#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 4369)
#pragma warning(disable: 4309)
#pragma warning(disable: 4244)
#pragma warning(disable: 4616)
#pragma warning(disable: 2039) 

#include "include.h"
#include <atomic>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdarg>

// Define M_PI if not defined
#ifndef M_PI
#define M_PI 3.1415926535f
#endif

// Globals
namespace Globals {
    std::atomic<SDK::UWorld*> World{ nullptr };
    std::atomic<SDK::AMordhauCharacter*> LocalCharacter{ nullptr };
    std::atomic<SDK::APlayerController*> LocalController{ nullptr };
    std::atomic<bool> bAutoParryEnabled{ true };
    std::atomic<bool> bDebugMode{ true };
    
    // Cached FNames for optimization
    SDK::FName NameReceiveTick;
    SDK::FName NameTick;
    SDK::FName NameBPTick;
    SDK::FName NameUpdateAnim;
}

namespace Config {
    float SafeMargin = 80.0f;
    float SimulatedPing = 80.0f;
    float MaxAngle = 0.2f;
}

// Math & Utils
float GetDistance(SDK::FVector v1, SDK::FVector v2) {
    float dx = v1.X - v2.X;
    float dy = v1.Y - v2.Y;
    float dz = v1.Z - v2.Z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

float DotProduct(SDK::FVector v1, SDK::FVector v2) {
    float v1_len = sqrt(v1.X * v1.X + v1.Y * v1.Y + v1.Z * v1.Z);
    float v2_len = sqrt(v2.X * v2.X + v2.Y * v2.Y + v2.Z * v2.Z);

    if (v1_len == 0.0f || v2_len == 0.0f) return 0.0f;

    SDK::FVector v1_norm = { v1.X / v1_len, v1.Y / v1_len, v1.Z / v1_len };
    SDK::FVector v2_norm = { v2.X / v2_len, v2.Y / v2_len, v2.Z / v2_len };

    return v1_norm.X * v2_norm.X + v1_norm.Y * v2_norm.Y + v1_norm.Z * v2_norm.Z;
}

const char* GetAttackStageName(SDK::EAttackStage Stage) {
    switch (Stage) {
    case SDK::EAttackStage::Windup: return "Windup (0)";
    case SDK::EAttackStage::Release: return "Release (1)";
    case SDK::EAttackStage::Recovery: return "Recovery (2)";
    case SDK::EAttackStage::EAttackStage_MAX: return "MAX (3)";
    default: return "Unknown";
    }
}

void Log(const char* fmt, ...) {
    if (!Globals::bDebugMode) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void PressParry() {
    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
}

// Process Logic
void ProcessCharacter(SDK::AMordhauCharacter* LocalChar, SDK::AMordhauCharacter* Enemy) {
    if (!LocalChar || !Enemy || LocalChar == Enemy) return;

    // 0. RIPOSTE/ATTACK LOCK CHECK
    if (LocalChar->MotionSystemComponent && LocalChar->MotionSystemComponent->Motion) {
        auto LocalMotion = LocalChar->MotionSystemComponent->Motion;
        if (LocalMotion->IsA(SDK::UAttackMotion::StaticClass())) {
            return;
        }
    }

    SDK::FVector LocalCharLocation = LocalChar->K2_GetActorLocation();

    // 1. Distance Calculation
    float PingCompensation = Config::SimulatedPing * 0.65f;
    float FinalTriggerDist = Config::SafeMargin + PingCompensation;

    float CurrentDist = GetDistance(Enemy->K2_GetActorLocation(), LocalCharLocation);
    if (CurrentDist > 500.0f || Enemy->Health <= 0.0f) return;

    // 4. Check Weapon
    SDK::AMordhauEquipment* EnemyEquipment = Enemy->RightHandEquipment;
    if (!EnemyEquipment || !EnemyEquipment->IsA(SDK::AMordhauWeapon::StaticClass())) return;
    
    SDK::AMordhauWeapon* EnemyWeapon = static_cast<SDK::AMordhauWeapon*>(EnemyEquipment);
    auto WeaponMesh = EnemyWeapon->SkeletalMeshComponent;
    if (!WeaponMesh) return;

    // 5. Motion System
    auto EnemyMotionSystem = Enemy->MotionSystemComponent;
    if (!EnemyMotionSystem || !EnemyMotionSystem->Motion || !EnemyMotionSystem->Motion->IsA(SDK::UAttackMotion::StaticClass())) return;

    // 6. Attack Phase
    const int ATTACK_STAGE_OFFSET = 0x10E9; // Offset from original code
    SDK::UAttackMotion* AttackMotion = static_cast<SDK::UAttackMotion*>(EnemyMotionSystem->Motion);
    uintptr_t MotionAddress = (uintptr_t)AttackMotion;
    SDK::EAttackStage AttackStage = *reinterpret_cast<SDK::EAttackStage*>(MotionAddress + ATTACK_STAGE_OFFSET);

    if (AttackStage != SDK::EAttackStage::Release) return;

    // 7. Geometry
    static SDK::FName nHead = SDK::UKismetStringLibrary::Conv_StringToName(L"Head");
    static SDK::FName nHandL = SDK::UKismetStringLibrary::Conv_StringToName(L"hand_l");
    static SDK::FName nHandR = SDK::UKismetStringLibrary::Conv_StringToName(L"hand_r");
    static SDK::FName nFootL = SDK::UKismetStringLibrary::Conv_StringToName(L"foot_l");
    static SDK::FName nFootR = SDK::UKismetStringLibrary::Conv_StringToName(L"foot_r");
    static SDK::FName nSpine = SDK::UKismetStringLibrary::Conv_StringToName(L"spine_03");

    static SDK::FName nTraceEnd = SDK::UKismetStringLibrary::Conv_StringToName(L"TraceEnd");
    static SDK::FName nTraceStart = SDK::UKismetStringLibrary::Conv_StringToName(L"TraceStart");

    SDK::FVector TipPos = WeaponMesh->GetSocketLocation(nTraceEnd);
    SDK::FVector BasePos = WeaponMesh->GetSocketLocation(nTraceStart);

    SDK::FVector TargetPos = (GetDistance(TipPos, LocalCharLocation) < GetDistance(BasePos, LocalCharLocation)) ? TipPos : BasePos;

    std::vector<SDK::FName> CriticalSockets = { nHead, nHandL, nHandR, nFootL, nFootR, nSpine };
    float MinBodyDistance = 99999.0f;

    for (const auto& SocketName : CriticalSockets) {
        SDK::FVector BodyPoint = LocalChar->Mesh->GetSocketLocation(SocketName);
        float dist = GetDistance(TargetPos, BodyPoint);
        if (dist < MinBodyDistance) MinBodyDistance = dist;
    }

    if (MinBodyDistance < FinalTriggerDist) {
        SDK::FVector DirToMe = LocalCharLocation - Enemy->K2_GetActorLocation();
        SDK::FVector EnemyFwd = Enemy->GetActorForwardVector();

        float AngleDot = DotProduct(EnemyFwd, DirToMe);
        
        if (AngleDot > Config::MaxAngle) {
            PressParry();
            Log("[HOOK] PARRY TRIGGERED! Dist: %.2f | Enemy: %s", MinBodyDistance, Enemy->GetName().c_str());
        }
    }
}

// Hook
typedef void(__thiscall* ProcessEvent_t)(SDK::UObject*, SDK::UFunction*, void*);
ProcessEvent_t oProcessEvent = nullptr;

void __fastcall hkProcessEvent(SDK::UObject* Object, SDK::UFunction* Function, void* Params) {
    if (Globals::bAutoParryEnabled) {
        // Optimization: Filter by function name index
        if (Object && Object->IsA(SDK::AMordhauCharacter::StaticClass())) {
            
            // Check if it's "ReceiveTick" or similar
            if (Function->Name == Globals::NameReceiveTick || 
                Function->Name == Globals::NameTick || 
                Function->Name == Globals::NameBPTick ||
                Function->Name == Globals::NameUpdateAnim) {
                
                SDK::AMordhauCharacter* Character = static_cast<SDK::AMordhauCharacter*>(Object);
                SDK::AMordhauCharacter* Local = Globals::LocalCharacter.load();
                
                if (Local && Character != Local) {
                    ProcessCharacter(Local, Character);
                }
            }
        }
    }

    return oProcessEvent(Object, Function, Params);
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
    Globals::World = World;

    // Initialize FNames for the hook
    Globals::NameReceiveTick = SDK::UKismetStringLibrary::Conv_StringToName(L"ReceiveTick");
    Globals::NameTick = SDK::UKismetStringLibrary::Conv_StringToName(L"Tick");
    Globals::NameBPTick = SDK::UKismetStringLibrary::Conv_StringToName(L"K2_Tick"); 
    Globals::NameUpdateAnim = SDK::UKismetStringLibrary::Conv_StringToName(L"BlueprintUpdateAnimation");

    std::cout << "[SUCCESS] World found. Installing Hooks...\n";
    
    // Install Hook
    uintptr_t ProcessEventAddr = SDK::InSDKUtils::GetImageBase() + SDK::Offsets::ProcessEvent;
    
    if (MH_Initialize() != MH_OK) {
        std::cout << "[ERROR] MinHook Initialize failed.\n";
        return 1;
    }

    if (MH_CreateHook((LPVOID)ProcessEventAddr, &hkProcessEvent, reinterpret_cast<LPVOID*>(&oProcessEvent)) != MH_OK) {
        std::cout << "[ERROR] CreateHook failed.\n";
        return 1;
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        std::cout << "[ERROR] EnableHook failed.\n";
        return 1;
    }

    std::cout << "[HOOK] Hook installed at 0x" << std::hex << ProcessEventAddr << std::dec << "\n";
    std::cout << "[INFO] Press F3 (Toggle), F4 (Debug), END (Unload)\n";

    while (true) {
        if (GetAsyncKeyState(VK_END) & 1) break;

        if (GetAsyncKeyState(VK_F3) & 1) {
            bool b = !Globals::bAutoParryEnabled;
            Globals::bAutoParryEnabled = b;
            std::cout << "[TOGGLE] Auto-Parry: " << (b ? "ON" : "OFF") << "\n";
            Sleep(200); // Debounce
        }

        if (GetAsyncKeyState(VK_F4) & 1) {
            bool b = !Globals::bDebugMode;
            Globals::bDebugMode = b;
            std::cout << "[TOGGLE] Debug Log: " << (b ? "ON" : "OFF") << "\n";
            Sleep(200); // Debounce
        }

        // Update Local Character slowly
        if (World && World->OwningGameInstance && World->OwningGameInstance->LocalPlayers.IsValidIndex(0)) {
             auto LP = World->OwningGameInstance->LocalPlayers[0];
             if (LP && LP->PlayerController) {
                 Globals::LocalController = LP->PlayerController;
                 if (LP->PlayerController->Pawn) {
                     auto Pawn = LP->PlayerController->Pawn;
                     if (Pawn->IsA(SDK::AMordhauCharacter::StaticClass())) {
                         Globals::LocalCharacter = static_cast<SDK::AMordhauCharacter*>(Pawn);
                     } else {
                         Globals::LocalCharacter = nullptr;
                     }
                 }
             }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[INFO] Unloading...\n";
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    
    // Small sleep to ensure hooks are exited
    Sleep(100);

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
