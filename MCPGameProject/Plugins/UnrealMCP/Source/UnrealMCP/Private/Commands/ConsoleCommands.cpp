#include "Commands/ConsoleCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/OutputDevice.h"
#include "Logging/LogVerbosity.h"

FUnrealMCPConsoleCommands::FUnrealMCPConsoleCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPConsoleCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("execute_console_command"))
    {
        return HandleExecuteConsoleCommand(Params);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown console command: %s"), *CommandType));
}

namespace
{
    /**
     * Внутренний FOutputDevice — буферизует все строки, прошедшие через GLog,
     * пока подписка активна. MaxLines защищает от log-flood (например
     * Automation тесты могут писать тысячи строк в секунду).
     */
    class FConsoleCaptureOutputDevice : public FOutputDevice
    {
    public:
        TArray<FString> Lines;
        static constexpr int32 MaxLines = 500;
        bool bTruncated = false;

        virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            if (Lines.Num() >= MaxLines)
            {
                bTruncated = true;
                return;
            }

            // Формат строки имитирует то, как Output Log сам отображает запись:
            // "LogCategory: Verbosity: message". Verbosity мапится по таблице
            // ниже — для соответствия привычному виду логов UE.
            const TCHAR* VerbosityStr = TEXT("Log");
            switch (Verbosity)
            {
                case ELogVerbosity::Fatal:      VerbosityStr = TEXT("Fatal");   break;
                case ELogVerbosity::Error:      VerbosityStr = TEXT("Error");   break;
                case ELogVerbosity::Warning:    VerbosityStr = TEXT("Warning"); break;
                case ELogVerbosity::Display:    VerbosityStr = TEXT("Display"); break;
                case ELogVerbosity::Log:        VerbosityStr = TEXT("Log");     break;
                case ELogVerbosity::Verbose:    VerbosityStr = TEXT("Verbose"); break;
                case ELogVerbosity::VeryVerbose:VerbosityStr = TEXT("VeryVerbose"); break;
                default: break;
            }

            const FString Formatted = FString::Printf(TEXT("%s: %s: %s"),
                *Category.ToString(), VerbosityStr, V);
            Lines.Add(Formatted);
        }
    };
}

TSharedPtr<FJsonObject> FUnrealMCPConsoleCommands::HandleExecuteConsoleCommand(const TSharedPtr<FJsonObject>& Params)
{
    FString Command;
    if (!Params->TryGetStringField(TEXT("cmd"), Command) || Command.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'cmd' parameter"));
    }

    FConsoleCaptureOutputDevice Capture;
    if (GLog)
    {
        GLog->AddOutputDevice(&Capture);
    }

    // Выбор контекста World. Для большинства команд (Automation RunTests,
    // CVar, stat) контекст не критичен, но Exec() требует валидный pointer
    // или nullptr — мы предпочитаем PIE world если он есть, иначе editor world.
    UWorld* World = nullptr;
    if (GEditor)
    {
        if (GEditor->PlayWorld)
        {
            World = GEditor->PlayWorld;
        }
        else
        {
            World = GEditor->GetEditorWorldContext().World();
        }
    }

    bool bExecResult = false;
    FString ErrorMsg;

    if (GEngine)
    {
        // GLog здесь — куда Exec может писать "встроенные" ответы команды
        // (например `stat fps` возвращает не только лог, но и Ar.Log в это Ar).
        // Мы передаём GLog, что значит "лог уже подписан Capture, всё там
        // окажется".
        bExecResult = GEngine->Exec(World, *Command, *GLog);
    }
    else
    {
        ErrorMsg = TEXT("GEngine is null");
    }

    if (GLog)
    {
        GLog->RemoveOutputDevice(&Capture);
        GLog->Flush();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), bExecResult && ErrorMsg.IsEmpty());
    Result->SetStringField(TEXT("command"), Command);

    TArray<TSharedPtr<FJsonValue>> JsonLines;
    JsonLines.Reserve(Capture.Lines.Num());
    for (const FString& Line : Capture.Lines)
    {
        JsonLines.Add(MakeShared<FJsonValueString>(Line));
    }
    Result->SetArrayField(TEXT("log_lines"), JsonLines);
    Result->SetBoolField(TEXT("log_truncated"), Capture.bTruncated);
    Result->SetNumberField(TEXT("lines_captured"), Capture.Lines.Num());

    if (!ErrorMsg.IsEmpty())
    {
        Result->SetStringField(TEXT("error"), ErrorMsg);
    }

    return Result;
}
