#include "Bridge/BridgeServer.h"

#include "Common/TcpListener.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Misc/EngineVersion.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCPBridgeServer, Log, All);

namespace
{
constexpr uint16 ProtocolVersion = 1;
constexpr uint16 BridgePort = 55557;
constexpr uint32 MaxFrameBytes = 16 * 1024 * 1024;
constexpr double SocketWaitSeconds = 5.0;
const TCHAR* BridgeVersion = TEXT("0.1.0");

uint32 ReadBigEndianUint32(const uint8* Bytes)
{
    return (static_cast<uint32>(Bytes[0]) << 24) |
        (static_cast<uint32>(Bytes[1]) << 16) |
        (static_cast<uint32>(Bytes[2]) << 8) |
        static_cast<uint32>(Bytes[3]);
}

void WriteBigEndianUint32(uint32 Value, uint8* OutBytes)
{
    OutBytes[0] = static_cast<uint8>((Value >> 24) & 0xFF);
    OutBytes[1] = static_cast<uint8>((Value >> 16) & 0xFF);
    OutBytes[2] = static_cast<uint8>((Value >> 8) & 0xFF);
    OutBytes[3] = static_cast<uint8>(Value & 0xFF);
}

bool SerializeJsonObject(const TSharedRef<FJsonObject>& Object, FString& OutJson)
{
    OutJson.Reset();
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    return FJsonSerializer::Serialize(Object, Writer);
}

TSharedPtr<FJsonValue> MakeStringValue(const FString& Value)
{
    return MakeShared<FJsonValueString>(Value);
}

TSharedPtr<FJsonValue> MakeObjectValue(const TSharedRef<FJsonObject>& Value)
{
    return MakeShared<FJsonValueObject>(Value);
}
}

FBridgeServer::FBridgeServer()
    : bRunning(false)
{
}

FBridgeServer::~FBridgeServer()
{
    Stop();
}

bool FBridgeServer::Start()
{
    if (bRunning)
    {
        return true;
    }

    const FIPv4Endpoint Endpoint(FIPv4Address(127, 0, 0, 1), BridgePort);
    Listener = MakeUnique<FTcpListener>(Endpoint, FTimespan::FromMilliseconds(10));
    Listener->OnConnectionAccepted().BindRaw(this, &FBridgeServer::HandleAcceptedConnection);

    if (!Listener->IsActive())
    {
        UE_LOG(
            LogUnrealMCPBridgeServer,
            Error,
            TEXT("Failed to start Unreal MCP bridge on %s."),
            *Endpoint.ToString());
        Listener.Reset();
        return false;
    }

    bRunning = true;
    UE_LOG(LogUnrealMCPBridgeServer, Display, TEXT("Unreal MCP bridge listening on %s."), *Endpoint.ToString());
    return true;
}

void FBridgeServer::Stop()
{
    if (!bRunning)
    {
        return;
    }

    bRunning = false;

    if (Listener)
    {
        Listener->Stop();
        Listener.Reset();
    }

    UE_LOG(LogUnrealMCPBridgeServer, Display, TEXT("Unreal MCP bridge stopped."));
}

bool FBridgeServer::IsRunning() const
{
    return bRunning;
}

bool FBridgeServer::HandleAcceptedConnection(FSocket* ClientSocket, const FIPv4Endpoint& RemoteEndpoint)
{
    if (ClientSocket == nullptr)
    {
        return false;
    }

    UE_LOG(LogUnrealMCPBridgeServer, Verbose, TEXT("Accepted bridge connection from %s."), *RemoteEndpoint.ToString());
    HandleConnection(*ClientSocket);
    ClientSocket->Close();
    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
    return true;
}

