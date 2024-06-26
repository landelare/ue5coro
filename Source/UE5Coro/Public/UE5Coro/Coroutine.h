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
#include "UE5Coro/Private.h"

namespace UE5Coro
{
/** Coroutine handle. Return this type from a function, and it will be able
 *  to co_await various awaiters without blocking the calling thread.
 *  These objects do not represent ownership of the coroutine and do not need to
 *  be stored. Copies will refer to the same coroutine.
 *  TCoroutine<T> objects may be safely object sliced to TCoroutine<>, providing
 *  a return-type-erased handle to the same coroutine.
 *  @tparam T Optional result type of the coroutine. @p void if not provided. */
template<typename T = void>
class TCoroutine;

// Common functionality for TCoroutines of all return types.
template<>
class UE5CORO_API TCoroutine<>
{
	template<typename, typename>
	friend class Private::TCoroutinePromise;
	friend std::hash<TCoroutine>;

protected:
	std::shared_ptr<Private::FPromiseExtras> Extras;

	explicit TCoroutine(std::shared_ptr<Private::FPromiseExtras> Extras) noexcept
		: Extras(std::move(Extras)) { }

public:
	/** A coroutine that has already completed with no return value. */
	static const TCoroutine CompletedCoroutine;

	/** A coroutine that has already completed with the provided value. */
	template<typename V>
	[[nodiscard]] static TCoroutine<std::decay_t<V>> FromResult(V&& Value);

	/** Request the coroutine to stop executing at the next opportunity.
	 *  This function returns immediately, with the coroutine still running.
	 *  Has no effect on coroutines that have already completed. */
	void Cancel();

	/** Blocks until the coroutine completes for any reason, including being
	 *  unsuccessful or canceled.
	 *
	 *  This can lead to a deadlock if the coroutine wants to use the thread
	 *  that's waiting.
	 *  @return True if the coroutine completed, false on timeout. */
	bool Wait(uint32 WaitTimeMilliseconds = std::numeric_limits<uint32>::max(),
	          bool bIgnoreThreadIdleStats = false) const;

	/** Returns true if the coroutine has ended for any reason, including normal
	 *  completion, cancellation, or an unhandled exception. */
	[[nodiscard]] bool IsDone() const;

	/** Returns true if the coroutine ran to completion successfully.
	 *  Cancellations after successful completion don't change this flag. */
	[[nodiscard]] bool WasSuccessful() const noexcept;

	/** Calls the provided functor when this coroutine is complete, including
	 *  unsuccessful completions, such as being canceled.
	 *
	 *  If the coroutine is already complete, the parameter will be called
	 *  immediately, otherwise it will be called on the thread where the
	 *  coroutine completes. */
	void ContinueWith(std::invocable auto Fn);

	/** Like ContinueWith, but the provided functor will only be called if the
	 *  object is still alive at the time of the coroutine's completion.
	 *  @param Ptr UObject*, TSharedPtr, or std::shared_ptr */
	void ContinueWithWeak(Private::TStrongPtr auto Ptr, std::invocable auto Fn);

	/** Convenience overload that invokes the provided functor with the provided
	 *  pointer as the first argument for, e.g., UObject/Slate member function
	 *  pointers, or static functions with a world context.
	 *  @param Ptr UObject*, TSharedPtr, or std::shared_ptr */
	void ContinueWithWeak(Private::TStrongPtr auto Ptr,
	                      Private::TInvocableWithPtr<decltype(Ptr)> auto Fn);

	/** Sets a debug name for the currently-executing coroutine.
	 *  Only valid to call from within a coroutine returning TCoroutine.
	 *  Has no effect in release/shipping builds. */
	static void SetDebugName(const TCHAR* Name);

	/** Returns true if the two objects refer to the same coroutine invocation. */
	[[nodiscard]] bool operator==(const TCoroutine&) const noexcept;

	/** Compares this coroutine invocation to another one.
	 *  The order of TCoroutines is meaningless, but it is strict and total
	 *  across all type parameters. */
	[[nodiscard]] std::strong_ordering operator<=>(const TCoroutine&) const noexcept;
};

// Extra functionality for coroutines with non-void return types.
template<typename T>
class TCoroutine : public TCoroutine<>
{
protected:
	using TCoroutine<>::TCoroutine;

public:
	using TCoroutine<>::ContinueWith;
	using TCoroutine<>::ContinueWithWeak;

