// Реализация FUnrealMCPPIEUtils (MCP-PLUGIN-003).

#include "Commands/PIEUtils.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/UserWidget.h"
#include "UObject/UObjectIterator.h"
#include "Dom/JsonObject.h"

// ─────────────────────────────────────────────────────────────────────────────
// Внутренние хелперы.
// ─────────────────────────────────────────────────────────────────────────────

namespace UnrealMCPPIEInternal
{
    /**
     * Собрать все КЛИЕНТСКИЕ PIE-WorldContext'ы (отсортированные по PIEInstance).
     *
     * FIX-UI-008: исключаем dedicated-server контекст. Когда pie_start запущен с
     * dedicated_server=true (или любой UE net-flow, поднимающий server-world),
     * среди PIE-контекстов появляется мир с GetNetMode()==NM_DedicatedServer.
     * У него нет ни viewport'а, ни клиентского UMG — controller_index не должен
     * на него попадать. Отфильтровав server-контекст здесь, мы гарантируем, что
     * index 0/1 = первый/второй РЕАЛЬНЫЙ клиент.
     *
     * Для основного WarCard-кейса (PIE_Standalone, num_clients>1) серверного
     * контекста нет вовсе, поэтому фильтр — no-op, и просто возвращаются N
     * standalone-клиентов в порядке PIEInstance.
     */
    static TArray<const FWorldContext*> CollectPIEContexts()
    {
        TArray<const FWorldContext*> Result;
        if (!GEngine)
        {
            return Result;
        }
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
        {
            if (Ctx.WorldType != EWorldType::PIE)
            {
                continue;
            }

            // Пропускаем dedicated-server world — он не клиент.
            if (const UWorld* W = Ctx.World())
            {
                if (W->GetNetMode() == NM_DedicatedServer)
                {
                    continue;
                }
            }
            Result.Add(&Ctx);
        }
        Result.Sort([](const FWorldContext& A, const FWorldContext& B)
        {
            return A.PIEInstance < B.PIEInstance;
        });
        return Result;
    }

    /** Собрать все PlayerController'ы конкретного UWorld. */
    static TArray<APlayerController*> CollectControllersInWorld(UWorld* World)
    {
        TArray<APlayerController*> Out;
        if (!World)
        {
            return Out;
        }
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            if (APlayerController* PC = It->Get())
            {
                Out.Add(PC);
            }
        }
        return Out;
    }
} // namespace UnrealMCPPIEInternal

// ─────────────────────────────────────────────────────────────────────────────
// GetPlayerControllerByIndex
// ─────────────────────────────────────────────────────────────────────────────

