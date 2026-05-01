#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FBridgeServer;

class FUnrealMCPBridgeModule final : public IModuleInterface
{
public:
    void StartupModule() override;
    void ShutdownModule() override;

private:
    TUniquePtr<FBridgeServer> BridgeServer;
};
