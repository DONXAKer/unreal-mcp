#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Http.h"
#include "Json.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPBlueprintGraphCommands.h"
#include "Commands/UMGCommands.h"
#include "Commands/AssetCommands.h"
#include "Commands/TextureCommands.h"
#include "Commands/MaterialCommands.h"
#include "Commands/MeshCommands.h"
#include "Commands/LevelCommands.h"
#include "Commands/DataAssetCommands.h"
#include "Commands/NiagaraCommands.h"
#include "Commands/InputCommands.h"
#include "Commands/AnimationBPCommands.h"
#include "Commands/PIECommands.h"
#include "Commands/UMGTestCommands.h"
#include "Commands/UMGRuntimeCommands.h"
#include "Commands/EnhancedInputCommands.h"
#include "Commands/ConsoleCommands.h"
#include "Commands/WarCardGameCommands.h"
#include "EpicUnrealMCPBridge.generated.h"

class FMCPServerRunnable;

/**
 * Editor subsystem for MCP Bridge
 * Handles communication between external tools and the Unreal Editor
 * through a TCP socket connection. Commands are received as JSON and
 * routed to appropriate command handlers.
 */
UCLASS()
class UNREALMCP_API UEpicUnrealMCPBridge : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UEpicUnrealMCPBridge();
	virtual ~UEpicUnrealMCPBridge();

	// UEditorSubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Server functions
	void StartServer();
	void StopServer();
	bool IsRunning() const { return bIsRunning; }

	// Command execution
	FString ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Server state
	bool bIsRunning;
	TSharedPtr<FSocket> ListenerSocket;
	TSharedPtr<FSocket> ConnectionSocket;
	FRunnableThread* ServerThread;

	// Server configuration
	FIPv4Address ServerAddress;
	uint16 Port;

	// Command handler instances
	TSharedPtr<FEpicUnrealMCPEditorCommands> EditorCommands;
	TSharedPtr<FEpicUnrealMCPBlueprintCommands> BlueprintCommands;
	TSharedPtr<FEpicUnrealMCPBlueprintGraphCommands> BlueprintGraphCommands;
	TSharedPtr<FUnrealMCPUMGCommands> UMGCommands;
	TSharedPtr<FAssetCommands> AssetCommands;
	TSharedPtr<FTextureCommands> TextureCommands;
	TSharedPtr<FMaterialCommands> MaterialCommands;
	TSharedPtr<FMeshCommands> MeshCommands;
	TSharedPtr<FLevelCommands> LevelCommands;
	TSharedPtr<FDataAssetCommands> DataAssetCommands;
	TSharedPtr<FNiagaraCommands> NiagaraCommands;
	TSharedPtr<FInputCommands> InputCommands;
	TSharedPtr<FAnimationBPCommands> AnimationBPCommands;
	TSharedPtr<FPIECommands> PIECommands;
	TSharedPtr<FUMGTestCommands> UMGTestCommands;
	TSharedPtr<FUMGRuntimeCommands> UMGRuntimeCommands;
	TSharedPtr<FEnhancedInputCommands> EnhancedInputCommands;
	TSharedPtr<FUnrealMCPConsoleCommands> ConsoleCommands;
	TSharedPtr<FWarCardGameCommands> WarCardGameCommands;
};