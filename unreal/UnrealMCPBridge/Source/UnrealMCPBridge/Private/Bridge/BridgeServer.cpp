#include "Bridge/BridgeServer.h"

#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Common/TcpListener.h"
#include "CollisionQueryParams.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/StringConv.h"
#include "Camera/CameraActor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/RectLight.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Factories/MaterialFactoryNew.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeUtils.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"
#include "Runtime/UnrealMCPGameplayRuntimeComponents.h"
#include "Runtime/UnrealMCPRuntimeAnimationPreset.h"
#include "Runtime/UnrealMCPRuntimeAnimatorComponent.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCPBridgeServer, Log, All);

namespace
{
constexpr uint16 ProtocolVersion = 1;
constexpr uint16 BridgePort = 55557;
constexpr uint32 MaxFrameBytes = 16 * 1024 * 1024;
constexpr double SocketWaitSeconds = 5.0;
const TCHAR* BridgeVersion = TEXT("0.1.0");

uint32 ReadBigEndianUint32(const uint8* Bytes)
{
    return (static_cast<uint32>(Bytes[0]) << 24) |
        (static_cast<uint32>(Bytes[1]) << 16) |
        (static_cast<uint32>(Bytes[2]) << 8) |
        static_cast<uint32>(Bytes[3]);
}

void WriteBigEndianUint32(uint32 Value, uint8* OutBytes)
{
    OutBytes[0] = static_cast<uint8>((Value >> 24) & 0xFF);
    OutBytes[1] = static_cast<uint8>((Value >> 16) & 0xFF);
    OutBytes[2] = static_cast<uint8>((Value >> 8) & 0xFF);
    OutBytes[3] = static_cast<uint8>(Value & 0xFF);
}

bool SerializeJsonObject(const TSharedRef<FJsonObject>& Object, FString& OutJson)
{
    OutJson.Reset();
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    return FJsonSerializer::Serialize(Object, Writer);
}

TSharedPtr<FJsonValue> MakeStringValue(const FString& Value)
{
    return MakeShared<FJsonValueString>(Value);
}

TSharedPtr<FJsonValue> MakeObjectValue(const TSharedRef<FJsonObject>& Value)
{
    return MakeShared<FJsonValueObject>(Value);
}

TSharedPtr<FJsonValue> MakeNumberValue(double Value)
{
    return MakeShared<FJsonValueNumber>(Value);
}

TSharedRef<FJsonObject> MakeTaggedResult(const FString& Type, const TSharedRef<FJsonObject>& Data)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("type"), Type);
    Result->SetObjectField(TEXT("data"), Data);
    return Result;
}

TArray<TSharedPtr<FJsonValue>> MakeVectorArray(double X, double Y, double Z)
{
    return {
        MakeNumberValue(X),
        MakeNumberValue(Y),
        MakeNumberValue(Z),
    };
}

TArray<TSharedPtr<FJsonValue>> MakeVector4Array(double X, double Y, double Z, double W)
{
    return {
        MakeNumberValue(X),
        MakeNumberValue(Y),
        MakeNumberValue(Z),
        MakeNumberValue(W),
    };
}

bool ReadVectorField(
    const TSharedPtr<FJsonObject>& Object,
    const FString& FieldName,
    const FVector& DefaultValue,
    FVector& OutValue)
{
    OutValue = DefaultValue;
    if (!Object.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(FieldName, Values))
    {
        return true;
    }

    if (Values->Num() != 3)
    {
        return false;
    }

    OutValue = FVector(
        (*Values)[0]->AsNumber(),
        (*Values)[1]->AsNumber(),
        (*Values)[2]->AsNumber());
    return true;
}

bool ReadVector2DField(
    const TSharedPtr<FJsonObject>& Object,
    const FString& FieldName,
    const FVector2D& DefaultValue,
    FVector2D& OutValue)
{
    OutValue = DefaultValue;
    if (!Object.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(FieldName, Values))
    {
        return true;
    }

    if (Values->Num() != 2)
    {
        return false;
    }

    OutValue = FVector2D((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber());
    return true;
}

bool ReadColorField(
    const TSharedPtr<FJsonObject>& Object,
    const FString& FieldName,
    const FLinearColor& DefaultValue,
    FLinearColor& OutValue)
{
    OutValue = DefaultValue;
    if (!Object.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(FieldName, Values))
    {
        return true;
    }

    if (Values->Num() != 4)
    {
        return false;
    }

    OutValue = FLinearColor(
        (*Values)[0]->AsNumber(),
        (*Values)[1]->AsNumber(),
        (*Values)[2]->AsNumber(),
        (*Values)[3]->AsNumber());
    return true;
}

bool ReadRotatorField(
    const TSharedPtr<FJsonObject>& Object,
    const FString& FieldName,
    const FRotator& DefaultValue,
    FRotator& OutValue)
{
    FVector RotationVector;
    if (!ReadVectorField(Object, FieldName, FVector(DefaultValue.Pitch, DefaultValue.Yaw, DefaultValue.Roll), RotationVector))
    {
        return false;
    }

    OutValue = FRotator(RotationVector.X, RotationVector.Y, RotationVector.Z);
    return true;
}

TArray<FString> ReadStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
{
    TArray<FString> Strings;
    if (!Object.IsValid())
    {
        return Strings;
    }

    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(FieldName, Values))
    {
        return Strings;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        if (Value.IsValid())
        {
            Strings.Add(Value->AsString());
        }
    }

    return Strings;
}

FString ToObjectPath(const FString& PackagePath)
{
    if (PackagePath.Contains(TEXT(".")))
    {
        return PackagePath;
    }

    return FString::Printf(TEXT("%s.%s"), *PackagePath, *FPackageName::GetShortName(PackagePath));
}

bool SplitAssetPath(const FString& Path, FString& OutPackagePath, FString& OutAssetName, FString& OutError)
{
    if (Path.IsEmpty())
    {
        OutError = TEXT("asset path is empty");
        return false;
    }

    FString PackageName = Path;
    if (Path.Contains(TEXT(".")))
    {
        PackageName = FPackageName::ObjectPathToPackageName(Path);
    }

    FText Reason;
    if (!FPackageName::IsValidLongPackageName(PackageName, false, &Reason))
    {
        OutError = Reason.ToString();
        return false;
    }

    OutPackagePath = FPackageName::GetLongPackagePath(PackageName);
    OutAssetName = FPackageName::GetShortName(PackageName);
    if (OutPackagePath.IsEmpty() || OutAssetName.IsEmpty())
    {
        OutError = FString::Printf(TEXT("invalid asset path '%s'"), *Path);
        return false;
    }

    return true;
}

TSharedRef<FJsonObject> MakeAssetOperation(const FString& Path, bool bCreated)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("path"), Path);
    ResultData->SetBoolField(TEXT("created"), bCreated);
    return ResultData;
}

TSharedRef<FJsonObject> MakeMaterialOperation(const FString& Path, const FString& Parent, bool bCreated)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("path"), Path);
    if (Parent.IsEmpty())
    {
        ResultData->SetField(TEXT("parent"), MakeShared<FJsonValueNull>());
    }
    else
    {
        ResultData->SetStringField(TEXT("parent"), Parent);
    }
    ResultData->SetBoolField(TEXT("created"), bCreated);
    return ResultData;
}

TSharedRef<FJsonObject> MakeTextureOperation(const FString& Path, int32 Width, int32 Height, bool bCreated)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("path"), Path);
    ResultData->SetNumberField(TEXT("width"), Width);
    ResultData->SetNumberField(TEXT("height"), Height);
    ResultData->SetBoolField(TEXT("created"), bCreated);
    return ResultData;
}

TSharedRef<FJsonObject> MakeParameterOperation(
    const FString& Path,
    int32 ScalarCount,
    int32 VectorCount,
    int32 TextureCount)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("path"), Path);
    ResultData->SetNumberField(TEXT("scalar_count"), ScalarCount);
    ResultData->SetNumberField(TEXT("vector_count"), VectorCount);
    ResultData->SetNumberField(TEXT("texture_count"), TextureCount);
    return ResultData;
}

void AddAppliedActor(
    TArray<TSharedPtr<FJsonValue>>& Applied,
    const AActor& Actor,
    const FString& MaterialPath,
    int32 Slot)
{
    TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Actor.GetActorLabel());
    Item->SetStringField(TEXT("path"), Actor.GetPathName());
    Item->SetStringField(TEXT("material"), MaterialPath);
    Item->SetNumberField(TEXT("slot"), Slot);
    Applied.Add(MakeObjectValue(Item));
}

TSharedRef<FJsonObject> MakeMaterialApplyResult(const TArray<TSharedPtr<FJsonValue>>& Applied)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("applied"), Applied);
    ResultData->SetNumberField(TEXT("count"), Applied.Num());
    return ResultData;
}

void AddChangedActor(TArray<FString>& Changed, const AActor& Actor)
{
    Changed.AddUnique(Actor.GetActorLabel());
}

TSharedRef<FJsonObject> MakeLightingOperationResult(const TArray<FString>& Changed)
{
    TArray<TSharedPtr<FJsonValue>> ChangedValues;
    for (const FString& ActorName : Changed)
    {
        ChangedValues.Add(MakeStringValue(ActorName));
    }

    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("changed"), ChangedValues);
    ResultData->SetNumberField(TEXT("count"), ChangedValues.Num());
    return ResultData;
}

void AddLightSummary(TArray<TSharedPtr<FJsonValue>>& Lights, const AActor& Actor, const FString& Kind)
{
    TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Actor.GetActorLabel());
    Item->SetStringField(TEXT("path"), Actor.GetPathName());
    Item->SetStringField(TEXT("kind"), Kind);
    Lights.Add(MakeObjectValue(Item));
}

TSharedRef<FJsonObject> MakeBulkLightResult(const TArray<TSharedPtr<FJsonValue>>& Lights)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("lights"), Lights);
    ResultData->SetNumberField(TEXT("count"), Lights.Num());
    return ResultData;
}

TSharedRef<FJsonObject> MakeBlueprintOperation(const FString& Path, bool bCreated, bool bCompiled)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("path"), Path);
    ResultData->SetBoolField(TEXT("created"), bCreated);
    ResultData->SetBoolField(TEXT("compiled"), bCompiled);
    return ResultData;
}

TSharedRef<FJsonObject> MakeBlueprintComponentOperation(
    const FString& BlueprintPath,
    const TArray<FString>& Components)
{
    TArray<TSharedPtr<FJsonValue>> ComponentValues;
    for (const FString& Component : Components)
    {
        ComponentValues.Add(MakeStringValue(Component));
    }

    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("blueprint"), BlueprintPath);
    ResultData->SetArrayField(TEXT("components"), ComponentValues);
    ResultData->SetNumberField(TEXT("count"), ComponentValues.Num());
    return ResultData;
}

TSharedRef<FJsonObject> MakeRuntimeAnimationOperation(
    const FString& Path,
    const TArray<FString>& Attached)
{
    TArray<TSharedPtr<FJsonValue>> AttachedValues;
    for (const FString& Target : Attached)
    {
        AttachedValues.Add(MakeStringValue(Target));
    }

    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    if (Path.IsEmpty())
    {
        ResultData->SetField(TEXT("path"), MakeShared<FJsonValueNull>());
    }
    else
    {
        ResultData->SetStringField(TEXT("path"), Path);
    }
    ResultData->SetArrayField(TEXT("attached"), AttachedValues);
    ResultData->SetNumberField(TEXT("count"), AttachedValues.Num());
    return ResultData;
}

TSharedRef<FJsonObject> MakeLandscapeOperation(
    const FString& Name,
    const FString& Path,
    const FIntPoint& ComponentCount,
    const FIntPoint& VertexCount,
    const TArray<FString>& Changed)
{
    TArray<TSharedPtr<FJsonValue>> ChangedValues;
    for (const FString& Item : Changed)
    {
        ChangedValues.Add(MakeStringValue(Item));
    }

    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> ComponentCountValues;
    ComponentCountValues.Add(MakeNumberValue(ComponentCount.X));
    ComponentCountValues.Add(MakeNumberValue(ComponentCount.Y));
    TArray<TSharedPtr<FJsonValue>> VertexCountValues;
    VertexCountValues.Add(MakeNumberValue(VertexCount.X));
    VertexCountValues.Add(MakeNumberValue(VertexCount.Y));
    ResultData->SetStringField(TEXT("name"), Name);
    ResultData->SetStringField(TEXT("path"), Path);
    ResultData->SetArrayField(TEXT("component_count"), ComponentCountValues);
    ResultData->SetArrayField(TEXT("vertex_count"), VertexCountValues);
    ResultData->SetArrayField(TEXT("changed"), ChangedValues);
    return ResultData;
}

void AddSnappedActor(
    TArray<TSharedPtr<FJsonValue>>& Actors,
    const AActor& Actor,
    const FVector& OldLocation,
    const FVector& NewLocation)
{
    TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Actor.GetActorLabel());
    Item->SetStringField(TEXT("path"), Actor.GetPathName());
    Item->SetArrayField(TEXT("old_location"), MakeVectorArray(OldLocation.X, OldLocation.Y, OldLocation.Z));
    Item->SetArrayField(TEXT("new_location"), MakeVectorArray(NewLocation.X, NewLocation.Y, NewLocation.Z));
    Actors.Add(MakeObjectValue(Item));
}

TSharedRef<FJsonObject> MakePlacementSnapResult(const TArray<TSharedPtr<FJsonValue>>& Actors)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("actors"), Actors);
    ResultData->SetNumberField(TEXT("count"), Actors.Num());
    return ResultData;
}

void AddAssetImportOperation(
    TArray<TSharedPtr<FJsonValue>>& Assets,
    const FString& SourceFile,
    const FString& Path,
    const FString& ClassName,
    bool bImported,
    const FString& Message)
{
    TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("source_file"), SourceFile);
    Item->SetStringField(TEXT("path"), Path);
    Item->SetStringField(TEXT("class_name"), ClassName);
    Item->SetBoolField(TEXT("imported"), bImported);
    if (Message.IsEmpty())
    {
        Item->SetField(TEXT("message"), MakeShared<FJsonValueNull>());
    }
    else
    {
        Item->SetStringField(TEXT("message"), Message);
    }
    Assets.Add(MakeObjectValue(Item));
}

TSharedRef<FJsonObject> MakeAssetImportResult(const TArray<TSharedPtr<FJsonValue>>& Assets)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("assets"), Assets);
    ResultData->SetNumberField(TEXT("count"), Assets.Num());
    return ResultData;
}

TSharedRef<FJsonObject> MakeAssetValidationResult(const TArray<FString>& Paths)
{
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

    TArray<TSharedPtr<FJsonValue>> Assets;
    for (const FString& Path : Paths)
    {
        const FString ObjectPath = ToObjectPath(Path);
        const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));

        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), Path);
        Item->SetBoolField(TEXT("exists"), AssetData.IsValid());
        if (AssetData.IsValid())
        {
            Item->SetStringField(TEXT("class_name"), AssetData.AssetClassPath.GetAssetName().ToString());
        }
        else
        {
            Item->SetField(TEXT("class_name"), MakeShared<FJsonValueNull>());
        }
        Assets.Add(MakeObjectValue(Item));
    }

    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("assets"), Assets);
    ResultData->SetNumberField(TEXT("count"), Assets.Num());
    return ResultData;
}

TSharedRef<FJsonObject> MakeGeneratedMeshOperation(
    const FString& Path,
    bool bCreated,
    int32 VertexCount,
    int32 TriangleCount)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("path"), Path);
    ResultData->SetBoolField(TEXT("created"), bCreated);
    ResultData->SetNumberField(TEXT("vertex_count"), VertexCount);
    ResultData->SetNumberField(TEXT("triangle_count"), TriangleCount);
    return ResultData;
}

void AddStaticMeshOperation(
    TArray<TSharedPtr<FJsonValue>>& Meshes,
    const FString& Path,
    bool bChanged,
    const FString& CollisionTrace)
{
    TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("path"), Path);
    Item->SetBoolField(TEXT("changed"), bChanged);
    Item->SetStringField(TEXT("collision_trace"), CollisionTrace);
    Meshes.Add(MakeObjectValue(Item));
}

TSharedRef<FJsonObject> MakeStaticMeshOperationResult(const TArray<TSharedPtr<FJsonValue>>& Meshes)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("meshes"), Meshes);
    ResultData->SetNumberField(TEXT("count"), Meshes.Num());
    return ResultData;
}

struct FSceneAssemblyCounts
{
    int32 RoadCount = 0;
    int32 SidewalkCount = 0;
    int32 BuildingCount = 0;
    int32 PropCount = 0;
};

void AddSpawnedSceneActor(TArray<TSharedPtr<FJsonValue>>& Spawned, const AActor& Actor)
{
    TSharedRef<FJsonObject> SpawnedActor = MakeShared<FJsonObject>();
    SpawnedActor->SetStringField(TEXT("name"), Actor.GetActorLabel());
    SpawnedActor->SetStringField(TEXT("path"), Actor.GetPathName());
    Spawned.Add(MakeObjectValue(SpawnedActor));
}

TSharedRef<FJsonObject> MakeSceneAssemblyResult(
    const TArray<TSharedPtr<FJsonValue>>& Spawned,
    const FSceneAssemblyCounts& Counts)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("spawned"), Spawned);
    ResultData->SetNumberField(TEXT("count"), Spawned.Num());
    ResultData->SetNumberField(TEXT("road_count"), Counts.RoadCount);
    ResultData->SetNumberField(TEXT("sidewalk_count"), Counts.SidewalkCount);
    ResultData->SetNumberField(TEXT("building_count"), Counts.BuildingCount);
    ResultData->SetNumberField(TEXT("prop_count"), Counts.PropCount);
    return ResultData;
}

FString ReadStringFieldOrDefault(
    const TSharedPtr<FJsonObject>& Object,
    const FString& FieldName,
    const FString& DefaultValue)
{
    FString Value;
    if (Object.IsValid() && Object->TryGetStringField(FieldName, Value) && !Value.IsEmpty())
    {
        return Value;
    }
    return DefaultValue;
}

UStaticMesh* LoadSceneStaticMesh(
    const TSharedPtr<FJsonObject>& SpecData,
    const FString& FieldName,
    const FString& DefaultPath,
    FString& OutError)
{
    const FString MeshPath = ReadStringFieldOrDefault(SpecData, FieldName, DefaultPath);
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *ToObjectPath(MeshPath));
    if (!Mesh)
    {
        OutError = FString::Printf(TEXT("failed to load static mesh '%s'"), *MeshPath);
    }
    return Mesh;
}

bool SpawnSceneStaticMeshActor(
    UWorld& World,
    const FString& Name,
    UStaticMesh& Mesh,
    const FVector& Location,
    const FRotator& Rotation,
    const FVector& Scale,
    const FString& Scene,
    const FString& Group,
    const FString& SceneActorKind,
    TArray<TSharedPtr<FJsonValue>>& Spawned)
{
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = MakeUniqueObjectName(&World, AStaticMeshActor::StaticClass(), FName(*Name));
    AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>(Location, Rotation, SpawnParameters);
    if (!Actor)
    {
        return false;
    }

    Actor->Modify();
    Actor->SetActorLabel(Name);
    Actor->SetActorScale3D(Scale);
    Actor->Tags.AddUnique(TEXT("mcp.generated"));
    if (!Scene.IsEmpty())
    {
        Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.scene:%s"), *Scene)));
    }
    if (!Group.IsEmpty())
    {
        Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.group:%s"), *Group)));
    }
    if (!SceneActorKind.IsEmpty())
    {
        Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.scene_actor:%s"), *SceneActorKind)));
    }

    if (UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent())
    {
        MeshComponent->SetStaticMesh(&Mesh);
        MeshComponent->SetMobility(EComponentMobility::Static);
    }

    AddSpawnedSceneActor(Spawned, *Actor);
    return true;
}

void IncrementSceneCount(FSceneAssemblyCounts& Counts, const FString& SceneActorKind)
{
    if (SceneActorKind == TEXT("road"))
    {
        ++Counts.RoadCount;
    }
    else if (SceneActorKind == TEXT("sidewalk"))
    {
        ++Counts.SidewalkCount;
    }
    else if (SceneActorKind == TEXT("building"))
    {
        ++Counts.BuildingCount;
    }
    else
    {
        ++Counts.PropCount;
    }
}

struct FGameplayCounts
{
    int32 PlayerCount = 0;
    int32 CheckpointCount = 0;
    int32 InteractionCount = 0;
    int32 CollectibleCount = 0;
    int32 ObjectiveCount = 0;
};

TSharedRef<FJsonObject> MakeGameplayOperationResult(
    const TArray<TSharedPtr<FJsonValue>>& Spawned,
    const FGameplayCounts& Counts)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("spawned"), Spawned);
    ResultData->SetNumberField(TEXT("count"), Spawned.Num());
    ResultData->SetNumberField(TEXT("player_count"), Counts.PlayerCount);
    ResultData->SetNumberField(TEXT("checkpoint_count"), Counts.CheckpointCount);
    ResultData->SetNumberField(TEXT("interaction_count"), Counts.InteractionCount);
    ResultData->SetNumberField(TEXT("collectible_count"), Counts.CollectibleCount);
    ResultData->SetNumberField(TEXT("objective_count"), Counts.ObjectiveCount);
    return ResultData;
}

struct FGameplayRuntimeCounts
{
    int32 CollectibleCount = 0;
    int32 CheckpointCount = 0;
    int32 InteractionCount = 0;
    int32 ObjectiveCount = 0;
};

void AddGameplayRuntimeBinding(
    TArray<TSharedPtr<FJsonValue>>& Bindings,
    const AActor& Actor,
    const UActorComponent& Component)
{
    TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Actor.GetActorLabel());
    Item->SetStringField(TEXT("path"), Actor.GetPathName());
    Item->SetStringField(TEXT("component"), Component.GetName());
    Bindings.Add(MakeObjectValue(Item));
}

TSharedRef<FJsonObject> MakeGameplayRuntimeOperationResult(
    const AActor* Manager,
    const TArray<TSharedPtr<FJsonValue>>& Bindings,
    const FGameplayRuntimeCounts& Counts)
{
    TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
    if (Manager)
    {
        TSharedRef<FJsonObject> ManagerData = MakeShared<FJsonObject>();
        ManagerData->SetStringField(TEXT("name"), Manager->GetActorLabel());
        ManagerData->SetStringField(TEXT("path"), Manager->GetPathName());
        ResultData->SetObjectField(TEXT("manager"), ManagerData);
    }
    else
    {
        ResultData->SetField(TEXT("manager"), MakeShared<FJsonValueNull>());
    }
    ResultData->SetArrayField(TEXT("bindings"), Bindings);
    ResultData->SetNumberField(TEXT("count"), Bindings.Num());
    ResultData->SetNumberField(TEXT("collectible_count"), Counts.CollectibleCount);
    ResultData->SetNumberField(TEXT("checkpoint_count"), Counts.CheckpointCount);
    ResultData->SetNumberField(TEXT("interaction_count"), Counts.InteractionCount);
    ResultData->SetNumberField(TEXT("objective_count"), Counts.ObjectiveCount);
    return ResultData;
}

void AddGameplayTags(
    AActor& Actor,
    const FString& Scene,
    const FString& Group,
    const FString& GameplayKind,
    const TArray<FString>& ExtraTags = TArray<FString>())
{
    Actor.Tags.AddUnique(TEXT("mcp.generated"));
    Actor.Tags.AddUnique(TEXT("mcp.gameplay"));
    if (!Scene.IsEmpty())
    {
        Actor.Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.scene:%s"), *Scene)));
    }
    if (!Group.IsEmpty())
    {
        Actor.Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.group:%s"), *Group)));
    }
    if (!GameplayKind.IsEmpty())
    {
        Actor.Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.gameplay_actor:%s"), *GameplayKind)));
    }
    for (const FString& Tag : ExtraTags)
    {
        if (!Tag.IsEmpty())
        {
            Actor.Tags.AddUnique(FName(*Tag));
        }
    }
}

void AddGameplayRuntimeTags(
    AActor& Actor,
    const FString& Scene,
    const FString& Group,
    const TArray<FString>& ExtraTags = TArray<FString>())
{
    Actor.Tags.AddUnique(TEXT("mcp.generated"));
    Actor.Tags.AddUnique(TEXT("mcp.gameplay_runtime"));
    if (!Scene.IsEmpty())
    {
        Actor.Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.scene:%s"), *Scene)));
    }
    if (!Group.IsEmpty())
    {
        Actor.Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.group:%s"), *Group)));
    }
    for (const FString& Tag : ExtraTags)
    {
        if (!Tag.IsEmpty())
        {
            Actor.Tags.AddUnique(FName(*Tag));
        }
    }
}

template <typename TComponent>
TComponent* FindOrCreateActorComponent(AActor& Actor, const FName& ComponentName, bool& bOutCreated)
{
    bOutCreated = false;
    if (TComponent* Existing = Actor.FindComponentByClass<TComponent>())
    {
        return Existing;
    }

    TComponent* Component = NewObject<TComponent>(&Actor, ComponentName, RF_Transactional);
    if (!Component)
    {
        return nullptr;
    }

    Actor.AddInstanceComponent(Component);
    Component->RegisterComponent();
    bOutCreated = true;
    return Component;
}

bool TryReadActorTagValue(const AActor& Actor, const FString& Prefix, FString& OutValue)
{
    for (const FName& Tag : Actor.Tags)
    {
        const FString TagString = Tag.ToString();
        if (TagString.StartsWith(Prefix))
        {
            OutValue = TagString.RightChop(Prefix.Len());
            return !OutValue.IsEmpty();
        }
    }

    return false;
}

FString ReadActorTagValueOrDefault(const AActor& Actor, const FString& Prefix, const FString& DefaultValue)
{
    FString Value;
    return TryReadActorTagValue(Actor, Prefix, Value) ? Value : DefaultValue;
}

int32 ReadActorTagIntOrDefault(const AActor& Actor, const FString& Prefix, int32 DefaultValue)
{
    FString Value;
    if (!TryReadActorTagValue(Actor, Prefix, Value))
    {
        return DefaultValue;
    }

    return FCString::Atoi(*Value);
}

