using UnrealBuildTool;

public class UnrealMCPBridge : ModuleRules
{
    public UnrealMCPBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "EditorSubsystem",
            "Json",
            "JsonUtilities",
            "Networking",
            "Projects",
            "Sockets",
            "UnrealEd"
        });
    }
}
