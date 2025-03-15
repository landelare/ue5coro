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
#include <memory>
#include <type_traits>
#include <utility>
#include "Delegates/DelegateBase.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/GarbageCollection.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UObject/SparseDelegate.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

struct FForceLatentCoroutine;
class UUE5CoroAnimCallbackTarget;
namespace UE5Coro::Private
{
// Default passthrough
template<typename, typename T>
struct TAwaitTransform
{
	static_assert(!std::is_reference_v<T>);

	decltype(auto) operator()(T& Awaitable)
	{
		if constexpr (requires { Awaitable.operator co_await(); })
			return Awaitable.operator co_await();
		else
			return Awaitable;
	}

	decltype(auto) operator()(T&& Awaitable)
	{
		if constexpr (requires { std::move(Awaitable).operator co_await(); })
			return std::move(Awaitable).operator co_await();
		else
			return std::move(Awaitable);
	}
};

extern thread_local bool GDestroyedEarly;
UE5CORO_API std::tuple<class FLatentAwaiter, UObject*> UntilDelegateCore();
enum class ELatentExitReason : uint8;
class FAllAwaiter;
class FAnyAwaiter;
class FAsyncAwaiter;
class FAsyncTimeAwaiter;
class FAsyncYieldAwaiter;
class FCancellationAwaiter;
class FEventAwaiter;
class FHttpAwaiter;
class FLatentChainAwaiter;
class FLatentPromise;
class FNewThreadAwaiter;
struct FPackageLoadAwaiter;
class FPromise;
struct FPromiseExtras;
class FRaceAwaiter;
class FSemaphoreAwaiter;
class FTaskAwaiter;
class FThreadPoolAwaiter;
class FTwoLives;
namespace Test { class FTestHelper; }
template<typename> struct TAnimAwaiter;
template<typename, int> struct TAsyncLoadAwaiter;
template<typename> struct TAsyncQueryAwaiter;
template<typename> struct TAsyncQueryAwaiterRV;
template<typename> class TCancelableAwaiter;
template<typename, typename> class TCoroutinePromise;
template<typename, typename...> class TDelegateAwaiter;
template<typename, typename...> class TDynamicDelegateAwaiter;
template<typename> class TFutureAwaiter;
template<typename> class TGeneratorPromise;
template<typename> class TTaskAwaiter;

template<typename>
constexpr bool bFalse = false;

template<typename T>
concept TIsSparseDelegate = std::derived_from<T, FSparseDelegate>;

template<typename T>
concept TIsDynamicDelegate =
	std::derived_from<T, FScriptDelegate> ||
	std::derived_from<T, FMulticastScriptDelegate> ||
	// Sparse delegates are always dynamic multicast
	TIsSparseDelegate<T>;

template<typename T>
concept TIsMulticastDelegate =
	std::derived_from<T, FDefaultDelegateUserPolicy::FMulticastDelegateExtras> ||
	std::derived_from<T, FDefaultTSDelegateUserPolicy::FMulticastDelegateExtras> ||
	std::derived_from<T, TMulticastScriptDelegate<>> ||
	// Sparse delegates are always dynamic multicast
	TIsSparseDelegate<T>;

template<typename T>
concept TIsDelegate =
	std::derived_from<T, FDefaultDelegateUserPolicy::FDelegateExtras> ||
	std::derived_from<T, FDefaultTSDelegateUserPolicy::FDelegateExtras> ||
	TIsDynamicDelegate<T> || TIsMulticastDelegate<T>;

template<typename T>
concept TIsUObjectPtr = std::convertible_to<T, const UObject*> &&
                        std::is_pointer_v<T>;

template<typename>
struct TWeak : std::false_type { };

template<TIsUObjectPtr T>
struct TWeak<T> : std::true_type
{
#if UE_VERSION_OLDER_THAN(5, 5, 0)
	using strong = TGCObjectScopeGuard<std::remove_pointer_t<T>>;
#else
	using strong = TStrongObjectPtr<std::remove_pointer_t<T>>;
#endif
	using weak = TWeakObjectPtr<std::remove_pointer_t<T>>;
	using ptr = T;
	static strong Strengthen(const weak& Weak)
	{
#if UE_VERSION_OLDER_THAN(5, 5, 0)
		FGCScopeGuard _;
		// There's no API to convert a weak ptr to a strong one...
		return strong(Weak.Get());
#else
		return Weak.Pin();
#endif
	}
	static ptr Get(const strong& Strong) { return Strong.Get(); }
};

template<typename T>
struct TWeak<TSharedPtr<T, ESPMode::ThreadSafe>> : std::true_type
{
	using strong = TSharedPtr<T, ESPMode::ThreadSafe>;
	using weak = TWeakPtr<T, ESPMode::ThreadSafe>;
	using ptr = T*;
	static strong Strengthen(const weak& Weak) { return Weak.Pin(); }
	static ptr Get(const strong& Strong) { return Strong.Get(); }
};

template<typename T>
struct TWeak<TSharedPtr<T, ESPMode::NotThreadSafe>> : std::true_type
{
	using strong = TSharedPtr<T, ESPMode::NotThreadSafe>;
	using weak = TWeakPtr<T, ESPMode::NotThreadSafe>;
	using ptr = T*;
	static strong Strengthen(const weak& Weak) { return Weak.Pin(); }
	static ptr Get(const strong& Strong) { return Strong.Get(); }
};

template<typename T>
struct TWeak<std::shared_ptr<T>> : std::true_type
{
	using strong = std::shared_ptr<T>;
	using weak = std::weak_ptr<T>;
	using ptr = T*;
	static strong Strengthen(const weak& Weak) { return Weak.lock(); }
	static ptr Get(const strong& Strong) { return Strong.get(); }
};

// UObject*, TSharedPtr, or std::shared_ptr
template<typename T>
concept TStrongPtr = TWeak<T>::value;

// Invocable with (P′*, A...) where P′ is unwrapped from a strong/shared pointer
template<typename T, typename P, typename... A>
concept TInvocableWithPtr = std::invocable<T, typename TWeak<P>::ptr, A...>;
}