void FBridgeServer::HandleConnection(FSocket& ClientSocket)
{
    uint8 LengthBytes[4] = {0, 0, 0, 0};
    if (!ReadExact(ClientSocket, LengthBytes, UE_ARRAY_COUNT(LengthBytes)))
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge request ended before the frame length was read."));
        return;
    }

    const uint32 FrameLength = ReadBigEndianUint32(LengthBytes);
    if (FrameLength > MaxFrameBytes)
    {
        UE_LOG(
            LogUnrealMCPBridgeServer,
            Warning,
            TEXT("Bridge request frame too large: %u bytes exceeds %u bytes."),
            FrameLength,
            MaxFrameBytes);
        return;
    }

    TArray<uint8> RequestBody;
    RequestBody.SetNumUninitialized(static_cast<int32>(FrameLength) + 1);
    if (FrameLength > 0 && !ReadExact(ClientSocket, RequestBody.GetData(), static_cast<int32>(FrameLength)))
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge request ended before the JSON body was read."));
        return;
    }
    RequestBody[FrameLength] = 0;

    const FString RequestJson = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(RequestBody.GetData()));
    FString ResponseJson;
    if (!BuildResponse(RequestJson, ResponseJson))
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge request could not be parsed as a protocol envelope."));
        return;
    }

    const FTCHARToUTF8 ResponseUtf8(*ResponseJson);
    const int32 ResponseLength = ResponseUtf8.Length();
    if (ResponseLength < 0 || static_cast<uint32>(ResponseLength) > MaxFrameBytes)
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge response frame exceeded the frame limit."));
        return;
    }

    uint8 ResponseLengthBytes[4] = {0, 0, 0, 0};
    WriteBigEndianUint32(static_cast<uint32>(ResponseLength), ResponseLengthBytes);
    if (!WriteExact(ClientSocket, ResponseLengthBytes, UE_ARRAY_COUNT(ResponseLengthBytes)))
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge response length could not be written."));
        return;
    }

    if (ResponseLength > 0 &&
        !WriteExact(ClientSocket, reinterpret_cast<const uint8*>(ResponseUtf8.Get()), ResponseLength))
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge response JSON body could not be written."));
    }
}

bool FBridgeServer::ReadExact(FSocket& ClientSocket, uint8* Destination, int32 BytesToRead) const
{
    int32 TotalBytesRead = 0;
    while (TotalBytesRead < BytesToRead && bRunning)
    {
        if (!ClientSocket.Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(SocketWaitSeconds)))
        {
            return false;
        }

        int32 BytesRead = 0;
        if (!ClientSocket.Recv(Destination + TotalBytesRead, BytesToRead - TotalBytesRead, BytesRead) || BytesRead <= 0)
        {
            return false;
        }

        TotalBytesRead += BytesRead;
    }

    return TotalBytesRead == BytesToRead;
}

bool FBridgeServer::WriteExact(FSocket& ClientSocket, const uint8* Source, int32 BytesToWrite) const
{
    int32 TotalBytesWritten = 0;
    while (TotalBytesWritten < BytesToWrite && bRunning)
    {
        if (!ClientSocket.Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromSeconds(SocketWaitSeconds)))
        {
            return false;
        }

        int32 BytesWritten = 0;
        if (!ClientSocket.Send(Source + TotalBytesWritten, BytesToWrite - TotalBytesWritten, BytesWritten) ||
            BytesWritten <= 0)
        {
            return false;
        }

        TotalBytesWritten += BytesWritten;
    }

    return TotalBytesWritten == BytesToWrite;
}

