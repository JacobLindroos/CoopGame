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
#include "Components/SphereComponent.h"
#include "SCharacter.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundCue.h"

// Sets default values
ASTrackerBot::ASTrackerBot()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	//Mesh material setup
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetCanEverAffectNavigation(false);
	MeshComp->SetSimulatePhysics(true);
	RootComponent = MeshComp;

	//Health setup
	HealthComp = CreateDefaultSubobject<USHealthComponent>(TEXT("HealthComp"));
	HealthComp->OnHealthChanged.AddDynamic(this, &ASTrackerBot::HandleTakeDamage);

	//Sphere collision setup
	SphereComp = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComp"));
	SphereComp->SetSphereRadius(200.f);
	SphereComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SphereComp->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SphereComp->SetupAttachment(RootComponent);

	bUseVelocityChange = false;
	MovementForce = 1000.f;
	RequiredDistanceToTarget = 100.f;
	ExplosionRadius = 200.f;
	ExplosionDamage = 40.f;
	SelfDamageInterval = 0.25f;
}


// Called when the game starts or when spawned
void ASTrackerBot::BeginPlay()
{
	Super::BeginPlay();

	if (Role == ROLE_Authority)
	{
		//Find initial move-to
		NextPathPoint = GetNextPawnPoint();
	}
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


//Handles effect and sound on self destruction and apply damage to nearby actors 
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
	
	//Plays explosion sound where it exploded
	UGameplayStatics::PlaySoundAtLocation(this, ExplodeSound, GetActorLocation());
	
	MeshComp->SetVisibility(false, true);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (Role == ROLE_Authority)
	{
		//Actors to ignore when this actor explodes
		TArray<AActor*> IgnoredActors;
		IgnoredActors.Add(this);

		//Apply radial damage
		UGameplayStatics::ApplyRadialDamage(this, ExplosionDamage, GetActorLocation(), ExplosionRadius, nullptr, IgnoredActors, this, GetInstigatorController(), true);

		//Draw a debug sphere line for the explosion, just for visual 
		DrawDebugSphere(GetWorld(), GetActorLocation(), ExplosionRadius, 12.f, FColor::Red, false, 2.f, 0, 1.f);

		SetLifeSpan(2.f);
	}
}


//Apply damage to self
void ASTrackerBot::DamageSelf()
{	
	UGameplayStatics::ApplyDamage(this, 20.f, GetInstigatorController(), this, nullptr);
}


// Called every frame
void ASTrackerBot::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Role == ROLE_Authority && !bExploded)
	{
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

}


//When player overlap a timer is started that calls DamageSelf()
void ASTrackerBot::NotifyActorBeginOverlap(AActor* OtherActor)
{
	if (!bStartedSelfDestruction && !bExploded)
	{
		ASCharacter* PlayerPawn = Cast<ASCharacter>(OtherActor);
		if (PlayerPawn)
		{
			//If we overlapped with a player!

			if (Role == ROLE_Authority)
			{
				//Start self destruction sequence
				GetWorldTimerManager().SetTimer(TimerHandel_SelfDamage, this, &ASTrackerBot::DamageSelf, SelfDamageInterval, true, 0.f);
			}
			
			bStartedSelfDestruction = true;

			//Sound attached just to make the sound follow along the tracker bot when it moves around
			UGameplayStatics::SpawnSoundAttached(SelfDestructSound, RootComponent);
		}
	}
}
