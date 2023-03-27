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
#include <memory>
#include "CoroutinePrivate.inl"
#include "Coroutine.generated.h"

namespace UE5Coro
{
/**
 * Asynchronous coroutine. Return this type from a function and it will be able
 * to co_await various awaiters without blocking the calling thread.<br>
 * These objects do not represent ownership of the coroutine and do not need to
 * be stored. Copies will refer to the same coroutine.<br>
 * TCoroutine<T> objects may be safely object sliced to TCoroutine<>, providing
 * a return-type-erased handle to the same coroutine.
 * @tparam T Optional return value of the coroutine. @p void if not provided.
 */
template<typename T = void>
class TCoroutine;

/** Common functionality for TCoroutines of all return types. */
template<>
class UE5CORO_API TCoroutine<>
{
	template<typename, typename>
	friend class Private::TCoroutinePromise;
	friend UE5CORO_API uint32 GetTypeHash(const TCoroutine<>&) noexcept; // ADL
	friend std::hash<TCoroutine<>>;

protected:
	std::shared_ptr<Private::FPromiseExtras> Extras;

	explicit TCoroutine(std::shared_ptr<Private::FPromiseExtras> Extras)
		: Extras(std::move(Extras)) { }

public:
	/** A coroutine that has already completed with no return value. */
	static const TCoroutine<> CompletedCoroutine;

	/** A coroutine that has already completed with the provided value. */
	template<typename V>
	static auto FromResult(V&& Value)
		-> TCoroutine<std::remove_cv_t<std::remove_reference_t<V>>>;

	/** Request the coroutine to stop executing at the next opportunity.<br>
	 *  This function returns immediately, with the coroutine still running.<br>
	 *  Has no effect on coroutines that have already completed. */
	void Cancel();

	/** Returns a delegate broadcasting this coroutine's completion for any
	 *  reason, including being unsuccessful or canceled.
	 *  This will be Broadcast() on the same thread where the coroutine is
	 *	destroyed. */
	[[deprecated("This method only works if the coroutine is not complete yet. "
	             "Use ContinueWith instead.")]]
	TMulticastDelegate<void()>& OnCompletion();

	/** Blocks until the coroutine completes for any reason, including being
	 *  unsuccessful or canceled.
	 *  This could result in a deadlock if the coroutine wants to use the thread
	 *  that's blocking.
	 *  @return True if the coroutine completed, false on timeout. */
	bool Wait(uint32 WaitTimeMilliseconds = MAX_uint32,
	          bool bIgnoreThreadIdleStats = false) const;

	/** Returns if the coroutine has run to completion or faulted. */
	[[nodiscard]] bool IsDone() const;

	/** Calls the provided functor when this coroutine is complete, including
	 *  unsuccessful completions such as being canceled.<br>
	 *  If the coroutine is already complete, it will be called immediately,
	 *  otherwise it will be called on the same thread where the coroutine
	 *  completes. */
	template<typename F>
	auto ContinueWith(F Continuation)
		-> std::enable_if_t<std::is_invocable_v<F>>;

	/** Like ContinueWith, but the provided functor will only be called if the
	 *  object is still alive at the time of coroutine completion.<br>
	 *  The first parameter may be UObject*, TSharedPtr, or std::shared_ptr. */
	template<typename U, typename F>
	auto ContinueWithWeak(U Ptr, F Continuation)
		-> std::enable_if_t<Private::TWeak<U>::value && std::is_invocable_v<F>>;

	/** Convenience overload that also passes the object as the first argument
	 *  for, e.g., UObject/Slate member function pointers or static methods with
	 *  a world context.<br>
	 *  The first parameter may be UObject*, TSharedPtr, or std::shared_ptr.<br>
	 *  Example usage: ContinueWithWeak(this, &ThisClass::Method) */
	template<typename U, typename F>
	auto ContinueWithWeak(U Ptr, F Continuation)
		-> std::enable_if_t<std::is_invocable_v<F, typename Private::TWeak<U>::ptr>>;

	/** Sets a debug name for the currently-executing coroutine.
	 *  Only valid to call from within a coroutine returning TCoroutine. */
	static void SetDebugName(const TCHAR* Name);

