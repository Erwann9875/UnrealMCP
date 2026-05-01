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
            "BlueprintGraph",
            "EditorSubsystem",
            "Foliage",
            "ImageCore",
            "ImageWrapper",
            "Json",
            "JsonUtilities",
            "KismetCompiler",
            "Landscape",
            "MaterialEditor",
            "MeshDescription",
            "Networking",
            "Projects",
            "Sockets",
            "StaticMeshDescription",
            "UnrealEd"
        });
    }
}
