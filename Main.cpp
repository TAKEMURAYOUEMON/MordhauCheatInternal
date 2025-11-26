#define _CRT_SECURE_NO_WARNINGS

// ОТКЛЮЧЕНИЕ МУСОРНЫХ ВАРНИНГОВ SDK
#pragma warning(disable: 4369)
#pragma warning(disable: 4309)
#pragma warning(disable: 4244)
#pragma warning(disable: 4616)
#pragma warning(disable: 2039) 

#include <Windows.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm> 
#include <cstdio> // Явно включаем для freopen

#include "MinHook.h"

// ПОДКЛЮЧЕНИЕ ТВОЕГО SDK
#include "SDK/Engine_classes.hpp"
#include "SDK/Engine_parameters.hpp"
#include "SDK/Engine_structs.hpp"
#include "SDK/Mordhau_classes.hpp"

#define M_PI 3.1415926535f

// =================================================================================
// 🚨 ИНФОРМАЦИЯ ИЗ SDK ПОЛЬЗОВАТЕЛЯ
// =================================================================================

// Enum Mordhau.EAttackStage (по информации пользователя)
enum class EAttackStage : uint8_t
{
    Windup = 0,
    Release = 1,
    Recovery = 2,
    EAttackStage_MAX = 3
};

// =================================================================================
// ⚙️ КОНФИГУРАЦИЯ
// =================================================================================

namespace Config {
    bool bEnabled = true;
    bool bDebugMode = true;

    // --- НАСТРОЙКИ ТОЧНОСТИ ---
    // Увеличенный запас для компенсации пинга. (80.0f + 52.0f = 132.0f триггер при 80 пинга)
    float SafeMargin = 80.0f;
    float SimulatedPing = 80.0f;

    // Фильтр угла: Отсекает Windup, направленный далеко от нас.
    float MaxAngle = 0.2f;
}

// =================================================================================
// 🛠 МАТЕМАТИКА И УТИЛИТЫ
// =================================================================================

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

const char* GetAttackStageName(EAttackStage Stage) {
    switch (Stage) {
    case EAttackStage::Windup: return "Windup (0)";
    case EAttackStage::Release: return "Release (1)";
    case EAttackStage::Recovery: return "Recovery (2)";
    case EAttackStage::EAttackStage_MAX: return "MAX (3)";
    default: return "Unknown";
    }
}