	/** @return True if the two objects refer to the same coroutine invocation. */
	bool operator==(const TCoroutine<>&) const noexcept;

#if UE5CORO_CPP20
	std::strong_ordering operator<=>(const TCoroutine<>&) const noexcept;
#else
	bool operator!=(const TCoroutine<>&) const noexcept;
	bool operator<(const TCoroutine<>&) const noexcept;
#endif
};

/** Extra functionality for coroutines with non-void return types. */
template<typename T>
class TCoroutine : public TCoroutine<>
{
protected:
	using TCoroutine<>::TCoroutine;

public:
	/** A coroutine that has already completed with the provided value. */
	static TCoroutine<T> FromResult(T Value);

	/** Waits for the coroutine to finish, then gets its result. */
	const T& GetResult() const;

	/** Waits for the coroutine to finish, then gets its result as an rvalue.<br>
	 *  Depending on T, this will often invalidate further GetResult and
	 *  ContinueWith calls across all copies that refer to the same coroutine. */
	T&& MoveResult();

	/** Calls the provided functor with this coroutine's result when it's
	 *  complete, including unsuccessful completions such as being canceled.<br>
	 *  If the coroutine is already complete, it will be called immediately,
	 *  otherwise it will be called on the same thread where the coroutine
	 *  completes. */
	template<typename F>
	auto ContinueWith(F Continuation)
		-> std::enable_if_t<std::is_invocable_v<F> || std::is_invocable_v<F, T>>;

	/** Like ContinueWith, but the provided functor will only be called if the
	 *  object is still alive at the time of coroutine completion.<br>
	 *  The first parameter may be UObject*, TSharedPtr, or std::shared_ptr. */
	template<typename U, typename F>
	auto ContinueWithWeak(U Ptr, F Continuation)
		-> std::enable_if_t<Private::TWeak<U>::value &&
		                    (std::is_invocable_v<F> || std::is_invocable_v<F, T>)>;

	/** Convenience overload that also passes the object as the first argument
	 *  for, e.g., UObject/Slate member function pointers or static methods with
	 *  a world context.<br>
	 *  The first parameter may be UObject*, TSharedPtr, or std::shared_ptr.<br>
	 *  Example usage: ContinueWithWeak(this, &ThisClass::Method) */
	template<typename U, typename F>
	auto ContinueWithWeak(U Ptr, F Continuation) -> std::enable_if_t<
		std::is_invocable_v<F, typename Private::TWeak<U>::ptr> ||
		std::is_invocable_v<F, typename Private::TWeak<U>::ptr, T>>;
};

static_assert(sizeof(TCoroutine<int>) == sizeof(TCoroutine<>));
#if UE5CORO_CPP20
static_assert(std::totally_ordered<TCoroutine<>>);
static_assert(std::totally_ordered_with<TCoroutine<>, TCoroutine<int>>);
#endif
}

/** USTRUCT wrapper for TCoroutine<>. */
USTRUCT(BlueprintInternalUseOnly)
struct UE5CORO_API FAsyncCoroutine
#if CPP
	: UE5Coro::TCoroutine<>
#endif
{
	GENERATED_BODY()

	/** This constructor is public to placate the reflection system and BP.<br>
	 *  Do not use directly. Interacting with default-constructed
	 *  FAsyncCoroutines is undefined behavior. */
	FAsyncCoroutine() : TCoroutine(nullptr) { }

	/** Implicit conversion from any TCoroutine. */
	template<typename T>
	FAsyncCoroutine(const TCoroutine<T>& Coroutine) : TCoroutine(Coroutine) { }
};

static_assert(sizeof(FAsyncCoroutine) == sizeof(UE5Coro::TCoroutine<>));


#pragma region std::hash
template<>
struct UE5CORO_API std::hash<UE5Coro::TCoroutine<>>
{
	size_t operator()(const UE5Coro::TCoroutine<>&) const noexcept;
};
template<>
struct std::hash<FAsyncCoroutine>
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

/** Taking this struct as a parameter in a coroutine will force latent execution
 *  mode, even if it does not have a FLatentActionInfo parameter.<br>
 *  It is compatible with UFUNCTIONs and hidden on BP call nodes. */
USTRUCT(BlueprintInternalUseOnly)
struct UE5CORO_API FForceLatentCoroutine
{
	GENERATED_BODY()
};

#if CPP
#include "UE5Coro/AsyncCoroutine.h"
#ifndef UE5CORO_PRIVATE_SUPPRESS_COROUTINE_INL
#include "UE5Coro/Coroutine.inl"
#endif
#endif
