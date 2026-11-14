// Copyright (c) 2026 SpatialFabric Contributors. MIT License.
#include "ShowcasePlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ASpatialFabricShowcaseCharacter::ASpatialFabricShowcaseCharacter()
{
    GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);
    bUseControllerRotationYaw = true;
    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->MaxWalkSpeed = 450.f;
    GetCharacterMovement()->JumpZVelocity = 420.f;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("WalkthroughCamera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
    Camera->bUsePawnControlRotation = true;
}
void ASpatialFabricShowcaseCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("MoveForward"), this, &ASpatialFabricShowcaseCharacter::MoveForward);
    Input->BindAxis(TEXT("MoveRight"), this, &ASpatialFabricShowcaseCharacter::MoveRight);
    Input->BindAxis(TEXT("Turn"), this, &APawn::AddControllerYawInput);
    Input->BindAxis(TEXT("LookUp"), this, &APawn::AddControllerPitchInput);
    Input->BindAction(TEXT("Jump"), IE_Pressed, this, &ACharacter::Jump);
    Input->BindAction(TEXT("Jump"), IE_Released, this, &ACharacter::StopJumping);
}
void ASpatialFabricShowcaseCharacter::MoveForward(float Value)
{
    if (Controller && Value != 0.f)
        AddMovementInput(FRotationMatrix(FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f)).GetUnitAxis(EAxis::X), Value);
}
void ASpatialFabricShowcaseCharacter::MoveRight(float Value)
{
    if (Controller && Value != 0.f)
        AddMovementInput(FRotationMatrix(FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f)).GetUnitAxis(EAxis::Y), Value);
}
ASpatialFabricShowcaseGameMode::ASpatialFabricShowcaseGameMode()
{
    DefaultPawnClass = ASpatialFabricShowcaseCharacter::StaticClass();
    HUDClass = nullptr;
}