bool SpawnGameplayStaticMeshActor(
    UWorld& World,
    const FString& Name,
    UStaticMesh& Mesh,
    const FVector& Location,
    const FRotator& Rotation,
    const FVector& Scale,
    const FString& Scene,
    const FString& Group,
    const FString& GameplayKind,
    const TArray<FString>& ExtraTags,
    TArray<TSharedPtr<FJsonValue>>& Spawned)
{
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = MakeUniqueObjectName(&World, AStaticMeshActor::StaticClass(), FName(*Name));
    AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>(Location, Rotation, SpawnParameters);
    if (!Actor)
    {
        return false;
    }

    Actor->Modify();
    Actor->SetActorLabel(Name);
    Actor->SetActorScale3D(Scale);
    AddGameplayTags(*Actor, Scene, Group, GameplayKind, ExtraTags);

    if (UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent())
    {
        MeshComponent->SetStaticMesh(&Mesh);
        MeshComponent->SetMobility(EComponentMobility::Static);
    }

    AddSpawnedSceneActor(Spawned, *Actor);
    return true;
}

bool SpawnGameplayPlayer(
    UWorld& World,
    const TSharedPtr<FJsonObject>& SpecData,
    TArray<TSharedPtr<FJsonValue>>& Spawned,
    FGameplayCounts& Counts,
    FString& OutError)
{
    FString Name;
    if (!SpecData->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
    {
        OutError = TEXT("game player requires name");
        return false;
    }

    FString Scene;
    SpecData->TryGetStringField(TEXT("scene"), Scene);
    FString Group;
    SpecData->TryGetStringField(TEXT("group"), Group);
    FString SpawnTag;
    SpecData->TryGetStringField(TEXT("spawn_tag"), SpawnTag);

    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    if (!ReadVectorField(SpecData, TEXT("location"), FVector::ZeroVector, Location) ||
        !ReadRotatorField(SpecData, TEXT("rotation"), FRotator::ZeroRotator, Rotation))
    {
        OutError = TEXT("player location/rotation must have 3 numbers");
        return false;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = MakeUniqueObjectName(&World, APlayerStart::StaticClass(), FName(*Name));
    APlayerStart* PlayerStart = World.SpawnActor<APlayerStart>(Location, Rotation, SpawnParameters);
    if (!PlayerStart)
    {
        OutError = TEXT("failed to spawn player start");
        return false;
    }

    PlayerStart->Modify();
    PlayerStart->SetActorLabel(Name);
    TArray<FString> ExtraTags;
    if (!SpawnTag.IsEmpty())
    {
        ExtraTags.Add(FString::Printf(TEXT("mcp.spawn:%s"), *SpawnTag));
    }
    AddGameplayTags(*PlayerStart, Scene, Group, TEXT("player_start"), ExtraTags);
    AddSpawnedSceneActor(Spawned, *PlayerStart);
    ++Counts.PlayerCount;

    bool bCreateCamera = false;
    SpecData->TryGetBoolField(TEXT("create_camera"), bCreateCamera);
    if (bCreateCamera)
    {
        FString CameraName = FString::Printf(TEXT("%s_Camera"), *Name);
        SpecData->TryGetStringField(TEXT("camera_name"), CameraName);
        FVector CameraLocation(-400.0, 0.0, 260.0);
        FRotator CameraRotation(-10.0, 0.0, 0.0);
        if (!ReadVectorField(SpecData, TEXT("camera_location"), CameraLocation, CameraLocation) ||
            !ReadRotatorField(SpecData, TEXT("camera_rotation"), CameraRotation, CameraRotation))
        {
            OutError = TEXT("camera_location/camera_rotation must have 3 numbers");
            return false;
        }

        FActorSpawnParameters CameraSpawnParameters;
        CameraSpawnParameters.Name = MakeUniqueObjectName(&World, ACameraActor::StaticClass(), FName(*CameraName));
        ACameraActor* Camera = World.SpawnActor<ACameraActor>(CameraLocation, CameraRotation, CameraSpawnParameters);
        if (Camera)
        {
            Camera->Modify();
            Camera->SetActorLabel(CameraName);
            AddGameplayTags(*Camera, Scene, Group, TEXT("camera"), TArray<FString>());
            AddSpawnedSceneActor(Spawned, *Camera);
            ++Counts.PlayerCount;
        }
    }

    return true;
}

bool SpawnGameplayCheckpoint(
    UWorld& World,
    const TSharedPtr<FJsonObject>& SpecData,
    UStaticMesh& MarkerMesh,
    TArray<TSharedPtr<FJsonValue>>& Spawned,
    FGameplayCounts& Counts,
    FString& OutError)
{
    FString Name;
    if (!SpecData->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
    {
        OutError = TEXT("game checkpoint requires name");
        return false;
    }

    FString Scene;
    SpecData->TryGetStringField(TEXT("scene"), Scene);
    FString Group;
    SpecData->TryGetStringField(TEXT("group"), Group);
    FString CheckpointId = Name;
    SpecData->TryGetStringField(TEXT("checkpoint_id"), CheckpointId);

    int32 Order = 0;
    SpecData->TryGetNumberField(TEXT("order"), Order);

    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    FVector Scale(2.0, 2.0, 0.25);
    if (!ReadVectorField(SpecData, TEXT("location"), FVector::ZeroVector, Location) ||
        !ReadRotatorField(SpecData, TEXT("rotation"), FRotator::ZeroRotator, Rotation) ||
        !ReadVectorField(SpecData, TEXT("scale"), Scale, Scale))
    {
        OutError = TEXT("checkpoint location/rotation/scale must have 3 numbers");
        return false;
    }

    TArray<FString> ExtraTags;
    ExtraTags.Add(FString::Printf(TEXT("mcp.checkpoint:%s"), *CheckpointId));
    ExtraTags.Add(FString::Printf(TEXT("mcp.order:%d"), Order));
    if (SpawnGameplayStaticMeshActor(World, Name, MarkerMesh, Location, Rotation, Scale, Scene, Group, TEXT("checkpoint"), ExtraTags, Spawned))
    {
        ++Counts.CheckpointCount;
    }
    return true;
}

bool SpawnGameplayInteraction(
    UWorld& World,
    const TSharedPtr<FJsonObject>& SpecData,
    UStaticMesh& MarkerMesh,
    TArray<TSharedPtr<FJsonValue>>& Spawned,
    FGameplayCounts& Counts,
    FString& OutError)
{
    FString Name;
    FString Kind;
    if (!SpecData->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() ||
        !SpecData->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
    {
        OutError = TEXT("game interaction requires name and kind");
        return false;
    }

    FString Scene;
    SpecData->TryGetStringField(TEXT("scene"), Scene);
    FString Group;
    SpecData->TryGetStringField(TEXT("group"), Group);
    FString InteractionId = Name;
    SpecData->TryGetStringField(TEXT("interaction_id"), InteractionId);

    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    FVector Scale(0.6, 0.6, 0.6);
    if (!ReadVectorField(SpecData, TEXT("location"), FVector::ZeroVector, Location) ||
        !ReadRotatorField(SpecData, TEXT("rotation"), FRotator::ZeroRotator, Rotation) ||
        !ReadVectorField(SpecData, TEXT("scale"), Scale, Scale))
    {
        OutError = TEXT("interaction location/rotation/scale must have 3 numbers");
        return false;
    }

    TArray<FString> ExtraTags;
    ExtraTags.Add(FString::Printf(TEXT("mcp.interaction:%s"), *Kind));
    ExtraTags.Add(FString::Printf(TEXT("mcp.interaction_id:%s"), *InteractionId));
    FString Target;
    if (SpecData->TryGetStringField(TEXT("target"), Target) && !Target.IsEmpty())
    {
        ExtraTags.Add(FString::Printf(TEXT("mcp.target:%s"), *Target));
    }
    FString Action;
    if (SpecData->TryGetStringField(TEXT("action"), Action) && !Action.IsEmpty())
    {
        ExtraTags.Add(FString::Printf(TEXT("mcp.action:%s"), *Action));
    }
    FString Prompt;
    if (SpecData->TryGetStringField(TEXT("prompt"), Prompt) && !Prompt.IsEmpty())
    {
        ExtraTags.Add(FString::Printf(TEXT("mcp.prompt:%s"), *Prompt));
    }

    if (SpawnGameplayStaticMeshActor(World, Name, MarkerMesh, Location, Rotation, Scale, Scene, Group, TEXT("interaction"), ExtraTags, Spawned))
    {
        ++Counts.InteractionCount;
    }
    return true;
}

bool SpawnGameplayCollectibles(
    UWorld& World,
    const TSharedPtr<FJsonObject>& SpecData,
    UStaticMesh& Mesh,
    TArray<TSharedPtr<FJsonValue>>& Spawned,
    FGameplayCounts& Counts,
    FString& OutError)
{
    FString NamePrefix;
    if (!SpecData->TryGetStringField(TEXT("name_prefix"), NamePrefix) || NamePrefix.IsEmpty())
    {
        OutError = TEXT("game collectibles require name_prefix");
        return false;
    }

    FString Scene;
    SpecData->TryGetStringField(TEXT("scene"), Scene);
    FString Group;
    SpecData->TryGetStringField(TEXT("group"), Group);

    FVector Origin = FVector::ZeroVector;
    FVector2D Spacing(180.0, 180.0);
    FRotator Rotation = FRotator::ZeroRotator;
    FVector Scale(0.25, 0.25, 0.25);
    if (!ReadVectorField(SpecData, TEXT("origin"), FVector::ZeroVector, Origin) ||
        !ReadVector2DField(SpecData, TEXT("spacing"), Spacing, Spacing) ||
        !ReadRotatorField(SpecData, TEXT("rotation"), FRotator::ZeroRotator, Rotation) ||
        !ReadVectorField(SpecData, TEXT("scale"), Scale, Scale))
    {
        OutError = TEXT("collectibles origin/rotation/scale must have 3 numbers and spacing must have 2 numbers");
        return false;
    }

    int32 Rows = 2;
    int32 Columns = 2;
    int32 Value = 1;
    SpecData->TryGetNumberField(TEXT("rows"), Rows);
    SpecData->TryGetNumberField(TEXT("columns"), Columns);
    SpecData->TryGetNumberField(TEXT("value"), Value);
    Rows = FMath::Clamp(Rows, 1, 200);
    Columns = FMath::Clamp(Columns, 1, 200);
    Spacing.X = FMath::Max(Spacing.X, 1.0);
    Spacing.Y = FMath::Max(Spacing.Y, 1.0);

    FString AnimationPath;
    SpecData->TryGetStringField(TEXT("animation"), AnimationPath);

    const double StartX = Origin.X - ((Columns - 1) * Spacing.X) * 0.5;
    const double StartY = Origin.Y - ((Rows - 1) * Spacing.Y) * 0.5;
    for (int32 Row = 0; Row < Rows; ++Row)
    {
        for (int32 Column = 0; Column < Columns; ++Column)
        {
            TArray<FString> ExtraTags;
            ExtraTags.Add(FString::Printf(TEXT("mcp.value:%d"), Value));
            ExtraTags.Add(FString::Printf(TEXT("mcp.order:%d"), Row * Columns + Column));
            if (!AnimationPath.IsEmpty())
            {
                ExtraTags.Add(FString::Printf(TEXT("mcp.animation:%s"), *AnimationPath));
            }

            const FString Name = FString::Printf(TEXT("%s_%03d_%03d"), *NamePrefix, Row, Column);
            if (SpawnGameplayStaticMeshActor(
                    World,
                    Name,
                    Mesh,
                    FVector(StartX + Column * Spacing.X, StartY + Row * Spacing.Y, Origin.Z),
                    Rotation,
                    Scale,
                    Scene,
                    Group,
                    TEXT("collectible"),
                    ExtraTags,
                    Spawned))
            {
                ++Counts.CollectibleCount;
            }
        }
    }

    return true;
}

bool SpawnGameplayObjectiveFlow(
    UWorld& World,
    const TSharedPtr<FJsonObject>& SpecData,
    UStaticMesh& MarkerMesh,
    TArray<TSharedPtr<FJsonValue>>& Spawned,
    FGameplayCounts& Counts,
    FString& OutError)
{
    FString NamePrefix;
    if (!SpecData->TryGetStringField(TEXT("name_prefix"), NamePrefix) || NamePrefix.IsEmpty())
    {
        OutError = TEXT("game objective flow requires name_prefix");
        return false;
    }

    FString Scene;
    SpecData->TryGetStringField(TEXT("scene"), Scene);
    FString Group;
    SpecData->TryGetStringField(TEXT("group"), Group);

    const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
    if (!SpecData->TryGetArrayField(TEXT("steps"), Steps) || !Steps)
    {
        return true;
    }

    for (int32 Index = 0; Index < Steps->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> Step = (*Steps)[Index].IsValid() ? (*Steps)[Index]->AsObject() : nullptr;
        if (!Step.IsValid())
        {
            continue;
        }

        FString StepId;
        if (!Step->TryGetStringField(TEXT("id"), StepId) || StepId.IsEmpty())
        {
            StepId = FString::Printf(TEXT("%03d"), Index);
        }
        FString Label = StepId;
        Step->TryGetStringField(TEXT("label"), Label);
        FString Kind = TEXT("location");
        Step->TryGetStringField(TEXT("kind"), Kind);

        FVector Location = FVector::ZeroVector;
        FRotator Rotation = FRotator::ZeroRotator;
        FVector Scale(1.0, 1.0, 1.0);
        if (!ReadVectorField(Step, TEXT("location"), FVector::ZeroVector, Location) ||
            !ReadRotatorField(Step, TEXT("rotation"), FRotator::ZeroRotator, Rotation) ||
            !ReadVectorField(Step, TEXT("scale"), Scale, Scale))
        {
            OutError = TEXT("objective step location/rotation/scale must have 3 numbers");
            return false;
        }

        TArray<FString> ExtraTags;
        ExtraTags.Add(FString::Printf(TEXT("mcp.objective:%s"), *StepId));
        ExtraTags.Add(FString::Printf(TEXT("mcp.objective_kind:%s"), *Kind));
        ExtraTags.Add(FString::Printf(TEXT("mcp.objective_label:%s"), *Label));
        ExtraTags.Add(FString::Printf(TEXT("mcp.order:%d"), Index));

        const FString Name = FString::Printf(TEXT("%s_%03d_%s"), *NamePrefix, Index, *StepId);
        if (SpawnGameplayStaticMeshActor(World, Name, MarkerMesh, Location, Rotation, Scale, Scene, Group, TEXT("objective"), ExtraTags, Spawned))
        {
            ++Counts.ObjectiveCount;
        }
    }

    return true;
}

bool ParseCollisionTraceFlag(const FString& CollisionTrace, ECollisionTraceFlag& OutFlag)
{
    FString Normalized = CollisionTrace;
    Normalized.ToLowerInline();

    if (Normalized.IsEmpty() || Normalized == TEXT("project_default"))
    {
        OutFlag = CTF_UseDefault;
        return true;
    }
    if (Normalized == TEXT("simple_and_complex"))
    {
        OutFlag = CTF_UseSimpleAndComplex;
        return true;
    }
    if (Normalized == TEXT("use_simple_as_complex"))
    {
        OutFlag = CTF_UseSimpleAsComplex;
        return true;
    }
    if (Normalized == TEXT("use_complex_as_simple"))
    {
        OutFlag = CTF_UseComplexAsSimple;
        return true;
    }

    return false;
}

bool ConfigureStaticMeshCollision(
    UStaticMesh& StaticMesh,
    const FString& CollisionTrace,
    bool bSimpleCollision,
    FString& OutError)
{
    ECollisionTraceFlag TraceFlag = CTF_UseDefault;
    if (!ParseCollisionTraceFlag(CollisionTrace, TraceFlag))
    {
        OutError = FString::Printf(TEXT("unsupported collision_trace '%s'"), *CollisionTrace);
        return false;
    }

    StaticMesh.Modify();
    StaticMesh.CreateBodySetup();
    UBodySetup* BodySetup = StaticMesh.GetBodySetup();
    if (!BodySetup)
    {
        OutError = FString::Printf(TEXT("failed to create BodySetup for '%s'"), *StaticMesh.GetPathName());
        return false;
    }

    BodySetup->Modify();
    BodySetup->CollisionTraceFlag = TraceFlag;
    if (bSimpleCollision)
    {
        const FBox Bounds = StaticMesh.GetBoundingBox();
        const FVector Size = Bounds.IsValid ? Bounds.GetSize() : FVector(100.0, 100.0, 100.0);
        const FVector Center = Bounds.IsValid ? Bounds.GetCenter() : FVector::ZeroVector;

        BodySetup->AggGeom.EmptyElements();
        FKBoxElem Box;
        Box.Center = Center;
        Box.X = FMath::Max(Size.X, 1.0);
        Box.Y = FMath::Max(Size.Y, 1.0);
        Box.Z = FMath::Max(Size.Z, 1.0);
        BodySetup->AggGeom.BoxElems.Add(Box);
    }

    BodySetup->InvalidatePhysicsData();
    BodySetup->CreatePhysicsMeshes();
    StaticMesh.PostEditChange();
    StaticMesh.MarkPackageDirty();
    return true;
}

void AddQuadToMeshDescription(
    FMeshDescription& MeshDescription,
    FStaticMeshAttributes& Attributes,
    FPolygonGroupID PolygonGroup,
    const FVector3f& A,
    const FVector3f& B,
    const FVector3f& C,
    const FVector3f& D,
    int32& VertexCount,
    int32& TriangleCount)
{
    TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
    TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
    TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();
    TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

    const FVector3f Normal = ((B - A) ^ (C - A)).GetSafeNormal();
    const FVector3f Tangent = (B - A).GetSafeNormal();
    const FVector3f Positions[4] = {A, B, C, D};
    const FVector2f UVs[4] = {
        FVector2f(0.0f, 0.0f),
        FVector2f(1.0f, 0.0f),
        FVector2f(1.0f, 1.0f),
        FVector2f(0.0f, 1.0f),
    };

    TArray<FVertexInstanceID> VertexInstanceIDs;
    VertexInstanceIDs.SetNum(4);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FVertexID VertexID = MeshDescription.CreateVertex();
        VertexPositions[VertexID] = Positions[Index];

        const FVertexInstanceID VertexInstanceID = MeshDescription.CreateVertexInstance(VertexID);
        VertexInstanceNormals[VertexInstanceID] = Normal;
        VertexInstanceTangents[VertexInstanceID] = Tangent;
        VertexInstanceBinormalSigns[VertexInstanceID] = 1.0f;
        VertexInstanceColors[VertexInstanceID] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
        VertexInstanceUVs.Set(VertexInstanceID, 0, UVs[Index]);
        VertexInstanceIDs[Index] = VertexInstanceID;
    }

    MeshDescription.CreatePolygon(PolygonGroup, VertexInstanceIDs);
    VertexCount += 4;
    TriangleCount += 2;
}

void AddBoxToMeshDescription(
    FMeshDescription& MeshDescription,
    FStaticMeshAttributes& Attributes,
    FPolygonGroupID PolygonGroup,
    const FVector3f& Min,
    const FVector3f& Max,
    int32& VertexCount,
    int32& TriangleCount)
{
    AddQuadToMeshDescription(
        MeshDescription,
        Attributes,
        PolygonGroup,
        FVector3f(Min.X, Min.Y, Min.Z),
        FVector3f(Max.X, Min.Y, Min.Z),
        FVector3f(Max.X, Min.Y, Max.Z),
        FVector3f(Min.X, Min.Y, Max.Z),
        VertexCount,
        TriangleCount);
    AddQuadToMeshDescription(
        MeshDescription,
        Attributes,
        PolygonGroup,
        FVector3f(Max.X, Max.Y, Min.Z),
        FVector3f(Min.X, Max.Y, Min.Z),
        FVector3f(Min.X, Max.Y, Max.Z),
        FVector3f(Max.X, Max.Y, Max.Z),
        VertexCount,
        TriangleCount);
    AddQuadToMeshDescription(
        MeshDescription,
        Attributes,
        PolygonGroup,
        FVector3f(Max.X, Min.Y, Min.Z),
        FVector3f(Max.X, Max.Y, Min.Z),
        FVector3f(Max.X, Max.Y, Max.Z),
        FVector3f(Max.X, Min.Y, Max.Z),
        VertexCount,
        TriangleCount);
    AddQuadToMeshDescription(
        MeshDescription,
        Attributes,
        PolygonGroup,
        FVector3f(Min.X, Max.Y, Min.Z),
        FVector3f(Min.X, Min.Y, Min.Z),
        FVector3f(Min.X, Min.Y, Max.Z),
        FVector3f(Min.X, Max.Y, Max.Z),
        VertexCount,
        TriangleCount);
    AddQuadToMeshDescription(
        MeshDescription,
        Attributes,
        PolygonGroup,
        FVector3f(Min.X, Min.Y, Max.Z),
        FVector3f(Max.X, Min.Y, Max.Z),
        FVector3f(Max.X, Max.Y, Max.Z),
        FVector3f(Min.X, Max.Y, Max.Z),
        VertexCount,
        TriangleCount);
    AddQuadToMeshDescription(
        MeshDescription,
        Attributes,
        PolygonGroup,
        FVector3f(Min.X, Max.Y, Min.Z),
        FVector3f(Max.X, Max.Y, Min.Z),
        FVector3f(Max.X, Min.Y, Min.Z),
        FVector3f(Min.X, Min.Y, Min.Z),
        VertexCount,
        TriangleCount);
}

bool CreateOrUpdateGeneratedStaticMesh(
    const FString& Path,
    FMeshDescription& MeshDescription,
    UMaterialInterface* Material,
    int32 VertexCount,
    int32 TriangleCount,
    UStaticMesh*& OutStaticMesh,
    bool& bOutCreated,
    FString& OutError)
{
    FString PackagePath;
    FString AssetName;
    if (!SplitAssetPath(Path, PackagePath, AssetName, OutError))
    {
        return false;
    }

    bOutCreated = false;
    OutStaticMesh = LoadObject<UStaticMesh>(nullptr, *ToObjectPath(Path));
    if (!OutStaticMesh)
    {
        UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), nullptr, *ToObjectPath(Path));
        if (ExistingObject)
        {
            OutError = FString::Printf(TEXT("asset '%s' exists but is not a StaticMesh"), *Path);
            return false;
        }

        UPackage* Package = CreatePackage(*(PackagePath / AssetName));
        OutStaticMesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        bOutCreated = true;
    }

    if (!OutStaticMesh)
    {
        OutError = FString::Printf(TEXT("failed to create static mesh '%s'"), *Path);
        return false;
    }

    UMaterialInterface* MeshMaterial = Material ? Material : UMaterial::GetDefaultMaterial(MD_Surface);
    OutStaticMesh->Modify();
    OutStaticMesh->SetNumSourceModels(0);
    FStaticMeshSourceModel& SourceModel = OutStaticMesh->AddSourceModel();
    SourceModel.BuildSettings.bRecomputeNormals = false;
    SourceModel.BuildSettings.bRecomputeTangents = false;
    SourceModel.BuildSettings.bRemoveDegenerates = true;
    SourceModel.BuildSettings.bUseFullPrecisionUVs = false;

    TArray<FStaticMaterial> StaticMaterials;
    StaticMaterials.Add(FStaticMaterial(MeshMaterial, TEXT("Generated"), TEXT("Generated")));
    OutStaticMesh->SetStaticMaterials(StaticMaterials);
    OutStaticMesh->CreateMeshDescription(0, MeshDescription);
    OutStaticMesh->CommitMeshDescription(0);

    FMeshSectionInfo SectionInfo = OutStaticMesh->GetSectionInfoMap().Get(0, 0);
    SectionInfo.MaterialIndex = 0;
    SectionInfo.bEnableCollision = true;
    OutStaticMesh->GetSectionInfoMap().Set(0, 0, SectionInfo);
    OutStaticMesh->GetOriginalSectionInfoMap().Set(0, 0, SectionInfo);
    OutStaticMesh->SetLightMapCoordinateIndex(0);
    OutStaticMesh->SetLightMapResolution(64);
    OutStaticMesh->SetImportVersion(EImportStaticMeshVersion::LastVersion);
    OutStaticMesh->Build();
    OutStaticMesh->PostEditChange();
    OutStaticMesh->MarkPackageDirty();
    if (UPackage* Package = OutStaticMesh->GetOutermost())
    {
        Package->MarkPackageDirty();
    }
    if (bOutCreated)
    {
        FAssetRegistryModule::AssetCreated(OutStaticMesh);
    }

    FString CollisionError;
    ConfigureStaticMeshCollision(*OutStaticMesh, TEXT("use_simple_as_complex"), true, CollisionError);
    return VertexCount > 0 && TriangleCount > 0;
}

bool BuildBuildingMeshDescription(
    const TSharedPtr<FJsonObject>& SpecData,
    FMeshDescription& MeshDescription,
    int32& OutVertexCount,
    int32& OutTriangleCount,
    FString& OutError)
{
    double Width = 800.0;
    double Depth = 600.0;
    double Height = 2400.0;
    int32 Floors = 12;
    int32 WindowRows = 12;
    int32 WindowColumns = 6;
    SpecData->TryGetNumberField(TEXT("width"), Width);
    SpecData->TryGetNumberField(TEXT("depth"), Depth);
    SpecData->TryGetNumberField(TEXT("height"), Height);
    SpecData->TryGetNumberField(TEXT("floors"), Floors);
    SpecData->TryGetNumberField(TEXT("window_rows"), WindowRows);
    SpecData->TryGetNumberField(TEXT("window_columns"), WindowColumns);

    Width = FMath::Max(Width, 1.0);
    Depth = FMath::Max(Depth, 1.0);
    Height = FMath::Max(Height, 1.0);
    Floors = FMath::Max(Floors, 1);
    WindowRows = FMath::Clamp(WindowRows <= 0 ? Floors : WindowRows, 0, 200);
    WindowColumns = FMath::Clamp(WindowColumns, 0, 200);

    FStaticMeshAttributes Attributes(MeshDescription);
    Attributes.Register();
    Attributes.GetVertexInstanceUVs().SetNumChannels(1);
    TPolygonGroupAttributesRef<FName> MaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
    const FPolygonGroupID PolygonGroup = MeshDescription.CreatePolygonGroup();
    MaterialSlotNames[PolygonGroup] = TEXT("Generated");

    OutVertexCount = 0;
    OutTriangleCount = 0;
    const float HalfWidth = static_cast<float>(Width * 0.5);
    const float HalfDepth = static_cast<float>(Depth * 0.5);
    AddBoxToMeshDescription(
        MeshDescription,
        Attributes,
        PolygonGroup,
        FVector3f(-HalfWidth, -HalfDepth, 0.0f),
        FVector3f(HalfWidth, HalfDepth, static_cast<float>(Height)),
        OutVertexCount,
        OutTriangleCount);

    if (WindowRows > 0 && WindowColumns > 0)
    {
        const float FloorHeight = static_cast<float>(Height / WindowRows);
        const float ColumnWidth = static_cast<float>(Width / WindowColumns);
        const float WindowWidth = ColumnWidth * 0.42f;
        const float WindowHeight = FloorHeight * 0.35f;
        const float WindowDepth = 8.0f;

        for (int32 Row = 0; Row < WindowRows; ++Row)
        {
            const float CenterZ = FloorHeight * (Row + 0.55f);
            for (int32 Column = 0; Column < WindowColumns; ++Column)
            {
                const float CenterX = -HalfWidth + ColumnWidth * (Column + 0.5f);
                AddBoxToMeshDescription(
                    MeshDescription,
                    Attributes,
                    PolygonGroup,
                    FVector3f(CenterX - WindowWidth * 0.5f, -HalfDepth - WindowDepth, CenterZ - WindowHeight * 0.5f),
                    FVector3f(CenterX + WindowWidth * 0.5f, -HalfDepth, CenterZ + WindowHeight * 0.5f),
                    OutVertexCount,
                    OutTriangleCount);
            }
        }
    }

    return true;
}

bool BuildSignMeshDescription(
    const TSharedPtr<FJsonObject>& SpecData,
    FMeshDescription& MeshDescription,
    int32& OutVertexCount,
    int32& OutTriangleCount,
    FString& OutError)
{
    double Width = 900.0;
    double Height = 240.0;
    double Depth = 30.0;
    SpecData->TryGetNumberField(TEXT("width"), Width);
    SpecData->TryGetNumberField(TEXT("height"), Height);
    SpecData->TryGetNumberField(TEXT("depth"), Depth);

    Width = FMath::Max(Width, 1.0);
    Height = FMath::Max(Height, 1.0);
    Depth = FMath::Max(Depth, 1.0);

    FStaticMeshAttributes Attributes(MeshDescription);
    Attributes.Register();
    Attributes.GetVertexInstanceUVs().SetNumChannels(1);
    TPolygonGroupAttributesRef<FName> MaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
    const FPolygonGroupID PolygonGroup = MeshDescription.CreatePolygonGroup();
    MaterialSlotNames[PolygonGroup] = TEXT("Generated");

    OutVertexCount = 0;
    OutTriangleCount = 0;
    AddBoxToMeshDescription(
        MeshDescription,
        Attributes,
        PolygonGroup,
        FVector3f(static_cast<float>(Width * -0.5), static_cast<float>(Depth * -0.5), 0.0f),
        FVector3f(static_cast<float>(Width * 0.5), static_cast<float>(Depth * 0.5), static_cast<float>(Height)),
        OutVertexCount,
        OutTriangleCount);
    return true;
}

UTexture2D* ImportTextureAssetDirect(
    const FString& SourceFile,
    const FString& DestinationPath,
    bool bReplaceExisting,
    bool bSRGB,
    bool& bOutCreated,
    FString& OutError)
{
    FString PackagePath;
    FString AssetName;
    if (!SplitAssetPath(DestinationPath, PackagePath, AssetName, OutError))
    {
        return nullptr;
    }

    TArray64<uint8> CompressedData;
    if (!FFileHelper::LoadFileToArray(CompressedData, *SourceFile))
    {
        OutError = FString::Printf(TEXT("failed to read texture file '%s'"), *SourceFile);
        return nullptr;
    }

    FImage Image;
    IImageWrapperModule& ImageWrapperModule =
        FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    if (!ImageWrapperModule.DecompressImage(CompressedData.GetData(), CompressedData.Num(), Image) ||
        Image.SizeX <= 0 ||
        Image.SizeY <= 0)
    {
        OutError = FString::Printf(TEXT("failed to decode texture file '%s'"), *SourceFile);
        return nullptr;
    }
    Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);

    bOutCreated = false;
    UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ToObjectPath(DestinationPath));
    if (!Texture)
    {
        UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), nullptr, *ToObjectPath(DestinationPath));
        if (ExistingObject)
        {
            OutError = FString::Printf(TEXT("asset '%s' exists but is not a Texture2D"), *DestinationPath);
            return nullptr;
        }

        UPackage* Package = CreatePackage(*(PackagePath / AssetName));
        Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        bOutCreated = true;
    }
    else if (!bReplaceExisting)
    {
        OutError = TEXT("asset already exists");
        return nullptr;
    }

    if (!Texture)
    {
        OutError = FString::Printf(TEXT("failed to create texture '%s'"), *DestinationPath);
        return nullptr;
    }

    Texture->Modify();
    Texture->Source.Init(Image.SizeX, Image.SizeY, 1, 1, TSF_BGRA8, Image.RawData.GetData());
    Texture->SRGB = bSRGB;
    Texture->MipGenSettings = TMGS_FromTextureGroup;
    Texture->CompressionSettings = TC_Default;
    Texture->PostEditChange();
    Texture->MarkPackageDirty();
    if (UPackage* Package = Texture->GetOutermost())
    {
        Package->MarkPackageDirty();
    }
    if (bOutCreated)
    {
        FAssetRegistryModule::AssetCreated(Texture);
    }

    return Texture;
}

