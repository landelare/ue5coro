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

#pragma once

#include "CoreMinimal.h"
#include "UE5CoroAI/Definition.h"
#include <optional>
#include "AITypes.h"
#include "Tasks/AITask_MoveTo.h"
#include "UE5CoroAI/Private.h"

namespace UE5Coro::AI
{
/** Starts an async pathfinding operation, resumes the awaiting coroutine once
 *  it finishes.
 *  The result of the await expression is
 *  TTuple<ENavigationQueryResult::Type, FNavPathSharedPtr>. */
UE5COROAI_API auto FindPath(UObject* WorldContextObject,
	const FPathFindingQuery& Query,
	EPathFindingMode::Type Mode = EPathFindingMode::Regular)
	-> Private::FPathFindingAwaiter;

/** Issues a "move to" command to the specified controller, resumes the awaiting
 *  coroutine once it finishes.
 *  The result of the await expression is EPathFollowingResult. */
UE5COROAI_API auto AIMoveTo(
	AAIController* Controller, FVector Target, float AcceptanceRadius = -1,
	EAIOptionFlag::Type StopOnOverlap = EAIOptionFlag::Default,
	EAIOptionFlag::Type AcceptPartialPath = EAIOptionFlag::Default,
	bool bUsePathfinding = true, bool bLockAILogic = true,
	bool bUseContinuousGoalTracking = false,
	EAIOptionFlag::Type ProjectGoalOnNavigation = EAIOptionFlag::Default)
	-> Private::FMoveToAwaiter;

/** Issues a "move to" command to the specified controller, resumes the awaiting
 *  coroutine once it finishes.<br>
 *  The result of the await expression is EPathFollowingResult. */
UE5COROAI_API auto AIMoveTo(
	AAIController* Controller, AActor* Target, float AcceptanceRadius = -1,
	EAIOptionFlag::Type StopOnOverlap = EAIOptionFlag::Default,
	EAIOptionFlag::Type AcceptPartialPath = EAIOptionFlag::Default,
	bool bUsePathfinding = true, bool bLockAILogic = true,
	bool bUseContinuousGoalTracking = false,
	EAIOptionFlag::Type ProjectGoalOnNavigation = EAIOptionFlag::Default)
	-> Private::FMoveToAwaiter;

/** Performs similar behavior to UAIBlueprintHelperLibrary's SimpleMoveTo,
 *  such as injecting components into the controller, issues a "move to"
 *  command, and resumes the awaiting coroutine once it finishes.
 *  The result of the await expression is FPathFollowingResult. */
UE5COROAI_API auto SimpleMoveTo(AController* Controller, FVector Target)
	-> Private::FSimpleMoveToAwaiter;

/** Performs similar behavior to UAIBlueprintHelperLibrary's SimpleMoveTo,
 *  such as injecting components into the controller, issues a "move to"
 *  command, and resumes the awaiting coroutine once it finishes.<br>
 *  The result of the await expression is FPathFollowingResult. */
UE5COROAI_API auto SimpleMoveTo(AController* Controller, AActor* Target)
	-> Private::FSimpleMoveToAwaiter;
}

namespace UE5Coro::Private
{
class [[nodiscard]] UE5COROAI_API FPathFindingAwaiter : public FLatentAwaiter
{
public:
	explicit FPathFindingAwaiter(void*); // FFindPathSharedPtr*
	TTuple<ENavigationQueryResult::Type, FNavPathSharedPtr> await_resume();
};

class [[nodiscard]] UE5COROAI_API FMoveToAwaiter : public FLatentAwaiter
{
public:
	explicit FMoveToAwaiter(UAITask_MoveTo*);
	EPathFollowingResult::Type await_resume() noexcept;
};

class [[nodiscard]] UE5COROAI_API FSimpleMoveToAwaiter : public FLatentAwaiter
{
	struct FComplexData final
	{
		FAIRequestID RequestID;
		TWeakObjectPtr<UPathFollowingComponent> PathFollow;
		FDelegateHandle Handle;
		std::optional<FPathFollowingResult> Result;
		void RequestFinished(FAIRequestID, const FPathFollowingResult&);
	};
	static bool ShouldResume(void*, bool);

public:
	explicit FSimpleMoveToAwaiter(EPathFollowingResult::Type);
	explicit FSimpleMoveToAwaiter(UPathFollowingComponent*, FAIRequestID);
	FPathFollowingResult await_resume() noexcept;
};

static_assert(sizeof(FPathFindingAwaiter) == sizeof(FLatentAwaiter));
static_assert(sizeof(FMoveToAwaiter) == sizeof(FLatentAwaiter));
static_assert(sizeof(FSimpleMoveToAwaiter) == sizeof(FLatentAwaiter));
}
