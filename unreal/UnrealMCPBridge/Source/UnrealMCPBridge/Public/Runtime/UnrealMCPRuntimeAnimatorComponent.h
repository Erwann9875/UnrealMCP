#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Runtime/UnrealMCPRuntimeAnimationPreset.h"

#include "UnrealMCPRuntimeAnimatorComponent.generated.h"

class ULightComponent;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class USceneComponent;

USTRUCT()
struct FUnrealMCPRuntimeSceneState
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> Component = nullptr;

    UPROPERTY(Transient)
    FVector InitialRelativeLocation = FVector::ZeroVector;

    UPROPERTY(Transient)
    float InitialIntensity = 0.0f;
};

USTRUCT()
struct FUnrealMCPRuntimeMaterialState
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TObjectPtr<UPrimitiveComponent> Component = nullptr;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
};

UCLASS(ClassGroup = (UnrealMCP), meta = (BlueprintSpawnableComponent))
class UNREALMCPBRIDGE_API UUnrealMCPRuntimeAnimatorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UUnrealMCPRuntimeAnimatorComponent();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    TArray<TObjectPtr<UUnrealMCPRuntimeAnimationPreset>> Presets;

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    bool MatchesTarget(const UActorComponent& Component, const UUnrealMCPRuntimeAnimationPreset& Preset) const;
    void ApplyMaterialPreset(const UUnrealMCPRuntimeAnimationPreset& Preset, float Wave);
    void ApplyMovingLightPreset(const UUnrealMCPRuntimeAnimationPreset& Preset, float Sine, float Wave);

private:
    UPROPERTY(Transient)
    TArray<FUnrealMCPRuntimeSceneState> SceneStates;

    UPROPERTY(Transient)
    TArray<FUnrealMCPRuntimeMaterialState> MaterialStates;
};