void Log(const char* fmt, ...) {
    if (!Config::bDebugMode) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void PressParry() {
    // Чистый WinAPI для симуляции нажатия правой кнопки мыши
    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
}

// =================================================================================
// ⚔️ ЛОГИКА
// =================================================================================

void AutoParryTick(SDK::UWorld* World, SDK::AMordhauCharacter* LocalChar, SDK::APlayerController* PlayerController) {
    if (!World || !LocalChar || !LocalChar->Mesh || !PlayerController) return;

    // КРИТИЧЕСКИЙ ОФФСЕТ (для AttackStage)
    const int ATTACK_STAGE_OFFSET = 0x10E9;

    // КРИТИЧЕСКИЕ ТОЧКИ ТЕЛА (СОКЕТЫ/КОСТИ)
    static SDK::FName nHead = SDK::UKismetStringLibrary::Conv_StringToName(L"Head");
    static SDK::FName nHandL = SDK::UKismetStringLibrary::Conv_StringToName(L"hand_l");
    static SDK::FName nHandR = SDK::UKismetStringLibrary::Conv_StringToName(L"hand_r");
    static SDK::FName nFootL = SDK::UKismetStringLibrary::Conv_StringToName(L"foot_l");
    static SDK::FName nFootR = SDK::UKismetStringLibrary::Conv_StringToName(L"foot_r");
    static SDK::FName nSpine = SDK::UKismetStringLibrary::Conv_StringToName(L"spine_03");

    // ТОЧКИ ОРУЖИЯ
    static SDK::FName nTraceEnd = SDK::UKismetStringLibrary::Conv_StringToName(L"TraceEnd");
    static SDK::FName nTraceStart = SDK::UKismetStringLibrary::Conv_StringToName(L"TraceStart");

    // 0. RIPOSTE/ATTACK LOCK CHECK (Проверка, что мы сами не атакуем)
    if (LocalChar->MotionSystemComponent && LocalChar->MotionSystemComponent->Motion) {
        auto LocalMotion = LocalChar->MotionSystemComponent->Motion;
        if (LocalMotion->IsA(SDK::UAttackMotion::StaticClass())) {
            Log("  [LOCAL CHECK FAILED] Local Character is already attacking/riposting. (Skipping Parry)");
            Log("--- TICK END --- (Local Lock)");
            return;
        }
    }

    SDK::FVector LocalCharLocation = LocalChar->K2_GetActorLocation();

    // 1. РАСЧЕТ ДИСТАНСИИ ТРИГГЕРА
    float PingCompensation = Config::SimulatedPing * 0.65f;
    float FinalTriggerDist = Config::SafeMargin + PingCompensation;

    SDK::TArray<SDK::AActor*>& Actors = World->PersistentLevel->Actors;

    Log("--- TICK START | Actors: %d | Trigger Dist: %.1f (Margin %.1f + PingComp %.1f) ---",
        Actors.Num(), FinalTriggerDist, Config::SafeMargin, PingCompensation);

    for (int i = 0; i < Actors.Num(); i++) {
        SDK::AActor* Actor = Actors[i];

        if (!Actor || Actor == LocalChar || !Actor->IsA(SDK::AMordhauCharacter::StaticClass())) {
            continue;
        }

        SDK::AMordhauCharacter* Enemy = static_cast<SDK::AMordhauCharacter*>(Actor);

        float CurrentDist = GetDistance(Enemy->K2_GetActorLocation(), LocalCharLocation);

        if (CurrentDist > 500.0f || Enemy->Health <= 0.0f) {
            continue;
        }

        Log("=================================================================");
        Log("[ENEMY] %s (Dist: %.1f)", Enemy->GetName().c_str(), CurrentDist);

        // 4. ПРОВЕРКА ОРУЖИЯ
        SDK::AMordhauEquipment* EnemyEquipment = Enemy->RightHandEquipment;
        if (!EnemyEquipment || !EnemyEquipment->IsA(SDK::AMordhauWeapon::StaticClass())) {
            Log("  [CHECK FAILED] No Weapon or Not a Weapon.");
            continue;
        }

        SDK::AMordhauWeapon* EnemyWeapon = static_cast<SDK::AMordhauWeapon*>(EnemyEquipment);
        auto WeaponMesh = EnemyWeapon->SkeletalMeshComponent;

        if (!WeaponMesh) {
            Log("  [CRITICAL CHECK FAILED] No Weapon Mesh found.");
            continue;
        }

        // 5. ИЕРАРХИЯ MotionSystem 
        auto EnemyMotionSystem = Enemy->MotionSystemComponent;
        if (!EnemyMotionSystem || !EnemyMotionSystem->Motion || !EnemyMotionSystem->Motion->IsA(SDK::UAttackMotion::StaticClass())) {
            Log("  [CHECK FAILED] Enemy is not in an AttackMotion.");
            continue;
        }

        // 6. ФАЗА АТАКИ (АНТИ-ФИНТ ЛОГИКА - СТРОГИЙ RELEASE)
        SDK::UAttackMotion* AttackMotion = static_cast<SDK::UAttackMotion*>(EnemyMotionSystem->Motion);
        uintptr_t MotionAddress = (uintptr_t)AttackMotion;

        EAttackStage AttackStage = *reinterpret_cast<EAttackStage*>(MotionAddress + ATTACK_STAGE_OFFSET);

        Log("  [ATTACK STATE] Stage: %d (%s)", (int)AttackStage, GetAttackStageName(AttackStage));

        // ⚠️ ГЛАВНАЯ ПРОВЕРКА АНТИ-ФИНТА: Парируем ТОЛЬКО в Release.
        if (AttackStage != EAttackStage::Release) {
            Log("  [CHECK FAILED] State is not Release (Windup/Recovery). Waiting for committed attack. (Skipping)");
            Log("=================================================================");
            continue;
        }

        Log("  [COMMITMENT OK] State is Release! Checking Geometry.");

        // 7. ГЕОМЕТРИЯ - ПОИСК МИНИМАЛЬНОЙ БЛИЗОСТИ К ТЕЛУ

        SDK::FVector TipPos = WeaponMesh->GetSocketLocation(nTraceEnd);
        SDK::FVector BasePos = WeaponMesh->GetSocketLocation(nTraceStart);

        // Выбираем ближайшую точку оружия к нам
        SDK::FVector TargetPos = (GetDistance(TipPos, LocalCharLocation) < GetDistance(BasePos, LocalCharLocation)) ? TipPos : BasePos;

        // Список сокетов для проверки 
        std::vector<SDK::FName> CriticalSockets = { nHead, nHandL, nHandR, nFootL, nFootR, nSpine };
        float MinBodyDistance = 99999.0f;

        for (const auto& SocketName : CriticalSockets) {
            SDK::FVector BodyPoint = LocalChar->Mesh->GetSocketLocation(SocketName);
            float dist = GetDistance(TargetPos, BodyPoint);

            if (dist < MinBodyDistance) {
                MinBodyDistance = dist;
            }
        }

        // Триггер теперь 132.0f
        Log("  [GEOM] Closest Dist (to Body Point): %.1f (Trigger: %.1f)", MinBodyDistance, FinalTriggerDist);

        // 8. ТРИГГЕР ДИСТАНЦИИ И УГЛА
        if (MinBodyDistance < FinalTriggerDist) {
            Log("  [CHECK OK] Distance Triggered (%.1f < %.1f).", MinBodyDistance, FinalTriggerDist);

            // Расчет угла для атаки (для фильтрации замахов, направленных в сторону)
            SDK::FVector DirToMe = LocalCharLocation - Enemy->K2_GetActorLocation();
            SDK::FVector EnemyFwd = Enemy->GetActorForwardVector();

            float AngleDot = DotProduct(EnemyFwd, DirToMe);
            Log("  [ANGLE] Dot Product: %.2f (Required > %.2f)", AngleDot, Config::MaxAngle);

            if (AngleDot > Config::MaxAngle) {

                // ФИНАЛЬНЫЙ ТРИГГЕР: ПАРИРОВАНИЕ
                PressParry();

                Log("  [SUCCESS] PARRY TRIGGERED! Dist: %.2f", MinBodyDistance);
                Log("=================================================================");
                return;
            }
            else {
                Log("  [CHECK FAILED] Angle too wide (Dot: %.2f).", AngleDot);
            }
        }
        else {
            Log("  [CHECK FAILED] Too far (Min Dist: %.1f > %.1f).", MinBodyDistance, FinalTriggerDist);
        }
    }
    Log("--- TICK END ---");
}

// =================================================================================
// 🪝 HOOKING
// =================================================================================

typedef void(*ExecOnHitFunc)(void* Context, void* Stack, void* Result);
ExecOnHitFunc OriginalExecOnHit = nullptr;

uintptr_t PatternScan(const char* signature, const char* mask) {
    static uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    static PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    static PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    static uintptr_t size = nt->OptionalHeader.SizeOfImage;

    uintptr_t patternLength = strlen(mask);
    for (uintptr_t i = 0; i < size - patternLength; i++) {
        bool found = true;
        for (uintptr_t j = 0; j < patternLength; j++) {
            if (mask[j] != '?' && signature[j] != *(char*)(base + i + j)) {
                found = false;
                break;
            }
        }
        if (found) return base + i;
    }
    return 0;
}

void HK_ExecOnHit(void* Context, void* Stack, void* Result) {
    // 1. Call Original
    if (OriginalExecOnHit) {
        OriginalExecOnHit(Context, Stack, Result);
    }

    // 2. Logic: If local player got hit -> PressParry()
    if (Config::bEnabled && Context) {
        SDK::UObject* Obj = static_cast<SDK::UObject*>(Context);
        
        // We need to check if this is the local player.
        // We can access GWorld via SDK::UWorld::GetWorld().
        SDK::UWorld* World = SDK::UWorld::GetWorld();
        if (World && World->OwningGameInstance && World->OwningGameInstance->LocalPlayers.IsValidIndex(0)) {
            auto LP = World->OwningGameInstance->LocalPlayers[0];
            if (LP && LP->PlayerController && LP->PlayerController->Pawn) {
                 if (LP->PlayerController->Pawn == Obj) {
                     Log("[RAGE PARRY] Local Player Hit Detected! Triggering Parry.");
                     PressParry();
                 }
            }
        }
    }
}

// =================================================================================
// 🚀 MAIN THREAD 
// =================================================================================

DWORD WINAPI MainThread(LPVOID lpParam) {
    HMODULE hModule = static_cast<HMODULE>(lpParam);
    AllocConsole();
    // ⚠️ ИСПРАВЛЕНИЕ ОШИБКИ C2064: Используем freopen для большей стабильности
    (void)freopen("CONOUT$", "w", stdout);

    std::cout << "[INFO] Waiting for Game Engine...\n";

    SDK::UWorld* World = nullptr;

    while (!World) {
        World = SDK::UWorld::GetWorld();
        Sleep(100);
    }
    // Если твой SDK требует инициализации GEngine, то раскомментируй, если нет - оставь закомментированным:
    // SDK::UEngine::GetEngine(); 

    std::cout << "[SUCCESS] Ready! Press F3(Parry), F4(Debug), END(Unload).\n";

    // --- HOOK INSTALLATION ---
    std::cout << "[HOOK] Initializing MinHook...\n";
    if (MH_Initialize() != MH_OK) {
        std::cout << "[HOOK] Failed to initialize MinHook (might be already initialized).\n";
    }

    const char* Sig = "\x48\x89\x5C\x24\x00\x56\x57\x41\x56\x48\x83\xEC\x00\x33\xFF\x48\x8B\xDA\x48\x89\x7C\x24\x00\x4C\x8B\xF1\xE8\x00\x00\x00\x00\x48\x8B\xCB\x48\x39\x7B\x00\x74\x00\x48\x8B\x53\x00\x4C\x8D\x44\x24\x00\xE8\x00\x00\x00\x00\xEB\x00\x4C\x8B\x83\x00\x00\x00\x00\x48\x8D\x54\x24\x00\x49\x8B\x40\x00\x48\x89\x83\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x89\xBC\x24";
    const char* Mask = "xxxx?xxxxxxx?xxxxxxxxx?xxxx????xxxxxx?x?xxx?xxxx?x????x?xxx????xxxx?xxx?xxx????x????xxxx";

    std::cout << "[HOOK] Scanning for execOnHit...\n";
    uintptr_t ExecOnHitAddr = PatternScan(Sig, Mask);
    if (ExecOnHitAddr) {
        std::cout << "[HOOK] Found execOnHit at: " << std::hex << ExecOnHitAddr << std::dec << "\n";
        MH_STATUS status = MH_CreateHook((LPVOID)ExecOnHitAddr, (LPVOID)HK_ExecOnHit, (LPVOID*)&OriginalExecOnHit);
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)ExecOnHitAddr);
            std::cout << "[HOOK] execOnHit Hooked Successfully!\n";
        } else {
            std::cout << "[HOOK] Failed to create hook: " << MH_StatusToString(status) << "\n";
        }
    } else {
        std::cout << "[HOOK] FAILED to find execOnHit pattern!\n";
    }
    // -------------------------

    float PingCompensation = Config::SimulatedPing * 0.65f;
    float FinalTriggerDist = Config::SafeMargin + PingCompensation;

    std::cout << "[CONFIG] Final Trigger Dist: " << FinalTriggerDist << " (Margin: " << Config::SafeMargin << " + Ping Comp: " << PingCompensation << ")\n";

    while (true) {
        if (GetAsyncKeyState(VK_END) & 1) break;

        if (GetAsyncKeyState(VK_F3) & 1) {
            Config::bEnabled = !Config::bEnabled;
            std::cout << "[TOGGLE] Auto-Parry: " << (Config::bEnabled ? "ON" : "OFF") << "\n";
        }

        if (GetAsyncKeyState(VK_F4) & 1) {
            Config::bDebugMode = !Config::bDebugMode;
            std::cout << "[TOGGLE] Debug Log: " << (Config::bDebugMode ? "ON" : "OFF") << "\n";
        }

        if (Config::bEnabled && World) {
            if (World->OwningGameInstance && World->OwningGameInstance->LocalPlayers.IsValidIndex(0)) {
                auto LP = World->OwningGameInstance->LocalPlayers[0];
                if (LP && LP->PlayerController && LP->PlayerController->Pawn) {
                    auto Pawn = LP->PlayerController->Pawn;
                    if (Pawn->IsA(SDK::AMordhauCharacter::StaticClass())) {
                        AutoParryTick(World, static_cast<SDK::AMordhauCharacter*>(Pawn), LP->PlayerController);
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
