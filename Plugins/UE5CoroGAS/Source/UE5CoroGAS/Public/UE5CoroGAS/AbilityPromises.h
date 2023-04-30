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

/******************************************************************************
 *          This file only contains private implementation details.           *
 ******************************************************************************/

#include "CoreMinimal.h"
#include "UE5Coro/Definitions.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "UE5Coro/AsyncCoroutine.h"

class UUE5CoroAbilityTask;
class UUE5CoroGameplayAbility;

namespace UE5Coro
{
namespace Private
{
class FAbilityPromise;
}

namespace GAS
{
/** A special marker type for ability coroutines, needed due to C++ limitations.
 *  Only use it when overriding methods that already have this return type. */
class UE5COROGAS_API FAbilityCoroutine : public TCoroutine<>
{
	friend Private::FAbilityPromise;
	explicit FAbilityCoroutine(std::shared_ptr<Private::FPromiseExtras>);
};
}

namespace Private
{
class [[nodiscard]] UE5COROGAS_API FAbilityPromise
	: public TCoroutinePromise<void, FLatentPromise>
{
	using Super = TCoroutinePromise;
	static FLatentActionInfo MakeLatentInfo(UObject&);

public:
	explicit FAbilityPromise(UObject&);
	GAS::FAbilityCoroutine get_return_object() noexcept;
	FFinalSuspend final_suspend() noexcept;
};

template<typename T>
struct [[nodiscard]] UE5COROGAS_API TAbilityPromise final : FAbilityPromise
{
	static bool bCalledFromActivate;
	explicit TAbilityPromise(T& Target);
	template<typename U, typename... A>
	explicit TAbilityPromise(U& Target, A&&...)
		: TAbilityPromise(static_cast<T&>(Target)) { }
};

// C++17 SFINAE helpers
template<typename...>
using TAbilityCoroutine = GAS::FAbilityCoroutine;

template<typename Base, typename Derived>
using TAbilityCoroutineIfBaseOf = TAbilityCoroutine<
	decltype(sizeof(Derived)), // Filter incomplete types before enable_if_t
	std::enable_if_t<std::is_base_of_v<Base, Derived>>>;
}
}

template<typename T>
struct UE5Coro::Private::stdcoro::coroutine_traits<
	UE5Coro::Private::TAbilityCoroutineIfBaseOf<UUE5CoroAbilityTask, T>, T&>
{
	using promise_type = UE5Coro::Private::TAbilityPromise<UUE5CoroAbilityTask>;
};

template<typename T>
struct UE5Coro::Private::stdcoro::coroutine_traits<
	UE5Coro::Private::TAbilityCoroutineIfBaseOf<UUE5CoroGameplayAbility, T>, T&,
	FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
	FGameplayAbilityActivationInfo, const FGameplayEventData*>
{
	using promise_type = UE5Coro::Private::TAbilityPromise<UUE5CoroGameplayAbility>;
};
