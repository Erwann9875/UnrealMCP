#include "Runtime/UnrealMCPGameplayRuntimeComponents.h"

#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

UUnrealMCPGameplayManagerComponent::UUnrealMCPGameplayManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UUnrealMCPGameplayManagerComponent::AddScore(int32 Value, FName SourceId)
{
    Score += FMath::Max(0, Value);
    if (!SourceId.IsNone())
    {
        TriggeredInteractions.AddUnique(SourceId);
    }
}

void UUnrealMCPGameplayManagerComponent::ActivateCheckpoint(
    FName CheckpointId,
    int32 Order,
    FVector Location,
    FRotator Rotation)
{
    if (Order < ActiveCheckpointOrder)
    {
        return;
    }

    ActiveCheckpointId = CheckpointId;
    ActiveCheckpointOrder = Order;
    RespawnLocation = Location;
    RespawnRotation = Rotation;
}

void UUnrealMCPGameplayManagerComponent::RegisterInteraction(FName InteractionId)
{
    if (!InteractionId.IsNone())
    {
        TriggeredInteractions.AddUnique(InteractionId);
    }
}

bool UUnrealMCPGameplayManagerComponent::CompleteObjective(FName ObjectiveId, int32 Order)
{
    if (Order > CurrentObjectiveIndex)
    {
        return false;
    }

    if (!ObjectiveId.IsNone())
    {
        CompletedObjectives.AddUnique(ObjectiveId);
    }
    CurrentObjectiveIndex = FMath::Max(CurrentObjectiveIndex, Order + 1);
    return true;
}

UUnrealMCPGameplayTriggerComponent::UUnrealMCPGameplayTriggerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UUnrealMCPGameplayTriggerComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    TArray<UPrimitiveComponent*> PrimitiveComponents;
    Owner->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
    for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
    {
        if (!PrimitiveComponent)
        {
            continue;
        }

        PrimitiveComponent->SetGenerateOverlapEvents(true);
        PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        PrimitiveComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
        PrimitiveComponent->OnComponentBeginOverlap.AddUniqueDynamic(
            this,
            &UUnrealMCPGameplayTriggerComponent::OnOwnerBeginOverlap);
    }
}

void UUnrealMCPGameplayTriggerComponent::HandleTriggered(AActor& OtherActor)
{
}

UUnrealMCPGameplayManagerComponent* UUnrealMCPGameplayTriggerComponent::ResolveManager() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor) || Actor->GetActorLabel() != ManagerActorLabel.ToString())
        {
            continue;
        }

        return Actor->FindComponentByClass<UUnrealMCPGameplayManagerComponent>();
    }

    return nullptr;
}

void UUnrealMCPGameplayTriggerComponent::OnOwnerBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    if (ShouldAcceptOverlap(OtherActor))
    {
        HandleTriggered(*OtherActor);
    }
}

bool UUnrealMCPGameplayTriggerComponent::ShouldAcceptOverlap(const AActor* OtherActor) const
{
    if (!OtherActor || OtherActor == GetOwner())
    {
        return false;
    }

    return !bOnlyTriggerWithPlayers || Cast<APawn>(OtherActor) != nullptr;
}

void UUnrealMCPCollectibleComponent::HandleTriggered(AActor& OtherActor)
{
    if (bCollected)
    {
        return;
    }
    bCollected = true;

    if (UUnrealMCPGameplayManagerComponent* Manager = ResolveManager())
    {
        Manager->AddScore(Value, CollectibleId);
    }

    if (AActor* Owner = GetOwner())
    {
        if (bDestroyOnCollect)
        {
            Owner->Destroy();
        }
        else
        {
            Owner->SetActorHiddenInGame(true);
            Owner->SetActorEnableCollision(false);
        }
    }
}

void UUnrealMCPCheckpointComponent::HandleTriggered(AActor& OtherActor)
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }

    if (UUnrealMCPGameplayManagerComponent* Manager = ResolveManager())
    {
        Manager->ActivateCheckpoint(CheckpointId, Order, Owner->GetActorLocation(), Owner->GetActorRotation());
    }
}

void UUnrealMCPInteractionComponent::HandleTriggered(AActor& OtherActor)
{
    if (UUnrealMCPGameplayManagerComponent* Manager = ResolveManager())
    {
        Manager->RegisterInteraction(InteractionId);
    }
    ApplyAction();
}

void UUnrealMCPInteractionComponent::ApplyAction()
{
    if (Action != TEXT("open") || TargetActorLabel.IsNone())
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor) || Actor->GetActorLabel() != TargetActorLabel.ToString())
        {
            continue;
        }

        Actor->SetActorHiddenInGame(true);
        Actor->SetActorEnableCollision(false);
        return;
    }
}

void UUnrealMCPObjectiveComponent::HandleTriggered(AActor& OtherActor)
{
    if (UUnrealMCPGameplayManagerComponent* Manager = ResolveManager())
    {
        Manager->CompleteObjective(ObjectiveId, Order);
    }
}

