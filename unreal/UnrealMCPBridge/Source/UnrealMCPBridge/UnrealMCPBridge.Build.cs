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
            "AssetTools",
            "EditorSubsystem",
            "Json",
            "JsonUtilities",
            "MaterialEditor",
            "Networking",
            "Projects",
            "Sockets",
            "UnrealEd"
        });
    }
}
