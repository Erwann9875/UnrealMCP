#include "Bridge/BridgeServer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Common/TcpListener.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "ScopedTransaction.h"

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

TSharedPtr<FJsonValue> MakeNumberValue(double Value)
{
    return MakeShared<FJsonValueNumber>(Value);
}

TSharedRef<FJsonObject> MakeTaggedResult(const FString& Type, const TSharedRef<FJsonObject>& Data)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("type"), Type);
    Result->SetObjectField(TEXT("data"), Data);
    return Result;
}

TArray<TSharedPtr<FJsonValue>> MakeVectorArray(double X, double Y, double Z)
{
    return {
        MakeNumberValue(X),
        MakeNumberValue(Y),
        MakeNumberValue(Z),
    };
}

bool ReadVectorField(
    const TSharedPtr<FJsonObject>& Object,
    const FString& FieldName,
    const FVector& DefaultValue,
    FVector& OutValue)
{
    OutValue = DefaultValue;
    if (!Object.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(FieldName, Values))
    {
        return true;
    }

    if (Values->Num() != 3)
    {
        return false;
    }

    OutValue = FVector(
        (*Values)[0]->AsNumber(),
        (*Values)[1]->AsNumber(),
        (*Values)[2]->AsNumber());
    return true;
}

bool ReadRotatorField(
    const TSharedPtr<FJsonObject>& Object,
    const FString& FieldName,
    const FRotator& DefaultValue,
    FRotator& OutValue)
{
    FVector RotationVector;
    if (!ReadVectorField(Object, FieldName, FVector(DefaultValue.Pitch, DefaultValue.Yaw, DefaultValue.Roll), RotationVector))
    {
        return false;
    }

    OutValue = FRotator(RotationVector.X, RotationVector.Y, RotationVector.Z);
    return true;
}

TArray<FString> ReadStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
{
    TArray<FString> Strings;
    if (!Object.IsValid())
    {
        return Strings;
    }

    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(FieldName, Values))
    {
        return Strings;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        if (Value.IsValid())
        {
            Strings.Add(Value->AsString());
        }
    }

    return Strings;
}

bool TryGetCommandData(const TSharedPtr<FJsonObject>& CommandObject, TSharedPtr<FJsonObject>& OutData)
{
    const TSharedPtr<FJsonObject>* Data = nullptr;
    if (CommandObject.IsValid() && CommandObject->TryGetObjectField(TEXT("data"), Data) && Data != nullptr)
    {
        OutData = *Data;
        return OutData.IsValid();
    }

    OutData = MakeShared<FJsonObject>();
    return true;
}

UWorld* GetEditorWorld()
{
    return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

bool ActorMatches(const AActor& Actor, const TArray<FString>& Names, const TArray<FString>& Tags)
{
    bool bMatchesName = Names.IsEmpty();
    for (const FString& Name : Names)
    {
        if (Actor.GetName() == Name || Actor.GetActorLabel() == Name)
        {
            bMatchesName = true;
            break;
        }
    }

    bool bMatchesTag = Tags.IsEmpty();
    for (const FString& Tag : Tags)
    {
        if (Actor.ActorHasTag(FName(*Tag)))
        {
            bMatchesTag = true;
            break;
        }
    }

    return bMatchesName && bMatchesTag;
}

TSharedRef<FJsonObject> ActorToJson(const AActor& Actor)
{
    const FVector Location = Actor.GetActorLocation();
    const FRotator Rotation = Actor.GetActorRotation();
    const FVector Scale = Actor.GetActorScale3D();

    TArray<TSharedPtr<FJsonValue>> Tags;
    for (const FName& Tag : Actor.Tags)
    {
        Tags.Add(MakeStringValue(Tag.ToString()));
    }

    TSharedRef<FJsonObject> Transform = MakeShared<FJsonObject>();
    Transform->SetArrayField(TEXT("location"), MakeVectorArray(Location.X, Location.Y, Location.Z));
    Transform->SetArrayField(TEXT("rotation"), MakeVectorArray(Rotation.Pitch, Rotation.Yaw, Rotation.Roll));
    Transform->SetArrayField(TEXT("scale"), MakeVectorArray(Scale.X, Scale.Y, Scale.Z));

    TSharedRef<FJsonObject> ActorJson = MakeShared<FJsonObject>();
    ActorJson->SetStringField(TEXT("name"), Actor.GetActorLabel());
    ActorJson->SetStringField(TEXT("path"), Actor.GetPathName());
    ActorJson->SetStringField(TEXT("class_name"), Actor.GetClass()->GetName());
    ActorJson->SetObjectField(TEXT("transform"), Transform);
    ActorJson->SetArrayField(TEXT("tags"), Tags);
    return ActorJson;
}

TSharedRef<FJsonObject> QueryWorldActors(
    UWorld& World,
    const TArray<FString>& Names,
    TArray<FString> Tags,
    bool bIncludeGenerated,
    int32 Limit)
{
    if (bIncludeGenerated && Tags.IsEmpty())
    {
        Tags.Add(TEXT("mcp.generated"));
    }

    TArray<TSharedPtr<FJsonValue>> Actors;
    int32 TotalCount = 0;
    const int32 EffectiveLimit = Limit > 0 ? Limit : 500;

    for (TActorIterator<AActor> It(&World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor) || !ActorMatches(*Actor, Names, Tags))
        {
            continue;
        }

        ++TotalCount;
        if (Actors.Num() < EffectiveLimit)
        {
            Actors.Add(MakeObjectValue(ActorToJson(*Actor)));
        }
    }

    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetArrayField(TEXT("actors"), Actors);
    Query->SetNumberField(TEXT("total_count"), TotalCount);
    return Query;
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
    if (bRunning.load(std::memory_order_acquire))
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

    bRunning.store(true, std::memory_order_release);
    UE_LOG(LogUnrealMCPBridgeServer, Display, TEXT("Unreal MCP bridge listening on %s."), *Endpoint.ToString());
    return true;
}

