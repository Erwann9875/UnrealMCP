#pragma once

#include "CoreMinimal.h"

class FBridgeServer
{
public:
    FBridgeServer();
    ~FBridgeServer();

    bool Start();
    void Stop();
    bool IsRunning() const;

private:
    bool bRunning;
};
