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
#include "UE5Coro/Definition.h"
#include <concepts>
#include <functional>
#include "Engine/LatentActionManager.h"
#include "Engine/World.h"
#include "UE5Coro/UE5CoroSubsystem.h"

#pragma region Private
namespace UE5Coro::Private
{
UE5CORO_API std::tuple<FLatentActionInfo, FTwoLives*> MakeLatentInfo();

class [[nodiscard]] UE5CORO_API FLatentChainAwaiter : public FLatentAwaiter
{
public:
	explicit FLatentChainAwaiter(FTwoLives* Done) noexcept;
	FLatentChainAwaiter(FLatentChainAwaiter&&) noexcept = default;
	bool await_resume();
};

static_assert(sizeof(FLatentAwaiter) == sizeof(FLatentChainAwaiter));

template<typename T>
concept TWorldContext = std::same_as<std::decay_t<T>, UObject*> ||
                        std::same_as<std::decay_t<T>, const UObject*> ||
                        std::same_as<std::decay_t<T>, UWorld*> ||
                        std::same_as<std::decay_t<T>, const UWorld*>;

template<typename T>
concept TLatentInfo = std::same_as<std::decay_t<T>, FLatentActionInfo>;

template<typename T>
using TForwardRef =
	std::conditional_t<std::is_lvalue_reference_v<T>,
	                   std::reference_wrapper<std::remove_reference_t<T>>, T&&>;

// Terminator
template<bool, bool bInfo, typename...>
struct FLatentChain
{
	static void Call(auto&& Fn, FLatentActionInfo, auto&&... Args)
	{
		static_assert(!bInfo, "Chained function is not latent");
		static_assert(sizeof...(Args) == 0,
		              "Too many parameters provided for chained call");
		std::forward<decltype(Fn)>(Fn)();
	}
};

// World context
template<bool bInfo, TWorldContext Type, typename... Types>
struct FLatentChain<true, bInfo, Type, Types...>
{
	static void Call(auto&& Fn, FLatentActionInfo LatentInfo, auto&&... Args)
	{
		checkf(IsValid(GWorld),
		       TEXT("Could not chain latent action: no valid world found"));
		FLatentChain<false, bInfo, Types...>::Call(
			std::bind_front(std::forward<decltype(Fn)>(Fn), &*GWorld),
			std::move(LatentInfo),
			std::forward<decltype(Args)>(Args)...);
	}
};

// LatentInfo
template<bool bWorld, TLatentInfo Type, typename... Types>
struct FLatentChain<bWorld, true, Type, Types...>
{
	static void Call(auto&& Fn, FLatentActionInfo LatentInfo, auto&&... Args)
	{
		FLatentChain<bWorld, false, Types...>::Call(
			std::bind_front(std::forward<decltype(Fn)>(Fn), LatentInfo),
			LatentInfo,
			std::forward<decltype(Args)>(Args)...);
	}
};

// Everything else
template<bool bWorld, bool bInfo, typename Type, typename... Types>
struct FLatentChain<bWorld, bInfo, Type, Types...>
{
	static void Call(auto&&, FLatentActionInfo)
	{
		static_assert(bFalse<Type>,
		              "Not enough parameters provided for chained call");
	}

	static void Call(auto&& Fn, FLatentActionInfo LatentInfo,
	                 auto&& Arg1, auto&&... Args)
	{
		FLatentChain<bWorld, bInfo, Types...>::Call(
			std::bind_front(std::forward<decltype(Fn)>(Fn),
			                TForwardRef<Type>(Arg1)),
			std::move(LatentInfo),
			std::forward<decltype(Args)>(Args)...);
	}
};
}

namespace UE5Coro::Latent
{
template<typename... FnParams>
Private::FLatentChainAwaiter Chain(auto (*Function)(FnParams...), auto&&... Args)
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	auto [LatentInfo, Done] = Private::MakeLatentInfo();
	Private::FLatentChain<true, true, FnParams...>::Call(
		Function,
		LatentInfo,
		std::forward<decltype(Args)>(Args)...);
	return Private::FLatentChainAwaiter(Done);
}

template<std::derived_from<UObject> Class, typename... FnParams>
Private::FLatentChainAwaiter Chain(Class* Object,
                                   auto (Class::*Function)(FnParams...),
                                   auto&&... Args)
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	auto [LatentInfo, Done] = Private::MakeLatentInfo();
	Private::FLatentChain<true, true, FnParams...>::Call(
		std::bind_front(Function, Object),
		LatentInfo,
		std::forward<decltype(Args)>(Args)...);
	return Private::FLatentChainAwaiter(Done);
}

Private::FLatentChainAwaiter ChainEx(auto&& Function, auto&&... Args)
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	if constexpr ((... || (std::is_placeholder_v<std::decay_t<decltype(Args)>> == 1)))
		checkf(IsValid(GWorld),
		       TEXT("Could not chain latent action: no valid world found for _1"));
	static_assert((... || (std::is_placeholder_v<std::decay_t<decltype(Args)>> == 2)),
	              "The _2 parameter for LatentInfo is mandatory");

	auto [LatentInfo, Done] = Private::MakeLatentInfo();
	std::bind(std::forward<decltype(Function)>(Function),
	          std::forward<decltype(Args)>(Args)...)(&*GWorld, LatentInfo);
	return Private::FLatentChainAwaiter(Done);
}
}
#pragma endregion
