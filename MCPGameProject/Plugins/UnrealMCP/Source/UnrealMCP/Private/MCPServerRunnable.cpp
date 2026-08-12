#include "MCPServerRunnable.h"
#include "EpicUnrealMCPBridge.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "JsonObjectConverter.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"

FMCPServerRunnable::FMCPServerRunnable(UEpicUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InListenerSocket)
    : Bridge(InBridge)
    , ListenerSocket(InListenerSocket)
    , bRunning(true)
{
    UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Created server runnable"));
}

FMCPServerRunnable::~FMCPServerRunnable()
{
    // Note: We don't delete the sockets here as they're owned by the bridge
}

bool FMCPServerRunnable::Init()
{
    return true;
}

uint32 FMCPServerRunnable::Run()
{
    UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Server thread starting..."));
    
    while (bRunning)
    {
        // UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Waiting for client connection..."));
        
        bool bPending = false;
        if (ListenerSocket->HasPendingConnection(bPending) && bPending)
        {
            UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Client connection pending, accepting..."));
            
            ClientSocket = MakeShareable(ListenerSocket->Accept(TEXT("MCPClient")));
            if (ClientSocket.IsValid())
            {
                UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Client connection accepted"));
                
                // Set socket options to improve connection stability
                ClientSocket->SetNoDelay(true);
                int32 SocketBufferSize = 65536;  // 64KB buffer
                ClientSocket->SetSendBufferSize(SocketBufferSize, SocketBufferSize);
                ClientSocket->SetReceiveBufferSize(SocketBufferSize, SocketBufferSize);
                
                uint8 Buffer[8192];

                // FIX-MCP-001: накопительный буфер запроса. Раньше каждый
                // Recv() (максимум 8191 байт) парсился как САМОСТОЯТЕЛЬНЫЙ
                // JSON-документ — любой запрос длиннее одного TCP-фрагмента
                // (material_set_node_param с крупным Custom HLSL-кодом,
                // import_datatable_from_csv с большой таблицей) ловил
                // MALFORMED_REQUEST_JSON на первом же фрагменте, отправлял его
                // клиенту как ОКОНЧАТЕЛЬНЫЙ ответ и никогда не дочитывал
                // остаток — команда не выполнялась вовсе, а диагностика про
                // NaN/Infinity уводила разбор в сторону. Теперь парсим ТОЛЬКО
                // накопленный целиком буфер; поштучный Recv() лишь пополняет
                // его.
                //
                // UTF8-конверсия — тоже только на полном накопленном буфере, а
                // не на сыром `Buffer` каждого фрагмента: многобайтовая
                // последовательность (кириллица в имени ноды, HLSL-код в
                // props) регулярно рвётся серединой на границе 8 КБ-чанка, и
                // `UTF8_TO_TCHAR` на оборванном хвосте портит текст независимо
                // от того, распарсился JSON или нет.
                TArray<uint8> RequestBytes;
                double RequestAssemblyStartSeconds = 0.0;
                // Хватает с большим запасом: request-полезная нагрузка на
                // порядки меньше самых больших ОТВЕТОВ (material_get_graph
                // отдаёт сотни КБ), а по localhost TCP она приходит за доли
                // секунды при наличии данных вообще. Долгая сборка означает
                // мусор на проводе, а не медленную сеть.
                constexpr double RequestAssemblyTimeoutSeconds = 30.0;
                constexpr int32 MaxRequestBytes = 64 * 1024 * 1024; // 64 MB safety cap

                while (bRunning)
                {
                    int32 BytesRead = 0;
                    if (ClientSocket->Recv(Buffer, sizeof(Buffer), BytesRead))
                    {
                        if (BytesRead == 0)
                        {
                            if (RequestBytes.Num() > 0)
                            {
                                UE_LOG(LogTemp, Warning,
                                    TEXT("MCPServerRunnable: client disconnected mid-request (%d bytes accumulated, never parsed as JSON)"),
                                    RequestBytes.Num());
                            }
                            UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Client disconnected (zero bytes)"));
                            break;
                        }

                        if (RequestBytes.Num() == 0)
                        {
                            RequestAssemblyStartSeconds = FPlatformTime::Seconds();
                        }
                        RequestBytes.Append(Buffer, BytesRead);
                        UE_LOG(LogTemp, Verbose, TEXT("MCPServerRunnable: Received %d bytes (%d accumulated)"),
                            BytesRead, RequestBytes.Num());

                        // Отправка ответа вынесена в лямбду, потому что отвечать
                        // обязаны ВСЕ три пути, а не только удачный. Раньше на
                        // нераспарсенный JSON и на запрос без поля `type` сервер
                        // молча писал Warning в лог и не слал ничего: клиент
                        // досиживал до собственного таймаута. Пока тот был 5 с,
                        // это выглядело мелочью; после согласования таймаутов с
                        // потолком моста цена молчания выросла до двух минут.
                        auto SendResponse = [this, &ClientSocketRef = ClientSocket](const FString& Response)
                        {
                            UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Command executed, response length: %d"), Response.Len());

                            // Log response for debugging (truncated for large responses)
                            FString LogResponse = Response.Len() > 200 ? Response.Left(200) + TEXT("...") : Response;
                            UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Sending response (%d bytes): %s"),
                                   Response.Len(), *LogResponse);

                            // Convert to UTF8 once
                            FTCHARToUTF8 UTF8Response(*Response);
                            const uint8* DataToSend = (const uint8*)UTF8Response.Get();
                            int32 TotalDataSize = UTF8Response.Length();
                            int32 TotalBytesSent = 0;
                            bool bSuccess = true;

                            // Сокет НЕблокирующий: при переполненном буфере
                            // отправки Send отдаёт SE_EWOULDBLOCK (или true с
                            // BytesSent == 0). Раньше это ломало цикл по break —
                            // ответ уходил обрезанным, клиент получал битый JSON
                            // и ждал до таймаута, снаружи неотличимо от висяка.
                            // Первой командой, штатно выдающей сотни КБ, стала
                            // material_get_graph, на ней это и всплывало.
                            // bRunning в условии обязателен: клиент, ушедший по
                            // своему таймауту, оставляет буфер вечно полным, и
                            // цикл честно досиживает все 30 с. Без этой проверки
                            // Stop() (закрытие редактора, выгрузка модуля) на
                            // такой отправке не действует — shutdown повисает на
                            // полминуты за каждый застрявший ответ.
                            const double SendDeadlineSeconds = FPlatformTime::Seconds() + 30.0;
                            while (TotalBytesSent < TotalDataSize && bRunning)
                            {
                                int32 BytesSent = 0;
                                const bool bSendResult = ClientSocketRef->Send(DataToSend + TotalBytesSent,
                                                                           TotalDataSize - TotalBytesSent,
                                                                           BytesSent);

                                if (!bSendResult || BytesSent <= 0)
                                {
                                    // Код ошибки осмыслен ТОЛЬКО когда Send отчитался
                                    // о провале. При bSendResult == true с BytesSent == 0
                                    // (полный буфер — штатное состояние на сотнях КБ)
                                    // GetLastErrorCode отдаёт остаток от прошлой
                                    // операции, обычно SE_NO_ERROR, и лог печатал
                                    // «Error code: 0», уводя разбор в сторону.
                                    const int32 LastError = bSendResult
                                        ? (int32)SE_NO_ERROR
                                        : (int32)ISocketSubsystem::Get()->GetLastErrorCode();
                                    const bool bTransient =
                                        (!bSendResult && (LastError == SE_EWOULDBLOCK || LastError == SE_EINTR))
                                        || (bSendResult && BytesSent == 0);

                                    if (bTransient && FPlatformTime::Seconds() < SendDeadlineSeconds)
                                    {
                                        // Буфер сокета полон — подождать и дослать,
                                        // а не бросать ответ на полуслове.
                                        FPlatformProcess::Sleep(0.005f);
                                        continue;
                                    }

                                    UE_LOG(LogTemp, Error, TEXT("MCPServerRunnable: Failed to send response after %d/%d bytes — Send %s, error code: %d"),
                                           TotalBytesSent, TotalDataSize,
                                           bSendResult ? TEXT("stalled (send buffer full, client likely gone)") : TEXT("failed"),
                                           LastError);
                                    bSuccess = false;
                                    break;
                                }

                                TotalBytesSent += BytesSent;
                            }

                            // Раньше здесь стояло `bSuccess && TotalBytesSent < TotalDataSize`,
                            // и блок был недостижим: цикл выходил либо по выполненному
                            // условию, либо через break, который уже снял bSuccess.
                            // Диагностика «клиент увидит битый JSON» не печаталась
                            // НИКОГДА — а именно она отличает «ответ не дошёл» от
                            // «ответ не отправлялся». Достижимо и при выходе по !bRunning.
                            if (TotalBytesSent < TotalDataSize)
                            {
                                UE_LOG(LogTemp, Error, TEXT("MCPServerRunnable: Response TRUNCATED (%d/%d bytes) — client will see malformed JSON"),
                                       TotalBytesSent, TotalDataSize);
                                bSuccess = false;
                            }

                            if (bSuccess)
                            {
                                UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Response sent successfully (%d bytes)"),
                                       TotalBytesSent);
                            }
                        };

                        // Ошибка протокола в том же конверте, что и всё остальное:
                        // {status:error, ok:false, error:{category,code,message}} —
                        // Python-обёртка разбирает верхнеуровневый `ok` первым.
                        auto SendProtocolError = [&SendResponse](const TCHAR* Code, const FString& Message)
                        {
                            TSharedPtr<FJsonObject> Error = MakeShareable(new FJsonObject);
                            Error->SetStringField(TEXT("category"), TEXT("user"));
                            Error->SetStringField(TEXT("code"), Code);
                            Error->SetStringField(TEXT("message"), Message);
                            Error->SetObjectField(TEXT("details"), MakeShared<FJsonObject>());

                            TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);
                            Json->SetStringField(TEXT("status"), TEXT("error"));
                            Json->SetBoolField(TEXT("ok"), false);
                            Json->SetObjectField(TEXT("error"), Error);

                            FString Out;
                            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
                            FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
                            SendResponse(Out);
                        };

                        if (RequestBytes.Num() > MaxRequestBytes)
                        {
                            UE_LOG(LogTemp, Error, TEXT("MCPServerRunnable: request exceeded %d MB without producing valid JSON — aborting"),
                                MaxRequestBytes / (1024 * 1024));
                            SendProtocolError(TEXT("REQUEST_TOO_LARGE"),
                                FString::Printf(TEXT("Request exceeded %d MB without ever parsing as valid JSON"),
                                    MaxRequestBytes / (1024 * 1024)));
                            RequestBytes.Reset();
                            break; // malformed/runaway client — do not keep serving this connection
                        }

                        // Конвертируем и пытаемся распарсить НАКОПЛЕННЫЙ буфер
                        // целиком — не фрагмент. На неполном сообщении Deserialize
                        // штатно отдаёт false (тот же признак, на который
                        // ориентируется Python-приёмник receive_full_response при
                        // JSONDecodeError) — это НЕ повод отвечать клиенту прямо
                        // сейчас, а повод дочитать ещё.
                        FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(RequestBytes.GetData()), RequestBytes.Num());
                        FString ReceivedText(Converter.Length(), Converter.Get());

                        TSharedPtr<FJsonObject> JsonObject;
                        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReceivedText);

                        if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
                        {
                            UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Parsed complete request (%d bytes)"), RequestBytes.Num());
                            RequestBytes.Reset(); // ready for a possible next message on this connection

                            // Get command type
                            FString CommandType;
                            if (JsonObject->TryGetStringField(TEXT("type"), CommandType))
                            {
                                UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Executing command: %s"), *CommandType);

                                const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
                                TSharedPtr<FJsonObject> Params =
                                    (JsonObject->TryGetObjectField(TEXT("params"), ParamsObj) && ParamsObj && (*ParamsObj).IsValid())
                                        ? *ParamsObj
                                        : MakeShared<FJsonObject>();

                                SendResponse(Bridge->ExecuteCommand(CommandType, Params));
                            }
                            else
                            {
                                UE_LOG(LogTemp, Warning, TEXT("MCPServerRunnable: Missing 'type' field in command"));
                                SendProtocolError(TEXT("MISSING_COMMAND_TYPE"),
                                    TEXT("Request JSON has no 'type' field naming the command."));
                            }
                        }
                        else if (FPlatformTime::Seconds() - RequestAssemblyStartSeconds > RequestAssemblyTimeoutSeconds)
                        {
                            // Собирали дольше разумного и всё ещё не целое
                            // сообщение — это уже не фрагментация большого
                            // запроса (та закрывается за миллисекунды по
                            // localhost), а мусор на проводе (NaN/Infinity,
                            // оборванное соединение сверху и т.п.). Отвечаем
                            // ОДИН раз на всё сообщение целиком, не на фрагмент.
                            UE_LOG(LogTemp, Warning,
                                TEXT("MCPServerRunnable: Failed to parse request as JSON after %d bytes / %.0fs: %s"),
                                RequestBytes.Num(), RequestAssemblyTimeoutSeconds, *ReceivedText);
                            SendProtocolError(TEXT("MALFORMED_REQUEST_JSON"),
                                FString::Printf(TEXT("Could not parse the request as JSON (%d bytes received over %.0fs). ")
                                                TEXT("Note that NaN/Infinity are not valid JSON."),
                                                RequestBytes.Num(), RequestAssemblyTimeoutSeconds));
                            RequestBytes.Reset();
                        }
                        // else: возможно, просто ещё не всё пришло — читаем дальше,
                        // ничего не отправляя (см. комментарий над циклом).
                    }
                    else
                    {
                        int32 LastError = (int32)ISocketSubsystem::Get()->GetLastErrorCode();
                        // Don't break the connection for WouldBlock error, which is normal for non-blocking sockets
                        bool bShouldBreak = true;
                        
                        // Check for "would block" error which isn't a real error for non-blocking sockets
                        if (LastError == SE_EWOULDBLOCK) 
                        {
                            UE_LOG(LogTemp, Verbose, TEXT("MCPServerRunnable: Socket would block, continuing..."));
                            bShouldBreak = false;
                            // Small sleep to prevent tight loop when no data
                            FPlatformProcess::Sleep(0.01f);
                        }
                        // Check for other transient errors we might want to tolerate
                        else if (LastError == SE_EINTR) // Interrupted system call
                        {
                            UE_LOG(LogTemp, Warning, TEXT("MCPServerRunnable: Socket read interrupted, continuing..."));
                            bShouldBreak = false;
                        }
                        else 
                        {
                            UE_LOG(LogTemp, Warning, TEXT("MCPServerRunnable: Client disconnected or error. Last error code: %d"), LastError);
                        }
                        
                        if (bShouldBreak)
                        {
                            break;
                        }
                    }
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("MCPServerRunnable: Failed to accept client connection"));
            }
        }
        
        // Small sleep to prevent tight loop
        FPlatformProcess::Sleep(0.1f);
    }
    
    UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Server thread stopping"));
    return 0;
}

void FMCPServerRunnable::Stop()
{
    bRunning = false;
}

void FMCPServerRunnable::Exit()
{
}
