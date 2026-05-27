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
    /** Собрать все PIE-WorldContext'ы (отсортированные по PIEInstance). */
    static TArray<const FWorldContext*> CollectPIEContexts()
    {
        TArray<const FWorldContext*> Result;
        if (!GEngine)
        {
            return Result;
        }
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
        {
            if (Ctx.WorldType == EWorldType::PIE)
            {
                Result.Add(&Ctx);
            }
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

    // Стратегия 1: multi-PIE — каждый клиент в отдельном WorldContext.
    TArray<const FWorldContext*> Contexts = UnrealMCPPIEInternal::CollectPIEContexts();
    if (Contexts.Num() > Index)
    {
        if (UWorld* W = Contexts[Index]->World())
        {
            if (APlayerController* PC = W->GetFirstPlayerController())
            {
                return PC;
            }
        }
    }

    // Стратегия 2: один PIE world с N PlayerController'ами (split-screen).
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

    // Случай multi-PIE — клиентов = число PIE контекстов.
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

    TArray<const FWorldContext*> Contexts = UnrealMCPPIEInternal::CollectPIEContexts();
    if (Contexts.Num() > Index)
    {
        return Contexts[Index]->World();
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