bool ImportAssetFromSpec(
    const TSharedPtr<FJsonObject>& SpecData,
    const FString& ForcedKind,
    TArray<TSharedPtr<FJsonValue>>& Assets,
    FString& OutFatalError)
{
    FString SourceFile;
    FString DestinationPath;
    if (!SpecData->TryGetStringField(TEXT("source_file"), SourceFile) || SourceFile.IsEmpty() ||
        !SpecData->TryGetStringField(TEXT("destination_path"), DestinationPath) || DestinationPath.IsEmpty())
    {
        OutFatalError = TEXT("asset import requires source_file and destination_path");
        return false;
    }

    FString PackagePath;
    FString AssetName;
    FString PathError;
    if (!SplitAssetPath(DestinationPath, PackagePath, AssetName, PathError))
    {
        OutFatalError = PathError;
        return false;
    }

    FString Kind = ForcedKind;
    if (Kind.IsEmpty())
    {
        SpecData->TryGetStringField(TEXT("kind"), Kind);
    }
    Kind.ToLowerInline();
    if (Kind != TEXT("texture") && Kind != TEXT("static_mesh"))
    {
        OutFatalError = FString::Printf(TEXT("unsupported asset import kind '%s'"), *Kind);
        return false;
    }

    bool bReplaceExisting = false;
    bool bSave = false;
    bool bGenerateCollision = Kind == TEXT("static_mesh");
    SpecData->TryGetBoolField(TEXT("replace_existing"), bReplaceExisting);
    SpecData->TryGetBoolField(TEXT("save"), bSave);
    SpecData->TryGetBoolField(TEXT("generate_collision"), bGenerateCollision);

    if (!FPaths::FileExists(SourceFile))
    {
        AddAssetImportOperation(Assets, SourceFile, DestinationPath, FString(), false, TEXT("source file does not exist"));
        return true;
    }

    UObject* ExistingAsset = LoadObject<UObject>(nullptr, *ToObjectPath(DestinationPath));
    if (ExistingAsset && !bReplaceExisting)
    {
        AddAssetImportOperation(Assets, SourceFile, DestinationPath, ExistingAsset->GetClass()->GetName(), false, TEXT("asset already exists"));
        return true;
    }

    if (Kind == TEXT("texture"))
    {
        bool bSRGB = true;
        SpecData->TryGetBoolField(TEXT("srgb"), bSRGB);
        bool bCreated = false;
        FString TextureError;
        UTexture2D* Texture = ImportTextureAssetDirect(SourceFile, DestinationPath, bReplaceExisting, bSRGB, bCreated, TextureError);
        if (!Texture)
        {
            AddAssetImportOperation(Assets, SourceFile, DestinationPath, FString(), false, TextureError);
            return true;
        }
        if (bSave)
        {
            UPackage* TexturePackage = Texture->GetOutermost();
            const FString PackageFilename = FPackageName::LongPackageNameToFilename(
                TexturePackage->GetName(),
                FPackageName::GetAssetPackageExtension());
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            SaveArgs.SaveFlags = SAVE_NoError;
            if (!UPackage::SavePackage(TexturePackage, Texture, *PackageFilename, SaveArgs))
            {
                AddAssetImportOperation(Assets, SourceFile, DestinationPath, Texture->GetClass()->GetName(), false, TEXT("failed to save texture package"));
                return true;
            }
        }

        AddAssetImportOperation(Assets, SourceFile, DestinationPath, Texture->GetClass()->GetName(), true, FString());
        return true;
    }

    UAssetImportTask* Task = NewObject<UAssetImportTask>();
    Task->AddToRoot();
    Task->Filename = SourceFile;
    Task->DestinationPath = PackagePath;
    Task->DestinationName = AssetName;
    Task->bReplaceExisting = bReplaceExisting;
    Task->bReplaceExistingSettings = bReplaceExisting;
    Task->bAutomated = true;
    Task->bSave = bSave;
    Task->bAsync = false;

    TArray<UAssetImportTask*> Tasks;
    Tasks.Add(Task);
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    AssetToolsModule.Get().ImportAssetTasks(Tasks);

    UObject* ImportedAsset = nullptr;
    for (UObject* Object : Task->GetObjects())
    {
        if ((Kind == TEXT("texture") && Object && Object->IsA<UTexture2D>()) ||
            (Kind == TEXT("static_mesh") && Object && Object->IsA<UStaticMesh>()))
        {
            ImportedAsset = Object;
            break;
        }
    }

    if (!ImportedAsset)
    {
        ImportedAsset = LoadObject<UObject>(nullptr, *ToObjectPath(DestinationPath));
    }

    Task->RemoveFromRoot();

    if (!ImportedAsset)
    {
        AddAssetImportOperation(Assets, SourceFile, DestinationPath, FString(), false, TEXT("import failed"));
        return true;
    }

    if (UTexture2D* Texture = Cast<UTexture2D>(ImportedAsset))
    {
        bool bSRGB = Texture->SRGB;
        if (SpecData->TryGetBoolField(TEXT("srgb"), bSRGB))
        {
            Texture->Modify();
            Texture->SRGB = bSRGB;
            Texture->PostEditChange();
            Texture->MarkPackageDirty();
        }
    }
    else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(ImportedAsset); StaticMesh && bGenerateCollision)
    {
        FString CollisionError;
        ConfigureStaticMeshCollision(*StaticMesh, TEXT("use_simple_as_complex"), true, CollisionError);
    }

    ImportedAsset->MarkPackageDirty();
    AddAssetImportOperation(Assets, SourceFile, DestinationPath, ImportedAsset->GetClass()->GetName(), true, FString());
    return true;
}

bool SpawnRoadNetwork(
    UWorld& World,
    const TSharedPtr<FJsonObject>& SpecData,
    UStaticMesh& RoadMesh,
    TArray<TSharedPtr<FJsonValue>>& Spawned,
    FSceneAssemblyCounts& Counts,
    FString& OutError)
{
    FString NamePrefix;
    if (!SpecData->TryGetStringField(TEXT("name_prefix"), NamePrefix) || NamePrefix.IsEmpty())
    {
        OutError = TEXT("scene road network requires name_prefix");
        return false;
    }

    FString Scene;
    SpecData->TryGetStringField(TEXT("scene"), Scene);
    FString Group;
    SpecData->TryGetStringField(TEXT("group"), Group);

    FVector Origin = FVector::ZeroVector;
    FVector2D BlockSize(2400.0, 1800.0);
    if (!ReadVectorField(SpecData, TEXT("origin"), FVector::ZeroVector, Origin) ||
        !ReadVector2DField(SpecData, TEXT("block_size"), BlockSize, BlockSize))
    {
        OutError = TEXT("origin must have 3 numbers and block_size must have 2 numbers");
        return false;
    }

    int32 Rows = 2;
    int32 Columns = 2;
    double RoadWidth = 320.0;
    double RoadThickness = 20.0;
    SpecData->TryGetNumberField(TEXT("rows"), Rows);
    SpecData->TryGetNumberField(TEXT("columns"), Columns);
    SpecData->TryGetNumberField(TEXT("road_width"), RoadWidth);
    SpecData->TryGetNumberField(TEXT("road_thickness"), RoadThickness);
    Rows = FMath::Clamp(Rows, 1, 100);
    Columns = FMath::Clamp(Columns, 1, 100);
    RoadWidth = FMath::Max(RoadWidth, 1.0);
    RoadThickness = FMath::Max(RoadThickness, 1.0);
    BlockSize.X = FMath::Max(BlockSize.X, 1.0);
    BlockSize.Y = FMath::Max(BlockSize.Y, 1.0);

    const double TotalX = Columns * BlockSize.X + (Columns + 1) * RoadWidth;
    const double TotalY = Rows * BlockSize.Y + (Rows + 1) * RoadWidth;
    const double Z = Origin.Z + RoadThickness * 0.5;

    for (int32 Column = 0; Column <= Columns; ++Column)
    {
        const double X = Origin.X - TotalX * 0.5 + RoadWidth * 0.5 + Column * (BlockSize.X + RoadWidth);
        const FString Name = FString::Printf(TEXT("%s_V_%03d"), *NamePrefix, Column);
        if (SpawnSceneStaticMeshActor(
                World,
                Name,
                RoadMesh,
                FVector(X, Origin.Y, Z),
                FRotator::ZeroRotator,
                FVector(RoadWidth / 100.0, TotalY / 100.0, RoadThickness / 100.0),
                Scene,
                Group,
                TEXT("road"),
                Spawned))
        {
            ++Counts.RoadCount;
        }
    }

    for (int32 Row = 0; Row <= Rows; ++Row)
    {
        const double Y = Origin.Y - TotalY * 0.5 + RoadWidth * 0.5 + Row * (BlockSize.Y + RoadWidth);
        const FString Name = FString::Printf(TEXT("%s_H_%03d"), *NamePrefix, Row);
        if (SpawnSceneStaticMeshActor(
                World,
                Name,
                RoadMesh,
                FVector(Origin.X, Y, Z),
                FRotator::ZeroRotator,
                FVector(TotalX / 100.0, RoadWidth / 100.0, RoadThickness / 100.0),
                Scene,
                Group,
                TEXT("road"),
                Spawned))
        {
            ++Counts.RoadCount;
        }
    }

    return true;
}

bool SpawnGridPlacement(
    UWorld& World,
    const TSharedPtr<FJsonObject>& SpecData,
    UStaticMesh& Mesh,
    const FString& SceneActorKind,
    TArray<TSharedPtr<FJsonValue>>& Spawned,
    FSceneAssemblyCounts& Counts,
    FString& OutError)
{
    FString NamePrefix;
    if (!SpecData->TryGetStringField(TEXT("name_prefix"), NamePrefix) || NamePrefix.IsEmpty())
    {
        OutError = TEXT("scene grid placement requires name_prefix");
        return false;
    }

    FString Scene;
    SpecData->TryGetStringField(TEXT("scene"), Scene);
    FString Group;
    SpecData->TryGetStringField(TEXT("group"), Group);

    FVector Origin = FVector::ZeroVector;
    FVector2D Spacing(600.0, 600.0);
    FRotator Rotation = FRotator::ZeroRotator;
    FVector Scale(1.0, 1.0, 1.0);
    if (!ReadVectorField(SpecData, TEXT("origin"), FVector::ZeroVector, Origin) ||
        !ReadVector2DField(SpecData, TEXT("spacing"), Spacing, Spacing) ||
        !ReadRotatorField(SpecData, TEXT("rotation"), FRotator::ZeroRotator, Rotation) ||
        !ReadVectorField(SpecData, TEXT("scale"), FVector(1.0, 1.0, 1.0), Scale))
    {
        OutError = TEXT("origin/rotation/scale must have 3 numbers and spacing must have 2 numbers");
        return false;
    }

    int32 Rows = 2;
    int32 Columns = 2;
    double YawVariation = 0.0;
    double ScaleVariation = 0.0;
    int32 Seed = 0;
    SpecData->TryGetNumberField(TEXT("rows"), Rows);
    SpecData->TryGetNumberField(TEXT("columns"), Columns);
    SpecData->TryGetNumberField(TEXT("yaw_variation"), YawVariation);
    SpecData->TryGetNumberField(TEXT("scale_variation"), ScaleVariation);
    SpecData->TryGetNumberField(TEXT("seed"), Seed);
    Rows = FMath::Clamp(Rows, 1, 200);
    Columns = FMath::Clamp(Columns, 1, 200);
    Spacing.X = FMath::Max(Spacing.X, 1.0);
    Spacing.Y = FMath::Max(Spacing.Y, 1.0);
    YawVariation = FMath::Max(YawVariation, 0.0);
    ScaleVariation = FMath::Clamp(ScaleVariation, 0.0, 0.95);

    FRandomStream Random(Seed);
    const double StartX = Origin.X - ((Columns - 1) * Spacing.X) * 0.5;
    const double StartY = Origin.Y - ((Rows - 1) * Spacing.Y) * 0.5;
    for (int32 Row = 0; Row < Rows; ++Row)
    {
        for (int32 Column = 0; Column < Columns; ++Column)
        {
            FRotator ActorRotation = Rotation;
            if (YawVariation > 0.0)
            {
                ActorRotation.Yaw += Random.FRandRange(-YawVariation, YawVariation);
            }

            FVector ActorScale = Scale;
            if (ScaleVariation > 0.0)
            {
                const float ScaleFactor = Random.FRandRange(1.0f - ScaleVariation, 1.0f + ScaleVariation);
                ActorScale *= ScaleFactor;
            }

            const FString Name = FString::Printf(TEXT("%s_%03d_%03d"), *NamePrefix, Row, Column);
            if (SpawnSceneStaticMeshActor(
                    World,
                    Name,
                    Mesh,
                    FVector(StartX + Column * Spacing.X, StartY + Row * Spacing.Y, Origin.Z),
                    ActorRotation,
                    ActorScale,
                    Scene,
                    Group,
                    SceneActorKind,
                    Spawned))
            {
                IncrementSceneCount(Counts, SceneActorKind);
            }
        }
    }

    return true;
}

bool SpawnCityBlock(
    UWorld& World,
    const TSharedPtr<FJsonObject>& SpecData,
    UStaticMesh& RoadMesh,
    UStaticMesh& SidewalkMesh,
    UStaticMesh& BuildingMesh,
    TArray<TSharedPtr<FJsonValue>>& Spawned,
    FSceneAssemblyCounts& Counts,
    FString& OutError)
{
    FString NamePrefix;
    if (!SpecData->TryGetStringField(TEXT("name_prefix"), NamePrefix) || NamePrefix.IsEmpty())
    {
        OutError = TEXT("scene city block requires name_prefix");
        return false;
    }

    FString Scene;
    SpecData->TryGetStringField(TEXT("scene"), Scene);
    FString Group;
    SpecData->TryGetStringField(TEXT("group"), Group);

    FVector Origin = FVector::ZeroVector;
    FVector2D Size(2400.0, 1800.0);
    FVector BuildingScale(3.0, 3.0, 8.0);
    if (!ReadVectorField(SpecData, TEXT("origin"), FVector::ZeroVector, Origin) ||
        !ReadVector2DField(SpecData, TEXT("size"), Size, Size) ||
        !ReadVectorField(SpecData, TEXT("building_scale"), BuildingScale, BuildingScale))
    {
        OutError = TEXT("origin/building_scale must have 3 numbers and size must have 2 numbers");
        return false;
    }

    double RoadWidth = 320.0;
    double SidewalkWidth = 180.0;
    int32 BuildingRows = 2;
    int32 BuildingColumns = 2;
    int32 Seed = 0;
    SpecData->TryGetNumberField(TEXT("road_width"), RoadWidth);
    SpecData->TryGetNumberField(TEXT("sidewalk_width"), SidewalkWidth);
    SpecData->TryGetNumberField(TEXT("building_rows"), BuildingRows);
    SpecData->TryGetNumberField(TEXT("building_columns"), BuildingColumns);
    SpecData->TryGetNumberField(TEXT("seed"), Seed);
    RoadWidth = FMath::Max(RoadWidth, 1.0);
    SidewalkWidth = FMath::Max(SidewalkWidth, 0.0);
    Size.X = FMath::Max(Size.X, 1.0);
    Size.Y = FMath::Max(Size.Y, 1.0);
    BuildingRows = FMath::Clamp(BuildingRows, 1, 100);
    BuildingColumns = FMath::Clamp(BuildingColumns, 1, 100);

    const double RoadThickness = 20.0;
    const double SidewalkThickness = 12.0;
    const double TotalX = Size.X + 2.0 * RoadWidth;
    const double TotalY = Size.Y + 2.0 * RoadWidth;
    struct FRectActor
    {
        FString Suffix;
        FVector Location;
        FVector Scale;
        UStaticMesh* Mesh;
        FString Kind;
    };

    TArray<FRectActor> Rects;
    Rects.Add({TEXT("Road_N"), Origin + FVector(0.0, Size.Y * 0.5 + RoadWidth * 0.5, RoadThickness * 0.5), FVector(TotalX / 100.0, RoadWidth / 100.0, RoadThickness / 100.0), &RoadMesh, TEXT("road")});
    Rects.Add({TEXT("Road_S"), Origin + FVector(0.0, -Size.Y * 0.5 - RoadWidth * 0.5, RoadThickness * 0.5), FVector(TotalX / 100.0, RoadWidth / 100.0, RoadThickness / 100.0), &RoadMesh, TEXT("road")});
    Rects.Add({TEXT("Road_E"), Origin + FVector(Size.X * 0.5 + RoadWidth * 0.5, 0.0, RoadThickness * 0.5), FVector(RoadWidth / 100.0, TotalY / 100.0, RoadThickness / 100.0), &RoadMesh, TEXT("road")});
    Rects.Add({TEXT("Road_W"), Origin + FVector(-Size.X * 0.5 - RoadWidth * 0.5, 0.0, RoadThickness * 0.5), FVector(RoadWidth / 100.0, TotalY / 100.0, RoadThickness / 100.0), &RoadMesh, TEXT("road")});

    if (SidewalkWidth > 0.0)
    {
        Rects.Add({TEXT("Sidewalk_N"), Origin + FVector(0.0, Size.Y * 0.5 - SidewalkWidth * 0.5, SidewalkThickness * 0.5), FVector(Size.X / 100.0, SidewalkWidth / 100.0, SidewalkThickness / 100.0), &SidewalkMesh, TEXT("sidewalk")});
        Rects.Add({TEXT("Sidewalk_S"), Origin + FVector(0.0, -Size.Y * 0.5 + SidewalkWidth * 0.5, SidewalkThickness * 0.5), FVector(Size.X / 100.0, SidewalkWidth / 100.0, SidewalkThickness / 100.0), &SidewalkMesh, TEXT("sidewalk")});
        Rects.Add({TEXT("Sidewalk_E"), Origin + FVector(Size.X * 0.5 - SidewalkWidth * 0.5, 0.0, SidewalkThickness * 0.5), FVector(SidewalkWidth / 100.0, FMath::Max(1.0, Size.Y - 2.0 * SidewalkWidth) / 100.0, SidewalkThickness / 100.0), &SidewalkMesh, TEXT("sidewalk")});
        Rects.Add({TEXT("Sidewalk_W"), Origin + FVector(-Size.X * 0.5 + SidewalkWidth * 0.5, 0.0, SidewalkThickness * 0.5), FVector(SidewalkWidth / 100.0, FMath::Max(1.0, Size.Y - 2.0 * SidewalkWidth) / 100.0, SidewalkThickness / 100.0), &SidewalkMesh, TEXT("sidewalk")});
    }

    for (const FRectActor& Rect : Rects)
    {
        if (SpawnSceneStaticMeshActor(World, FString::Printf(TEXT("%s_%s"), *NamePrefix, *Rect.Suffix), *Rect.Mesh, Rect.Location, FRotator::ZeroRotator, Rect.Scale, Scene, Group, Rect.Kind, Spawned))
        {
            IncrementSceneCount(Counts, Rect.Kind);
        }
    }

    const double Padding = FMath::Max(SidewalkWidth + 180.0, 1.0);
    const double UsableX = FMath::Max(Size.X - 2.0 * Padding, 1.0);
    const double UsableY = FMath::Max(Size.Y - 2.0 * Padding, 1.0);
    const double StepX = BuildingColumns > 1 ? UsableX / (BuildingColumns - 1) : 0.0;
    const double StepY = BuildingRows > 1 ? UsableY / (BuildingRows - 1) : 0.0;
    const double StartX = Origin.X - UsableX * 0.5;
    const double StartY = Origin.Y - UsableY * 0.5;
    FRandomStream Random(Seed);
    for (int32 Row = 0; Row < BuildingRows; ++Row)
    {
        for (int32 Column = 0; Column < BuildingColumns; ++Column)
        {
            FVector Scale = BuildingScale * Random.FRandRange(0.85f, 1.25f);
            const FString Name = FString::Printf(TEXT("%s_Building_%03d_%03d"), *NamePrefix, Row, Column);
            if (SpawnSceneStaticMeshActor(
                    World,
                    Name,
                    BuildingMesh,
                    FVector(StartX + Column * StepX, StartY + Row * StepY, Origin.Z),
                    FRotator(0.0, Random.FRandRange(-8.0f, 8.0f), 0.0),
                    Scale,
                    Scene,
                    Group,
                    TEXT("building"),
                    Spawned))
            {
                ++Counts.BuildingCount;
            }
        }
    }

    return true;
}

void GetDistrictPresetScale(const FString& Preset, FVector& OutScale, int32& OutBuildingsPerBlock)
{
    FString Normalized = Preset;
    Normalized.ToLowerInline();
    if (Normalized == TEXT("residential"))
    {
        OutScale = FVector(2.0, 2.0, 2.4);
        OutBuildingsPerBlock = 2;
    }
    else if (Normalized == TEXT("industrial"))
    {
        OutScale = FVector(4.5, 3.5, 3.0);
        OutBuildingsPerBlock = 2;
    }
    else if (Normalized == TEXT("beach"))
    {
        OutScale = FVector(2.2, 2.2, 1.6);
        OutBuildingsPerBlock = 1;
    }
    else if (Normalized == TEXT("hills"))
    {
        OutScale = FVector(2.0, 2.0, 2.8);
        OutBuildingsPerBlock = 1;
    }
    else
    {
        OutScale = FVector(2.8, 2.8, 9.0);
        OutBuildingsPerBlock = 4;
    }
}