	/** A coroutine that has already completed with the provided value. */
	[[nodiscard]] static TCoroutine<T> FromResult(T Value);

	/** Waits for the coroutine to finish, then gets its result.
	 *
	 *  This can lead to a deadlock if the coroutine wants to use the thread
	 *  that's waiting. */
	[[nodiscard]] const T& GetResult() const;

	/** Waits for the coroutine to finish, then gets its result as an rvalue.
	 *
	 *  Depending on T, this will often invalidate further GetResult and
	 *  ContinueWith calls across all copies that refer to the same coroutine.
	 *
	 *  Calling this can lead to a deadlock if the coroutine wants to use the
	 *  thread that's waiting. */
	[[nodiscard]] T&& MoveResult();

	/** Calls the provided functor with this coroutine's result when it's
	 *  complete, including unsuccessful completions, such as being canceled.
	 *  If the coroutine is already complete, it will be called immediately,
	 *  otherwise it will be called on the same thread where the coroutine
	 *  completes. */
	void ContinueWith(std::invocable<T> auto Fn);

	/** Like ContinueWith, but the provided functor will only be called if the
	 *  object is still alive at the time of coroutine completion.
	 *  @param Ptr UObject*, TSharedPtr, or std::shared_ptr */
	void ContinueWithWeak(Private::TStrongPtr auto Ptr,
	                      std::invocable<T> auto Fn);

	/** Convenience overload that also passes the object as the first argument
	 *  for, e.g., UObject/Slate member function pointers or static methods with
	 *  a world context.
	 *  @param Ptr UObject*, TSharedPtr, or std::shared_ptr */
	void ContinueWithWeak(Private::TStrongPtr auto Ptr,
	                      Private::TInvocableWithPtr<decltype(Ptr), T> auto Fn);
};

static_assert(sizeof(TCoroutine<>) == sizeof(TCoroutine<FTransform>));
static_assert(std::totally_ordered<TCoroutine<>>);
static_assert(std::totally_ordered_with<TCoroutine<>, TCoroutine<FTransform>>);

/** Taking this struct as a parameter in a coroutine will force latent execution
 *  mode, without automatic coroutine target or world context detection.
 *
 *  Instead, values from this struct are used directly.
 *
 *  See FForceLatentCoroutine for a simpler, UFUNCTION-compatible alternative
 *  that keeps automatic detection. */
template<std::derived_from<UObject> T = UObject>
struct TLatentContext final
{
	/** The object registered with the latent action manager for the coroutine. */
	T* Target;

	/** The world that will contain the latent action. */
	UWorld* World;

	/** Implicit conversion from T* that sets the world to GetWorld(). */
	TLatentContext(T* Target) noexcept
		: Target(Target), World(Target->GetWorld()) { }
	TLatentContext(T* Target, UWorld* World) noexcept
		: Target(Target), World(World) { }

	/** Makes this type useful as a `this` replacement in lambdas. */
	T* operator->() const noexcept { return Target; }
	T& operator*() const noexcept { return *Target; }
};
}

UE5CORO_API uint32 GetTypeHash(const UE5Coro::TCoroutine<>&) noexcept;

#pragma region Private
namespace UE5Coro::Private
{
template<typename T> constexpr bool bIsLatentContext = false;
template<typename T> constexpr bool bIsLatentContext<TLatentContext<T>> = true;
}

#pragma region std::hash
template<>
struct UE5CORO_API std::hash<UE5Coro::TCoroutine<>>
{
	size_t operator()(const UE5Coro::TCoroutine<>&) const noexcept;
};

template<>
struct std::hash<struct FVoidCoroutine>
{
	size_t operator()(const UE5Coro::TCoroutine<>& Handle) const noexcept
	{
		return std::hash<UE5Coro::TCoroutine<>>()(Handle);
	}
};

template<typename T>
struct std::hash<UE5Coro::TCoroutine<T>>
{
	size_t operator()(const UE5Coro::TCoroutine<T>& Handle) const noexcept
	{
		return std::hash<UE5Coro::TCoroutine<>>()(Handle);
	}
};
#pragma endregion

#if CPP
#include "UE5Coro/Promise.h"
#ifndef UE5CORO_PRIVATE_SUPPRESS_COROUTINE_INL
#include "UE5Coro/Coroutine.inl"
#endif
#endif
#pragma endregion
