// Copyright © Laura Andelare
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted (subject to the limitations in the disclaimer
// below) provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
// THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
// CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
// NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "UE5CoroAI/AIAwaiters.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "UE5CoroAICallbackTarget.h"

using namespace UE5Coro;
using namespace UE5Coro::AI;
using namespace UE5Coro::Private;

namespace
{
template<typename T>
concept TGoal = std::same_as<T, FVector> || std::same_as<T, AActor*>;

struct FFindPathState
{
	TWeakObjectPtr<UNavigationSystemV1> NS1;
	uint32 QueryID;
	TTuple<ENavigationQueryResult::Type, FNavPathSharedPtr> Result;
};
using FFindPathSharedPtr = TSharedPtr<FFindPathState, ESPMode::NotThreadSafe>;

bool ShouldResumeFindPath(void* State, bool bCleanup)
{
	auto* This = static_cast<FFindPathSharedPtr*>(State);
	if (UNLIKELY(bCleanup))
	{
		if (auto* NS1 = (*This)->NS1.Get();
		    NS1 && (*This)->QueryID != INVALID_NAVQUERYID)
			NS1->AbortAsyncFindPathRequest((*This)->QueryID);
		delete This;
	}

	return (*This)->QueryID == INVALID_NAVQUERYID;
}

bool ShouldResumeMoveTo(void* State, bool bCleanup)
{
	auto* Target = static_cast<TStrongObjectPtr<UUE5CoroAICallbackTarget>*>(State);
	if (UNLIKELY(bCleanup))
	{
		delete Target;
		return false;
	}

	return (*Target)->GetResult().has_value();
}

constexpr bool IsValid(const FVector&)
{
	return true;
}

FMoveToAwaiter AIMoveToCore(AAIController* Controller, TGoal auto Target,
                            float AcceptanceRadius,
                            EAIOptionFlag::Type StopOnOverlap,
                            EAIOptionFlag::Type AcceptPartialPath,
                            bool bUsePathfinding, bool bLockAILogic,
                            bool bUseContinuousGoalTracking,
                            EAIOptionFlag::Type ProjectGoalOnNavigation)
{
	checkf(IsInGameThread(),
	       TEXT("This method may only be called from the game thread"));
	checkf(IsValid(Controller), TEXT("Attempting to move invalid controller"));
	checkf(IsValid(Target), TEXT("Attempting to move to invalid target"));
#if ENABLE_NAN_DIAGNOSTIC
	if (FMath::IsNaN(AcceptanceRadius))
	{
		logOrEnsureNanError(TEXT("AsyncMoveTo started with NaN radius"));
	}
#endif

	FVector Vector;
	AActor* Actor;
	if constexpr (std::convertible_to<decltype(Target), FVector>)
		std::tie(Vector, Actor) = std::tuple(Target, nullptr);
	else
		std::tie(Vector, Actor) = std::tuple(FVector::ZeroVector, Target);
	return FMoveToAwaiter(UAITask_MoveTo::AIMoveTo(
		Controller, Vector, Actor, AcceptanceRadius, StopOnOverlap,
		AcceptPartialPath, bUsePathfinding, bLockAILogic,
		bUseContinuousGoalTracking, ProjectGoalOnNavigation));
}

FSimpleMoveToAwaiter SimpleMoveToCore(AController* Controller, TGoal auto Target)
{
	checkf(IsInGameThread(),
	       TEXT("This method may only be called from the game thread"));
	checkf(IsValid(Controller), TEXT("Attempting to move invalid controller"));
	checkf(IsValid(Controller->GetPawn()),
	       TEXT("Attempting to move invalid pawn"));
	checkf(IsValid(Target), TEXT("Attempting to move to invalid target"));

	auto* World = Controller->GetWorld();
	auto* NS1 = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	checkf(IsValid(NS1), TEXT("Cannot perform move without navigation system"));

	// This recreates InitNavigationControl's component injection
	UPathFollowingComponent* PathFollow;
	if (auto* AIC = Cast<AAIController>(Controller); IsValid(AIC))
		PathFollow = AIC->GetPathFollowingComponent();
	else
	{
		PathFollow = Controller->FindComponentByClass<UPathFollowingComponent>();
		if (!IsValid(PathFollow))
		{
			PathFollow = NewObject<UPathFollowingComponent>(Controller);
			PathFollow->RegisterComponentWithWorld(Controller->GetWorld());
			// The original does not call AddInstanceComponent
			PathFollow->Initialize();
		}
	}

	// Fail instantly if the PFC can't be used
	if (!IsValid(PathFollow) || !PathFollow->IsPathFollowingAllowed())
		return FSimpleMoveToAwaiter(EPathFollowingResult::Invalid);

	FVector From = Controller->GetNavAgentLocation();
	FVector To;
	if constexpr (std::convertible_to<decltype(Target), AActor*>)
		To = Target->GetActorLocation();
	else
		To = Target;

	bool bAlreadyThere = PathFollow->HasReached(
		To, EPathFollowingReachMode::OverlapAgentAndGoal);

	// Abort the previous move if there was any
	constexpr auto Flags = FPathFollowingResultFlags::ForcedScript |
	                       FPathFollowingResultFlags::NewRequest;
	if (PathFollow->GetStatus() != EPathFollowingStatus::Idle)
		PathFollow->AbortMove(*NS1, Flags, FAIRequestID::AnyRequest,
		                      bAlreadyThere ? EPathFollowingVelocityMode::Reset
		                                    : EPathFollowingVelocityMode::Keep);

	// Early exits for immediate failures/successes
	ANavigationData* NavData = NS1->GetNavDataForProps(
		Controller->GetNavAgentPropertiesRef(), From);
	if (!IsValid(NavData))
		return FSimpleMoveToAwaiter(EPathFollowingResult::Invalid);

	if (bAlreadyThere)
	{
		PathFollow->RequestMoveWithImmediateFinish(EPathFollowingResult::Success);
		return FSimpleMoveToAwaiter(EPathFollowingResult::Success);
	}

	FPathFindingQuery Query(Controller, *NavData, From, To);
	// Not calling FindPathAsync to match the original
	FPathFindingResult Result = NS1->FindPathSync(Query);
	if (Result.IsSuccessful())
	{
		if constexpr (std::convertible_to<decltype(Target), AActor*>)
			// Matching the hardcoded constant from UAIBlueprintHelperLibrary
			Result.Path->SetGoalActorObservation(*Target, 100);
		FAIRequestID ID = PathFollow->RequestMove(FAIMoveRequest(To),
		                                          Result.Path);

		// The interesting case
		return FSimpleMoveToAwaiter(PathFollow, ID);
	}

	if (PathFollow->GetStatus() != EPathFollowingStatus::Idle)
	{
		PathFollow->RequestMoveWithImmediateFinish(EPathFollowingResult::Invalid);
		return FSimpleMoveToAwaiter(EPathFollowingResult::Invalid);
	}

	return FSimpleMoveToAwaiter(EPathFollowingResult::Invalid);
}
}

FPathFindingAwaiter::FPathFindingAwaiter(void* State)
	: FLatentAwaiter(State, &ShouldResumeFindPath)
{
}

auto FPathFindingAwaiter::await_resume()
	-> TTuple<ENavigationQueryResult::Type, FNavPathSharedPtr>
{
	auto* This = static_cast<FFindPathSharedPtr*>(State);
	checkf((*This)->QueryID == INVALID_NAVQUERYID,
	       TEXT("Internal error: spurious resume"));
	return (*This)->Result;
}


FMoveToAwaiter::FMoveToAwaiter(UAITask_MoveTo* Task)
	: FLatentAwaiter(new TStrongObjectPtr(NewObject<UUE5CoroAICallbackTarget>()
	                                      ->SetTask(Task)), &ShouldResumeMoveTo)
{
}

EPathFollowingResult::Type FMoveToAwaiter::await_resume() noexcept
{
	auto* Target = static_cast<TStrongObjectPtr<UUE5CoroAICallbackTarget>*>(State);
	checkf((*Target)->GetResult().has_value(),
	       TEXT("Internal error: resuming with no result"));
	return *(*Target)->GetResult();
}

void FSimpleMoveToAwaiter::FComplexData::RequestFinished(
	FAIRequestID InID, const FPathFollowingResult& InResult)
{
	if (RequestID == InID)
		Result = InResult;
}

bool FSimpleMoveToAwaiter::ShouldResume(void* State, bool bCleanup)
{
	auto* Data = static_cast<FComplexData*>(State);
	if (UNLIKELY(bCleanup))
	{
		if (auto* Component = Data->PathFollow.Get())
			Component->OnRequestFinished.Remove(Data->Handle);
		delete Data;
		return false;
	}
	return Data->Result.has_value();
}

FSimpleMoveToAwaiter::FSimpleMoveToAwaiter(EPathFollowingResult::Type Result)
	: FLatentAwaiter(new FComplexData{.Result = {Result}}, &ShouldResume)
{
}

FSimpleMoveToAwaiter::FSimpleMoveToAwaiter(UPathFollowingComponent* PFC,
                                           FAIRequestID ID)
	: FLatentAwaiter(new FComplexData{.RequestID = ID, .PathFollow = PFC},
	                 &ShouldResume)
{
	auto* Data = static_cast<FComplexData*>(State);
	Data->Handle = PFC->OnRequestFinished.AddRaw(Data,
	                                             &FComplexData::RequestFinished);
}

FPathFollowingResult FSimpleMoveToAwaiter::await_resume() noexcept
{
	auto* Data = static_cast<FComplexData*>(State);
	checkf(Data->Result.has_value(), TEXT("Internal error: spurious wakeup"));
	return *Data->Result;
}

FPathFindingAwaiter AI::FindPath(UObject* WorldContextObject,
                                 const FPathFindingQuery& Query,
                                 EPathFindingMode::Type Mode)
{
	checkf(IsValid(WorldContextObject), TEXT("Invalid WCO supplied"));
	auto* World = WorldContextObject->GetWorld();
	checkf(IsValid(World), TEXT("Invalid world from WCO"));
	auto* NS1 = CastChecked<UNavigationSystemV1>(World->GetNavigationSystem());
	auto State = MakeShared<FFindPathState, ESPMode::NotThreadSafe>();
	auto Delegate = FNavPathQueryDelegate::CreateLambda(
		[State](uint32 QueryID, ENavigationQueryResult::Type Result,
		        FNavPathSharedPtr Path)
		{
			checkf(QueryID == State->QueryID,
			       TEXT("Internal error: QueryID mismatch"));
			State->Result = {Result, std::move(Path)};
			State->QueryID = INVALID_NAVQUERYID;
		});
	State->NS1 = NS1;
	State->QueryID = NS1->FindPathAsync(Query.NavAgentProperties, Query,
	                                    Delegate, Mode);
	return FPathFindingAwaiter(new FFindPathSharedPtr(std::move(State)));
}

FMoveToAwaiter AI::AIMoveTo(AAIController* Controller, FVector Target,
                            float AcceptanceRadius,
                            EAIOptionFlag::Type StopOnOverlap,
                            EAIOptionFlag::Type AcceptPartialPath,
                            bool bUsePathfinding, bool bLockAILogic,
                            bool bUseContinuousGoalTracking,
                            EAIOptionFlag::Type ProjectGoalOnNavigation)
{
	return AIMoveToCore(Controller, Target, AcceptanceRadius, StopOnOverlap,
	                    AcceptPartialPath, bUsePathfinding, bLockAILogic,
	                    bUseContinuousGoalTracking, ProjectGoalOnNavigation);
}

FMoveToAwaiter AI::AIMoveTo(AAIController* Controller, AActor* Target,
                            float AcceptanceRadius,
                            EAIOptionFlag::Type StopOnOverlap,
                            EAIOptionFlag::Type AcceptPartialPath,
                            bool bUsePathfinding, bool bLockAILogic,
                            bool bUseContinuousGoalTracking,
                            EAIOptionFlag::Type ProjectGoalOnNavigation)
{
	return AIMoveToCore(Controller, Target, AcceptanceRadius, StopOnOverlap,
	                    AcceptPartialPath, bUsePathfinding, bLockAILogic,
	                    bUseContinuousGoalTracking, ProjectGoalOnNavigation);
}

FSimpleMoveToAwaiter AI::SimpleMoveTo(AController* Controller, FVector Target)
{
	return SimpleMoveToCore(Controller, Target);
}

FSimpleMoveToAwaiter AI::SimpleMoveTo(AController* Controller, AActor* Target)
{
	return SimpleMoveToCore(Controller, Target);
}
