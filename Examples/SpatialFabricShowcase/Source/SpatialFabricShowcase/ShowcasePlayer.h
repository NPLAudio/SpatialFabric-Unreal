// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "ShowcasePlayer.generated.h"

/** Minimal walking camera: no Epic template character/animation assets are required. */
UCLASS()
class SPATIALFABRICSHOWCASE_API ASpatialFabricShowcaseCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ASpatialFabricShowcaseCharacter();
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UCameraComponent> Camera;
    void MoveForward(float Value);
    void MoveRight(float Value);
};

UCLASS()
class SPATIALFABRICSHOWCASE_API ASpatialFabricShowcaseGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ASpatialFabricShowcaseGameMode();
};
