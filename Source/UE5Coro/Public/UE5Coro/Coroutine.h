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
#include <memory>
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

namespace Private
{
class FPromiseExtras;
template<typename, typename> class TCoroutinePromise;
}

/** Common functionality for TCoroutines of all return types. */
template<>
class UE5CORO_API TCoroutine<>
{
	template<typename, typename>
	friend class Private::TCoroutinePromise;

protected:
	std::shared_ptr<Private::FPromiseExtras> Extras;

	explicit TCoroutine(std::shared_ptr<Private::FPromiseExtras> Extras)
		: Extras(std::move(Extras)) { }

public:
	/** Returns a delegate broadcasting this coroutine's completion for any
	 *  reason, including being unsuccessful or canceled.
	 *  This will be Broadcast() on the same thread where the coroutine is
	 *	destroyed. */
	TMulticastDelegate<void()>& OnCompletion();

	/** Blocks until the coroutine completes for any reason, including being
	 *  unsuccessful or canceled.
	 *  This could result in a deadlock if the coroutine wants to use the thread
	 *  that's blocking.
	 *  @return True if the coroutine completed, false on timeout. */
	bool Wait(uint32 WaitTimeMilliseconds = MAX_uint32,
	          bool bIgnoreThreadIdleStats = false) const;

	/** Sets a debug name for the currently-executing coroutine.
	 *  Only valid to call from within a coroutine returning TCoroutine. */
	static void SetDebugName(const TCHAR* Name);
};

/** Extra functionality for coroutines with non-void return types. */
template<typename T>
class TCoroutine : public TCoroutine<>
{
protected:
	using TCoroutine<>::TCoroutine;

public:
	/** Waits for the coroutine to finish, then gets its result. */
	const T& GetResult() const;

	/** Waits for the coroutine to finish, then gets its result as an rvalue.<br>
	 *  Depending on T, this will often invalidate further GetResult and
	 *  ContinueWith calls across all copies that refer to the same coroutine. */
	T&& MoveResult();
};

static_assert(sizeof(TCoroutine<int>) == sizeof(TCoroutine<>));
}

/** USTRUCT wrapper for TCoroutine<>. */
USTRUCT(BlueprintInternalUseOnly, Meta = (HiddenByDefault))
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
	FAsyncCoroutine(const TCoroutine<T>& Coroutine): TCoroutine(Coroutine) { }
};

static_assert(sizeof(FAsyncCoroutine) == sizeof(UE5Coro::TCoroutine<>));

#if CPP
#include "UE5Coro/AsyncCoroutine.h"
#include "UE5Coro/Coroutine.inl"
#endif