APlayerController* FUnrealMCPPIEUtils::GetPlayerControllerByIndex(int32 Index)
{
    if (Index < 0)
    {
        return nullptr;
    }
    if (!GEditor)
    {
        return nullptr;
    }

    // Стратегия 1: multi-PIE — каждый клиент в отдельном (клиентском) WorldContext.
    // FIX-UI-008: если найдено >1 клиентского контекста — это true multi-client
    // standalone. Мапим index СТРОГО на N-й клиентский world и НЕ откатываемся
    // на split-screen fallback (иначе оба индекса резолвятся в один PlayWorld).
    TArray<const FWorldContext*> Contexts = UnrealMCPPIEInternal::CollectPIEContexts();
    if (Contexts.Num() > 1)
    {
        if (Contexts.IsValidIndex(Index))
        {
            if (UWorld* W = Contexts[Index]->World())
            {
                // GetFirstPlayerController() может быть null на ранних тиках до
                // полного boot'а инстанса — это нормально, вызывающий ретраит.
                return W->GetFirstPlayerController();
            }
        }
        return nullptr;
    }

    // Один клиентский контекст: пробуем его world, иначе PlayWorld.
    if (Contexts.Num() == 1)
    {
        if (UWorld* W = Contexts[0]->World())
        {
            if (APlayerController* PC = W->GetFirstPlayerController())
            {
                // Если в единственном world несколько PC (split-screen) — индексируем по ним.
                TArray<APlayerController*> Controllers = UnrealMCPPIEInternal::CollectControllersInWorld(W);
                if (Controllers.IsValidIndex(Index))
                {
                    return Controllers[Index];
                }
                if (Index == 0)
                {
                    return PC;
                }
            }
        }
    }

    // Стратегия 2 (fallback): один PIE world с N PlayerController'ами (split-screen),
    // когда контексты ещё не собрались, но GEditor->PlayWorld уже есть.
    if (UWorld* PlayWorld = GEditor->PlayWorld)
    {
        TArray<APlayerController*> Controllers = UnrealMCPPIEInternal::CollectControllersInWorld(PlayWorld);
        if (Controllers.IsValidIndex(Index))
        {
            return Controllers[Index];
        }
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetNumPIEClients
// ─────────────────────────────────────────────────────────────────────────────

int32 FUnrealMCPPIEUtils::GetNumPIEClients()
{
    TArray<const FWorldContext*> Contexts = UnrealMCPPIEInternal::CollectPIEContexts();

    // FIX-UI-008: multi-client standalone — клиентов = число КЛИЕНТСКИХ PIE
    // контекстов (dedicated-server уже отфильтрован в CollectPIEContexts).
    if (Contexts.Num() > 1)
    {
        return Contexts.Num();
    }

    // Случай single-world (или ничего): берём количество PC в основном PIE world.
    if (!GEditor || !GEditor->PlayWorld)
    {
        return 0;
    }
    TArray<APlayerController*> Controllers = UnrealMCPPIEInternal::CollectControllersInWorld(GEditor->PlayWorld);
    if (Controllers.Num() > 0)
    {
        return Controllers.Num();
    }
    // PIE есть но PC ещё не создан — всё равно считаем что есть один клиент.
    return Contexts.Num() > 0 ? 1 : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetPIEWorldForClient
// ─────────────────────────────────────────────────────────────────────────────

UWorld* FUnrealMCPPIEUtils::GetPIEWorldForClient(int32 Index)
{
    if (Index < 0 || !GEditor)
    {
        return nullptr;
    }

    // FIX-UI-008: multi-client — мапим строго на N-й КЛИЕНТСКИЙ world
    // (dedicated-server отфильтрован в CollectPIEContexts). Для num_clients==1
    // (Contexts.Num()<=1) сохраняем fallback на GEditor->PlayWorld — общий кейс
    // не регрессирует.
    TArray<const FWorldContext*> Contexts = UnrealMCPPIEInternal::CollectPIEContexts();
    if (Contexts.Num() > 1)
    {
        return Contexts.IsValidIndex(Index) ? Contexts[Index]->World() : nullptr;
    }
    if (Contexts.Num() == 1 && Index == 0)
    {
        return Contexts[0]->World();
    }
    return GEditor->PlayWorld;
}

// ─────────────────────────────────────────────────────────────────────────────
// DescribeClient — JSON-описание для pie_status.
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPPIEUtils::DescribeClient(int32 Index)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetNumberField(TEXT("index"), Index);

    APlayerController* PC = GetPlayerControllerByIndex(Index);
    if (!PC)
    {
        Obj->SetStringField(TEXT("controller_class"), TEXT(""));
        Obj->SetStringField(TEXT("controller_name"), TEXT(""));
        Obj->SetStringField(TEXT("world_name"), TEXT(""));
        return Obj;
    }

    Obj->SetStringField(TEXT("controller_class"),
        PC->GetClass() ? PC->GetClass()->GetName() : TEXT("Unknown"));
    Obj->SetStringField(TEXT("controller_name"), PC->GetName());

    UWorld* World = PC->GetWorld();
    if (World)
    {
        Obj->SetStringField(TEXT("world_name"), World->GetName());
        if (World->GetCurrentLevel() && World->GetCurrentLevel()->GetOuter())
        {
            Obj->SetStringField(TEXT("current_level"),
                World->GetCurrentLevel()->GetOuter()->GetName());
        }

        // Текущий UMG (если есть) — берём первый IsInViewport=true виджет с этим World.
        FString CurrentWidget;
        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            UUserWidget* UW = *It;
            if (IsValid(UW) && UW->GetWorld() == World && UW->IsInViewport())
            {
                CurrentWidget = UW->GetClass() ? UW->GetClass()->GetName() : UW->GetName();
                break;
            }
        }
        Obj->SetStringField(TEXT("current_widget"), CurrentWidget);
    }
    return Obj;
}
