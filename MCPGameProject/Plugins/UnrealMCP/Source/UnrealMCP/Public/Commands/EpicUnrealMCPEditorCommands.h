#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Editor-related MCP commands
 * Handles viewport control, actor manipulation, and level management
 */
class UNREALMCP_API FEpicUnrealMCPEditorCommands
{
public:
    	FEpicUnrealMCPEditorCommands();

    // Handle editor commands
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    // Actor manipulation commands
    TSharedPtr<FJsonObject> HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSpawnActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params);

    // Blueprint actor spawning
    TSharedPtr<FJsonObject> HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params);

    // Reflection-based actor introspection and viewport control (v1.9.0)
    TSharedPtr<FJsonObject> HandleGetActorProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFocusViewport(const TSharedPtr<FJsonObject>& Params);

    // Viewport screenshot capture (v2.0.0). Queues an async PNG write via
    // FScreenshotRequest; returns the planned output path immediately.
    TSharedPtr<FJsonObject> HandleTakeScreenshot(const TSharedPtr<FJsonObject>& Params);

    // Lookdev / deterministic framing (v3.14.0).
    // focus_viewport ставит камеру ОТНОСИТЕЛЬНО актора и потому не воспроизводим
    // между прогонами. Эти команды задают и читают камеру точно, что является
    // предусловием любого сравнения кадров.
    TSharedPtr<FJsonObject> HandleViewportSetCamera(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleViewportGetCamera(const TSharedPtr<FJsonObject>& Params);
    // Режим отображения вьюпорта, включая VMI_PathTracing — стайл-гайд требует
    // Path Tracer для промо-кадров. Захват делает существующий take_screenshot.
    TSharedPtr<FJsonObject> HandleViewportSetViewMode(const TSharedPtr<FJsonObject>& Params);
    // Синхронный захват level-вьюпорта. take_screenshot использует
    // FScreenshotRequest, который обслуживается game viewport'ом и потому в
    // редакторе БЕЗ PIE не срабатывает вообще — файл не появляется. Здесь
    // рисуем вьюпорт и читаем пиксели сразу: к моменту ответа файл на диске.
    TSharedPtr<FJsonObject> HandleViewportCapture(const TSharedPtr<FJsonObject>& Params);
    // PostProcessVolume: generic set_actor_property не умеет вложенный
    // FPostProcessSettings и парные bOverride_* флаги — без флага записанное
    // значение молча игнорируется движком.
    // Свойства КОМПОНЕНТА level-актора (v3.15.0). set_actor_property работает
    // только по свойствам самого AActor и не спускается в компоненты, а
    // set_component_property адресует компоненты Blueprint-класса, а не актора
    // на уровне. Свет, туман, атмосфера настраиваются именно на компонентах.
    TSharedPtr<FJsonObject> HandleActorGetComponentProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleActorSetComponentProperty(const TSharedPtr<FJsonObject>& Params);

    TSharedPtr<FJsonObject> HandlePostProcessGetParams(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandlePostProcessSetParams(const TSharedPtr<FJsonObject>& Params);
};