bool SpawnDistrict(
    UWorld& World,
    const TSharedPtr<FJsonObject>& SpecData,
    UStaticMesh& RoadMesh,
    UStaticMesh& BuildingMesh,
    TArray<TSharedPtr<FJsonValue>>& Spawned,
    FSceneAssemblyCounts& Counts,
    FString& OutError)
{
    FString NamePrefix;
    if (!SpecData->TryGetStringField(TEXT("name_prefix"), NamePrefix) || NamePrefix.IsEmpty())
    {
        OutError = TEXT("scene district requires name_prefix");
        return false;
    }

    FString Scene;
    SpecData->TryGetStringField(TEXT("scene"), Scene);
    FString Group;
    SpecData->TryGetStringField(TEXT("group"), Group);
    FString Preset = TEXT("downtown");
    SpecData->TryGetStringField(TEXT("preset"), Preset);

    FVector Origin = FVector::ZeroVector;
    FVector2D BlockSize(2400.0, 1800.0);
    FVector2D Blocks(2.0, 2.0);
    if (!ReadVectorField(SpecData, TEXT("origin"), FVector::ZeroVector, Origin) ||
        !ReadVector2DField(SpecData, TEXT("block_size"), BlockSize, BlockSize) ||
        !ReadVector2DField(SpecData, TEXT("blocks"), Blocks, Blocks))
    {
        OutError = TEXT("origin must have 3 numbers and block_size/blocks must have 2 numbers");
        return false;
    }

    int32 BlocksX = FMath::Clamp(FMath::RoundToInt(Blocks.X), 1, 50);
    int32 BlocksY = FMath::Clamp(FMath::RoundToInt(Blocks.Y), 1, 50);
    double RoadWidth = 320.0;
    int32 Seed = 0;
    SpecData->TryGetNumberField(TEXT("road_width"), RoadWidth);
    SpecData->TryGetNumberField(TEXT("seed"), Seed);
    RoadWidth = FMath::Max(RoadWidth, 1.0);

    TSharedRef<FJsonObject> RoadSpec = MakeShared<FJsonObject>();
    RoadSpec->SetStringField(TEXT("name_prefix"), FString::Printf(TEXT("%s_Road"), *NamePrefix));
    RoadSpec->SetStringField(TEXT("scene"), Scene);
    RoadSpec->SetStringField(TEXT("group"), Group);
    RoadSpec->SetArrayField(TEXT("origin"), MakeVectorArray(Origin.X, Origin.Y, Origin.Z));
    TArray<TSharedPtr<FJsonValue>> BlockSizeValue;
    BlockSizeValue.Add(MakeNumberValue(BlockSize.X));
    BlockSizeValue.Add(MakeNumberValue(BlockSize.Y));
    RoadSpec->SetArrayField(TEXT("block_size"), BlockSizeValue);
    RoadSpec->SetNumberField(TEXT("rows"), BlocksY);
    RoadSpec->SetNumberField(TEXT("columns"), BlocksX);
    RoadSpec->SetNumberField(TEXT("road_width"), RoadWidth);
    RoadSpec->SetNumberField(TEXT("road_thickness"), 20.0);
    if (!SpawnRoadNetwork(World, RoadSpec, RoadMesh, Spawned, Counts, OutError))
    {
        return false;
    }

    FVector BaseScale;
    int32 BuildingsPerBlock = 4;
    GetDistrictPresetScale(Preset, BaseScale, BuildingsPerBlock);
    const double TotalX = BlocksX * BlockSize.X + (BlocksX + 1) * RoadWidth;
    const double TotalY = BlocksY * BlockSize.Y + (BlocksY + 1) * RoadWidth;
    FRandomStream Random(Seed);

    TArray<FVector2D> SlotOffsets;
    SlotOffsets.Add(FVector2D(-0.22, -0.22));
    SlotOffsets.Add(FVector2D(0.22, -0.22));
    SlotOffsets.Add(FVector2D(-0.22, 0.22));
    SlotOffsets.Add(FVector2D(0.22, 0.22));
    SlotOffsets.Add(FVector2D(0.0, -0.32));
    SlotOffsets.Add(FVector2D(0.0, 0.32));

    for (int32 Y = 0; Y < BlocksY; ++Y)
    {
        for (int32 X = 0; X < BlocksX; ++X)
        {
            const double CenterX = Origin.X - TotalX * 0.5 + RoadWidth + BlockSize.X * 0.5 + X * (BlockSize.X + RoadWidth);
            const double CenterY = Origin.Y - TotalY * 0.5 + RoadWidth + BlockSize.Y * 0.5 + Y * (BlockSize.Y + RoadWidth);
            for (int32 Slot = 0; Slot < BuildingsPerBlock; ++Slot)
            {
                const FVector2D Offset = SlotOffsets[Slot % SlotOffsets.Num()];
                const FVector Location(
                    CenterX + Offset.X * BlockSize.X,
                    CenterY + Offset.Y * BlockSize.Y,
                    Origin.Z);
                FVector Scale = BaseScale * Random.FRandRange(0.75f, 1.35f);
                const FString Name = FString::Printf(TEXT("%s_Building_%03d_%03d_%02d"), *NamePrefix, Y, X, Slot);
                if (SpawnSceneStaticMeshActor(
                        World,
                        Name,
                        BuildingMesh,
                        Location,
                        FRotator(0.0, Random.FRandRange(-12.0f, 12.0f), 0.0),
                        Scale,
                        Scene,
                        Group,
                        TEXT("building"),
                        Spawned))
                {
                    ++Counts.BuildingCount;
                }
            }
        }
    }

    return true;
}

void AddGeneratedLightingTags(AActor& Actor, const TArray<FString>& ExtraTags = TArray<FString>())
{
    Actor.Tags.AddUnique(TEXT("mcp.generated"));
    Actor.Tags.AddUnique(TEXT("mcp.lighting"));
    for (const FString& Tag : ExtraTags)
    {
        if (!Tag.IsEmpty())
        {
            Actor.Tags.AddUnique(FName(*Tag));
        }
    }
}

template <typename TActor>
TActor* FindActorByLabel(UWorld& World, const FString& Label)
{
    for (TActorIterator<TActor> It(&World); It; ++It)
    {
        TActor* Actor = *It;
        if (IsValid(Actor) && Actor->GetActorLabel() == Label)
        {
            return Actor;
        }
    }

    return nullptr;
}

template <typename TActor>
TActor* FindOrSpawnNamedActor(UWorld& World, const FString& Label, const FVector& Location, const FRotator& Rotation, bool& bOutCreated)
{
    bOutCreated = false;
    if (TActor* Existing = FindActorByLabel<TActor>(World, Label))
    {
        return Existing;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = MakeUniqueObjectName(&World, TActor::StaticClass(), FName(*Label));
    TActor* Actor = World.SpawnActor<TActor>(Location, Rotation, SpawnParameters);
    if (Actor)
    {
        Actor->SetActorLabel(Label);
        AddGeneratedLightingTags(*Actor);
        bOutCreated = true;
    }

    return Actor;
}

void ConfigureExclusiveDirectionalLight(UWorld& World, ADirectionalLight& ActiveLight)
{
    for (TActorIterator<ADirectionalLight> It(&World); It; ++It)
    {
        ADirectionalLight* DirectionalLight = *It;
        if (!IsValid(DirectionalLight))
        {
            continue;
        }

        UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(DirectionalLight->GetLightComponent());
        if (!LightComponent)
        {
            continue;
        }

        DirectionalLight->Modify();
        LightComponent->Modify();
        const bool bIsActive = DirectionalLight == &ActiveLight;
        LightComponent->SetVisibility(bIsActive);
        if (!bIsActive)
        {
            LightComponent->SetIntensity(0.0f);
            DirectionalLight->Tags.AddUnique(TEXT("mcp.disabled_directional"));
        }
    }
}

ADirectionalLight* ConfigureDirectionalLight(
    UWorld& World,
    const FString& Label,
    const FRotator& Rotation,
    double Intensity,
    const FLinearColor& Color,
    TArray<FString>& Changed)
{
    bool bCreated = false;
    ADirectionalLight* DirectionalLight =
        FindOrSpawnNamedActor<ADirectionalLight>(World, Label, FVector::ZeroVector, Rotation, bCreated);
    if (!DirectionalLight)
    {
        return nullptr;
    }

    DirectionalLight->Modify();
    DirectionalLight->SetActorRotation(Rotation);
    AddGeneratedLightingTags(*DirectionalLight);

    if (UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(DirectionalLight->GetLightComponent()))
    {
        LightComponent->Modify();
        LightComponent->SetVisibility(true);
        LightComponent->SetIntensity(static_cast<float>(Intensity));
        LightComponent->SetLightColor(Color);
        LightComponent->MarkRenderStateDirty();
    }

    ConfigureExclusiveDirectionalLight(World, *DirectionalLight);
    AddChangedActor(Changed, *DirectionalLight);
    return DirectionalLight;
}

ASkyLight* ConfigureSkyLight(UWorld& World, double SkyIntensity, const FLinearColor& LowerHemisphereColor, TArray<FString>& Changed)
{
    bool bCreated = false;
    ASkyLight* SkyLight = FindOrSpawnNamedActor<ASkyLight>(World, TEXT("MCP_SkyLight"), FVector::ZeroVector, FRotator::ZeroRotator, bCreated);
    if (!SkyLight)
    {
        return nullptr;
    }

    SkyLight->Modify();
    AddGeneratedLightingTags(*SkyLight);
    if (USkyLightComponent* SkyComponent = SkyLight->GetLightComponent())
    {
        SkyComponent->Modify();
        SkyComponent->SetVisibility(true);
        SkyComponent->SetIntensity(static_cast<float>(SkyIntensity));
        SkyComponent->SetLightColor(FLinearColor::White);
        SkyComponent->bLowerHemisphereIsBlack = false;
        SkyComponent->LowerHemisphereColor = LowerHemisphereColor;
        SkyComponent->RecaptureSky();
    }

    AddChangedActor(Changed, *SkyLight);
    return SkyLight;
}

ASkyAtmosphere* ConfigureSkyAtmosphere(UWorld& World, TArray<FString>& Changed)
{
    bool bCreated = false;
    ASkyAtmosphere* Atmosphere =
        FindOrSpawnNamedActor<ASkyAtmosphere>(World, TEXT("MCP_SkyAtmosphere"), FVector::ZeroVector, FRotator::ZeroRotator, bCreated);
    if (!Atmosphere)
    {
        return nullptr;
    }

    Atmosphere->Modify();
    AddGeneratedLightingTags(*Atmosphere);
    AddChangedActor(Changed, *Atmosphere);
    return Atmosphere;
}

AExponentialHeightFog* ConfigureFog(
    UWorld& World,
    double Density,
    double HeightFalloff,
    const FLinearColor& Color,
    double StartDistance,
    TArray<FString>& Changed)
{
    bool bCreated = false;
    AExponentialHeightFog* Fog =
        FindOrSpawnNamedActor<AExponentialHeightFog>(World, TEXT("MCP_NightFog"), FVector::ZeroVector, FRotator::ZeroRotator, bCreated);
    if (!Fog)
    {
        return nullptr;
    }

    Fog->Modify();
    AddGeneratedLightingTags(*Fog);
    if (UExponentialHeightFogComponent* FogComponent = Fog->GetComponent())
    {
        FogComponent->Modify();
        FogComponent->SetFogDensity(static_cast<float>(FMath::Max(Density, 0.0)));
        FogComponent->SetFogHeightFalloff(static_cast<float>(FMath::Max(HeightFalloff, 0.0)));
        FogComponent->SetFogInscatteringColor(Color);
        FogComponent->StartDistance = static_cast<float>(FMath::Max(StartDistance, 0.0));
        FogComponent->MarkRenderStateDirty();
    }

    AddChangedActor(Changed, *Fog);
    return Fog;
}

APostProcessVolume* ConfigurePostProcess(
    UWorld& World,
    double ExposureCompensation,
    double MinBrightness,
    double MaxBrightness,
    double BloomIntensity,
    TArray<FString>& Changed)
{
    bool bCreated = false;
    APostProcessVolume* Volume =
        FindOrSpawnNamedActor<APostProcessVolume>(World, TEXT("MCP_PostProcess"), FVector::ZeroVector, FRotator::ZeroRotator, bCreated);
    if (!Volume)
    {
        return nullptr;
    }

    Volume->Modify();
    Volume->bUnbound = true;
    AddGeneratedLightingTags(*Volume);
    FPostProcessSettings& Settings = Volume->Settings;
    Settings.bOverride_AutoExposureBias = true;
    Settings.AutoExposureBias = static_cast<float>(ExposureCompensation);
    Settings.bOverride_AutoExposureMinBrightness = true;
    Settings.AutoExposureMinBrightness = static_cast<float>(FMath::Max(MinBrightness, 0.0));
    Settings.bOverride_AutoExposureMaxBrightness = true;
    Settings.AutoExposureMaxBrightness = static_cast<float>(FMath::Max(MaxBrightness, 0.0));
    Settings.bOverride_BloomIntensity = true;
    Settings.BloomIntensity = static_cast<float>(FMath::Max(BloomIntensity, 0.0));

    AddChangedActor(Changed, *Volume);
    return Volume;
}

bool IsLightActorKind(const AActor* Actor, const FString& Kind)
{
    if (Kind.Equals(TEXT("point"), ESearchCase::IgnoreCase))
    {
        return Cast<APointLight>(Actor) != nullptr;
    }

    if (Kind.Equals(TEXT("rect"), ESearchCase::IgnoreCase))
    {
        return Cast<ARectLight>(Actor) != nullptr;
    }

    if (Kind.Equals(TEXT("spot"), ESearchCase::IgnoreCase))
    {
        return Cast<ASpotLight>(Actor) != nullptr;
    }

    return false;
}

USCS_Node* FindSCSNodeByName(UBlueprint& Blueprint, const FName ComponentName)
{
    if (!Blueprint.SimpleConstructionScript)
    {
        return nullptr;
    }

    for (USCS_Node* Node : Blueprint.SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName() == ComponentName)
        {
            return Node;
        }
    }

    return nullptr;
}

template <typename TComponent>
TComponent* FindOrCreateBlueprintComponent(UBlueprint& Blueprint, const FName ComponentName, bool& bOutCreated)
{
    bOutCreated = false;
    if (!Blueprint.SimpleConstructionScript)
    {
        Blueprint.SimpleConstructionScript = NewObject<USimpleConstructionScript>(&Blueprint);
    }

    if (USCS_Node* ExistingNode = FindSCSNodeByName(Blueprint, ComponentName))
    {
        return Cast<TComponent>(ExistingNode->ComponentTemplate);
    }

    USCS_Node* Node = Blueprint.SimpleConstructionScript->CreateNode(TComponent::StaticClass(), ComponentName);
    if (!Node)
    {
        return nullptr;
    }

    Blueprint.SimpleConstructionScript->AddNode(Node);
    bOutCreated = true;
    return Cast<TComponent>(Node->ComponentTemplate);
}

void ApplyRelativeTransform(USceneComponent& Component, const TSharedPtr<FJsonObject>& Data)
{
    TSharedPtr<FJsonObject> TransformData = Data;
    const TSharedPtr<FJsonObject>* NestedTransform = nullptr;
    if (Data.IsValid() && Data->TryGetObjectField(TEXT("transform"), NestedTransform) && NestedTransform != nullptr &&
        NestedTransform->IsValid())
    {
        TransformData = *NestedTransform;
    }

    FVector Location = FVector::ZeroVector;
    FVector Scale = FVector(1.0, 1.0, 1.0);
    FRotator Rotation = FRotator::ZeroRotator;
    ReadVectorField(TransformData, TEXT("location"), FVector::ZeroVector, Location);
    ReadRotatorField(TransformData, TEXT("rotation"), FRotator::ZeroRotator, Rotation);
    ReadVectorField(TransformData, TEXT("scale"), FVector(1.0, 1.0, 1.0), Scale);
    Component.SetRelativeLocation(Location);
    Component.SetRelativeRotation(Rotation);
    Component.SetRelativeScale3D(Scale);
}

UBlueprint* LoadBlueprintAsset(const FString& BlueprintPath)
{
    return LoadObject<UBlueprint>(nullptr, *ToObjectPath(BlueprintPath));
}

UClass* ResolveBlueprintParentClass(const FString& ParentClass)
{
    if (ParentClass.IsEmpty() || ParentClass.Equals(TEXT("Actor"), ESearchCase::IgnoreCase))
    {
        return AActor::StaticClass();
    }

    if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *ParentClass))
    {
        return LoadedClass;
    }

    return nullptr;
}

bool ReadAnimationSpecData(const TSharedPtr<FJsonObject>& Data, TSharedPtr<FJsonObject>& OutSpecData)
{
    OutSpecData = Data;
    const TSharedPtr<FJsonObject>* NestedSpec = nullptr;
    if (Data.IsValid() && Data->TryGetObjectField(TEXT("spec"), NestedSpec) && NestedSpec != nullptr && NestedSpec->IsValid())
    {
        OutSpecData = *NestedSpec;
    }

    return OutSpecData.IsValid();
}

bool ConfigureAnimationPreset(
    UUnrealMCPRuntimeAnimationPreset& Preset,
    EUnrealMCPRuntimeAnimationKind Kind,
    const TSharedPtr<FJsonObject>& SpecData)
{
    if (!SpecData.IsValid())
    {
        return false;
    }

    Preset.Kind = Kind;

    FString TargetComponent;
    if (SpecData->TryGetStringField(TEXT("target_component"), TargetComponent) && !TargetComponent.IsEmpty())
    {
        Preset.TargetComponentName = FName(*TargetComponent);
    }
    else
    {
        Preset.TargetComponentName = NAME_None;
    }

    FString ParameterName;
    if (SpecData->TryGetStringField(TEXT("parameter_name"), ParameterName) && !ParameterName.IsEmpty())
    {
        Preset.ParameterName = FName(*ParameterName);
    }

    ReadColorField(SpecData, TEXT("color_a"), Preset.ColorA, Preset.ColorA);
    ReadColorField(SpecData, TEXT("color_b"), Preset.ColorB, Preset.ColorB);

    double NumberValue = 0.0;
    if (SpecData->TryGetNumberField(TEXT("from_scalar"), NumberValue))
    {
        Preset.FromScalar = static_cast<float>(NumberValue);
    }
    if (SpecData->TryGetNumberField(TEXT("to_scalar"), NumberValue))
    {
        Preset.ToScalar = static_cast<float>(NumberValue);
    }
    if (SpecData->TryGetNumberField(TEXT("speed"), NumberValue))
    {
        Preset.Speed = static_cast<float>(NumberValue);
    }
    if (SpecData->TryGetNumberField(TEXT("amplitude"), NumberValue))
    {
        Preset.Amplitude = static_cast<float>(NumberValue);
    }
    if (SpecData->TryGetNumberField(TEXT("base_intensity"), NumberValue))
    {
        Preset.BaseIntensity = static_cast<float>(NumberValue);
    }
    if (SpecData->TryGetNumberField(TEXT("phase_offset"), NumberValue))
    {
        Preset.PhaseOffset = static_cast<float>(NumberValue);
    }

    ReadVectorField(SpecData, TEXT("axis"), Preset.Axis, Preset.Axis);
    return true;
}

void AddPresetsToAnimator(
    UUnrealMCPRuntimeAnimatorComponent& Animator,
    const TArray<UUnrealMCPRuntimeAnimationPreset*>& Presets)
{
    for (UUnrealMCPRuntimeAnimationPreset* Preset : Presets)
    {
        if (Preset)
        {
            Animator.Presets.AddUnique(Preset);
        }
    }
}

uint8 ColorChannelToByte(double Value)
{
    return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0, 1.0) * 255.0));
}

void WriteColorPixel(TArray<uint8>& Pixels, int32 PixelIndex, const FLinearColor& Color)
{
    const int32 Offset = PixelIndex * 4;
    Pixels[Offset] = ColorChannelToByte(Color.B);
    Pixels[Offset + 1] = ColorChannelToByte(Color.G);
    Pixels[Offset + 2] = ColorChannelToByte(Color.R);
    Pixels[Offset + 3] = ColorChannelToByte(Color.A);
}