void FBridgeServer::Stop()
{
    if (!bRunning.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }

    if (Listener)
    {
        Listener->Stop();
        Listener.Reset();
    }

    UE_LOG(LogUnrealMCPBridgeServer, Display, TEXT("Unreal MCP bridge stopped."));
}

bool FBridgeServer::IsRunning() const
{
    return bRunning.load(std::memory_order_acquire);
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
    while (TotalBytesRead < BytesToRead && bRunning.load(std::memory_order_acquire))
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
    while (TotalBytesWritten < BytesToWrite && bRunning.load(std::memory_order_acquire))
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

        TSharedPtr<FJsonObject> CommandError;
        TSharedPtr<FJsonObject> CommandResult =
            ExecuteCommandOnGameThread(CommandType, *CommandObject, CommandIndex, CommandError);
        if (CommandResult.IsValid())
        {
            Results.Add(MakeObjectValue(CommandResult.ToSharedRef()));
        }
        else
        {
            if (!CommandError.IsValid())
            {
                CommandError = BuildError(
                    CommandIndex,
                    TEXT("unknown_command"),
                    FString::Printf(TEXT("unknown command type '%s'"), *CommandType));
            }

            Errors.Add(MakeObjectValue(CommandError.ToSharedRef()));
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

TSharedPtr<FJsonObject> FBridgeServer::ExecuteCommandOnGameThread(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& CommandObject,
    int32 CommandIndex,
    TSharedPtr<FJsonObject>& OutError) const
{
    if (IsInGameThread())
    {
        return ExecuteCommand(CommandType, CommandObject, CommandIndex, OutError);
    }

    TSharedPtr<FJsonObject> Result;
    FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
    AsyncTask(ENamedThreads::GameThread, [this, CommandType, CommandObject, CommandIndex, &Result, &OutError, DoneEvent]() {
        Result = ExecuteCommand(CommandType, CommandObject, CommandIndex, OutError);
        DoneEvent->Trigger();
    });
    DoneEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
    return Result;
}

TSharedPtr<FJsonObject> FBridgeServer::ExecuteCommand(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& CommandObject,
    int32 CommandIndex,
    TSharedPtr<FJsonObject>& OutError) const
{
    TSharedPtr<FJsonObject> Data;
    TryGetCommandData(CommandObject, Data);

    if (CommandType == TEXT("ping"))
    {
        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetStringField(TEXT("bridge_version"), BridgeVersion);
        return MakeTaggedResult(TEXT("pong"), ResultData);
    }

    if (CommandType == TEXT("status"))
    {
        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetBoolField(TEXT("connected"), bRunning.load(std::memory_order_acquire));
        ResultData->SetStringField(TEXT("bridge_version"), BridgeVersion);
        ResultData->SetStringField(TEXT("unreal_version"), FEngineVersion::Current().ToString());
        return MakeTaggedResult(TEXT("status"), ResultData);
    }

    if (CommandType == TEXT("capabilities"))
    {
        TArray<TSharedPtr<FJsonValue>> CommandNames;
        CommandNames.Add(MakeStringValue(TEXT("connection.ping")));
        CommandNames.Add(MakeStringValue(TEXT("connection.status")));
        CommandNames.Add(MakeStringValue(TEXT("connection.capabilities")));
        CommandNames.Add(MakeStringValue(TEXT("level.create")));
        CommandNames.Add(MakeStringValue(TEXT("level.open")));
        CommandNames.Add(MakeStringValue(TEXT("level.save")));
        CommandNames.Add(MakeStringValue(TEXT("level.list")));
        CommandNames.Add(MakeStringValue(TEXT("world.bulk_spawn")));
        CommandNames.Add(MakeStringValue(TEXT("world.bulk_delete")));
        CommandNames.Add(MakeStringValue(TEXT("world.query")));
        CommandNames.Add(MakeStringValue(TEXT("world.snapshot")));

        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetArrayField(TEXT("commands"), CommandNames);
        return MakeTaggedResult(TEXT("capabilities"), ResultData);
    }

    if (CommandType == TEXT("level_list"))
    {
        TArray<TSharedPtr<FJsonValue>> Levels;
        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        TArray<FAssetData> Assets;
        AssetRegistryModule.Get().GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), Assets);

        for (const FAssetData& Asset : Assets)
        {
            TSharedRef<FJsonObject> Level = MakeShared<FJsonObject>();
            Level->SetStringField(TEXT("path"), Asset.PackageName.ToString());
            Level->SetStringField(TEXT("name"), Asset.AssetName.ToString());
            Levels.Add(MakeObjectValue(Level));
        }

        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetArrayField(TEXT("levels"), Levels);
        return MakeTaggedResult(TEXT("level_list"), ResultData);
    }

    if (CommandType == TEXT("level_create"))
    {
        FString Path;
        const bool bHasPath = Data->TryGetStringField(TEXT("path"), Path);
        if (!bHasPath || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("level.create requires data.path"));
            return nullptr;
        }

        bool bOpen = true;
        bool bSave = false;
        Data->TryGetBoolField(TEXT("open"), bOpen);
        Data->TryGetBoolField(TEXT("save"), bSave);

        UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to create blank map"));
            return nullptr;
        }

        const bool bSaved = bSave ? UEditorLoadingAndSavingUtils::SaveMap(World, Path) : false;
        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetStringField(TEXT("path"), Path);
        ResultData->SetBoolField(TEXT("opened"), bOpen);
        ResultData->SetBoolField(TEXT("saved"), bSaved);
        return MakeTaggedResult(TEXT("level_operation"), ResultData);
    }

    if (CommandType == TEXT("level_open"))
    {
        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("level.open requires data.path"));
            return nullptr;
        }

        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Path);
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to open level '%s'"), *Path));
            return nullptr;
        }

        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetStringField(TEXT("path"), Path);
        ResultData->SetBoolField(TEXT("opened"), true);
        ResultData->SetBoolField(TEXT("saved"), false);
        return MakeTaggedResult(TEXT("level_operation"), ResultData);
    }

    if (CommandType == TEXT("level_save"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            Path = World->GetOutermost()->GetName();
        }

        const bool bSaved = UEditorLoadingAndSavingUtils::SaveMap(World, Path);
        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetStringField(TEXT("path"), Path);
        ResultData->SetBoolField(TEXT("opened"), true);
        ResultData->SetBoolField(TEXT("saved"), bSaved);
        return MakeTaggedResult(TEXT("level_operation"), ResultData);
    }

    if (CommandType == TEXT("world_bulk_spawn"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
        if (!Data->TryGetArrayField(TEXT("actors"), Actors))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("world.bulk_spawn requires data.actors"));
            return nullptr;
        }

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "BulkSpawnActors", "MCP Bulk Spawn Actors"));
        TArray<TSharedPtr<FJsonValue>> Spawned;
        for (const TSharedPtr<FJsonValue>& ActorValue : *Actors)
        {
            const TSharedPtr<FJsonObject>* ActorSpec = nullptr;
            if (!ActorValue.IsValid() || !ActorValue->TryGetObject(ActorSpec) || ActorSpec == nullptr || !ActorSpec->IsValid())
            {
                continue;
            }

            FString Name;
            FString MeshPath;
            (*ActorSpec)->TryGetStringField(TEXT("name"), Name);
            (*ActorSpec)->TryGetStringField(TEXT("mesh"), MeshPath);
            if (Name.IsEmpty() || MeshPath.IsEmpty())
            {
                continue;
            }

            TSharedPtr<FJsonObject> TransformSpec = *ActorSpec;
            const TSharedPtr<FJsonObject>* NestedTransform = nullptr;
            if ((*ActorSpec)->TryGetObjectField(TEXT("transform"), NestedTransform) && NestedTransform != nullptr &&
                NestedTransform->IsValid())
            {
                TransformSpec = *NestedTransform;
            }

            FVector Location = FVector::ZeroVector;
            FVector Scale = FVector(1.0, 1.0, 1.0);
            FRotator Rotation = FRotator::ZeroRotator;
            if (!ReadVectorField(TransformSpec, TEXT("location"), FVector::ZeroVector, Location) ||
                !ReadRotatorField(TransformSpec, TEXT("rotation"), FRotator::ZeroRotator, Rotation) ||
                !ReadVectorField(TransformSpec, TEXT("scale"), FVector(1.0, 1.0, 1.0), Scale))
            {
                continue;
            }

            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
            if (!Mesh)
            {
                continue;
            }

            FActorSpawnParameters SpawnParameters;
            SpawnParameters.Name = MakeUniqueObjectName(World, AStaticMeshActor::StaticClass(), FName(*Name));
            AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, Rotation, SpawnParameters);
            if (!Actor)
            {
                continue;
            }

            Actor->Modify();
            Actor->SetActorLabel(Name);
            Actor->SetActorScale3D(Scale);
            Actor->Tags.AddUnique(TEXT("mcp.generated"));

            FString Scene;
            if ((*ActorSpec)->TryGetStringField(TEXT("scene"), Scene) && !Scene.IsEmpty())
            {
                Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.scene:%s"), *Scene)));
            }

            FString Group;
            if ((*ActorSpec)->TryGetStringField(TEXT("group"), Group) && !Group.IsEmpty())
            {
                Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.group:%s"), *Group)));
            }

            UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
            if (MeshComponent)
            {
                MeshComponent->SetStaticMesh(Mesh);
                MeshComponent->SetMobility(EComponentMobility::Static);
            }

            TSharedRef<FJsonObject> SpawnedActor = MakeShared<FJsonObject>();
            SpawnedActor->SetStringField(TEXT("name"), Actor->GetActorLabel());
            SpawnedActor->SetStringField(TEXT("path"), Actor->GetPathName());
            Spawned.Add(MakeObjectValue(SpawnedActor));
        }

        World->MarkPackageDirty();

        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetArrayField(TEXT("spawned"), Spawned);
        ResultData->SetNumberField(TEXT("count"), Spawned.Num());
        return MakeTaggedResult(TEXT("world_bulk_spawn"), ResultData);
    }

    if (CommandType == TEXT("world_bulk_delete"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        const TArray<FString> Names = ReadStringArrayField(Data, TEXT("names"));
        const TArray<FString> Tags = ReadStringArrayField(Data, TEXT("tags"));
        if (Names.IsEmpty() && Tags.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("world.bulk_delete requires names or tags"));
            return nullptr;
        }

        TArray<AActor*> ActorsToDelete;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (IsValid(Actor) && ActorMatches(*Actor, Names, Tags))
            {
                ActorsToDelete.Add(Actor);
            }
        }

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "BulkDeleteActors", "MCP Bulk Delete Actors"));
        TArray<TSharedPtr<FJsonValue>> Deleted;
        for (AActor* Actor : ActorsToDelete)
        {
            Deleted.Add(MakeStringValue(Actor->GetActorLabel()));
            World->EditorDestroyActor(Actor, true);
        }

        World->MarkPackageDirty();

        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetArrayField(TEXT("deleted"), Deleted);
        ResultData->SetNumberField(TEXT("count"), Deleted.Num());
        return MakeTaggedResult(TEXT("world_bulk_delete"), ResultData);
    }

    if (CommandType == TEXT("world_query"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        TArray<FString> Names = ReadStringArrayField(Data, TEXT("names"));
        TArray<FString> Tags = ReadStringArrayField(Data, TEXT("tags"));
        bool bIncludeGenerated = false;
        Data->TryGetBoolField(TEXT("include_generated"), bIncludeGenerated);
        int32 Limit = 500;
        Data->TryGetNumberField(TEXT("limit"), Limit);
        return MakeTaggedResult(TEXT("world_query"), QueryWorldActors(*World, Names, Tags, bIncludeGenerated, Limit));
    }

    if (CommandType == TEXT("world_snapshot"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        TArray<FString> Tags = ReadStringArrayField(Data, TEXT("tags"));
        TSharedRef<FJsonObject> Query = QueryWorldActors(*World, TArray<FString>(), Tags, Tags.IsEmpty(), 100000);

        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            Path = FPaths::ProjectSavedDir() / TEXT("UnrealMCP/snapshots/world_snapshot.json");
        }

        FString SnapshotJson;
        SerializeJsonObject(Query, SnapshotJson);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        if (!FFileHelper::SaveStringToFile(SnapshotJson, *Path))
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to write snapshot '%s'"), *Path));
            return nullptr;
        }

        int32 TotalCount = 0;
        Query->TryGetNumberField(TEXT("total_count"), TotalCount);
        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetStringField(TEXT("path"), Path);
        ResultData->SetNumberField(TEXT("total_count"), TotalCount);
        return MakeTaggedResult(TEXT("world_snapshot"), ResultData);
    }

    return nullptr;
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
