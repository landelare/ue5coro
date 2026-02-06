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
#include "UE5Coro/Coroutine.h"
#include "UE5Coro/Private.h"

namespace UE5Coro
{
/** This coroutine handle represents a coroutine that is suspended until
 *  SetResult is called, then co_returns the provided result.
 *  This can be useful to wrap synchronous or callback-based code and treat its
 *  result the same way as a coroutine.
 *
 *  Copies of a TManualCoroutine share ownership of the coroutine. The last one
 *  to be destroyed or reassigned to another coroutine will cancel the coroutine.
 *
 *  Instances of TManualCoroutine<T> may be safely copied, as well as
 *  object sliced to non-owning TCoroutine<T> or TCoroutine<> handles, but
 *  unlike TCoroutine<T> and TCoroutine<>, TManualCoroutine<T> and
 *  TManualCoroutine<void> are incompatible.
 *  It is illegal to cast a sliced TCoroutine<T> back to TManualCoroutine<T>. */
template<typename T>
struct [[nodiscard]] TManualCoroutine : TCoroutine<T>
{
	/** Unlike TCoroutine, TManualCoroutine should be default constructed.
	 *  @param DebugName Will be passed to TCoroutine<>::SetDebugName() */
	explicit TManualCoroutine(FString DebugName = {});
	TManualCoroutine(const TManualCoroutine& Other);
	~TManualCoroutine();

	TManualCoroutine& operator=(const TManualCoroutine& Other);

	/** Completes the coroutine with Result, ensures if completed already */
	void SetResult(T Result);
	/** Attempts to set Result, returns false if completed already */
	bool TrySetResult(T Result);
};

// Specialization for void, removes the parameter from (Try)SetResult
template<>
struct [[nodiscard]] UE5CORO_API TManualCoroutine<void> : TCoroutine<>
{
	/** Unlike TCoroutine, TManualCoroutine should be default constructed.
	 *  @param DebugName Will be passed to TCoroutine<>::SetDebugName() */
	explicit TManualCoroutine(FString DebugName = {});
	TManualCoroutine(const TManualCoroutine& Other);
	~TManualCoroutine();

	TManualCoroutine& operator=(const TManualCoroutine& Other);

	/** Completes the coroutine successfully, ensures if completed already */
	void SetResult();
	/** Attempts to set Result, returns false if completed already */
	bool TrySetResult();
};
}

#pragma region Private
static_assert(sizeof(UE5Coro::TManualCoroutine<void>) ==
              sizeof(UE5Coro::TCoroutine<>));
static_assert(sizeof(UE5Coro::TManualCoroutine<FTransform>) ==
              sizeof(UE5Coro::TCoroutine<FTransform>));

template<typename T, typename... Args>
struct std::coroutine_traits<UE5Coro::TManualCoroutine<T>, Args...>
{
	static_assert(UE5Coro::Private::bFalse<T>,
	              "Do not return TManualCoroutine from a coroutine");
};

template<typename T, typename... Args>
struct std::coroutine_traits<const UE5Coro::TManualCoroutine<T>, Args...>
{
	static_assert(UE5Coro::Private::bFalse<T>,
	              "Do not return TManualCoroutine from a coroutine");
};
#include "UE5Coro/ManualCoroutine.inl"
#pragma endregion
