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

#include "UE5CoroAI/AIAwaiter.h"
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

struct FFindPathState final
{
	using FPtr = TSharedRef<FFindPathState, ESPMode::NotThreadSafe>;
	TWeakObjectPtr<UNavigationSystemV1> NS1;
	uint32 QueryID;
	TTuple<ENavigationQueryResult::Type, FNavPathSharedPtr> Result;

	void ReceiveResult(uint32 InQueryID, ENavigationQueryResult::Type InResult,
	                   FNavPathSharedPtr InPath)
	{
		checkf(IsInGameThread(),
		       TEXT("Internal error: expected result on game thread"));
		checkf(QueryID == InQueryID,
		       TEXT("Internal error: QueryID mismatch"));
		Result = {InResult, std::move(InPath)};
		QueryID = INVALID_NAVQUERYID;
	}
};
using FAICallbackTargetPtr = TStrongObjectPtr<UUE5CoroAICallbackTarget>;

bool ShouldResumeFindPath(void* State, bool bCleanup)
{
	auto& This = static_cast<FFindPathState::FPtr*>(State)->Get();
	if (bCleanup) [[unlikely]]
	{
		if (auto* NS1 = This.NS1.Get();
		    NS1 && This.QueryID != INVALID_NAVQUERYID)
			NS1->AbortAsyncFindPathRequest(This.QueryID);
		delete static_cast<FFindPathState::FPtr*>(State);
		return false;
	}

	return This.QueryID == INVALID_NAVQUERYID;
}

bool ShouldResumeMoveTo(void* State, bool bCleanup)
{
	auto& Target = reinterpret_cast<FAICallbackTargetPtr&>(State);
	if (bCleanup) [[unlikely]]
	{
		Target.~FAICallbackTargetPtr();
		return false;
	}

	return Target->GetResult().has_value();
}

consteval bool IsValid(const FVector&)
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
	       TEXT("This function may only be called from the game thread"));
	checkf(IsValid(Controller), TEXT("Attempting to move invalid controller"));
	checkf(IsValid(Target), TEXT("Attempting to move to invalid target"));
#if ENABLE_NAN_DIAGNOSTIC
	if (FMath::IsNaN(AcceptanceRadius))
	{
		logOrEnsureNanError(TEXT("AsyncMoveTo started with NaN radius"));
	}
	if constexpr (std::same_as<decltype(Target), FVector>)
		if (Target.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("AsyncMoveTo started with NaN target"));
		}
#endif

	FVector Vector(ForceInit);
	AActor* Actor = nullptr;
	if constexpr (std::same_as<decltype(Target), FVector>)
		Vector = Target;
	else
		Actor = Target;

	return FMoveToAwaiter(UAITask_MoveTo::AIMoveTo(
		Controller, Vector, Actor, AcceptanceRadius, StopOnOverlap,
		AcceptPartialPath, bUsePathfinding, bLockAILogic,
		bUseContinuousGoalTracking, ProjectGoalOnNavigation));
}

FSimpleMoveToAwaiter SimpleMoveToCore(AController* Controller, TGoal auto Target)
{
	checkf(IsInGameThread(),
	       TEXT("This function may only be called from the game thread"));
	checkf(IsValid(Controller), TEXT("Attempting to move invalid controller"));
	checkf(IsValid(Controller->GetPawn()),
	       TEXT("Attempting to move invalid pawn"));
	checkf(IsValid(Target), TEXT("Attempting to move to invalid target"));

	auto* World = Controller->GetWorld();
	checkf(IsValid(World), TEXT("Cannot perform move in invalid world"));
	auto* NS1 = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	checkf(IsValid(NS1), TEXT("Cannot perform move without navigation system"));

	// This recreates InitNavigationControl's component injection
	UPathFollowingComponent* PathFollow;
	if (auto* AIC = Cast<AAIController>(GetValid(Controller)))
		PathFollow = AIC->GetPathFollowingComponent();
	else
	{
		PathFollow = Controller->FindComponentByClass<UPathFollowingComponent>();
		if (!IsValid(PathFollow))
		{
			PathFollow = NewObject<UPathFollowingComponent>(Controller);
			PathFollow->RegisterComponentWithWorld(World);
			// The original does not call AddInstanceComponent
			PathFollow->Initialize();
		}
	}

	// Fail instantly if the PFC can't be used
	if (!IsValid(PathFollow) || !PathFollow->IsPathFollowingAllowed())
		return FSimpleMoveToAwaiter(EPathFollowingResult::Invalid);

	FVector From = Controller->GetNavAgentLocation();
	FVector To;
	if constexpr (std::same_as<decltype(Target), AActor*>)
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
	if (FPathFindingResult Result = NS1->FindPathSync(Query);
	    Result.IsSuccessful())
	{
		if constexpr (std::same_as<decltype(Target), AActor*>)
			// Matching the hardcoded constant from UAIBlueprintHelperLibrary
			Result.Path->SetGoalActorObservation(*Target, 100);
		FAIRequestID ID = PathFollow->RequestMove(FAIMoveRequest(To),
		                                          Result.Path);
		return FSimpleMoveToAwaiter(PathFollow, ID); // The interesting case
	}

	if (PathFollow->GetStatus() != EPathFollowingStatus::Idle)
		PathFollow->RequestMoveWithImmediateFinish(EPathFollowingResult::Invalid);
	return FSimpleMoveToAwaiter(EPathFollowingResult::Invalid);
}
}

