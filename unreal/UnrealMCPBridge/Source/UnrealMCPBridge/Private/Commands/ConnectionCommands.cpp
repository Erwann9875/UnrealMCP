#include "Commands/ConnectionCommands.h"

#include "Misc/EngineVersion.h"

FString FConnectionCommands::Ping()
{
    return TEXT("{\"type\":\"pong\",\"bridge_version\":\"0.1.0\"}");
}

FString FConnectionCommands::Status(bool bBridgeRunning)
{
    return FString::Printf(
        TEXT("{\"connected\":%s,\"bridge_version\":\"0.1.0\",\"unreal_version\":\"%s\"}"),
        bBridgeRunning ? TEXT("true") : TEXT("false"),
        *FEngineVersion::Current().ToString());
}

FString FConnectionCommands::Capabilities()
{
    return TEXT("{\"commands\":[\"connection.ping\",\"connection.status\",\"connection.capabilities\"]}");
}
