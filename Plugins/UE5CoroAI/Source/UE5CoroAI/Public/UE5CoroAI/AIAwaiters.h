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
#include "UE5Coro/Definitions.h"
#if !UE5CORO_CPP20
#error UE5CoroAI does not support C++17.
#endif
#include <optional>
#include "AITypes.h"
#include "Tasks/AITask_MoveTo.h"
#include "UE5Coro/LatentAwaiters.h"

namespace UE5Coro::Private
{
class FMoveToAwaiter;
}

namespace UE5Coro::AI
{
template<typename T>
concept TGoal = std::same_as<T, FVector> || std::same_as<T, AActor*>;

/** Issues a "move to" command to the specified controller, resumes the awaiting
 *  coroutine once it finishes.<br>
 *  The result of the co_await expression is EPathFollowingResult. */
UE5COROAI_API Private::FMoveToAwaiter AIMoveTo(
	AAIController* Controller, TGoal auto Target, float AcceptanceRadius = -1,
	EAIOptionFlag::Type StopOnOverlap = EAIOptionFlag::Default,
	EAIOptionFlag::Type AcceptPartialPath = EAIOptionFlag::Default,
	bool bUsePathfinding = true, bool bLockAILogic = true,
	bool bUseContinuousGoalTracking = false,
	EAIOptionFlag::Type ProjectGoalOnNavigation = EAIOptionFlag::Default);
}

namespace UE5Coro::Private
{
class [[nodiscard]] UE5COROAI_API FMoveToAwaiter : public FLatentAwaiter
{
public:
	explicit FMoveToAwaiter(UAITask_MoveTo*);
	EPathFollowingResult::Type await_resume() noexcept;
};

static_assert(sizeof(FMoveToAwaiter) == sizeof(FLatentAwaiter));
}