bool ApplyMaterialParameters(
    const TSharedPtr<FJsonObject>& Data,
    UMaterialInstanceConstant& Instance,
    int32& ScalarCount,
    int32& VectorCount,
    int32& TextureCount)
{
    ScalarCount = 0;
    VectorCount = 0;
    TextureCount = 0;
    if (!Data.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Scalars = nullptr;
    if (Data->TryGetArrayField(TEXT("scalar_parameters"), Scalars))
    {
        for (const TSharedPtr<FJsonValue>& Value : *Scalars)
        {
            const TSharedPtr<FJsonObject>* Param = nullptr;
            const TSharedPtr<FJsonObject>* ParamValue = nullptr;
            FString Name;
            double ScalarValue = 0.0;
            if (Value.IsValid() && Value->TryGetObject(Param) && Param != nullptr && (*Param)->TryGetStringField(TEXT("name"), Name) &&
                (*Param)->TryGetObjectField(TEXT("value"), ParamValue) && ParamValue != nullptr &&
                (*ParamValue)->TryGetNumberField(TEXT("value"), ScalarValue))
            {
                Instance.SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName(*Name)), static_cast<float>(ScalarValue));
                ++ScalarCount;
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Vectors = nullptr;
    if (Data->TryGetArrayField(TEXT("vector_parameters"), Vectors))
    {
        for (const TSharedPtr<FJsonValue>& Value : *Vectors)
        {
            const TSharedPtr<FJsonObject>* Param = nullptr;
            const TSharedPtr<FJsonObject>* ParamValue = nullptr;
            FString Name;
            FLinearColor VectorValue = FLinearColor::White;
            if (Value.IsValid() && Value->TryGetObject(Param) && Param != nullptr && (*Param)->TryGetStringField(TEXT("name"), Name) &&
                (*Param)->TryGetObjectField(TEXT("value"), ParamValue) && ParamValue != nullptr &&
                ReadColorField(*ParamValue, TEXT("value"), FLinearColor::White, VectorValue))
            {
                Instance.SetVectorParameterValueEditorOnly(FMaterialParameterInfo(FName(*Name)), VectorValue);
                ++VectorCount;
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Textures = nullptr;
    if (Data->TryGetArrayField(TEXT("texture_parameters"), Textures))
    {
        for (const TSharedPtr<FJsonValue>& Value : *Textures)
        {
            const TSharedPtr<FJsonObject>* Param = nullptr;
            const TSharedPtr<FJsonObject>* ParamValue = nullptr;
            FString Name;
            FString TexturePath;
            if (Value.IsValid() && Value->TryGetObject(Param) && Param != nullptr && (*Param)->TryGetStringField(TEXT("name"), Name) &&
                (*Param)->TryGetObjectField(TEXT("value"), ParamValue) && ParamValue != nullptr &&
                (*ParamValue)->TryGetStringField(TEXT("value"), TexturePath))
            {
                UTexture* Texture = LoadObject<UTexture>(nullptr, *ToObjectPath(TexturePath));
                if (Texture)
                {
                    Instance.SetTextureParameterValueEditorOnly(FMaterialParameterInfo(FName(*Name)), Texture);
                    ++TextureCount;
                }
            }
        }
    }

    Instance.PostEditChange();
    Instance.MarkPackageDirty();
    return true;
}

bool ActorMatches(const AActor& Actor, const TArray<FString>& Names, const TArray<FString>& Tags);

void ApplyMaterialToActors(
    UWorld& World,
    const FString& MaterialPath,
    const TArray<FString>& Names,
    const TArray<FString>& Tags,
    int32 Slot,
    TArray<TSharedPtr<FJsonValue>>& Applied)
{
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *ToObjectPath(MaterialPath));
    if (!Material)
    {
        return;
    }

    for (TActorIterator<AActor> It(&World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor) || !ActorMatches(*Actor, Names, Tags))
        {
            continue;
        }

        UStaticMeshComponent* MeshComponent = Actor->FindComponentByClass<UStaticMeshComponent>();
        if (!MeshComponent)
        {
            continue;
        }

        Actor->Modify();
        MeshComponent->Modify();
        MeshComponent->SetMaterial(Slot, Material);
        AddAppliedActor(Applied, *Actor, MaterialPath, Slot);
    }
}

bool TryGetCommandData(const TSharedPtr<FJsonObject>& CommandObject, TSharedPtr<FJsonObject>& OutData)
{
    const TSharedPtr<FJsonObject>* Data = nullptr;
    if (CommandObject.IsValid() && CommandObject->TryGetObjectField(TEXT("data"), Data) && Data != nullptr)
    {
        OutData = *Data;
        return OutData.IsValid();
    }

    OutData = MakeShared<FJsonObject>();
    return true;
}

UWorld* GetEditorWorld()
{
    return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

bool ActorMatches(const AActor& Actor, const TArray<FString>& Names, const TArray<FString>& Tags)
{
    bool bMatchesName = Names.IsEmpty();
    for (const FString& Name : Names)
    {
        if (Actor.GetName() == Name || Actor.GetActorLabel() == Name)
        {
            bMatchesName = true;
            break;
        }
    }

    bool bMatchesTag = Tags.IsEmpty();
    for (const FString& Tag : Tags)
    {
        if (Actor.ActorHasTag(FName(*Tag)))
        {
            bMatchesTag = true;
            break;
        }
    }

    return bMatchesName && bMatchesTag;
}

TSharedRef<FJsonObject> ActorToJson(const AActor& Actor)
{
    const FVector Location = Actor.GetActorLocation();
    const FRotator Rotation = Actor.GetActorRotation();
    const FVector Scale = Actor.GetActorScale3D();

    TArray<TSharedPtr<FJsonValue>> Tags;
    for (const FName& Tag : Actor.Tags)
    {
        Tags.Add(MakeStringValue(Tag.ToString()));
    }

    TSharedRef<FJsonObject> Transform = MakeShared<FJsonObject>();
    Transform->SetArrayField(TEXT("location"), MakeVectorArray(Location.X, Location.Y, Location.Z));
    Transform->SetArrayField(TEXT("rotation"), MakeVectorArray(Rotation.Pitch, Rotation.Yaw, Rotation.Roll));
    Transform->SetArrayField(TEXT("scale"), MakeVectorArray(Scale.X, Scale.Y, Scale.Z));

    TSharedRef<FJsonObject> ActorJson = MakeShared<FJsonObject>();
    ActorJson->SetStringField(TEXT("name"), Actor.GetActorLabel());
    ActorJson->SetStringField(TEXT("path"), Actor.GetPathName());
    ActorJson->SetStringField(TEXT("class_name"), Actor.GetClass()->GetName());
    ActorJson->SetObjectField(TEXT("transform"), Transform);
    ActorJson->SetArrayField(TEXT("tags"), Tags);
    return ActorJson;
}

TSharedRef<FJsonObject> QueryWorldActors(
    UWorld& World,
    const TArray<FString>& Names,
    TArray<FString> Tags,
    bool bIncludeGenerated,
    int32 Limit)
{
    if (bIncludeGenerated && Tags.IsEmpty())
    {
        Tags.Add(TEXT("mcp.generated"));
    }

    TArray<TSharedPtr<FJsonValue>> Actors;
    int32 TotalCount = 0;
    const int32 EffectiveLimit = Limit > 0 ? Limit : 500;

    for (TActorIterator<AActor> It(&World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor) || !ActorMatches(*Actor, Names, Tags))
        {
            continue;
        }

        ++TotalCount;
        if (Actors.Num() < EffectiveLimit)
        {
            Actors.Add(MakeObjectValue(ActorToJson(*Actor)));
        }
    }

    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetArrayField(TEXT("actors"), Actors);
    Query->SetNumberField(TEXT("total_count"), TotalCount);
    return Query;
}

TSharedPtr<FJsonObject> GetNestedObjectOrSelf(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
{
    const TSharedPtr<FJsonObject>* Nested = nullptr;
    if (Object.IsValid() && Object->TryGetObjectField(FieldName, Nested) && Nested != nullptr && Nested->IsValid())
    {
        return *Nested;
    }

    return Object;
}

bool ReadIntPointField(
    const TSharedPtr<FJsonObject>& Object,
    const FString& FieldName,
    const FIntPoint& DefaultValue,
    FIntPoint& OutValue)
{
    OutValue = DefaultValue;
    if (!Object.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(FieldName, Values))
    {
        return true;
    }

    if (Values->Num() != 2)
    {
        return false;
    }

    OutValue = FIntPoint(
        static_cast<int32>((*Values)[0]->AsNumber()),
        static_cast<int32>((*Values)[1]->AsNumber()));
    return true;
}

void AddGeneratedLandscapeTags(AActor& Actor, const TArray<FString>& ExtraTags = TArray<FString>())
{
    Actor.Tags.AddUnique(TEXT("mcp.generated"));
    Actor.Tags.AddUnique(TEXT("mcp.landscape"));
    for (const FString& Tag : ExtraTags)
    {
        if (!Tag.IsEmpty())
        {
            Actor.Tags.AddUnique(FName(*Tag));
        }
    }
}

ALandscape* FindLandscapeByLabel(UWorld& World, const FString& Label)
{
    for (TActorIterator<ALandscape> It(&World); It; ++It)
    {
        ALandscape* Landscape = *It;
        if (IsValid(Landscape) && (Landscape->GetActorLabel() == Label || Landscape->GetName() == Label))
        {
            return Landscape;
        }
    }

    return nullptr;
}

bool GetLandscapeExtent(ALandscape& Landscape, FIntRect& OutExtent)
{
    ULandscapeInfo* LandscapeInfo = Landscape.GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        LandscapeInfo = Landscape.CreateLandscapeInfo();
    }

    return LandscapeInfo && LandscapeInfo->GetLandscapeExtent(&Landscape, OutExtent);
}

FIntPoint GetLandscapeVertexCount(ALandscape& Landscape)
{
    FIntRect Extent;
    if (!GetLandscapeExtent(Landscape, Extent))
    {
        return FIntPoint::ZeroValue;
    }

    return FIntPoint(Extent.Max.X - Extent.Min.X + 1, Extent.Max.Y - Extent.Min.Y + 1);
}

FIntPoint GetLandscapeComponentCount(ALandscape& Landscape)
{
    FIntRect Extent;
    if (!GetLandscapeExtent(Landscape, Extent) || Landscape.ComponentSizeQuads <= 0)
    {
        return FIntPoint::ZeroValue;
    }

    return FIntPoint(
        (Extent.Max.X - Extent.Min.X) / Landscape.ComponentSizeQuads,
        (Extent.Max.Y - Extent.Min.Y) / Landscape.ComponentSizeQuads);
}

uint16 WorldHeightToLandscapeHeight(const ALandscape& Landscape, double WorldHeight)
{
    const double ScaleZ = FMath::Max(FMath::Abs(Landscape.GetActorScale3D().Z), UE_SMALL_NUMBER);
    return LandscapeDataAccess::GetTexHeight(static_cast<float>(WorldHeight / ScaleZ));
}

bool ValidateLandscapeShape(
    const FIntPoint& ComponentCount,
    int32 SectionSize,
    int32 SectionsPerComponent,
    FString& OutError)
{
    if (ComponentCount.X <= 0 || ComponentCount.Y <= 0)
    {
        OutError = TEXT("landscape component_count values must be positive");
        return false;
    }

    if (SectionsPerComponent != 1 && SectionsPerComponent != 2)
    {
        OutError = TEXT("landscape sections_per_component must be 1 or 2");
        return false;
    }

    if (SectionSize < 1 || SectionSize > 255 || !FMath::IsPowerOfTwo(SectionSize + 1))
    {
        OutError = TEXT("landscape section_size + 1 must be a power of two between 2 and 256");
        return false;
    }

    const int64 VertexCountX = static_cast<int64>(ComponentCount.X) * SectionSize * SectionsPerComponent + 1;
    const int64 VertexCountY = static_cast<int64>(ComponentCount.Y) * SectionSize * SectionsPerComponent + 1;
    if (VertexCountX * VertexCountY > 1024 * 1024)
    {
        OutError = TEXT("landscape vertex count exceeds the v1 limit of 1,048,576 vertices");
        return false;
    }

    return true;
}

TArray<uint16> MakeFlatLandscapeHeightData(const FIntPoint& VertexCount)
{
    TArray<uint16> HeightData;
    HeightData.Init(LandscapeDataAccess::GetTexHeight(0.0f), VertexCount.X * VertexCount.Y);
    return HeightData;
}

double SmoothStep(double Edge0, double Edge1, double Value)
{
    if (Edge1 <= Edge0)
    {
        return Value >= Edge1 ? 1.0 : 0.0;
    }

    const double T = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

bool BuildHeightDataFromPatch(
    const TSharedPtr<FJsonObject>& PatchData,
    ALandscape& Landscape,
    TArray<uint16>& OutHeightData,
    FIntRect& OutExtent,
    FString& OutError)
{
    if (!GetLandscapeExtent(Landscape, OutExtent))
    {
        OutError = TEXT("failed to read landscape extent");
        return false;
    }

    const int32 Width = OutExtent.Max.X - OutExtent.Min.X + 1;
    const int32 Height = OutExtent.Max.Y - OutExtent.Min.Y + 1;

    double RequestedWidth = 0.0;
    double RequestedHeight = 0.0;
    PatchData->TryGetNumberField(TEXT("width"), RequestedWidth);
    PatchData->TryGetNumberField(TEXT("height"), RequestedHeight);
    if ((RequestedWidth > 0.0 && static_cast<int32>(RequestedWidth) != Width) ||
        (RequestedHeight > 0.0 && static_cast<int32>(RequestedHeight) != Height))
    {
        OutError = FString::Printf(TEXT("heightfield size must match landscape extent %dx%d"), Width, Height);
        return false;
    }

    double BaseHeight = 0.0;
    double Amplitude = 0.0;
    double Frequency = 1.0;
    double Seed = 0.0;
    double CityPadRadius = 0.0;
    double CityPadFalloff = 1000.0;
    PatchData->TryGetNumberField(TEXT("base_height"), BaseHeight);
    PatchData->TryGetNumberField(TEXT("amplitude"), Amplitude);
    PatchData->TryGetNumberField(TEXT("frequency"), Frequency);
    PatchData->TryGetNumberField(TEXT("seed"), Seed);
    PatchData->TryGetNumberField(TEXT("city_pad_radius"), CityPadRadius);
    PatchData->TryGetNumberField(TEXT("city_pad_falloff"), CityPadFalloff);

    const TArray<TSharedPtr<FJsonValue>>* Samples = nullptr;
    const bool bHasSamples = PatchData->TryGetArrayField(TEXT("samples"), Samples) && Samples != nullptr && Samples->Num() > 0;
    if (bHasSamples && Samples->Num() != Width * Height)
    {
        OutError = FString::Printf(TEXT("samples must contain exactly %d values"), Width * Height);
        return false;
    }

    OutHeightData.Reset(Width * Height);
    OutHeightData.AddUninitialized(Width * Height);

    const FVector Scale = Landscape.GetActorScale3D();
    const double HalfWidth = FMath::Max(1.0, static_cast<double>(Width - 1)) * FMath::Abs(Scale.X) * 0.5;
    const double HalfHeight = FMath::Max(1.0, static_cast<double>(Height - 1)) * FMath::Abs(Scale.Y) * 0.5;
    const double SeedOffset = Seed * 0.001;

    for (int32 Y = 0; Y < Height; ++Y)
    {
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 Index = Y * Width + X;
            double Sample = 0.0;
            if (bHasSamples)
            {
                Sample = (*Samples)[Index]->AsNumber();
            }
            else if (Amplitude != 0.0)
            {
                const double NX = Width > 1 ? (static_cast<double>(X) / (Width - 1)) * 2.0 - 1.0 : 0.0;
                const double NY = Height > 1 ? (static_cast<double>(Y) / (Height - 1)) * 2.0 - 1.0 : 0.0;
                const double Waves = 0.55 * FMath::Sin((NX * 5.21 + SeedOffset) * Frequency) +
                    0.45 * FMath::Cos((NY * 7.17 - SeedOffset) * Frequency);
                const double Ridge = FMath::Pow(FMath::Abs(FMath::Sin((NX + NY + SeedOffset) * Frequency * 2.1)), 1.8);
                Sample = FMath::Clamp(Waves * 0.6 + Ridge * 0.4, -1.0, 1.0);
            }

            double PadAlpha = 1.0;
            if (CityPadRadius > 0.0)
            {
                const double WorldX = (Width > 1 ? (static_cast<double>(X) / (Width - 1)) * 2.0 - 1.0 : 0.0) * HalfWidth;
                const double WorldY = (Height > 1 ? (static_cast<double>(Y) / (Height - 1)) * 2.0 - 1.0 : 0.0) * HalfHeight;
                const double Distance = FMath::Sqrt(WorldX * WorldX + WorldY * WorldY);
                PadAlpha = SmoothStep(CityPadRadius, CityPadRadius + FMath::Max(CityPadFalloff, 1.0), Distance);
            }

            const double WorldHeight = BaseHeight + Sample * Amplitude * PadAlpha;
            OutHeightData[Index] = WorldHeightToLandscapeHeight(Landscape, WorldHeight);
        }
    }

    return true;
}

UMaterialInterface* LoadMaterialInterfaceFromPath(const FString& MaterialPath)
{
    if (MaterialPath.IsEmpty())
    {
        return nullptr;
    }

    return LoadObject<UMaterialInterface>(nullptr, *ToObjectPath(MaterialPath));
}

void AssignLandscapeMaterial(ALandscape& Landscape, UMaterialInterface& Material)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Landscape.LandscapeMaterial = &Material;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
    Landscape.UpdateAllComponentMaterialInstances();
}

ULandscapeLayerInfoObject* FindOrCreateLandscapeLayerInfo(ALandscape& Landscape, const FString& LayerName)
{
    if (LayerName.IsEmpty())
    {
        return nullptr;
    }

    ULandscapeInfo* LandscapeInfo = Landscape.GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        LandscapeInfo = Landscape.CreateLandscapeInfo();
    }

    if (LandscapeInfo)
    {
        if (ULandscapeLayerInfoObject* Existing = LandscapeInfo->GetLayerInfoByName(FName(*LayerName), &Landscape))
        {
            return Existing;
        }
    }

    ULandscapeLayerInfoObject* LayerInfo =
        UE::Landscape::CreateTargetLayerInfo(FName(*LayerName), UE::Landscape::GetSharedAssetsPath(Landscape.GetLevel()));
    if (LayerInfo)
    {
        LayerInfo->SetLayerName(FName(*LayerName), true);
    }

    return LayerInfo;
}

void RegisterLandscapeLayer(ALandscape& Landscape, ULandscapeLayerInfoObject& LayerInfo)
{
    const FName LayerName = LayerInfo.GetLayerName();
    FLandscapeTargetLayerSettings Settings(&LayerInfo);
    if (Landscape.HasTargetLayer(&LayerInfo))
    {
        Landscape.UpdateTargetLayer(LayerName, Settings, true);
    }
    else
    {
        Landscape.AddTargetLayer(LayerName, Settings, true);
    }
}

bool PaintLandscapeLayers(
    ALandscape& Landscape,
    const TArray<FString>& LayerNames,
    TArray<FString>& Changed,
    FString& OutError)
{
    if (LayerNames.IsEmpty())
    {
        return true;
    }

    ULandscapeInfo* LandscapeInfo = Landscape.GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        LandscapeInfo = Landscape.CreateLandscapeInfo();
    }

    FIntRect Extent;
    if (!LandscapeInfo || !LandscapeInfo->GetLandscapeExtent(&Landscape, Extent))
    {
        OutError = TEXT("failed to read landscape extent for layer painting");
        return false;
    }

    const int32 Width = Extent.Max.X - Extent.Min.X + 1;
    const int32 Height = Extent.Max.Y - Extent.Min.Y + 1;
    TArray<uint8> Weights;
    Weights.SetNumUninitialized(Width * Height);

    Landscape.Modify();
    FLandscapeEditDataInterface Edit(LandscapeInfo);
    for (int32 LayerIndex = 0; LayerIndex < LayerNames.Num(); ++LayerIndex)
    {
        ULandscapeLayerInfoObject* LayerInfo = FindOrCreateLandscapeLayerInfo(Landscape, LayerNames[LayerIndex]);
        if (!LayerInfo)
        {
            OutError = FString::Printf(TEXT("failed to create landscape layer '%s'"), *LayerNames[LayerIndex]);
            return false;
        }

        RegisterLandscapeLayer(Landscape, *LayerInfo);
        const uint8 FillValue = LayerIndex == 0 ? 255 : 0;
        for (uint8& Weight : Weights)
        {
            Weight = FillValue;
        }
        Edit.SetAlphaData(LayerInfo, Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y, Weights.GetData(), 0);
        Changed.AddUnique(LayerNames[LayerIndex]);
    }

    return true;
}
}

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
    if (bRunning.load(std::memory_order_acquire))
    {
        return true;
    }

    const FIPv4Endpoint Endpoint(FIPv4Address(127, 0, 0, 1), BridgePort);
    Listener = MakeUnique<FTcpListener>(Endpoint, FTimespan::FromMilliseconds(10));
    Listener->OnConnectionAccepted().BindRaw(this, &FBridgeServer::HandleAcceptedConnection);

    if (!Listener->IsActive())
    {
        UE_LOG(
            LogUnrealMCPBridgeServer,
            Error,
            TEXT("Failed to start Unreal MCP bridge on %s."),
            *Endpoint.ToString());
        Listener.Reset();
        return false;
    }

    bRunning.store(true, std::memory_order_release);
    UE_LOG(LogUnrealMCPBridgeServer, Display, TEXT("Unreal MCP bridge listening on %s."), *Endpoint.ToString());
    return true;
}

void FBridgeServer::Stop()
{
    if (!bRunning.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }

    if (Listener)
    {
        Listener->Stop();
        Listener.Reset();
    }

    UE_LOG(LogUnrealMCPBridgeServer, Display, TEXT("Unreal MCP bridge stopped."));
}

bool FBridgeServer::IsRunning() const
{
    return bRunning.load(std::memory_order_acquire);
}

bool FBridgeServer::HandleAcceptedConnection(FSocket* ClientSocket, const FIPv4Endpoint& RemoteEndpoint)
{
    if (ClientSocket == nullptr)
    {
        return false;
    }

    UE_LOG(LogUnrealMCPBridgeServer, Verbose, TEXT("Accepted bridge connection from %s."), *RemoteEndpoint.ToString());
    HandleConnection(*ClientSocket);
    ClientSocket->Close();
    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
    return true;
}

void FBridgeServer::HandleConnection(FSocket& ClientSocket)
{
    uint8 LengthBytes[4] = {0, 0, 0, 0};
    if (!ReadExact(ClientSocket, LengthBytes, UE_ARRAY_COUNT(LengthBytes)))
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge request ended before the frame length was read."));
        return;
    }

    const uint32 FrameLength = ReadBigEndianUint32(LengthBytes);
    if (FrameLength > MaxFrameBytes)
    {
        UE_LOG(
            LogUnrealMCPBridgeServer,
            Warning,
            TEXT("Bridge request frame too large: %u bytes exceeds %u bytes."),
            FrameLength,
            MaxFrameBytes);
        return;
    }

    TArray<uint8> RequestBody;
    RequestBody.SetNumUninitialized(static_cast<int32>(FrameLength) + 1);
    if (FrameLength > 0 && !ReadExact(ClientSocket, RequestBody.GetData(), static_cast<int32>(FrameLength)))
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge request ended before the JSON body was read."));
        return;
    }
    RequestBody[FrameLength] = 0;

    const FString RequestJson = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(RequestBody.GetData()));
    FString ResponseJson;
    if (!BuildResponse(RequestJson, ResponseJson))
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge request could not be parsed as a protocol envelope."));
        return;
    }

    const FTCHARToUTF8 ResponseUtf8(*ResponseJson);
    const int32 ResponseLength = ResponseUtf8.Length();
    if (ResponseLength < 0 || static_cast<uint32>(ResponseLength) > MaxFrameBytes)
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge response frame exceeded the frame limit."));
        return;
    }

    uint8 ResponseLengthBytes[4] = {0, 0, 0, 0};
    WriteBigEndianUint32(static_cast<uint32>(ResponseLength), ResponseLengthBytes);
    if (!WriteExact(ClientSocket, ResponseLengthBytes, UE_ARRAY_COUNT(ResponseLengthBytes)))
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge response length could not be written."));
        return;
    }

    if (ResponseLength > 0 &&
        !WriteExact(ClientSocket, reinterpret_cast<const uint8*>(ResponseUtf8.Get()), ResponseLength))
    {
        UE_LOG(LogUnrealMCPBridgeServer, Warning, TEXT("Bridge response JSON body could not be written."));
    }
}

bool FBridgeServer::ReadExact(FSocket& ClientSocket, uint8* Destination, int32 BytesToRead) const
{
    int32 TotalBytesRead = 0;
    while (TotalBytesRead < BytesToRead && bRunning.load(std::memory_order_acquire))
    {
        if (!ClientSocket.Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(SocketWaitSeconds)))
        {
            return false;
        }

        int32 BytesRead = 0;
        if (!ClientSocket.Recv(Destination + TotalBytesRead, BytesToRead - TotalBytesRead, BytesRead) || BytesRead <= 0)
        {
            return false;
        }

        TotalBytesRead += BytesRead;
    }

    return TotalBytesRead == BytesToRead;
}

bool FBridgeServer::WriteExact(FSocket& ClientSocket, const uint8* Source, int32 BytesToWrite) const
{
    int32 TotalBytesWritten = 0;
    while (TotalBytesWritten < BytesToWrite && bRunning.load(std::memory_order_acquire))
    {
        if (!ClientSocket.Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromSeconds(SocketWaitSeconds)))
        {
            return false;
        }

        int32 BytesWritten = 0;
        if (!ClientSocket.Send(Source + TotalBytesWritten, BytesToWrite - TotalBytesWritten, BytesWritten) ||
            BytesWritten <= 0)
        {
            return false;
        }

        TotalBytesWritten += BytesWritten;
    }

    return TotalBytesWritten == BytesToWrite;
}

