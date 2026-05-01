#pragma once

#include "CoreMinimal.h"

class FConnectionCommands
{
public:
    static FString Ping();
    static FString Status(bool bBridgeRunning);
    static FString Capabilities();
};
