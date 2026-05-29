#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IUnrealMCPCommandHandler;

/**
 * Модуль плагина UnrealMCP.
 *
 * Помимо lifecycle хранит реестр внешних обработчиков команд
 * (IUnrealMCPCommandHandler). Project-specific плагины регистрируют свои
 * обработчики здесь — bridge маршрутизирует в них команды, которые не совпали
 * ни с одним встроенным handler'ом. Это держит ядро плагина generic.
 */
class UNREALMCP_API FEpicUnrealMCPModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static inline FEpicUnrealMCPModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FEpicUnrealMCPModule>("UnrealMCP");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("UnrealMCP");
	}

	/** Зарегистрировать внешний обработчик команд (idempotent). */
	void RegisterCommandHandler(const TSharedPtr<IUnrealMCPCommandHandler>& Handler);

	/** Снять регистрацию обработчика (безопасно для несуществующего). */
	void UnregisterCommandHandler(const TSharedPtr<IUnrealMCPCommandHandler>& Handler);

	/** Все зарегистрированные внешние обработчики (для опроса в bridge). */
	const TArray<TSharedPtr<IUnrealMCPCommandHandler>>& GetCommandHandlers() const { return ExternalHandlers; }

private:
	/** Внешние project-specific обработчики команд. */
	TArray<TSharedPtr<IUnrealMCPCommandHandler>> ExternalHandlers;
};
