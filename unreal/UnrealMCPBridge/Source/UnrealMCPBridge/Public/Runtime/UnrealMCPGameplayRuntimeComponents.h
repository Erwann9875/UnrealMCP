#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "UnrealMCPGameplayRuntimeComponents.generated.h"

class UPrimitiveComponent;

UCLASS(ClassGroup = (UnrealMCP), meta = (BlueprintSpawnableComponent))
class UNREALMCPBRIDGE_API UUnrealMCPGameplayManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UUnrealMCPGameplayManagerComponent();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    int32 Score = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName ActiveCheckpointId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    int32 ActiveCheckpointOrder = -1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FVector RespawnLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FRotator RespawnRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    int32 CurrentObjectiveIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    TArray<FName> CompletedObjectives;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    TArray<FName> TriggeredInteractions;

    UFUNCTION(BlueprintCallable, Category = "Unreal MCP")
    void AddScore(int32 Value, FName SourceId);

    UFUNCTION(BlueprintCallable, Category = "Unreal MCP")
    void ActivateCheckpoint(FName CheckpointId, int32 Order, FVector Location, FRotator Rotation);

    UFUNCTION(BlueprintCallable, Category = "Unreal MCP")
    void RegisterInteraction(FName InteractionId);

    UFUNCTION(BlueprintCallable, Category = "Unreal MCP")
    bool CompleteObjective(FName ObjectiveId, int32 Order);
};

UCLASS(Abstract, ClassGroup = (UnrealMCP), meta = (BlueprintSpawnableComponent))
class UNREALMCPBRIDGE_API UUnrealMCPGameplayTriggerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UUnrealMCPGameplayTriggerComponent();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName ManagerActorLabel = TEXT("MCP_GameplayManager");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    bool bOnlyTriggerWithPlayers = true;

protected:
    virtual void BeginPlay() override;
    virtual void HandleTriggered(AActor& OtherActor);

    UUnrealMCPGameplayManagerComponent* ResolveManager() const;

private:
    UFUNCTION()
    void OnOwnerBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComponent,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);

    bool ShouldAcceptOverlap(const AActor* OtherActor) const;
};

UCLASS(ClassGroup = (UnrealMCP), meta = (BlueprintSpawnableComponent))
class UNREALMCPBRIDGE_API UUnrealMCPCollectibleComponent : public UUnrealMCPGameplayTriggerComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName CollectibleId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    int32 Value = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    bool bDestroyOnCollect = true;

protected:
    virtual void HandleTriggered(AActor& OtherActor) override;

private:
    bool bCollected = false;
};

UCLASS(ClassGroup = (UnrealMCP), meta = (BlueprintSpawnableComponent))
class UNREALMCPBRIDGE_API UUnrealMCPCheckpointComponent : public UUnrealMCPGameplayTriggerComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName CheckpointId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    int32 Order = 0;

protected:
    virtual void HandleTriggered(AActor& OtherActor) override;
};

UCLASS(ClassGroup = (UnrealMCP), meta = (BlueprintSpawnableComponent))
class UNREALMCPBRIDGE_API UUnrealMCPInteractionComponent : public UUnrealMCPGameplayTriggerComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName InteractionId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName Kind;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName Action;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName TargetActorLabel;

protected:
    virtual void HandleTriggered(AActor& OtherActor) override;

private:
    void ApplyAction();
};

UCLASS(ClassGroup = (UnrealMCP), meta = (BlueprintSpawnableComponent))
class UNREALMCPBRIDGE_API UUnrealMCPObjectiveComponent : public UUnrealMCPGameplayTriggerComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName ObjectiveId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName Label;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    FName Kind;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unreal MCP")
    int32 Order = 0;

protected:
    virtual void HandleTriggered(AActor& OtherActor) override;
};

