#include "EpicUnrealMCPModule.h"
#include "EpicUnrealMCPBridge.h"
#include "IUnrealMCPCommandHandler.h"
#include "Modules/ModuleManager.h"
#include "EditorSubsystem.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FEpicUnrealMCPModule"

void FEpicUnrealMCPModule::StartupModule()
{
	UE_LOG(LogTemp, Display, TEXT("Epic Unreal MCP Module has started"));
}

void FEpicUnrealMCPModule::ShutdownModule()
{
	ExternalHandlers.Reset();
	UE_LOG(LogTemp, Display, TEXT("Epic Unreal MCP Module has shut down"));
}

void FEpicUnrealMCPModule::RegisterCommandHandler(const TSharedPtr<IUnrealMCPCommandHandler>& Handler)
{
	if (Handler.IsValid())
	{
		ExternalHandlers.AddUnique(Handler);
		UE_LOG(LogTemp, Display, TEXT("UnrealMCP: registered external command handler (%d total)"), ExternalHandlers.Num());
	}
}

void FEpicUnrealMCPModule::UnregisterCommandHandler(const TSharedPtr<IUnrealMCPCommandHandler>& Handler)
{
	ExternalHandlers.Remove(Handler);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FEpicUnrealMCPModule, UnrealMCP) 