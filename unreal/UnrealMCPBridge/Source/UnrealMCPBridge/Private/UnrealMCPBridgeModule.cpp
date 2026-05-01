#include "UnrealMCPBridgeModule.h"

#include "Bridge/BridgeServer.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCPBridge, Log, All);

void FUnrealMCPBridgeModule::StartupModule()
{
    BridgeServer = MakeUnique<FBridgeServer>();
    BridgeServer->Start();
    UE_LOG(LogUnrealMCPBridge, Display, TEXT("Unreal MCP Bridge module started."));
}

void FUnrealMCPBridgeModule::ShutdownModule()
{
    if (BridgeServer)
    {
        BridgeServer->Stop();
        BridgeServer.Reset();
    }
    UE_LOG(LogUnrealMCPBridge, Display, TEXT("Unreal MCP Bridge module stopped."));
}

IMPLEMENT_MODULE(FUnrealMCPBridgeModule, UnrealMCPBridge)
