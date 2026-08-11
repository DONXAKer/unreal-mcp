#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"
#include "Interfaces/IPv4/IPv4Address.h"

class UEpicUnrealMCPBridge;

/**
 * Runnable class for the MCP server thread
 */
class FMCPServerRunnable : public FRunnable
{
public:
	FMCPServerRunnable(UEpicUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InListenerSocket);
	virtual ~FMCPServerRunnable();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	// HandleClientConnection/ProcessMessage удалены: вызывающих не было ни в
	// одном плагине, а внутри жила вторая, необновлённая копия цикла отправки —
	// без ретраев, без дедлайна и с `return` посреди ответа. Правки приёмки и
	// отправки применялись только к Run(), поэтому мёртвая ветка расходилась с
	// живой и служила ловушкой: починить в ней — не починить ничего.

private:
	UEpicUnrealMCPBridge* Bridge;
	TSharedPtr<FSocket> ListenerSocket;
	TSharedPtr<FSocket> ClientSocket;
	bool bRunning;
}; 