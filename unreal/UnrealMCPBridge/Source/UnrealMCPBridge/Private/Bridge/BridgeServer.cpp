#include "Bridge/BridgeServer.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCPBridgeServer, Log, All);

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
    bRunning = true;
    UE_LOG(LogUnrealMCPBridgeServer, Display, TEXT("Bridge server marked running on localhost."));
    return bRunning;
}

void FBridgeServer::Stop()
{
    if (!bRunning)
    {
        return;
    }

    bRunning = false;
    UE_LOG(LogUnrealMCPBridgeServer, Display, TEXT("Bridge server stopped."));
}

bool FBridgeServer::IsRunning() const
{
    return bRunning;
}
