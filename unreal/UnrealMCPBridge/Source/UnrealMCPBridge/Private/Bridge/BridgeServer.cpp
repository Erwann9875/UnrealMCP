#include "Bridge/BridgeServer.h"

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Common/TcpListener.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Factories/MaterialFactoryNew.h"
#include "FileHelpers.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
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
#include "UObject/Package.h"

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

            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
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
