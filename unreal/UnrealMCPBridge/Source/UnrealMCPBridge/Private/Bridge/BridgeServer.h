#pragma once

#include "CoreMinimal.h"

#include <atomic>

class FSocket;
class FTcpListener;
class FJsonObject;
class FJsonValue;
struct FIPv4Endpoint;

class FBridgeServer
{
public:
    FBridgeServer();
    ~FBridgeServer();

    bool Start();
    void Stop();
    bool IsRunning() const;

private:
    bool HandleAcceptedConnection(FSocket* ClientSocket, const FIPv4Endpoint& RemoteEndpoint);
    void HandleConnection(FSocket& ClientSocket);
    bool ReadExact(FSocket& ClientSocket, uint8* Destination, int32 BytesToRead) const;
    bool WriteExact(FSocket& ClientSocket, const uint8* Source, int32 BytesToWrite) const;
    bool BuildResponse(const FString& RequestJson, FString& OutResponseJson) const;
    TSharedRef<FJsonObject> BuildSuccessResponse(
        uint64 RequestId,
        uint32 ElapsedMs,
        const TArray<TSharedPtr<FJsonValue>>& Results) const;
    TSharedRef<FJsonObject> BuildFailureResponse(
        uint64 RequestId,
        uint32 ElapsedMs,
        const TArray<TSharedPtr<FJsonValue>>& Errors) const;
    TSharedRef<FJsonObject> BuildError(
        int32 CommandIndex,
        const FString& Code,
        const FString& Message) const;

private:
    TUniquePtr<FTcpListener> Listener;
    std::atomic_bool bRunning;
};