FPathFindingAwaiter::FPathFindingAwaiter(void* InState)
	: FLatentAwaiter(InState, &ShouldResumeFindPath, std::true_type())
{
}

auto FPathFindingAwaiter::await_resume()
	-> TTuple<ENavigationQueryResult::Type, FNavPathSharedPtr>
{
	auto& This = static_cast<FFindPathState::FPtr*>(State)->Get();
	checkf(This.QueryID == INVALID_NAVQUERYID,
	       TEXT("Internal error: spurious resume"));
	return This.Result;
}


FMoveToAwaiter::FMoveToAwaiter(UAITask_MoveTo* Task)
	: FLatentAwaiter(nullptr, &ShouldResumeMoveTo, std::true_type())
{
	static_assert(sizeof(FAICallbackTargetPtr) <= sizeof(State));
	new (&State) FAICallbackTargetPtr(
		NewObject<UUE5CoroAICallbackTarget>()->SetTask(Task));
}

EPathFollowingResult::Type FMoveToAwaiter::await_resume() noexcept
{
	auto& Target = reinterpret_cast<FAICallbackTargetPtr&>(State);
	checkf(Target->GetResult().has_value(),
	       TEXT("Internal error: resuming with no result"));
	return *Target->GetResult();
}

void FSimpleMoveToAwaiter::FComplexData::RequestFinished(
	FAIRequestID InID, const FPathFollowingResult& InResult)
{
	if (RequestID == InID)
		Result = InResult;
}

bool FSimpleMoveToAwaiter::ShouldResume(void* InState, bool bCleanup)
{
	auto* Data = static_cast<FComplexData*>(InState);
	if (bCleanup) [[unlikely]]
	{
		if (auto* Component = Data->PathFollow.Get())
			Component->OnRequestFinished.Remove(Data->Handle);
		delete Data;
		return false;
	}
	return Data->Result.has_value();
}

FSimpleMoveToAwaiter::FSimpleMoveToAwaiter(EPathFollowingResult::Type Result)
	: FLatentAwaiter(new FComplexData{.Result = {Result}}, &ShouldResume,
	                 std::true_type())
{
}

FSimpleMoveToAwaiter::FSimpleMoveToAwaiter(UPathFollowingComponent* PFC,
                                           FAIRequestID ID)
	: FLatentAwaiter(new FComplexData{.RequestID = ID, .PathFollow = PFC},
	                 &ShouldResume, std::true_type())
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
	// UNavigationSystemV1::AddAsyncQuery requires this
	checkf(IsInGameThread(),
	       TEXT("This function may only be used on the game thread"));
	checkf(IsValid(WorldContextObject), TEXT("Invalid WCO supplied"));
	auto* World = WorldContextObject->GetWorld();
	checkf(IsValid(World), TEXT("Invalid world from WCO"));
	auto* NS1 = CastChecked<UNavigationSystemV1>(World->GetNavigationSystem());
	checkf(IsValid(NS1), TEXT("Navigation system not available or invalid"));
	FFindPathState::FPtr State(new FFindPathState);
	auto Delegate = FNavPathQueryDelegate::CreateSP(
		State, &FFindPathState::ReceiveResult);
	State->NS1 = NS1;
	State->QueryID = NS1->FindPathAsync(Query.NavAgentProperties, Query,
	                                    Delegate, Mode);
	return FPathFindingAwaiter(new FFindPathState::FPtr(std::move(State)));
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
