#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "UnrealMCPRuntimeAnimationPreset.generated.h"

UENUM(BlueprintType)
enum class EUnrealMCPRuntimeAnimationKind : uint8
{
    Led,
    MovingLight,
    MaterialParameter
};

UCLASS(BlueprintType)
class UNREALMCPBRIDGE_API UUnrealMCPRuntimeAnimationPreset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    EUnrealMCPRuntimeAnimationKind Kind = EUnrealMCPRuntimeAnimationKind::Led;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName TargetComponentName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName ParameterName = TEXT("EmissiveColor");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FLinearColor ColorA = FLinearColor::Black;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FLinearColor ColorB = FLinearColor(0.0f, 0.85f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    float FromScalar = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    float ToScalar = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    float Speed = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    float Amplitude = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FVector Axis = FVector::UpVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    float BaseIntensity = 5000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    float PhaseOffset = 0.0f;
};