bool FBridgeServer::BuildResponse(const FString& RequestJson, FString& OutResponseJson) const
{
    const double StartSeconds = FPlatformTime::Seconds();
    TSharedPtr<FJsonObject> Request;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        return false;
    }

    uint64 RequestId = 0;
    if (!Request->TryGetNumberField(TEXT("request_id"), RequestId))
    {
        return false;
    }

    auto ElapsedMs = [&StartSeconds]() -> uint32 {
        return static_cast<uint32>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
    };

    uint32 RequestProtocolVersion = 0;
    if (!Request->TryGetNumberField(TEXT("protocol_version"), RequestProtocolVersion) ||
        RequestProtocolVersion != ProtocolVersion)
    {
        TArray<TSharedPtr<FJsonValue>> Errors;
        Errors.Add(MakeObjectValue(BuildError(
            0,
            TEXT("unsupported_protocol_version"),
            FString::Printf(
                TEXT("unsupported protocol version %u, expected %u"),
                RequestProtocolVersion,
                ProtocolVersion))));
        return SerializeJsonObject(BuildFailureResponse(RequestId, ElapsedMs(), Errors), OutResponseJson);
    }

    const TArray<TSharedPtr<FJsonValue>>* Commands = nullptr;
    if (!Request->TryGetArrayField(TEXT("commands"), Commands))
    {
        TArray<TSharedPtr<FJsonValue>> Errors;
        Errors.Add(MakeObjectValue(BuildError(0, TEXT("missing_commands"), TEXT("request envelope missing commands array"))));
        return SerializeJsonObject(BuildFailureResponse(RequestId, ElapsedMs(), Errors), OutResponseJson);
    }

    FString ErrorMode = TEXT("stop");
    Request->TryGetStringField(TEXT("error_mode"), ErrorMode);
    const bool bContinueOnError = ErrorMode.Equals(TEXT("continue"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Errors;
    for (int32 CommandIndex = 0; CommandIndex < Commands->Num(); ++CommandIndex)
    {
        const TSharedPtr<FJsonValue>& CommandValue = (*Commands)[CommandIndex];
        const TSharedPtr<FJsonObject>* CommandObject = nullptr;
        if (!CommandValue.IsValid() || !CommandValue->TryGetObject(CommandObject) || CommandObject == nullptr ||
            !CommandObject->IsValid())
        {
            Errors.Add(MakeObjectValue(BuildError(
                CommandIndex,
                TEXT("invalid_command"),
                TEXT("command entry must be a JSON object"))));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        FString CommandType;
        if (!(*CommandObject)->TryGetStringField(TEXT("type"), CommandType))
        {
            Errors.Add(MakeObjectValue(BuildError(
                CommandIndex,
                TEXT("missing_command_type"),
                TEXT("command object missing type field"))));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        TSharedPtr<FJsonObject> CommandError;
        TSharedPtr<FJsonObject> CommandResult =
            ExecuteCommandOnGameThread(CommandType, *CommandObject, CommandIndex, CommandError);
        if (CommandResult.IsValid())
        {
            Results.Add(MakeObjectValue(CommandResult.ToSharedRef()));
        }
        else
        {
            if (!CommandError.IsValid())
            {
                CommandError = BuildError(
                    CommandIndex,
                    TEXT("unknown_command"),
                    FString::Printf(TEXT("unknown command type '%s'"), *CommandType));
            }

            Errors.Add(MakeObjectValue(CommandError.ToSharedRef()));
            if (!bContinueOnError)
            {
                break;
            }
        }
    }

    if (!Errors.IsEmpty())
    {
        return SerializeJsonObject(BuildFailureResponse(RequestId, ElapsedMs(), Errors), OutResponseJson);
    }

    return SerializeJsonObject(BuildSuccessResponse(RequestId, ElapsedMs(), Results), OutResponseJson);
}

TSharedPtr<FJsonObject> FBridgeServer::ExecuteCommandOnGameThread(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& CommandObject,
    int32 CommandIndex,
    TSharedPtr<FJsonObject>& OutError) const
{
    if (IsInGameThread())
    {
        return ExecuteCommand(CommandType, CommandObject, CommandIndex, OutError);
    }

    TSharedPtr<FJsonObject> Result;
    FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
    AsyncTask(ENamedThreads::GameThread, [this, CommandType, CommandObject, CommandIndex, &Result, &OutError, DoneEvent]() {
        Result = ExecuteCommand(CommandType, CommandObject, CommandIndex, OutError);
        DoneEvent->Trigger();
    });
    DoneEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
    return Result;
}

TSharedPtr<FJsonObject> FBridgeServer::ExecuteCommand(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& CommandObject,
    int32 CommandIndex,
    TSharedPtr<FJsonObject>& OutError) const
{
    TSharedPtr<FJsonObject> Data;
    TryGetCommandData(CommandObject, Data);

    if (CommandType == TEXT("ping"))
    {
        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetStringField(TEXT("bridge_version"), BridgeVersion);
        return MakeTaggedResult(TEXT("pong"), ResultData);
    }

    if (CommandType == TEXT("status"))
    {
        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetBoolField(TEXT("connected"), bRunning.load(std::memory_order_acquire));
        ResultData->SetStringField(TEXT("bridge_version"), BridgeVersion);
        ResultData->SetStringField(TEXT("unreal_version"), FEngineVersion::Current().ToString());
        return MakeTaggedResult(TEXT("status"), ResultData);
    }

    if (CommandType == TEXT("capabilities"))
    {
        TArray<TSharedPtr<FJsonValue>> CommandNames;
        CommandNames.Add(MakeStringValue(TEXT("connection.ping")));
        CommandNames.Add(MakeStringValue(TEXT("connection.status")));
        CommandNames.Add(MakeStringValue(TEXT("connection.capabilities")));
        CommandNames.Add(MakeStringValue(TEXT("level.create")));
        CommandNames.Add(MakeStringValue(TEXT("level.open")));
        CommandNames.Add(MakeStringValue(TEXT("level.save")));
        CommandNames.Add(MakeStringValue(TEXT("level.list")));
        CommandNames.Add(MakeStringValue(TEXT("world.bulk_spawn")));
        CommandNames.Add(MakeStringValue(TEXT("world.bulk_delete")));
        CommandNames.Add(MakeStringValue(TEXT("world.query")));
        CommandNames.Add(MakeStringValue(TEXT("world.snapshot")));
        CommandNames.Add(MakeStringValue(TEXT("asset.create_folder")));
        CommandNames.Add(MakeStringValue(TEXT("asset.import_texture")));
        CommandNames.Add(MakeStringValue(TEXT("asset.import_static_mesh")));
        CommandNames.Add(MakeStringValue(TEXT("asset.bulk_import")));
        CommandNames.Add(MakeStringValue(TEXT("asset.validate")));
        CommandNames.Add(MakeStringValue(TEXT("mesh.create_building")));
        CommandNames.Add(MakeStringValue(TEXT("mesh.create_sign")));
        CommandNames.Add(MakeStringValue(TEXT("static_mesh.set_collision")));
        CommandNames.Add(MakeStringValue(TEXT("road.create_network")));
        CommandNames.Add(MakeStringValue(TEXT("scene.bulk_place_on_grid")));
        CommandNames.Add(MakeStringValue(TEXT("scene.create_city_block")));
        CommandNames.Add(MakeStringValue(TEXT("scene.create_district")));
        CommandNames.Add(MakeStringValue(TEXT("material.create")));
        CommandNames.Add(MakeStringValue(TEXT("material.create_instance")));
        CommandNames.Add(MakeStringValue(TEXT("material.create_procedural_texture")));
        CommandNames.Add(MakeStringValue(TEXT("material.set_parameters")));
        CommandNames.Add(MakeStringValue(TEXT("material.bulk_apply")));
        CommandNames.Add(MakeStringValue(TEXT("world.bulk_set_materials")));
        CommandNames.Add(MakeStringValue(TEXT("lighting.set_night_scene")));
        CommandNames.Add(MakeStringValue(TEXT("lighting.set_sky")));
        CommandNames.Add(MakeStringValue(TEXT("lighting.set_fog")));
        CommandNames.Add(MakeStringValue(TEXT("lighting.set_post_process")));
        CommandNames.Add(MakeStringValue(TEXT("lighting.bulk_set_lights")));
        CommandNames.Add(MakeStringValue(TEXT("lighting.set_time_of_day")));
        CommandNames.Add(MakeStringValue(TEXT("landscape.create")));
        CommandNames.Add(MakeStringValue(TEXT("landscape.set_heightfield")));
        CommandNames.Add(MakeStringValue(TEXT("landscape.paint_layers")));
        CommandNames.Add(MakeStringValue(TEXT("placement.bulk_snap_to_ground")));
        CommandNames.Add(MakeStringValue(TEXT("blueprint.create_actor")));
        CommandNames.Add(MakeStringValue(TEXT("blueprint.add_static_mesh_component")));
        CommandNames.Add(MakeStringValue(TEXT("blueprint.add_light_component")));
        CommandNames.Add(MakeStringValue(TEXT("blueprint.compile")));
        CommandNames.Add(MakeStringValue(TEXT("runtime.create_led_animation")));
        CommandNames.Add(MakeStringValue(TEXT("runtime.create_moving_light_animation")));
        CommandNames.Add(MakeStringValue(TEXT("runtime.create_material_parameter_animation")));
        CommandNames.Add(MakeStringValue(TEXT("runtime.attach_animation_to_actor")));
        CommandNames.Add(MakeStringValue(TEXT("game.create_player")));
        CommandNames.Add(MakeStringValue(TEXT("game.create_checkpoint")));
        CommandNames.Add(MakeStringValue(TEXT("game.create_interaction")));
        CommandNames.Add(MakeStringValue(TEXT("game.create_collectibles")));
        CommandNames.Add(MakeStringValue(TEXT("game.create_objective_flow")));
        CommandNames.Add(MakeStringValue(TEXT("gameplay.create_system")));
        CommandNames.Add(MakeStringValue(TEXT("gameplay.bind_collectibles")));
        CommandNames.Add(MakeStringValue(TEXT("gameplay.bind_checkpoints")));
        CommandNames.Add(MakeStringValue(TEXT("gameplay.bind_interactions")));
        CommandNames.Add(MakeStringValue(TEXT("gameplay.bind_objective_flow")));

        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetArrayField(TEXT("commands"), CommandNames);
        return MakeTaggedResult(TEXT("capabilities"), ResultData);
    }

    if (CommandType == TEXT("level_list"))
    {
        TArray<TSharedPtr<FJsonValue>> Levels;
        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        TArray<FAssetData> Assets;
        AssetRegistryModule.Get().GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), Assets);

        for (const FAssetData& Asset : Assets)
        {
            TSharedRef<FJsonObject> Level = MakeShared<FJsonObject>();
            Level->SetStringField(TEXT("path"), Asset.PackageName.ToString());
            Level->SetStringField(TEXT("name"), Asset.AssetName.ToString());
            Levels.Add(MakeObjectValue(Level));
        }

        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetArrayField(TEXT("levels"), Levels);
        return MakeTaggedResult(TEXT("level_list"), ResultData);
    }

    if (CommandType == TEXT("level_create"))
    {
        FString Path;
        const bool bHasPath = Data->TryGetStringField(TEXT("path"), Path);
        if (!bHasPath || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("level.create requires data.path"));
            return nullptr;
        }

        bool bOpen = true;
        bool bSave = false;
        Data->TryGetBoolField(TEXT("open"), bOpen);
        Data->TryGetBoolField(TEXT("save"), bSave);

        UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to create blank map"));
            return nullptr;
        }

        const bool bSaved = bSave ? UEditorLoadingAndSavingUtils::SaveMap(World, Path) : false;
        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetStringField(TEXT("path"), Path);
        ResultData->SetBoolField(TEXT("opened"), bOpen);
        ResultData->SetBoolField(TEXT("saved"), bSaved);
        return MakeTaggedResult(TEXT("level_operation"), ResultData);
    }

    if (CommandType == TEXT("level_open"))
    {
        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("level.open requires data.path"));
            return nullptr;
        }

        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Path);
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to open level '%s'"), *Path));
            return nullptr;
        }

        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetStringField(TEXT("path"), Path);
        ResultData->SetBoolField(TEXT("opened"), true);
        ResultData->SetBoolField(TEXT("saved"), false);
        return MakeTaggedResult(TEXT("level_operation"), ResultData);
    }

    if (CommandType == TEXT("level_save"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            Path = World->GetOutermost()->GetName();
        }

        const bool bSaved = UEditorLoadingAndSavingUtils::SaveMap(World, Path);
        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetStringField(TEXT("path"), Path);
        ResultData->SetBoolField(TEXT("opened"), true);
        ResultData->SetBoolField(TEXT("saved"), bSaved);
        return MakeTaggedResult(TEXT("level_operation"), ResultData);
    }

    if (CommandType == TEXT("world_bulk_spawn"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
        if (!Data->TryGetArrayField(TEXT("actors"), Actors))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("world.bulk_spawn requires data.actors"));
            return nullptr;
        }

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "BulkSpawnActors", "MCP Bulk Spawn Actors"));
        TArray<TSharedPtr<FJsonValue>> Spawned;
        for (const TSharedPtr<FJsonValue>& ActorValue : *Actors)
        {
            const TSharedPtr<FJsonObject>* ActorSpec = nullptr;
            if (!ActorValue.IsValid() || !ActorValue->TryGetObject(ActorSpec) || ActorSpec == nullptr || !ActorSpec->IsValid())
            {
                continue;
            }

            FString Name;
            FString MeshPath;
            (*ActorSpec)->TryGetStringField(TEXT("name"), Name);
            (*ActorSpec)->TryGetStringField(TEXT("mesh"), MeshPath);
            if (Name.IsEmpty() || MeshPath.IsEmpty())
            {
                continue;
            }

            TSharedPtr<FJsonObject> TransformSpec = *ActorSpec;
            const TSharedPtr<FJsonObject>* NestedTransform = nullptr;
            if ((*ActorSpec)->TryGetObjectField(TEXT("transform"), NestedTransform) && NestedTransform != nullptr &&
                NestedTransform->IsValid())
            {
                TransformSpec = *NestedTransform;
            }

            FVector Location = FVector::ZeroVector;
            FVector Scale = FVector(1.0, 1.0, 1.0);
            FRotator Rotation = FRotator::ZeroRotator;
            if (!ReadVectorField(TransformSpec, TEXT("location"), FVector::ZeroVector, Location) ||
                !ReadRotatorField(TransformSpec, TEXT("rotation"), FRotator::ZeroRotator, Rotation) ||
                !ReadVectorField(TransformSpec, TEXT("scale"), FVector(1.0, 1.0, 1.0), Scale))
            {
                continue;
            }

            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *ToObjectPath(MeshPath));
            if (!Mesh)
            {
                continue;
            }

            FActorSpawnParameters SpawnParameters;
            SpawnParameters.Name = MakeUniqueObjectName(World, AStaticMeshActor::StaticClass(), FName(*Name));
            AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, Rotation, SpawnParameters);
            if (!Actor)
            {
                continue;
            }

            Actor->Modify();
            Actor->SetActorLabel(Name);
            Actor->SetActorScale3D(Scale);
            Actor->Tags.AddUnique(TEXT("mcp.generated"));

            FString Scene;
            if ((*ActorSpec)->TryGetStringField(TEXT("scene"), Scene) && !Scene.IsEmpty())
            {
                Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.scene:%s"), *Scene)));
            }

            FString Group;
            if ((*ActorSpec)->TryGetStringField(TEXT("group"), Group) && !Group.IsEmpty())
            {
                Actor->Tags.AddUnique(FName(*FString::Printf(TEXT("mcp.group:%s"), *Group)));
            }

            UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
            if (MeshComponent)
            {
                MeshComponent->SetStaticMesh(Mesh);
                MeshComponent->SetMobility(EComponentMobility::Static);
            }

            TSharedRef<FJsonObject> SpawnedActor = MakeShared<FJsonObject>();
            SpawnedActor->SetStringField(TEXT("name"), Actor->GetActorLabel());
            SpawnedActor->SetStringField(TEXT("path"), Actor->GetPathName());
            Spawned.Add(MakeObjectValue(SpawnedActor));
        }

        World->MarkPackageDirty();

        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetArrayField(TEXT("spawned"), Spawned);
        ResultData->SetNumberField(TEXT("count"), Spawned.Num());
        return MakeTaggedResult(TEXT("world_bulk_spawn"), ResultData);
    }

    if (CommandType == TEXT("world_bulk_delete"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        const TArray<FString> Names = ReadStringArrayField(Data, TEXT("names"));
        const TArray<FString> Tags = ReadStringArrayField(Data, TEXT("tags"));
        if (Names.IsEmpty() && Tags.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("world.bulk_delete requires names or tags"));
            return nullptr;
        }

        TArray<AActor*> ActorsToDelete;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (IsValid(Actor) && ActorMatches(*Actor, Names, Tags))
            {
                ActorsToDelete.Add(Actor);
            }
        }

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "BulkDeleteActors", "MCP Bulk Delete Actors"));
        TArray<TSharedPtr<FJsonValue>> Deleted;
        for (AActor* Actor : ActorsToDelete)
        {
            Deleted.Add(MakeStringValue(Actor->GetActorLabel()));
            World->EditorDestroyActor(Actor, true);
        }

        World->MarkPackageDirty();

        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetArrayField(TEXT("deleted"), Deleted);
        ResultData->SetNumberField(TEXT("count"), Deleted.Num());
        return MakeTaggedResult(TEXT("world_bulk_delete"), ResultData);
    }

    if (CommandType == TEXT("world_query"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        TArray<FString> Names = ReadStringArrayField(Data, TEXT("names"));
        TArray<FString> Tags = ReadStringArrayField(Data, TEXT("tags"));
        bool bIncludeGenerated = false;
        Data->TryGetBoolField(TEXT("include_generated"), bIncludeGenerated);
        int32 Limit = 500;
        Data->TryGetNumberField(TEXT("limit"), Limit);
        return MakeTaggedResult(TEXT("world_query"), QueryWorldActors(*World, Names, Tags, bIncludeGenerated, Limit));
    }

    if (CommandType == TEXT("world_snapshot"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        TArray<FString> Tags = ReadStringArrayField(Data, TEXT("tags"));
        TSharedRef<FJsonObject> Query = QueryWorldActors(*World, TArray<FString>(), Tags, Tags.IsEmpty(), 100000);

        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            Path = FPaths::ProjectSavedDir() / TEXT("UnrealMCP/snapshots/world_snapshot.json");
        }

        FString SnapshotJson;
        SerializeJsonObject(Query, SnapshotJson);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        if (!FFileHelper::SaveStringToFile(SnapshotJson, *Path))
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to write snapshot '%s'"), *Path));
            return nullptr;
        }

        int32 TotalCount = 0;
        Query->TryGetNumberField(TEXT("total_count"), TotalCount);
        TSharedRef<FJsonObject> ResultData = MakeShared<FJsonObject>();
        ResultData->SetStringField(TEXT("path"), Path);
        ResultData->SetNumberField(TEXT("total_count"), TotalCount);
        return MakeTaggedResult(TEXT("world_snapshot"), ResultData);
    }

    if (CommandType == TEXT("asset_create_folder"))
    {
        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("asset.create_folder requires data.path"));
            return nullptr;
        }

        FText Reason;
        if (!FPackageName::IsValidLongPackageName(Path, false, &Reason))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), Reason.ToString());
            return nullptr;
        }

        const FString FolderPath = FPackageName::LongPackageNameToFilename(Path);
        const bool bCreated = IFileManager::Get().MakeDirectory(*FolderPath, true);
        return MakeTaggedResult(TEXT("asset_operation"), MakeAssetOperation(Path, bCreated));
    }

    if (CommandType == TEXT("asset_import_texture") || CommandType == TEXT("asset_import_static_mesh"))
    {
        const FString ForcedKind = CommandType == TEXT("asset_import_texture") ? TEXT("texture") : TEXT("static_mesh");
        TSharedPtr<FJsonObject> SpecData = GetNestedObjectOrSelf(Data, TEXT("spec"));
        TArray<TSharedPtr<FJsonValue>> Assets;
        FString ImportError;
        if (!ImportAssetFromSpec(SpecData, ForcedKind, Assets, ImportError))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), ImportError);
            return nullptr;
        }

        return MakeTaggedResult(TEXT("asset_import"), MakeAssetImportResult(Assets));
    }

    if (CommandType == TEXT("asset_bulk_import"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        if (!Data->TryGetArrayField(TEXT("items"), Items))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("asset.bulk_import requires data.items"));
            return nullptr;
        }

        TArray<TSharedPtr<FJsonValue>> Assets;
        for (const TSharedPtr<FJsonValue>& ItemValue : *Items)
        {
            const TSharedPtr<FJsonObject>* Item = nullptr;
            if (!ItemValue.IsValid() || !ItemValue->TryGetObject(Item) || Item == nullptr || !Item->IsValid())
            {
                AddAssetImportOperation(Assets, FString(), FString(), FString(), false, TEXT("item is not an object"));
                continue;
            }

            FString ImportError;
            if (!ImportAssetFromSpec(*Item, FString(), Assets, ImportError))
            {
                FString SourceFile;
                FString DestinationPath;
                (*Item)->TryGetStringField(TEXT("source_file"), SourceFile);
                (*Item)->TryGetStringField(TEXT("destination_path"), DestinationPath);
                AddAssetImportOperation(Assets, SourceFile, DestinationPath, FString(), false, ImportError);
            }
        }

        return MakeTaggedResult(TEXT("asset_import"), MakeAssetImportResult(Assets));
    }

    if (CommandType == TEXT("asset_validate"))
    {
        TSharedPtr<FJsonObject> SpecData = GetNestedObjectOrSelf(Data, TEXT("spec"));
        const TArray<FString> Paths = ReadStringArrayField(SpecData, TEXT("paths"));
        if (Paths.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("asset.validate requires data.paths"));
            return nullptr;
        }

        return MakeTaggedResult(TEXT("asset_validation"), MakeAssetValidationResult(Paths));
    }

    if (CommandType == TEXT("mesh_create_building") || CommandType == TEXT("mesh_create_sign"))
    {
        TSharedPtr<FJsonObject> SpecData = GetNestedObjectOrSelf(Data, TEXT("spec"));
        FString Path;
        if (!SpecData->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("generated mesh creation requires data.path"));
            return nullptr;
        }

        UMaterialInterface* Material = nullptr;
        FString MaterialPath;
        if (SpecData->TryGetStringField(TEXT("material"), MaterialPath) && !MaterialPath.IsEmpty())
        {
            Material = LoadObject<UMaterialInterface>(nullptr, *ToObjectPath(MaterialPath));
            if (!Material)
            {
                OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to load material '%s'"), *MaterialPath));
                return nullptr;
            }
        }

        FMeshDescription MeshDescription;
        int32 VertexCount = 0;
        int32 TriangleCount = 0;
        FString MeshError;
        const bool bBuilt = CommandType == TEXT("mesh_create_building")
            ? BuildBuildingMeshDescription(SpecData, MeshDescription, VertexCount, TriangleCount, MeshError)
            : BuildSignMeshDescription(SpecData, MeshDescription, VertexCount, TriangleCount, MeshError);
        if (!bBuilt)
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), MeshError);
            return nullptr;
        }

        UStaticMesh* StaticMesh = nullptr;
        bool bCreated = false;
        if (!CreateOrUpdateGeneratedStaticMesh(Path, MeshDescription, Material, VertexCount, TriangleCount, StaticMesh, bCreated, MeshError))
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), MeshError);
            return nullptr;
        }

        return MakeTaggedResult(TEXT("generated_mesh"), MakeGeneratedMeshOperation(Path, bCreated, VertexCount, TriangleCount));
    }

    if (CommandType == TEXT("static_mesh_set_collision"))
    {
        TSharedPtr<FJsonObject> SpecData = GetNestedObjectOrSelf(Data, TEXT("spec"));
        const TArray<FString> Paths = ReadStringArrayField(SpecData, TEXT("paths"));
        if (Paths.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("static_mesh.set_collision requires data.paths"));
            return nullptr;
        }

        FString CollisionTrace = TEXT("project_default");
        bool bSimpleCollision = true;
        bool bRebuild = true;
        SpecData->TryGetStringField(TEXT("collision_trace"), CollisionTrace);
        SpecData->TryGetBoolField(TEXT("simple_collision"), bSimpleCollision);
        SpecData->TryGetBoolField(TEXT("rebuild"), bRebuild);

        TArray<TSharedPtr<FJsonValue>> Meshes;
        for (const FString& Path : Paths)
        {
            UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *ToObjectPath(Path));
            if (!StaticMesh)
            {
                AddStaticMeshOperation(Meshes, Path, false, CollisionTrace);
                continue;
            }

            FString CollisionError;
            const bool bChanged = ConfigureStaticMeshCollision(*StaticMesh, CollisionTrace, bSimpleCollision, CollisionError);
            if (bChanged && bRebuild)
            {
                StaticMesh->Build();
                StaticMesh->PostEditChange();
            }
            AddStaticMeshOperation(Meshes, Path, bChanged, CollisionTrace);
        }

        return MakeTaggedResult(TEXT("static_mesh_operation"), MakeStaticMeshOperationResult(Meshes));
    }

    if (CommandType == TEXT("road_create_network") ||
        CommandType == TEXT("scene_bulk_place_on_grid") ||
        CommandType == TEXT("scene_create_city_block") ||
        CommandType == TEXT("scene_create_district"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        TSharedPtr<FJsonObject> SpecData = GetNestedObjectOrSelf(Data, TEXT("spec"));
        FString MeshError;
        TArray<TSharedPtr<FJsonValue>> Spawned;
        FSceneAssemblyCounts Counts;
        bool bOk = false;

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "SceneAssembly", "MCP Scene Assembly"));
        if (CommandType == TEXT("road_create_network"))
        {
            UStaticMesh* RoadMesh = LoadSceneStaticMesh(SpecData, TEXT("road_mesh"), TEXT("/Engine/BasicShapes/Cube.Cube"), MeshError);
            if (!RoadMesh)
            {
                OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), MeshError);
                return nullptr;
            }
            bOk = SpawnRoadNetwork(*World, SpecData, *RoadMesh, Spawned, Counts, MeshError);
        }
        else if (CommandType == TEXT("scene_bulk_place_on_grid"))
        {
            UStaticMesh* Mesh = LoadSceneStaticMesh(SpecData, TEXT("mesh"), FString(), MeshError);
            if (!Mesh)
            {
                OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), MeshError);
                return nullptr;
            }
            bOk = SpawnGridPlacement(*World, SpecData, *Mesh, TEXT("building"), Spawned, Counts, MeshError);
        }
        else if (CommandType == TEXT("scene_create_city_block"))
        {
            UStaticMesh* RoadMesh = LoadSceneStaticMesh(SpecData, TEXT("road_mesh"), TEXT("/Engine/BasicShapes/Cube.Cube"), MeshError);
            UStaticMesh* SidewalkMesh = LoadSceneStaticMesh(SpecData, TEXT("sidewalk_mesh"), TEXT("/Engine/BasicShapes/Cube.Cube"), MeshError);
            UStaticMesh* BuildingMesh = LoadSceneStaticMesh(SpecData, TEXT("building_mesh"), FString(), MeshError);
            if (!RoadMesh || !SidewalkMesh || !BuildingMesh)
            {
                OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), MeshError);
                return nullptr;
            }
            bOk = SpawnCityBlock(*World, SpecData, *RoadMesh, *SidewalkMesh, *BuildingMesh, Spawned, Counts, MeshError);
        }
        else
        {
            UStaticMesh* RoadMesh = LoadSceneStaticMesh(SpecData, TEXT("road_mesh"), TEXT("/Engine/BasicShapes/Cube.Cube"), MeshError);
            UStaticMesh* BuildingMesh = LoadSceneStaticMesh(SpecData, TEXT("building_mesh"), FString(), MeshError);
            if (!RoadMesh || !BuildingMesh)
            {
                OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), MeshError);
                return nullptr;
            }
            bOk = SpawnDistrict(*World, SpecData, *RoadMesh, *BuildingMesh, Spawned, Counts, MeshError);
        }

        if (!bOk)
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), MeshError);
            return nullptr;
        }

        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("scene_assembly"), MakeSceneAssemblyResult(Spawned, Counts));
    }

    if (CommandType == TEXT("material_create"))
    {
        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("material.create requires data.path"));
            return nullptr;
        }

        FString PackagePath;
        FString AssetName;
        FString PathError;
        if (!SplitAssetPath(Path, PackagePath, AssetName, PathError))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), PathError);
            return nullptr;
        }

        FLinearColor BaseColor = FLinearColor(0.8f, 0.8f, 0.8f, 1.0f);
        FLinearColor EmissiveColor = FLinearColor::Black;
        if (!ReadColorField(Data, TEXT("base_color"), BaseColor, BaseColor) ||
            !ReadColorField(Data, TEXT("emissive_color"), EmissiveColor, EmissiveColor))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("material colors must be arrays of 4 numbers"));
            return nullptr;
        }

        double Metallic = 0.0;
        double Roughness = 0.5;
        double Specular = 0.5;
        Data->TryGetNumberField(TEXT("metallic"), Metallic);
        Data->TryGetNumberField(TEXT("roughness"), Roughness);
        Data->TryGetNumberField(TEXT("specular"), Specular);

        UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
        UMaterial* Material = Cast<UMaterial>(
            AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory));
        if (!Material)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to create material '%s'"), *Path));
            return nullptr;
        }

        UMaterialExpressionVectorParameter* BaseColorExpression = Cast<UMaterialExpressionVectorParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -400, 0));
        BaseColorExpression->ParameterName = TEXT("BaseColor");
        BaseColorExpression->DefaultValue = BaseColor;
        UMaterialEditingLibrary::ConnectMaterialProperty(BaseColorExpression, TEXT(""), MP_BaseColor);

        UMaterialExpressionScalarParameter* MetallicExpression = Cast<UMaterialExpressionScalarParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -400, 160));
        MetallicExpression->ParameterName = TEXT("Metallic");
        MetallicExpression->DefaultValue = static_cast<float>(Metallic);
        UMaterialEditingLibrary::ConnectMaterialProperty(MetallicExpression, TEXT(""), MP_Metallic);

        UMaterialExpressionScalarParameter* RoughnessExpression = Cast<UMaterialExpressionScalarParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -400, 320));
        RoughnessExpression->ParameterName = TEXT("Roughness");
        RoughnessExpression->DefaultValue = static_cast<float>(Roughness);
        UMaterialEditingLibrary::ConnectMaterialProperty(RoughnessExpression, TEXT(""), MP_Roughness);

        UMaterialExpressionScalarParameter* SpecularExpression = Cast<UMaterialExpressionScalarParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -400, 480));
        SpecularExpression->ParameterName = TEXT("Specular");
        SpecularExpression->DefaultValue = static_cast<float>(Specular);
        UMaterialEditingLibrary::ConnectMaterialProperty(SpecularExpression, TEXT(""), MP_Specular);

        if (!EmissiveColor.Equals(FLinearColor::Black))
        {
            UMaterialExpressionVectorParameter* EmissiveExpression = Cast<UMaterialExpressionVectorParameter>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -400, 640));
            EmissiveExpression->ParameterName = TEXT("EmissiveColor");
            EmissiveExpression->DefaultValue = EmissiveColor;
            UMaterialEditingLibrary::ConnectMaterialProperty(EmissiveExpression, TEXT(""), MP_EmissiveColor);
        }

        UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
        Material->PostEditChange();
        Material->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Material);

        return MakeTaggedResult(TEXT("material_operation"), MakeMaterialOperation(Path, FString(), true));
    }

    if (CommandType == TEXT("material_create_instance"))
    {
        FString Path;
        FString ParentPath;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty() ||
            !Data->TryGetStringField(TEXT("parent"), ParentPath) || ParentPath.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("material.create_instance requires data.path and data.parent"));
            return nullptr;
        }

        FString PackagePath;
        FString AssetName;
        FString PathError;
        if (!SplitAssetPath(Path, PackagePath, AssetName, PathError))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), PathError);
            return nullptr;
        }

        UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *ToObjectPath(ParentPath));
        if (!Parent)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to load parent material '%s'"), *ParentPath));
            return nullptr;
        }

        UPackage* Package = CreatePackage(*(PackagePath / AssetName));
        UMaterialInstanceConstant* Instance =
            NewObject<UMaterialInstanceConstant>(Package, *AssetName, RF_Public | RF_Standalone);
        Instance->SetParentEditorOnly(Parent);
        int32 ScalarCount = 0;
        int32 VectorCount = 0;
        int32 TextureCount = 0;
        ApplyMaterialParameters(Data, *Instance, ScalarCount, VectorCount, TextureCount);
        FAssetRegistryModule::AssetCreated(Instance);
        Package->MarkPackageDirty();

        return MakeTaggedResult(TEXT("material_operation"), MakeMaterialOperation(Path, ParentPath, true));
    }

    if (CommandType == TEXT("material_create_procedural_texture"))
    {
        TSharedPtr<FJsonObject> TextureData = Data;
        const TSharedPtr<FJsonObject>* NestedSpec = nullptr;
        if (Data->TryGetObjectField(TEXT("spec"), NestedSpec) && NestedSpec != nullptr && NestedSpec->IsValid())
        {
            TextureData = *NestedSpec;
        }

        FString Path;
        if (!TextureData->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("material.create_procedural_texture requires data.path"));
            return nullptr;
        }

        FString PackagePath;
        FString AssetName;
        FString PathError;
        if (!SplitAssetPath(Path, PackagePath, AssetName, PathError))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), PathError);
            return nullptr;
        }

        FString Pattern = TEXT("solid");
        TextureData->TryGetStringField(TEXT("pattern"), Pattern);
        int32 Width = 64;
        int32 Height = 64;
        int32 CheckerSize = 8;
        TextureData->TryGetNumberField(TEXT("width"), Width);
        TextureData->TryGetNumberField(TEXT("height"), Height);
        TextureData->TryGetNumberField(TEXT("checker_size"), CheckerSize);
        Width = FMath::Clamp(Width, 1, 4096);
        Height = FMath::Clamp(Height, 1, 4096);
        CheckerSize = FMath::Max(CheckerSize, 1);

        FLinearColor ColorA = FLinearColor::White;
        FLinearColor ColorB = FLinearColor::Black;
        if (!ReadColorField(TextureData, TEXT("color_a"), ColorA, ColorA) ||
            !ReadColorField(TextureData, TEXT("color_b"), ColorB, ColorB))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("texture colors must be arrays of 4 numbers"));
            return nullptr;
        }

        UPackage* Package = CreatePackage(*(PackagePath / AssetName));
        UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
        TArray<uint8> Pixels;
        Pixels.SetNumZeroed(Width * Height * 4);
        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                const bool bUseB = Pattern == TEXT("checker") && (((X / CheckerSize) + (Y / CheckerSize)) % 2 == 1);
                WriteColorPixel(Pixels, Y * Width + X, bUseB ? ColorB : ColorA);
            }
        }

        Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, Pixels.GetData());
        Texture->SRGB = true;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->PostEditChange();
        Texture->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Texture);
        Package->MarkPackageDirty();

        return MakeTaggedResult(TEXT("procedural_texture_operation"), MakeTextureOperation(Path, Width, Height, true));
    }

    if (CommandType == TEXT("material_set_parameters"))
    {
        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("material.set_parameters requires data.path"));
            return nullptr;
        }

        UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *ToObjectPath(Path));
        if (!Instance)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to load material instance '%s'"), *Path));
            return nullptr;
        }

        int32 ScalarCount = 0;
        int32 VectorCount = 0;
        int32 TextureCount = 0;
        ApplyMaterialParameters(Data, *Instance, ScalarCount, VectorCount, TextureCount);
        return MakeTaggedResult(TEXT("material_parameter_operation"), MakeParameterOperation(Path, ScalarCount, VectorCount, TextureCount));
    }

    if (CommandType == TEXT("material_bulk_apply"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        const TArray<TSharedPtr<FJsonValue>>* Assignments = nullptr;
        if (!Data->TryGetArrayField(TEXT("assignments"), Assignments))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("material.bulk_apply requires data.assignments"));
            return nullptr;
        }

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "BulkApplyMaterials", "MCP Bulk Apply Materials"));
        TArray<TSharedPtr<FJsonValue>> Applied;
        for (const TSharedPtr<FJsonValue>& AssignmentValue : *Assignments)
        {
            const TSharedPtr<FJsonObject>* Assignment = nullptr;
            if (!AssignmentValue.IsValid() || !AssignmentValue->TryGetObject(Assignment) || Assignment == nullptr || !Assignment->IsValid())
            {
                continue;
            }

            FString MaterialPath;
            (*Assignment)->TryGetStringField(TEXT("material"), MaterialPath);
            if (MaterialPath.IsEmpty())
            {
                continue;
            }

            const TArray<FString> Names = ReadStringArrayField(*Assignment, TEXT("names"));
            const TArray<FString> Tags = ReadStringArrayField(*Assignment, TEXT("tags"));
            if (Names.IsEmpty() && Tags.IsEmpty())
            {
                continue;
            }

            int32 Slot = 0;
            (*Assignment)->TryGetNumberField(TEXT("slot"), Slot);
            ApplyMaterialToActors(*World, MaterialPath, Names, Tags, Slot, Applied);
        }

        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("material_apply"), MakeMaterialApplyResult(Applied));
    }

    if (CommandType == TEXT("world_bulk_set_materials"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        FString MaterialPath;
        Data->TryGetStringField(TEXT("material"), MaterialPath);
        const TArray<FString> Names = ReadStringArrayField(Data, TEXT("names"));
        const TArray<FString> Tags = ReadStringArrayField(Data, TEXT("tags"));
        if (MaterialPath.IsEmpty() || (Names.IsEmpty() && Tags.IsEmpty()))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("world.bulk_set_materials requires material and names or tags"));
            return nullptr;
        }

        int32 Slot = 0;
        Data->TryGetNumberField(TEXT("slot"), Slot);
        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "BulkSetMaterials", "MCP Bulk Set Materials"));
        TArray<TSharedPtr<FJsonValue>> Applied;
        ApplyMaterialToActors(*World, MaterialPath, Names, Tags, Slot, Applied);
        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("material_apply"), MakeMaterialApplyResult(Applied));
    }

    if (CommandType == TEXT("lighting_set_night_scene"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        FRotator MoonRotation = FRotator(-35.0, -25.0, 0.0);
        FLinearColor MoonColor = FLinearColor(0.55f, 0.65f, 1.0f, 1.0f);
        if (!ReadRotatorField(Data, TEXT("moon_rotation"), MoonRotation, MoonRotation) ||
            !ReadColorField(Data, TEXT("moon_color"), MoonColor, MoonColor))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("lighting vectors must be fixed-size numeric arrays"));
            return nullptr;
        }

        double MoonIntensity = 0.12;
        double SkyIntensity = 0.05;
        double FogDensity = 0.01;
        double ExposureCompensation = -0.5;
        Data->TryGetNumberField(TEXT("moon_intensity"), MoonIntensity);
        Data->TryGetNumberField(TEXT("sky_intensity"), SkyIntensity);
        Data->TryGetNumberField(TEXT("fog_density"), FogDensity);
        Data->TryGetNumberField(TEXT("exposure_compensation"), ExposureCompensation);

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "SetNightLighting", "MCP Set Night Lighting"));
        TArray<FString> Changed;
        ConfigureDirectionalLight(*World, TEXT("MCP_MoonLight"), MoonRotation, MoonIntensity, MoonColor, Changed);
        ConfigureSkyLight(*World, SkyIntensity, FLinearColor(0.01f, 0.012f, 0.018f, 1.0f), Changed);
        ConfigureSkyAtmosphere(*World, Changed);
        ConfigureFog(*World, FogDensity, 0.2, FLinearColor(0.08f, 0.1f, 0.16f, 1.0f), 0.0, Changed);
        ConfigurePostProcess(*World, ExposureCompensation, 0.2, 1.0, 0.6, Changed);
        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("lighting_operation"), MakeLightingOperationResult(Changed));
    }

    if (CommandType == TEXT("lighting_set_sky"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        double SkyIntensity = 0.05;
        FLinearColor LowerHemisphereColor = FLinearColor(0.01f, 0.012f, 0.018f, 1.0f);
        Data->TryGetNumberField(TEXT("sky_intensity"), SkyIntensity);
        if (!ReadColorField(Data, TEXT("lower_hemisphere_color"), LowerHemisphereColor, LowerHemisphereColor))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("lower_hemisphere_color must be an array of 4 numbers"));
            return nullptr;
        }

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "SetSkyLighting", "MCP Set Sky Lighting"));
        TArray<FString> Changed;
        ConfigureSkyLight(*World, SkyIntensity, LowerHemisphereColor, Changed);
        ConfigureSkyAtmosphere(*World, Changed);
        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("lighting_operation"), MakeLightingOperationResult(Changed));
    }

    if (CommandType == TEXT("lighting_set_fog"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        double Density = 0.01;
        double HeightFalloff = 0.2;
        double StartDistance = 0.0;
        FLinearColor Color = FLinearColor(0.08f, 0.1f, 0.16f, 1.0f);
        Data->TryGetNumberField(TEXT("density"), Density);
        Data->TryGetNumberField(TEXT("height_falloff"), HeightFalloff);
        Data->TryGetNumberField(TEXT("start_distance"), StartDistance);
        if (!ReadColorField(Data, TEXT("color"), Color, Color))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("fog color must be an array of 4 numbers"));
            return nullptr;
        }

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "SetFogLighting", "MCP Set Fog Lighting"));
        TArray<FString> Changed;
        ConfigureFog(*World, Density, HeightFalloff, Color, StartDistance, Changed);
        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("lighting_operation"), MakeLightingOperationResult(Changed));
    }

    if (CommandType == TEXT("lighting_set_post_process"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        double ExposureCompensation = -0.5;
        double MinBrightness = 0.2;
        double MaxBrightness = 1.0;
        double BloomIntensity = 0.6;
        Data->TryGetNumberField(TEXT("exposure_compensation"), ExposureCompensation);
        Data->TryGetNumberField(TEXT("min_brightness"), MinBrightness);
        Data->TryGetNumberField(TEXT("max_brightness"), MaxBrightness);
        Data->TryGetNumberField(TEXT("bloom_intensity"), BloomIntensity);

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "SetPostProcessLighting", "MCP Set Post Process Lighting"));
        TArray<FString> Changed;
        ConfigurePostProcess(*World, ExposureCompensation, MinBrightness, MaxBrightness, BloomIntensity, Changed);
        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("lighting_operation"), MakeLightingOperationResult(Changed));
    }

    if (CommandType == TEXT("lighting_bulk_set_lights"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        const TArray<TSharedPtr<FJsonValue>>* LightSpecs = nullptr;
        if (!Data->TryGetArrayField(TEXT("lights"), LightSpecs))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("lighting.bulk_set_lights requires data.lights"));
            return nullptr;
        }

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "BulkSetLights", "MCP Bulk Set Lights"));
        TArray<TSharedPtr<FJsonValue>> Lights;
        for (const TSharedPtr<FJsonValue>& LightValue : *LightSpecs)
        {
            const TSharedPtr<FJsonObject>* LightSpec = nullptr;
            if (!LightValue.IsValid() || !LightValue->TryGetObject(LightSpec) || LightSpec == nullptr || !LightSpec->IsValid())
            {
                continue;
            }

            FString Name;
            (*LightSpec)->TryGetStringField(TEXT("name"), Name);
            if (Name.IsEmpty())
            {
                continue;
            }

            FString Kind = TEXT("point");
            (*LightSpec)->TryGetStringField(TEXT("kind"), Kind);
            Kind.ToLowerInline();
            if (!Kind.Equals(TEXT("point")) && !Kind.Equals(TEXT("rect")) && !Kind.Equals(TEXT("spot")))
            {
                continue;
            }

            TSharedPtr<FJsonObject> TransformSpec = *LightSpec;
            const TSharedPtr<FJsonObject>* NestedTransform = nullptr;
            if ((*LightSpec)->TryGetObjectField(TEXT("transform"), NestedTransform) && NestedTransform != nullptr &&
                NestedTransform->IsValid())
            {
                TransformSpec = *NestedTransform;
            }

            FVector Location = FVector::ZeroVector;
            FRotator Rotation = FRotator::ZeroRotator;
            FVector Scale = FVector(1.0, 1.0, 1.0);
            FLinearColor Color = FLinearColor(1.0f, 0.82f, 0.55f, 1.0f);
            if (!ReadVectorField(TransformSpec, TEXT("location"), FVector::ZeroVector, Location) ||
                !ReadRotatorField(TransformSpec, TEXT("rotation"), FRotator::ZeroRotator, Rotation) ||
                !ReadVectorField(TransformSpec, TEXT("scale"), FVector(1.0, 1.0, 1.0), Scale) ||
                !ReadColorField(*LightSpec, TEXT("color"), Color, Color))
            {
                continue;
            }

            AActor* ExistingActor = FindActorByLabel<AActor>(*World, Name);
            if (ExistingActor && !IsLightActorKind(ExistingActor, Kind))
            {
                if (ExistingActor->ActorHasTag(TEXT("mcp.lighting")) || ExistingActor->ActorHasTag(TEXT("mcp.generated")))
                {
                    World->EditorDestroyActor(ExistingActor, true);
                }
                else
                {
                    continue;
                }
            }

            double Intensity = 5000.0;
            double AttenuationRadius = 1000.0;
            double SourceRadius = 24.0;
            double SourceWidth = 64.0;
            double SourceHeight = 32.0;
            (*LightSpec)->TryGetNumberField(TEXT("intensity"), Intensity);
            (*LightSpec)->TryGetNumberField(TEXT("attenuation_radius"), AttenuationRadius);
            (*LightSpec)->TryGetNumberField(TEXT("source_radius"), SourceRadius);
            (*LightSpec)->TryGetNumberField(TEXT("source_width"), SourceWidth);
            (*LightSpec)->TryGetNumberField(TEXT("source_height"), SourceHeight);

            const TArray<FString> ExtraTags = ReadStringArrayField(*LightSpec, TEXT("tags"));
            AActor* Actor = nullptr;
            ULightComponent* LightComponent = nullptr;

            if (Kind.Equals(TEXT("rect")))
            {
                bool bCreated = false;
                ARectLight* RectLight = FindOrSpawnNamedActor<ARectLight>(*World, Name, Location, Rotation, bCreated);
                Actor = RectLight;
                LightComponent = RectLight ? RectLight->GetLightComponent() : nullptr;
                if (URectLightComponent* RectComponent = Cast<URectLightComponent>(LightComponent))
                {
                    RectComponent->SetAttenuationRadius(static_cast<float>(FMath::Max(AttenuationRadius, 0.0)));
                    RectComponent->SetSourceWidth(static_cast<float>(FMath::Max(SourceWidth, 0.0)));
                    RectComponent->SetSourceHeight(static_cast<float>(FMath::Max(SourceHeight, 0.0)));
                }
            }
            else if (Kind.Equals(TEXT("spot")))
            {
                bool bCreated = false;
                ASpotLight* SpotLight = FindOrSpawnNamedActor<ASpotLight>(*World, Name, Location, Rotation, bCreated);
                Actor = SpotLight;
                LightComponent = SpotLight ? SpotLight->GetLightComponent() : nullptr;
                if (USpotLightComponent* SpotComponent = Cast<USpotLightComponent>(LightComponent))
                {
                    SpotComponent->SetAttenuationRadius(static_cast<float>(FMath::Max(AttenuationRadius, 0.0)));
                    SpotComponent->SetSourceRadius(static_cast<float>(FMath::Max(SourceRadius, 0.0)));
                }
            }
            else
            {
                bool bCreated = false;
                APointLight* PointLight = FindOrSpawnNamedActor<APointLight>(*World, Name, Location, Rotation, bCreated);
                Actor = PointLight;
                LightComponent = PointLight ? PointLight->GetLightComponent() : nullptr;
                if (UPointLightComponent* PointComponent = Cast<UPointLightComponent>(LightComponent))
                {
                    PointComponent->SetAttenuationRadius(static_cast<float>(FMath::Max(AttenuationRadius, 0.0)));
                    PointComponent->SetSourceRadius(static_cast<float>(FMath::Max(SourceRadius, 0.0)));
                }
            }

            if (!Actor || !LightComponent)
            {
                continue;
            }

            Actor->Modify();
            Actor->SetActorLocationAndRotation(Location, Rotation);
            Actor->SetActorScale3D(Scale);
            AddGeneratedLightingTags(*Actor, ExtraTags);
            if (USceneComponent* RootComponent = Actor->GetRootComponent())
            {
                RootComponent->SetMobility(EComponentMobility::Movable);
            }

            LightComponent->Modify();
            LightComponent->SetVisibility(true);
            LightComponent->SetMobility(EComponentMobility::Movable);
            LightComponent->SetIntensity(static_cast<float>(FMath::Max(Intensity, 0.0)));
            LightComponent->SetLightColor(Color);
            LightComponent->MarkRenderStateDirty();
            AddLightSummary(Lights, *Actor, Kind);
        }

        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("lighting_bulk_set_lights"), MakeBulkLightResult(Lights));
    }

    if (CommandType == TEXT("lighting_set_time_of_day"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        FRotator SunRotation = FRotator(-10.0, 110.0, 0.0);
        FLinearColor SunColor = FLinearColor(1.0f, 0.93f, 0.82f, 1.0f);
        if (!ReadRotatorField(Data, TEXT("sun_rotation"), SunRotation, SunRotation) ||
            !ReadColorField(Data, TEXT("sun_color"), SunColor, SunColor))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("lighting vectors must be fixed-size numeric arrays"));
            return nullptr;
        }

        double SunIntensity = 1.0;
        Data->TryGetNumberField(TEXT("sun_intensity"), SunIntensity);

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "SetTimeOfDayLighting", "MCP Set Time Of Day Lighting"));
        TArray<FString> Changed;
        ConfigureDirectionalLight(*World, TEXT("MCP_SunLight"), SunRotation, SunIntensity, SunColor, Changed);
        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("lighting_operation"), MakeLightingOperationResult(Changed));
    }

    if (CommandType == TEXT("landscape_create"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        TSharedPtr<FJsonObject> SpecData = GetNestedObjectOrSelf(Data, TEXT("spec"));
        FString Name;
        if (!SpecData->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("landscape.create requires data.name"));
            return nullptr;
        }

        FIntPoint ComponentCount(4, 4);
        if (!ReadIntPointField(SpecData, TEXT("component_count"), ComponentCount, ComponentCount))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("component_count must contain two integers"));
            return nullptr;
        }

        double SectionSizeNumber = 63.0;
        double SectionsPerComponentNumber = 1.0;
        SpecData->TryGetNumberField(TEXT("section_size"), SectionSizeNumber);
        SpecData->TryGetNumberField(TEXT("sections_per_component"), SectionsPerComponentNumber);
        const int32 SectionSize = static_cast<int32>(SectionSizeNumber);
        const int32 SectionsPerComponent = static_cast<int32>(SectionsPerComponentNumber);

        FString ShapeError;
        if (!ValidateLandscapeShape(ComponentCount, SectionSize, SectionsPerComponent, ShapeError))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), ShapeError);
            return nullptr;
        }

        FVector Location = FVector::ZeroVector;
        FVector Scale = FVector(100.0, 100.0, 100.0);
        if (!ReadVectorField(SpecData, TEXT("location"), Location, Location) ||
            !ReadVectorField(SpecData, TEXT("scale"), Scale, Scale))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("landscape transform vectors must be fixed-size numeric arrays"));
            return nullptr;
        }

        UMaterialInterface* Material = nullptr;
        FString MaterialPath;
        if (SpecData->TryGetStringField(TEXT("material"), MaterialPath) && !MaterialPath.IsEmpty())
        {
            Material = LoadMaterialInterfaceFromPath(MaterialPath);
            if (!Material)
            {
                OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to load landscape material '%s'"), *MaterialPath));
                return nullptr;
            }
        }

        const TArray<FString> Tags = ReadStringArrayField(SpecData, TEXT("tags"));
        TArray<FString> Changed;
        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "CreateLandscape", "MCP Create Landscape"));

        ALandscape* Landscape = FindLandscapeByLabel(*World, Name);
        if (Landscape)
        {
            Landscape->Modify();
            Landscape->SetActorLocation(Location);
            Landscape->SetActorScale3D(Scale);
            AddGeneratedLandscapeTags(*Landscape, Tags);
            Changed.Add(TEXT("updated"));
            if (Material)
            {
                AssignLandscapeMaterial(*Landscape, *Material);
                Changed.Add(TEXT("material"));
            }
            Landscape->MarkPackageDirty();
            World->MarkPackageDirty();
            return MakeTaggedResult(
                TEXT("landscape_operation"),
                MakeLandscapeOperation(Name, Landscape->GetPathName(), GetLandscapeComponentCount(*Landscape), GetLandscapeVertexCount(*Landscape), Changed));
        }

        const FIntPoint VertexCount(
            ComponentCount.X * SectionSize * SectionsPerComponent + 1,
            ComponentCount.Y * SectionSize * SectionsPerComponent + 1);
        TArray<uint16> HeightData = MakeFlatLandscapeHeightData(VertexCount);

        FActorSpawnParameters SpawnParameters;
        SpawnParameters.Name = MakeUniqueObjectName(World, ALandscape::StaticClass(), FName(*Name));
        Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator, SpawnParameters);
        if (!Landscape)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to spawn landscape actor"));
            return nullptr;
        }

        Landscape->SetActorLabel(Name);
        Landscape->SetActorScale3D(Scale);
        AddGeneratedLandscapeTags(*Landscape, Tags);
        if (Material)
        {
            AssignLandscapeMaterial(*Landscape, *Material);
        }

        TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
        HeightDataPerLayers.Add(FGuid(), MoveTemp(HeightData));
        TMap<FGuid, TArray<FLandscapeImportLayerInfo>> ImportMaterialLayerInfosPerLayers;
        ImportMaterialLayerInfosPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

        Landscape->Import(
            FGuid::NewGuid(),
            0,
            0,
            VertexCount.X - 1,
            VertexCount.Y - 1,
            SectionsPerComponent,
            SectionSize,
            HeightDataPerLayers,
            TEXT(""),
            ImportMaterialLayerInfosPerLayers,
            ELandscapeImportAlphamapType::Additive,
            TArrayView<const FLandscapeLayer>());
        Landscape->CreateLandscapeInfo();
        Landscape->RegisterAllComponents();
        Landscape->PostEditChange();
        Landscape->MarkPackageDirty();
        World->MarkPackageDirty();

        Changed.Add(TEXT("created"));
        return MakeTaggedResult(
            TEXT("landscape_operation"),
            MakeLandscapeOperation(Name, Landscape->GetPathName(), ComponentCount, VertexCount, Changed));
    }

    if (CommandType == TEXT("landscape_set_heightfield"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        TSharedPtr<FJsonObject> PatchData = GetNestedObjectOrSelf(Data, TEXT("patch"));
        FString Name;
        if (!PatchData->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("landscape.set_heightfield requires data.name"));
            return nullptr;
        }

        ALandscape* Landscape = FindLandscapeByLabel(*World, Name);
        if (!Landscape)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to find landscape '%s'"), *Name));
            return nullptr;
        }

        TArray<uint16> HeightData;
        FIntRect Extent;
        FString HeightError;
        if (!BuildHeightDataFromPatch(PatchData, *Landscape, HeightData, Extent, HeightError))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), HeightError);
            return nullptr;
        }

        ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
        if (!LandscapeInfo)
        {
            LandscapeInfo = Landscape->CreateLandscapeInfo();
        }
        if (!LandscapeInfo)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to read landscape info"));
            return nullptr;
        }

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "SetLandscapeHeightfield", "MCP Set Landscape Heightfield"));
        Landscape->Modify();
        Landscape->PreEditChange(nullptr);
        FLandscapeEditDataInterface Edit(LandscapeInfo);
        Edit.SetHeightData(Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y, HeightData.GetData(), 0, true);
        Landscape->PostEditChange();
        Landscape->MarkPackageDirty();
        World->MarkPackageDirty();

        TArray<FString> Changed = {TEXT("heightfield")};
        return MakeTaggedResult(
            TEXT("landscape_operation"),
            MakeLandscapeOperation(Name, Landscape->GetPathName(), GetLandscapeComponentCount(*Landscape), GetLandscapeVertexCount(*Landscape), Changed));
    }

    if (CommandType == TEXT("landscape_paint_layers"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        TSharedPtr<FJsonObject> PaintData = GetNestedObjectOrSelf(Data, TEXT("paint"));
        FString Name;
        if (!PaintData->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("landscape.paint_layers requires data.name"));
            return nullptr;
        }

        ALandscape* Landscape = FindLandscapeByLabel(*World, Name);
        if (!Landscape)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to find landscape '%s'"), *Name));
            return nullptr;
        }

        UMaterialInterface* Material = nullptr;
        FString MaterialPath;
        if (PaintData->TryGetStringField(TEXT("material"), MaterialPath) && !MaterialPath.IsEmpty())
        {
            Material = LoadMaterialInterfaceFromPath(MaterialPath);
            if (!Material)
            {
                OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to load landscape material '%s'"), *MaterialPath));
                return nullptr;
            }
        }

        TArray<FString> Changed;
        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "PaintLandscapeLayers", "MCP Paint Landscape Layers"));
        Landscape->Modify();
        if (Material)
        {
            AssignLandscapeMaterial(*Landscape, *Material);
            Changed.Add(TEXT("material"));
        }

        FString PaintError;
        const TArray<FString> Layers = ReadStringArrayField(PaintData, TEXT("layers"));
        if (!PaintLandscapeLayers(*Landscape, Layers, Changed, PaintError))
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), PaintError);
            return nullptr;
        }

        Landscape->UpdateAllComponentMaterialInstances();
        Landscape->PostEditChange();
        Landscape->MarkPackageDirty();
        World->MarkPackageDirty();
        return MakeTaggedResult(
            TEXT("landscape_operation"),
            MakeLandscapeOperation(Name, Landscape->GetPathName(), GetLandscapeComponentCount(*Landscape), GetLandscapeVertexCount(*Landscape), Changed));
    }

    if (CommandType == TEXT("placement_bulk_snap_to_ground"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world is loaded"));
            return nullptr;
        }

        TSharedPtr<FJsonObject> SpecData = GetNestedObjectOrSelf(Data, TEXT("spec"));
        const TArray<FString> Names = ReadStringArrayField(SpecData, TEXT("names"));
        TArray<FString> Tags = ReadStringArrayField(SpecData, TEXT("tags"));
        bool bIncludeGenerated = false;
        SpecData->TryGetBoolField(TEXT("include_generated"), bIncludeGenerated);
        if (bIncludeGenerated && Tags.IsEmpty())
        {
            Tags.Add(TEXT("mcp.generated"));
        }

        if (Names.IsEmpty() && Tags.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("placement.bulk_snap_to_ground requires names, tags, or include_generated"));
            return nullptr;
        }

        double TraceDistance = 50000.0;
        double OffsetZ = 0.0;
        SpecData->TryGetNumberField(TEXT("trace_distance"), TraceDistance);
        SpecData->TryGetNumberField(TEXT("offset_z"), OffsetZ);
        TraceDistance = FMath::Max(TraceDistance, 1.0);

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "BulkSnapToGround", "MCP Bulk Snap To Ground"));
        TArray<TSharedPtr<FJsonValue>> SnappedActors;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor) || Actor->IsA<ALandscapeProxy>() || !ActorMatches(*Actor, Names, Tags))
            {
                continue;
            }

            const FVector OldLocation = Actor->GetActorLocation();
            const FVector Start = OldLocation + FVector(0.0, 0.0, TraceDistance);
            const FVector End = OldLocation - FVector(0.0, 0.0, TraceDistance);
            FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UnrealMCPBulkSnapToGround), true);
            QueryParams.AddIgnoredActor(Actor);

            FHitResult Hit;
            if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, QueryParams))
            {
                continue;
            }

            const FBox Bounds = Actor->GetComponentsBoundingBox(true);
            const double PivotToBottom = Bounds.IsValid ? OldLocation.Z - Bounds.Min.Z : 0.0;
            FVector NewLocation = OldLocation;
            NewLocation.Z = Hit.Location.Z + PivotToBottom + OffsetZ;
            Actor->Modify();
            Actor->SetActorLocation(NewLocation);
            AddSnappedActor(SnappedActors, *Actor, OldLocation, NewLocation);
        }

        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("placement_snap"), MakePlacementSnapResult(SnappedActors));
    }

    if (CommandType == TEXT("blueprint_create_actor"))
    {
        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("blueprint.create_actor requires data.path"));
            return nullptr;
        }

        FString ParentClassName = TEXT("Actor");
        Data->TryGetStringField(TEXT("parent_class"), ParentClassName);
        UClass* ParentClass = ResolveBlueprintParentClass(ParentClassName);
        if (!ParentClass)
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), FString::Printf(TEXT("failed to resolve parent class '%s'"), *ParentClassName));
            return nullptr;
        }

        FString PackagePath;
        FString AssetName;
        FString PathError;
        if (!SplitAssetPath(Path, PackagePath, AssetName, PathError))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), PathError);
            return nullptr;
        }

        if (UBlueprint* ExistingBlueprint = LoadBlueprintAsset(Path))
        {
            ExistingBlueprint->Modify();
            return MakeTaggedResult(TEXT("blueprint_operation"), MakeBlueprintOperation(Path, false, false));
        }

        UPackage* Package = CreatePackage(*(PackagePath / AssetName));
        UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
            ParentClass,
            Package,
            FName(*AssetName),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            FName(TEXT("UnrealMCPBridge")));
        if (!Blueprint)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to create Blueprint '%s'"), *Path));
            return nullptr;
        }

        FAssetRegistryModule::AssetCreated(Blueprint);
        Package->MarkPackageDirty();
        return MakeTaggedResult(TEXT("blueprint_operation"), MakeBlueprintOperation(Path, true, false));
    }

    if (CommandType == TEXT("blueprint_add_static_mesh_component"))
    {
        FString BlueprintPath;
        if (!Data->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("blueprint.add_static_mesh_component requires data.blueprint"));
            return nullptr;
        }

        const TSharedPtr<FJsonObject>* ComponentData = nullptr;
        if (!Data->TryGetObjectField(TEXT("component"), ComponentData) || ComponentData == nullptr || !ComponentData->IsValid())
        {
            ComponentData = &Data;
        }

        FString ComponentName;
        FString MeshPath;
        (*ComponentData)->TryGetStringField(TEXT("name"), ComponentName);
        (*ComponentData)->TryGetStringField(TEXT("mesh"), MeshPath);
        if (ComponentName.IsEmpty() || MeshPath.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("static mesh component requires name and mesh"));
            return nullptr;
        }

        UBlueprint* Blueprint = LoadBlueprintAsset(BlueprintPath);
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *ToObjectPath(MeshPath));
        if (!Blueprint || !Mesh)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to load Blueprint or static mesh"));
            return nullptr;
        }

        bool bCreated = false;
        UStaticMeshComponent* Component = FindOrCreateBlueprintComponent<UStaticMeshComponent>(*Blueprint, FName(*ComponentName), bCreated);
        if (!Component)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to create static mesh component template"));
            return nullptr;
        }

        Component->Modify();
        Component->SetStaticMesh(Mesh);
        Component->SetMobility(EComponentMobility::Movable);
        ApplyRelativeTransform(*Component, *ComponentData);

        FString MaterialPath;
        if ((*ComponentData)->TryGetStringField(TEXT("material"), MaterialPath) && !MaterialPath.IsEmpty())
        {
            if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *ToObjectPath(MaterialPath)))
            {
                Component->SetMaterial(0, Material);
            }
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        TArray<FString> Components = {ComponentName};
        return MakeTaggedResult(TEXT("blueprint_component_operation"), MakeBlueprintComponentOperation(BlueprintPath, Components));
    }

    if (CommandType == TEXT("blueprint_add_light_component"))
    {
        FString BlueprintPath;
        if (!Data->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("blueprint.add_light_component requires data.blueprint"));
            return nullptr;
        }

        const TSharedPtr<FJsonObject>* ComponentData = nullptr;
        if (!Data->TryGetObjectField(TEXT("component"), ComponentData) || ComponentData == nullptr || !ComponentData->IsValid())
        {
            ComponentData = &Data;
        }

        FString ComponentName;
        (*ComponentData)->TryGetStringField(TEXT("name"), ComponentName);
        if (ComponentName.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("light component requires name"));
            return nullptr;
        }

        FString Kind = TEXT("point");
        (*ComponentData)->TryGetStringField(TEXT("kind"), Kind);
        Kind.ToLowerInline();

        UBlueprint* Blueprint = LoadBlueprintAsset(BlueprintPath);
        if (!Blueprint)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to load Blueprint '%s'"), *BlueprintPath));
            return nullptr;
        }

        FLinearColor Color = FLinearColor(1.0f, 0.82f, 0.55f, 1.0f);
        ReadColorField(*ComponentData, TEXT("color"), Color, Color);
        double Intensity = 5000.0;
        double AttenuationRadius = 1000.0;
        double SourceRadius = 24.0;
        double SourceWidth = 64.0;
        double SourceHeight = 32.0;
        (*ComponentData)->TryGetNumberField(TEXT("intensity"), Intensity);
        (*ComponentData)->TryGetNumberField(TEXT("attenuation_radius"), AttenuationRadius);
        (*ComponentData)->TryGetNumberField(TEXT("source_radius"), SourceRadius);
        (*ComponentData)->TryGetNumberField(TEXT("source_width"), SourceWidth);
        (*ComponentData)->TryGetNumberField(TEXT("source_height"), SourceHeight);

        ULightComponent* Component = nullptr;
        if (Kind == TEXT("rect"))
        {
            bool bCreated = false;
            URectLightComponent* RectComponent = FindOrCreateBlueprintComponent<URectLightComponent>(*Blueprint, FName(*ComponentName), bCreated);
            if (RectComponent)
            {
                RectComponent->SetAttenuationRadius(static_cast<float>(AttenuationRadius));
                RectComponent->SetSourceWidth(static_cast<float>(SourceWidth));
                RectComponent->SetSourceHeight(static_cast<float>(SourceHeight));
            }
            Component = RectComponent;
        }
        else if (Kind == TEXT("spot"))
        {
            bool bCreated = false;
            USpotLightComponent* SpotComponent = FindOrCreateBlueprintComponent<USpotLightComponent>(*Blueprint, FName(*ComponentName), bCreated);
            if (SpotComponent)
            {
                SpotComponent->SetAttenuationRadius(static_cast<float>(AttenuationRadius));
                SpotComponent->SetSourceRadius(static_cast<float>(SourceRadius));
            }
            Component = SpotComponent;
        }
        else
        {
            bool bCreated = false;
            UPointLightComponent* PointComponent = FindOrCreateBlueprintComponent<UPointLightComponent>(*Blueprint, FName(*ComponentName), bCreated);
            if (PointComponent)
            {
                PointComponent->SetAttenuationRadius(static_cast<float>(AttenuationRadius));
                PointComponent->SetSourceRadius(static_cast<float>(SourceRadius));
            }
            Component = PointComponent;
        }

        if (!Component)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to create light component template"));
            return nullptr;
        }

        Component->Modify();
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetIntensity(static_cast<float>(Intensity));
        Component->SetLightColor(Color);
        ApplyRelativeTransform(*Component, *ComponentData);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        Blueprint->MarkPackageDirty();
        TArray<FString> Components = {ComponentName};
        return MakeTaggedResult(TEXT("blueprint_component_operation"), MakeBlueprintComponentOperation(BlueprintPath, Components));
    }

    if (CommandType == TEXT("blueprint_compile"))
    {
        FString Path;
        if (!Data->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("blueprint.compile requires data.path"));
            return nullptr;
        }

        UBlueprint* Blueprint = LoadBlueprintAsset(Path);
        if (!Blueprint)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to load Blueprint '%s'"), *Path));
            return nullptr;
        }

        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        Blueprint->MarkPackageDirty();
        return MakeTaggedResult(TEXT("blueprint_operation"), MakeBlueprintOperation(Path, false, true));
    }

    if (CommandType == TEXT("runtime_create_led_animation") ||
        CommandType == TEXT("runtime_create_moving_light_animation") ||
        CommandType == TEXT("runtime_create_material_parameter_animation"))
    {
        TSharedPtr<FJsonObject> SpecData;
        ReadAnimationSpecData(Data, SpecData);

        FString Path;
        if (!SpecData->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("runtime animation creation requires data.path"));
            return nullptr;
        }

        FString PackagePath;
        FString AssetName;
        FString PathError;
        if (!SplitAssetPath(Path, PackagePath, AssetName, PathError))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), PathError);
            return nullptr;
        }

        EUnrealMCPRuntimeAnimationKind Kind = EUnrealMCPRuntimeAnimationKind::Led;
        if (CommandType == TEXT("runtime_create_moving_light_animation"))
        {
            Kind = EUnrealMCPRuntimeAnimationKind::MovingLight;
        }
        else if (CommandType == TEXT("runtime_create_material_parameter_animation"))
        {
            Kind = EUnrealMCPRuntimeAnimationKind::MaterialParameter;
        }

        UUnrealMCPRuntimeAnimationPreset* Preset = LoadObject<UUnrealMCPRuntimeAnimationPreset>(nullptr, *ToObjectPath(Path));
        if (!Preset)
        {
            UPackage* Package = CreatePackage(*(PackagePath / AssetName));
            Preset = NewObject<UUnrealMCPRuntimeAnimationPreset>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
            FAssetRegistryModule::AssetCreated(Preset);
        }

        if (!Preset || !ConfigureAnimationPreset(*Preset, Kind, SpecData))
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), FString::Printf(TEXT("failed to create runtime animation '%s'"), *Path));
            return nullptr;
        }

        Preset->MarkPackageDirty();
        if (UPackage* Package = Preset->GetOutermost())
        {
            Package->MarkPackageDirty();
        }

        return MakeTaggedResult(TEXT("runtime_animation_operation"), MakeRuntimeAnimationOperation(Path, TArray<FString>()));
    }

    if (CommandType == TEXT("runtime_attach_animation_to_actor"))
    {
        const TArray<FString> Names = ReadStringArrayField(Data, TEXT("names"));
        const TArray<FString> Tags = ReadStringArrayField(Data, TEXT("tags"));
        const TArray<FString> AnimationPaths = ReadStringArrayField(Data, TEXT("animations"));
        FString BlueprintPath;
        Data->TryGetStringField(TEXT("blueprint"), BlueprintPath);

        if (AnimationPaths.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("runtime.attach_animation_to_actor requires animations"));
            return nullptr;
        }

        TArray<UUnrealMCPRuntimeAnimationPreset*> Presets;
        for (const FString& AnimationPath : AnimationPaths)
        {
            if (UUnrealMCPRuntimeAnimationPreset* Preset = LoadObject<UUnrealMCPRuntimeAnimationPreset>(nullptr, *ToObjectPath(AnimationPath)))
            {
                Presets.Add(Preset);
            }
        }

        if (Presets.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to load runtime animation assets"));
            return nullptr;
        }

        TArray<FString> Attached;
        if (!BlueprintPath.IsEmpty())
        {
            UBlueprint* Blueprint = LoadBlueprintAsset(BlueprintPath);
            if (Blueprint)
            {
                bool bCreated = false;
                UUnrealMCPRuntimeAnimatorComponent* Animator =
                    FindOrCreateBlueprintComponent<UUnrealMCPRuntimeAnimatorComponent>(*Blueprint, TEXT("MCP_RuntimeAnimator"), bCreated);
                if (Animator)
                {
                    Animator->Modify();
                    AddPresetsToAnimator(*Animator, Presets);
                    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
                    FKismetEditorUtilities::CompileBlueprint(Blueprint);
                    Blueprint->MarkPackageDirty();
                    Attached.Add(BlueprintPath);
                }
            }
        }

        UWorld* World = GetEditorWorld();
        if (World && (!Names.IsEmpty() || !Tags.IsEmpty()))
        {
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                AActor* Actor = *It;
                if (!IsValid(Actor) || !ActorMatches(*Actor, Names, Tags))
                {
                    continue;
                }

                UUnrealMCPRuntimeAnimatorComponent* Animator = Actor->FindComponentByClass<UUnrealMCPRuntimeAnimatorComponent>();
                if (!Animator)
                {
                    Animator = NewObject<UUnrealMCPRuntimeAnimatorComponent>(Actor, TEXT("MCP_RuntimeAnimator"), RF_Transactional);
                    Actor->AddInstanceComponent(Animator);
                    Animator->RegisterComponent();
                }

                Actor->Modify();
                Animator->Modify();
                AddPresetsToAnimator(*Animator, Presets);
                Attached.Add(Actor->GetActorLabel());
            }
            World->MarkPackageDirty();
        }

        return MakeTaggedResult(TEXT("runtime_animation_operation"), MakeRuntimeAnimationOperation(FString(), Attached));
    }

    if (CommandType == TEXT("game_create_player") ||
        CommandType == TEXT("game_create_checkpoint") ||
        CommandType == TEXT("game_create_interaction") ||
        CommandType == TEXT("game_create_collectibles") ||
        CommandType == TEXT("game_create_objective_flow"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world"));
            return nullptr;
        }

        TSharedPtr<FJsonObject> SpecData = GetNestedObjectOrSelf(Data, TEXT("spec"));
        TArray<TSharedPtr<FJsonValue>> Spawned;
        FGameplayCounts Counts;
        FString Error;

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "GameplayFoundation", "MCP Gameplay Foundation"));
        if (CommandType == TEXT("game_create_player"))
        {
            if (!SpawnGameplayPlayer(*World, SpecData, Spawned, Counts, Error))
            {
                OutError = BuildError(CommandIndex, TEXT("invalid_payload"), Error);
                return nullptr;
            }
        }
        else if (CommandType == TEXT("game_create_collectibles"))
        {
            UStaticMesh* Mesh = LoadSceneStaticMesh(SpecData, TEXT("mesh"), TEXT("/Engine/BasicShapes/Cube.Cube"), Error);
            if (!Mesh || !SpawnGameplayCollectibles(*World, SpecData, *Mesh, Spawned, Counts, Error))
            {
                OutError = BuildError(CommandIndex, TEXT("invalid_payload"), Error);
                return nullptr;
            }
        }
        else
        {
            UStaticMesh* MarkerMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
            if (!MarkerMesh)
            {
                OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to load default cube marker mesh"));
                return nullptr;
            }

            if (CommandType == TEXT("game_create_checkpoint"))
            {
                if (!SpawnGameplayCheckpoint(*World, SpecData, *MarkerMesh, Spawned, Counts, Error))
                {
                    OutError = BuildError(CommandIndex, TEXT("invalid_payload"), Error);
                    return nullptr;
                }
            }
            else if (CommandType == TEXT("game_create_interaction"))
            {
                if (!SpawnGameplayInteraction(*World, SpecData, *MarkerMesh, Spawned, Counts, Error))
                {
                    OutError = BuildError(CommandIndex, TEXT("invalid_payload"), Error);
                    return nullptr;
                }
            }
            else if (CommandType == TEXT("game_create_objective_flow"))
            {
                if (!SpawnGameplayObjectiveFlow(*World, SpecData, *MarkerMesh, Spawned, Counts, Error))
                {
                    OutError = BuildError(CommandIndex, TEXT("invalid_payload"), Error);
                    return nullptr;
                }
            }
        }

        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("gameplay_operation"), MakeGameplayOperationResult(Spawned, Counts));
    }

    if (CommandType == TEXT("gameplay_create_system"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world"));
            return nullptr;
        }

        TSharedPtr<FJsonObject> SpecData = GetNestedObjectOrSelf(Data, TEXT("spec"));
        FString Name;
        if (!SpecData->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("gameplay runtime system requires name"));
            return nullptr;
        }

        FVector Location = FVector::ZeroVector;
        if (!ReadVectorField(SpecData, TEXT("location"), FVector::ZeroVector, Location))
        {
            OutError = BuildError(CommandIndex, TEXT("invalid_payload"), TEXT("gameplay runtime system location must have 3 numbers"));
            return nullptr;
        }

        FString Scene;
        SpecData->TryGetStringField(TEXT("scene"), Scene);
        FString Group;
        SpecData->TryGetStringField(TEXT("group"), Group);
        const TArray<FString> Tags = ReadStringArrayField(SpecData, TEXT("tags"));

        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "GameplayRuntimeCreateSystem", "MCP Gameplay Runtime System"));
        AActor* ManagerActor = FindActorByLabel<AActor>(*World, Name);
        if (!ManagerActor)
        {
            FActorSpawnParameters SpawnParameters;
            SpawnParameters.Name = MakeUniqueObjectName(World, AActor::StaticClass(), FName(*Name));
            ManagerActor = World->SpawnActor<AActor>(Location, FRotator::ZeroRotator, SpawnParameters);
            if (!ManagerActor)
            {
                OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to create gameplay runtime manager"));
                return nullptr;
            }
            ManagerActor->SetActorLabel(Name);
        }

        ManagerActor->Modify();
        if (!ManagerActor->GetRootComponent())
        {
            USceneComponent* RootComponent = NewObject<USceneComponent>(ManagerActor, TEXT("MCP_GameplayManagerRoot"), RF_Transactional);
            if (!RootComponent)
            {
                OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to create gameplay manager root component"));
                return nullptr;
            }

            ManagerActor->SetRootComponent(RootComponent);
            ManagerActor->AddInstanceComponent(RootComponent);
            RootComponent->RegisterComponent();
        }
        ManagerActor->SetActorLocation(Location);
        AddGameplayRuntimeTags(*ManagerActor, Scene, Group, Tags);

        bool bCreated = false;
        UUnrealMCPGameplayManagerComponent* ManagerComponent =
            FindOrCreateActorComponent<UUnrealMCPGameplayManagerComponent>(*ManagerActor, TEXT("MCP_GameplayManagerRuntime"), bCreated);
        if (!ManagerComponent)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("failed to create gameplay manager component"));
            return nullptr;
        }
        ManagerComponent->Modify();

        TArray<TSharedPtr<FJsonValue>> Bindings;
        FGameplayRuntimeCounts Counts;
        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("gameplay_runtime_operation"), MakeGameplayRuntimeOperationResult(ManagerActor, Bindings, Counts));
    }

    if (CommandType == TEXT("gameplay_bind_collectibles") ||
        CommandType == TEXT("gameplay_bind_checkpoints") ||
        CommandType == TEXT("gameplay_bind_interactions") ||
        CommandType == TEXT("gameplay_bind_objective_flow"))
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            OutError = BuildError(CommandIndex, TEXT("unreal_api_failure"), TEXT("no editor world"));
            return nullptr;
        }

        TSharedPtr<FJsonObject> SpecData = GetNestedObjectOrSelf(Data, TEXT("spec"));
        TArray<FString> Names = ReadStringArrayField(SpecData, TEXT("names"));
        TArray<FString> Tags = ReadStringArrayField(SpecData, TEXT("tags"));
        if (Names.IsEmpty() && Tags.IsEmpty())
        {
            if (CommandType == TEXT("gameplay_bind_collectibles"))
            {
                Tags.Add(TEXT("mcp.gameplay_actor:collectible"));
            }
            else if (CommandType == TEXT("gameplay_bind_checkpoints"))
            {
                Tags.Add(TEXT("mcp.gameplay_actor:checkpoint"));
            }
            else if (CommandType == TEXT("gameplay_bind_interactions"))
            {
                Tags.Add(TEXT("mcp.gameplay_actor:interaction"));
            }
            else
            {
                Tags.Add(TEXT("mcp.gameplay_actor:objective"));
            }
        }
        bool bIncludeGenerated = false;
        SpecData->TryGetBoolField(TEXT("include_generated"), bIncludeGenerated);
        if (bIncludeGenerated && Names.IsEmpty() && Tags.IsEmpty())
        {
            Tags.Add(TEXT("mcp.generated"));
        }

        FString ManagerName = TEXT("MCP_GameplayManager");
        SpecData->TryGetStringField(TEXT("manager_name"), ManagerName);
        if (ManagerName.IsEmpty())
        {
            ManagerName = TEXT("MCP_GameplayManager");
        }

        int32 CollectibleValue = 1;
        SpecData->TryGetNumberField(TEXT("value"), CollectibleValue);
        bool bDestroyOnCollect = true;
        SpecData->TryGetBoolField(TEXT("destroy_on_collect"), bDestroyOnCollect);

        TArray<TSharedPtr<FJsonValue>> Bindings;
        FGameplayRuntimeCounts Counts;
        FScopedTransaction Transaction(NSLOCTEXT("UnrealMCPBridge", "GameplayRuntimeBind", "MCP Gameplay Runtime Bindings"));

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor) || Actor->ActorHasTag(TEXT("mcp.gameplay_runtime")) || !ActorMatches(*Actor, Names, Tags))
            {
                continue;
            }

            Actor->Modify();
            if (CommandType == TEXT("gameplay_bind_collectibles"))
            {
                bool bCreated = false;
                UUnrealMCPCollectibleComponent* Component =
                    FindOrCreateActorComponent<UUnrealMCPCollectibleComponent>(*Actor, TEXT("MCP_CollectibleRuntime"), bCreated);
                if (!Component)
                {
                    continue;
                }

                Component->Modify();
                Component->ManagerActorLabel = FName(*ManagerName);
                Component->CollectibleId = FName(*Actor->GetActorLabel());
                Component->Value = ReadActorTagIntOrDefault(*Actor, TEXT("mcp.value:"), CollectibleValue);
                Component->bDestroyOnCollect = bDestroyOnCollect;
                Actor->Tags.AddUnique(TEXT("mcp.runtime:collectible"));
                AddGameplayRuntimeBinding(Bindings, *Actor, *Component);
                ++Counts.CollectibleCount;
            }
            else if (CommandType == TEXT("gameplay_bind_checkpoints"))
            {
                bool bCreated = false;
                UUnrealMCPCheckpointComponent* Component =
                    FindOrCreateActorComponent<UUnrealMCPCheckpointComponent>(*Actor, TEXT("MCP_CheckpointRuntime"), bCreated);
                if (!Component)
                {
                    continue;
                }

                Component->Modify();
                Component->ManagerActorLabel = FName(*ManagerName);
                Component->CheckpointId = FName(*ReadActorTagValueOrDefault(*Actor, TEXT("mcp.checkpoint:"), Actor->GetActorLabel()));
                Component->Order = ReadActorTagIntOrDefault(*Actor, TEXT("mcp.order:"), 0);
                Actor->Tags.AddUnique(TEXT("mcp.runtime:checkpoint"));
                AddGameplayRuntimeBinding(Bindings, *Actor, *Component);
                ++Counts.CheckpointCount;
            }
            else if (CommandType == TEXT("gameplay_bind_interactions"))
            {
                bool bCreated = false;
                UUnrealMCPInteractionComponent* Component =
                    FindOrCreateActorComponent<UUnrealMCPInteractionComponent>(*Actor, TEXT("MCP_InteractionRuntime"), bCreated);
                if (!Component)
                {
                    continue;
                }

                Component->Modify();
                Component->ManagerActorLabel = FName(*ManagerName);
                Component->InteractionId = FName(*ReadActorTagValueOrDefault(*Actor, TEXT("mcp.interaction_id:"), Actor->GetActorLabel()));
                Component->Kind = FName(*ReadActorTagValueOrDefault(*Actor, TEXT("mcp.interaction:"), TEXT("interaction")));
                Component->Action = FName(*ReadActorTagValueOrDefault(*Actor, TEXT("mcp.action:"), FString()));
                Component->TargetActorLabel = FName(*ReadActorTagValueOrDefault(*Actor, TEXT("mcp.target:"), FString()));
                Actor->Tags.AddUnique(TEXT("mcp.runtime:interaction"));
                AddGameplayRuntimeBinding(Bindings, *Actor, *Component);
                ++Counts.InteractionCount;
            }
            else if (CommandType == TEXT("gameplay_bind_objective_flow"))
            {
                bool bCreated = false;
                UUnrealMCPObjectiveComponent* Component =
                    FindOrCreateActorComponent<UUnrealMCPObjectiveComponent>(*Actor, TEXT("MCP_ObjectiveRuntime"), bCreated);
                if (!Component)
                {
                    continue;
                }

                Component->Modify();
                Component->ManagerActorLabel = FName(*ManagerName);
                Component->ObjectiveId = FName(*ReadActorTagValueOrDefault(*Actor, TEXT("mcp.objective:"), Actor->GetActorLabel()));
                Component->Label = FName(*ReadActorTagValueOrDefault(*Actor, TEXT("mcp.objective_label:"), Actor->GetActorLabel()));
                Component->Kind = FName(*ReadActorTagValueOrDefault(*Actor, TEXT("mcp.objective_kind:"), TEXT("location")));
                Component->Order = ReadActorTagIntOrDefault(*Actor, TEXT("mcp.order:"), 0);
                Actor->Tags.AddUnique(TEXT("mcp.runtime:objective"));
                AddGameplayRuntimeBinding(Bindings, *Actor, *Component);
                ++Counts.ObjectiveCount;
            }
        }

        World->MarkPackageDirty();
        return MakeTaggedResult(TEXT("gameplay_runtime_operation"), MakeGameplayRuntimeOperationResult(nullptr, Bindings, Counts));
    }

    return nullptr;
}