bool FBridgeServer::BuildResponse(const FString& RequestJson, FString& OutResponseJson) const
{
    const double StartSeconds = FPlatformTime::Seconds();
    TSharedPtr<FJsonObject> Request;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        return false;
    }

    uint64 RequestId = 0;
    if (!Request->TryGetNumberField(TEXT("request_id"), RequestId))
    {
        return false;
    }

    auto ElapsedMs = [&StartSeconds]() -> uint32 {
        return static_cast<uint32>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
    };

    uint32 RequestProtocolVersion = 0;
    if (!Request->TryGetNumberField(TEXT("protocol_version"), RequestProtocolVersion) ||
        RequestProtocolVersion != ProtocolVersion)
    {
        TArray<TSharedPtr<FJsonValue>> Errors;
        Errors.Add(MakeObjectValue(BuildError(
            0,
            TEXT("unsupported_protocol_version"),
            FString::Printf(
                TEXT("unsupported protocol version %u, expected %u"),
                RequestProtocolVersion,
                ProtocolVersion))));
        return SerializeJsonObject(BuildFailureResponse(RequestId, ElapsedMs(), Errors), OutResponseJson);
    }

    const TArray<TSharedPtr<FJsonValue>>* Commands = nullptr;
    if (!Request->TryGetArrayField(TEXT("commands"), Commands))
    {
        TArray<TSharedPtr<FJsonValue>> Errors;
        Errors.Add(MakeObjectValue(BuildError(0, TEXT("missing_commands"), TEXT("request envelope missing commands array"))));
        return SerializeJsonObject(BuildFailureResponse(RequestId, ElapsedMs(), Errors), OutResponseJson);
    }

    FString ErrorMode = TEXT("stop");
    Request->TryGetStringField(TEXT("error_mode"), ErrorMode);
    const bool bContinueOnError = ErrorMode.Equals(TEXT("continue"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Errors;
    for (int32 CommandIndex = 0; CommandIndex < Commands->Num(); ++CommandIndex)
    {
        const TSharedPtr<FJsonValue>& CommandValue = (*Commands)[CommandIndex];
        const TSharedPtr<FJsonObject>* CommandObject = nullptr;
        if (!CommandValue.IsValid() || !CommandValue->TryGetObject(CommandObject) || CommandObject == nullptr ||
            !CommandObject->IsValid())
        {
            Errors.Add(MakeObjectValue(BuildError(
                CommandIndex,
                TEXT("invalid_command"),
                TEXT("command entry must be a JSON object"))));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        FString CommandType;
        if (!(*CommandObject)->TryGetStringField(TEXT("type"), CommandType))
        {
            Errors.Add(MakeObjectValue(BuildError(
                CommandIndex,
                TEXT("missing_command_type"),
                TEXT("command object missing type field"))));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        if (CommandType == TEXT("ping"))
        {
            TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetStringField(TEXT("bridge_version"), BridgeVersion);

            TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("type"), TEXT("pong"));
            Result->SetObjectField(TEXT("data"), Data);
            Results.Add(MakeObjectValue(Result));
        }
        else if (CommandType == TEXT("status"))
        {
            TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetBoolField(TEXT("connected"), bRunning);
            Data->SetStringField(TEXT("bridge_version"), BridgeVersion);
            Data->SetStringField(TEXT("unreal_version"), FEngineVersion::Current().ToString());

            TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("type"), TEXT("status"));
            Result->SetObjectField(TEXT("data"), Data);
            Results.Add(MakeObjectValue(Result));
        }
        else if (CommandType == TEXT("capabilities"))
        {
            TArray<TSharedPtr<FJsonValue>> CommandNames;
            CommandNames.Add(MakeStringValue(TEXT("connection.ping")));
            CommandNames.Add(MakeStringValue(TEXT("connection.status")));
            CommandNames.Add(MakeStringValue(TEXT("connection.capabilities")));

            TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetArrayField(TEXT("commands"), CommandNames);

            TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("type"), TEXT("capabilities"));
            Result->SetObjectField(TEXT("data"), Data);
            Results.Add(MakeObjectValue(Result));
        }
        else
        {
            Errors.Add(MakeObjectValue(BuildError(
                CommandIndex,
                TEXT("unknown_command"),
                FString::Printf(TEXT("unknown command type '%s'"), *CommandType))));
            if (!bContinueOnError)
            {
                break;
            }
        }
    }

    if (!Errors.IsEmpty())
    {
        return SerializeJsonObject(BuildFailureResponse(RequestId, ElapsedMs(), Errors), OutResponseJson);
    }

    return SerializeJsonObject(BuildSuccessResponse(RequestId, ElapsedMs(), Results), OutResponseJson);
}

TSharedRef<FJsonObject> FBridgeServer::BuildSuccessResponse(
    uint64 RequestId,
    uint32 ElapsedMs,
    const TArray<TSharedPtr<FJsonValue>>& Results) const
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetNumberField(TEXT("protocol_version"), ProtocolVersion);
    Response->SetNumberField(TEXT("request_id"), static_cast<double>(RequestId));
    Response->SetBoolField(TEXT("ok"), true);
    Response->SetNumberField(TEXT("elapsed_ms"), ElapsedMs);
    Response->SetArrayField(TEXT("results"), Results);
    Response->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
    return Response;
}

TSharedRef<FJsonObject> FBridgeServer::BuildFailureResponse(
    uint64 RequestId,
    uint32 ElapsedMs,
    const TArray<TSharedPtr<FJsonValue>>& Errors) const
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetNumberField(TEXT("protocol_version"), ProtocolVersion);
    Response->SetNumberField(TEXT("request_id"), static_cast<double>(RequestId));
    Response->SetBoolField(TEXT("ok"), false);
    Response->SetNumberField(TEXT("elapsed_ms"), ElapsedMs);
    Response->SetArrayField(TEXT("results"), TArray<TSharedPtr<FJsonValue>>());
    Response->SetArrayField(TEXT("errors"), Errors);
    return Response;
}

TSharedRef<FJsonObject> FBridgeServer::BuildError(int32 CommandIndex, const FString& Code, const FString& Message) const
{
    TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetNumberField(TEXT("command_index"), CommandIndex);
    Error->SetField(TEXT("item_index"), MakeShared<FJsonValueNull>());
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);
    return Error;
}
