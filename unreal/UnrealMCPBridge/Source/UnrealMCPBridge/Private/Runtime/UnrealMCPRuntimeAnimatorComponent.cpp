#include "Runtime/UnrealMCPRuntimeAnimatorComponent.h"

#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

UUnrealMCPRuntimeAnimatorComponent::UUnrealMCPRuntimeAnimatorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UUnrealMCPRuntimeAnimatorComponent::BeginPlay()
{
    Super::BeginPlay();

    SceneStates.Reset();
    MaterialStates.Reset();

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    TArray<USceneComponent*> SceneComponents;
    Owner->GetComponents<USceneComponent>(SceneComponents);
    for (USceneComponent* SceneComponent : SceneComponents)
    {
        if (!SceneComponent)
        {
            continue;
        }

        FUnrealMCPRuntimeSceneState State;
        State.Component = SceneComponent;
        State.InitialRelativeLocation = SceneComponent->GetRelativeLocation();
        if (const ULightComponent* LightComponent = Cast<ULightComponent>(SceneComponent))
        {
            State.InitialIntensity = LightComponent->Intensity;
        }
        SceneStates.Add(State);
    }

    TArray<UPrimitiveComponent*> PrimitiveComponents;
    Owner->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
    for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
    {
        if (!PrimitiveComponent)
        {
            continue;
        }

        FUnrealMCPRuntimeMaterialState State;
        State.Component = PrimitiveComponent;
        const int32 MaterialCount = PrimitiveComponent->GetNumMaterials();
        State.Materials.Reserve(MaterialCount);
        for (int32 Index = 0; Index < MaterialCount; ++Index)
        {
            State.Materials.Add(PrimitiveComponent->CreateDynamicMaterialInstance(Index));
        }
        MaterialStates.Add(State);
    }
}

void UUnrealMCPRuntimeAnimatorComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const float TimeSeconds = World->GetTimeSeconds();
    for (const UUnrealMCPRuntimeAnimationPreset* Preset : Presets)
    {
        if (!Preset)
        {
            continue;
        }

        const float Phase = (TimeSeconds * Preset->Speed) + Preset->PhaseOffset;
        const float Sine = FMath::Sin(Phase * UE_TWO_PI);
        const float Wave = (Sine + 1.0f) * 0.5f;

        if (Preset->Kind == EUnrealMCPRuntimeAnimationKind::MovingLight)
        {
            ApplyMovingLightPreset(*Preset, Sine, Wave);
        }
        else
        {
            ApplyMaterialPreset(*Preset, Wave);
        }
    }
}

bool UUnrealMCPRuntimeAnimatorComponent::MatchesTarget(
    const UActorComponent& Component,
    const UUnrealMCPRuntimeAnimationPreset& Preset) const
{
    return Preset.TargetComponentName.IsNone() ||
        Component.GetFName() == Preset.TargetComponentName;
}

void UUnrealMCPRuntimeAnimatorComponent::ApplyMaterialPreset(
    const UUnrealMCPRuntimeAnimationPreset& Preset,
    float Wave)
{
    for (const FUnrealMCPRuntimeMaterialState& State : MaterialStates)
    {
        if (!State.Component || !MatchesTarget(*State.Component, Preset))
        {
            continue;
        }

        for (UMaterialInstanceDynamic* Material : State.Materials)
        {
            if (!Material)
            {
                continue;
            }

            if (Preset.Kind == EUnrealMCPRuntimeAnimationKind::Led)
            {
                FLinearColor Color = FMath::Lerp(Preset.ColorA, Preset.ColorB, Wave);
                const float Intensity = FMath::Lerp(Preset.FromScalar, Preset.ToScalar, Wave);
                Color.R *= Intensity;
                Color.G *= Intensity;
                Color.B *= Intensity;
                Material->SetVectorParameterValue(Preset.ParameterName, Color);
            }
            else
            {
                Material->SetScalarParameterValue(
                    Preset.ParameterName,
                    FMath::Lerp(Preset.FromScalar, Preset.ToScalar, Wave));
            }
        }
    }
}

void UUnrealMCPRuntimeAnimatorComponent::ApplyMovingLightPreset(
    const UUnrealMCPRuntimeAnimationPreset& Preset,
    float Sine,
    float Wave)
{
    const FVector Axis = Preset.Axis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    for (const FUnrealMCPRuntimeSceneState& State : SceneStates)
    {
        if (!State.Component || !MatchesTarget(*State.Component, Preset))
        {
            continue;
        }

        State.Component->SetRelativeLocation(State.InitialRelativeLocation + (Axis * Preset.Amplitude * Sine));
        if (ULightComponent* LightComponent = Cast<ULightComponent>(State.Component))
        {
            const float BaseIntensity = Preset.BaseIntensity > 0.0f ? Preset.BaseIntensity : State.InitialIntensity;
            LightComponent->SetIntensity(BaseIntensity * FMath::Lerp(Preset.FromScalar, Preset.ToScalar, Wave));
            LightComponent->SetLightColor(FMath::Lerp(Preset.ColorA, Preset.ColorB, Wave));
        }
    }
}
