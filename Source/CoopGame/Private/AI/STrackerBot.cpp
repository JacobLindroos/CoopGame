// Fill out your copyright notice in the Description page of Project Settings.

#include "CoopGame/Public/AI/STrackerBot.h"
#include "Components/StaticMeshComponent.h"
#include <Components/ActorComponent.h>
#include <GameFramework/Character.h>
#include <Kismet/GameplayStatics.h>
#include <NavigationPath.h>
#include <AI/NavigationSystemBase.h>
#include "NavigationSystem.h"
#include <DrawDebugHelpers.h>
#include "CoopGame/Components/SHealthComponent.h"

// Sets default values
ASTrackerBot::ASTrackerBot()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetCanEverAffectNavigation(false);
	MeshComp->SetSimulatePhysics(true);
	RootComponent = MeshComp;

	HealthComp = CreateDefaultSubobject<USHealthComponent>(TEXT("HealthComp"));
	HealthComp->OnHealthChanged.AddDynamic(this, &ASTrackerBot::HandleTakeDamage);

	bUseVelocityChange = false;
	MovementForce = 1000.f;
	RequiredDistanceToTarget = 100.f;
	ExplosionRadius = 200.f;
	ExplosionDamage = 40.f;
}


// Called when the game starts or when spawned
void ASTrackerBot::BeginPlay()
{
	Super::BeginPlay();

	//Find initial move-to
	NextPathPoint = GetNextPawnPoint();
}


//Handling taken damage by player
void ASTrackerBot::HandleTakeDamage(USHealthComponent* OwningHealthComp, float Health, float HealthDelta, const class UDamageType* DamageType, class AController* InstigatedBy, AActor* DamageCauser)
{
	//Pulse the material on hit
	if (MatInst == nullptr)
	{
		MatInst = MeshComp->CreateDynamicMaterialInstance(0, MeshComp->GetMaterial(0));
	}

	if (MatInst)
	{
		MatInst->SetScalarParameterValue("LastTimeDamageTaken", GetWorld()->TimeSeconds);
	}
	
	UE_LOG(LogTemp, Log, TEXT("Health %s of %s"), *FString::SanitizeFloat(Health), *GetName());

	//Explode on hitpoints == 0
	if (Health == 0.f)
	{
		SelfDestruct();
	}
}


//Finds next point where target is
FVector ASTrackerBot::GetNextPawnPoint()
{
	//Hack to get player location

	//Gets the player pawn in the world
	ACharacter* PlayerPawn = UGameplayStatics::GetPlayerCharacter(this, 0);

	//Find path to actor in world
	UNavigationPath* NavPath = UNavigationSystemV1::FindPathToActorSynchronously(this, GetActorLocation(), PlayerPawn);

	//First point in the path array is the starting position of tracker bot, so we want to get the next point
	if (NavPath->PathPoints.Num() > 1)
	{
		//returns second point in array
		return NavPath->PathPoints[1];
	}
	
	return FVector();
}


void ASTrackerBot::SelfDestruct()
{
	//Make sure we don't explode a second time
	if (bExploded)
	{
		return;
	}

	bExploded = true;

	//Spawn explosion effect at actor location
	UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ExplosionEffect, GetActorLocation());

	//Actors to ignore when this actor explodes 
	TArray<AActor*> IgnoredActors;
	IgnoredActors.Add(this);

	//Apply radial damage
	UGameplayStatics::ApplyRadialDamage(this, ExplosionDamage, GetActorLocation(), ExplosionRadius, nullptr, IgnoredActors, this, GetInstigatorController(), true);

	//Draw a debug sphere line for the explosion, just for visual 
	DrawDebugSphere(GetWorld(), GetActorLocation(), ExplosionRadius, 12.f, FColor::Red, false, 2.f, 0, 1.f);

	//Delete Actor immediately
	Destroy();
	
}


// Called every frame
void ASTrackerBot::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//Gets distance to target
	float DistanceToTarget = (GetActorLocation() - NextPathPoint).Size();

	//when distance to target is smaller then required, then we will get the next path point
	if (DistanceToTarget <= RequiredDistanceToTarget)
	{
		NextPathPoint = GetNextPawnPoint();

		DrawDebugString(GetWorld(), GetActorLocation(), "Target Reached!");
	}
	else
	{
		//Keep moving to next target
		FVector ForceDirection = NextPathPoint - GetActorLocation();
		ForceDirection.Normalize();
		ForceDirection *= MovementForce;

		MeshComp->AddForce(ForceDirection, NAME_None, bUseVelocityChange);

		DrawDebugDirectionalArrow(GetWorld(),GetActorLocation(), GetActorLocation() + ForceDirection, 32.f, FColor::Yellow, false, 0.f, 0, 1.f);
	}

	DrawDebugSphere(GetWorld(), NextPathPoint,20.f,12, FColor::Yellow, false, 4.f, 1.f);

}
