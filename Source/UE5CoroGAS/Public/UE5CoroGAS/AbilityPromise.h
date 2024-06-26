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
#include "UE5CoroGAS/Definition.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "UE5Coro.h"

#pragma region Private
class UUE5CoroAbilityTask;
class UUE5CoroGameplayAbility;
namespace UE5Coro::Private { class FAbilityPromise; }
#pragma endregion

namespace UE5Coro::GAS
{
/** A special marker type for ability coroutines, needed due to C++ limitations.
 *  Only use it when overriding methods that already have this return type. */
class FAbilityCoroutine final : public TCoroutine<>
{
	friend Private::FAbilityPromise;
	FAbilityCoroutine(FAbilityCoroutine&&) = default;

	FAbilityCoroutine() = delete;
	auto operator=(FAbilityCoroutine&&) = delete;
};
static_assert(sizeof(FAbilityCoroutine) == sizeof(TCoroutine<>));
}

#pragma region Private
namespace UE5Coro::Private
{
class [[nodiscard]] UE5COROGAS_API FAbilityPromise
	: public TCoroutinePromise<void, FLatentPromise>
{
	using Super = TCoroutinePromise;

protected:
	// Matches UUE5CoroAbilityTask::Execute
	explicit FAbilityPromise(UUE5CoroAbilityTask&);

	// Matches UUE5CoroGameplayAbility::ExecuteAbility
	explicit FAbilityPromise(UUE5CoroGameplayAbility&,
	                         const FGameplayAbilitySpecHandle&,
	                         const FGameplayAbilityActorInfo*,
	                         const FGameplayAbilityActivationInfo&,
	                         const FGameplayEventData*);

public:
	GAS::FAbilityCoroutine get_return_object() noexcept;
};

template<typename T>
class [[nodiscard]] UE5COROGAS_API TAbilityPromise final : public FAbilityPromise
{
	void Init(T&);

public:
	static bool bCalledFromActivate;
	explicit TAbilityPromise(auto& Object, const auto&... Args)
		: FAbilityPromise(Object, Args...)
	{
		Init(Object);
	}
};
}

template<std::derived_from<UUE5CoroAbilityTask> T>
struct std::coroutine_traits<UE5Coro::GAS::FAbilityCoroutine, T&>
{
	using promise_type = UE5Coro::Private::TAbilityPromise<UUE5CoroAbilityTask>;
};

template<std::derived_from<UUE5CoroGameplayAbility> T>
struct std::coroutine_traits<UE5Coro::GAS::FAbilityCoroutine, T&,
	FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
	FGameplayAbilityActivationInfo, const FGameplayEventData*>
{
	using promise_type = UE5Coro::Private::TAbilityPromise<UUE5CoroGameplayAbility>;
};
#pragma endregion