TSharedRef<FJsonObject> FBridgeServer::BuildSuccessResponse(
    uint64 RequestId,
    uint32 ElapsedMs,
    const TArray<TSharedPtr<FJsonValue>>& Results) const
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetNumberField(TEXT("protocol_version"), ProtocolVersion);
    Response->SetNumberField(TEXT("request_id"), static_cast<double>(RequestId));
    Response->SetBoolField(TEXT("ok"), true);
    Response->SetNumberField(TEXT("elapsed_ms"), ElapsedMs);
    Response->SetArrayField(TEXT("results"), Results);
    Response->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
    return Response;
}

TSharedRef<FJsonObject> FBridgeServer::BuildFailureResponse(
    uint64 RequestId,
    uint32 ElapsedMs,
    const TArray<TSharedPtr<FJsonValue>>& Errors) const
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetNumberField(TEXT("protocol_version"), ProtocolVersion);
    Response->SetNumberField(TEXT("request_id"), static_cast<double>(RequestId));
    Response->SetBoolField(TEXT("ok"), false);
    Response->SetNumberField(TEXT("elapsed_ms"), ElapsedMs);
    Response->SetArrayField(TEXT("results"), TArray<TSharedPtr<FJsonValue>>());
    Response->SetArrayField(TEXT("errors"), Errors);
    return Response;
}

TSharedRef<FJsonObject> FBridgeServer::BuildError(int32 CommandIndex, const FString& Code, const FString& Message) const
{
    TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetNumberField(TEXT("command_index"), CommandIndex);
    Error->SetField(TEXT("item_index"), MakeShared<FJsonValueNull>());
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);
    return Error;
}
