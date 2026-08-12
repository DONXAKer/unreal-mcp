#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include <atomic>

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

	// Stop() (game thread, via UEpicUnrealMCPBridge::StopServer) writes this
	// while Run() (socket thread) reads it in a tight polling loop — needs to
	// be atomic, not a plain bool. A comment used to claim this "handles" the
	// interruption; a plain bool gives no such guarantee across threads
	// without a fence, only the appearance of one.
	std::atomic<bool> bRunning;
};