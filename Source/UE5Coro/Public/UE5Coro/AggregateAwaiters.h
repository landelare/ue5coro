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
#include "Misc/SpinLock.h"
#include "UE5Coro/AsyncCoroutine.h"

namespace UE5Coro::Private
{
class FAnyAwaiter;
class FAllAwaiter;

#if UE5CORO_CPP20
// If your WhenAny/WhenAll call doesn't satisfy this concept, you'll need to
// move the affected parameter into the function call with MoveTemp/std::move/etc.
template<typename T>
concept TAggregateAwaitable =
	TAwaitable<T> && std::is_constructible_v<std::remove_reference_t<T>, T&&>;
#endif
}

#if UE5CORO_CPP20
	#define UE5CORO_AWAITABLE UE5Coro::Private::TAggregateAwaitable
#else
	#define UE5CORO_AWAITABLE typename
#endif

namespace UE5Coro
{
/** co_awaits all parameters, resumes its own awaiting coroutine when any
 *  of them finishes.
 *  The result of the co_await expression is the index of the parameter that
 *  finished first.*/
template<UE5CORO_AWAITABLE... T>
Private::FAnyAwaiter WhenAny(T&&...);

/** co_awaits all parameters, resumes its own awaiting coroutine when all
 *  of them finish. */
template<UE5CORO_AWAITABLE... T>
Private::FAllAwaiter WhenAll(T&&...);
}

namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FAggregateAwaiter
	: public TAwaiter<FAggregateAwaiter>
{
	struct FData
	{
		UE::FSpinLock Lock;
		int Count;
		int Index = -1;
		FPromise* Promise = nullptr;

		explicit FData(int Count) : Count(Count) { }
	};
	std::shared_ptr<FData> Data;

	template<typename T>
	static TCoroutine<> Consume(std::shared_ptr<FData>, int, T&&);

protected:
	int GetResumerIndex() const;

public:
	template<typename... T>
	FAggregateAwaiter(int Count, T&&... Awaiters)
		: Data(std::make_shared<FData>(Count))
	{
		int Idx = 0;
		(Consume(Data, Idx++, std::forward<T>(Awaiters)), ...);
	}

	bool await_ready();
	void Suspend(FPromise&);
};

class [[nodiscard]] FAnyAwaiter : public FAggregateAwaiter
{
public:
	template<typename... T>
	FAnyAwaiter(T&&... Args)
		: FAggregateAwaiter(std::forward<T>(Args)...) { }
	int await_resume() { return GetResumerIndex(); }
};

class [[nodiscard]] FAllAwaiter : public FAggregateAwaiter
{
public:
	template<typename... T>
	FAllAwaiter(T&&... Args)
		: FAggregateAwaiter(std::forward<T>(Args)...) { }
	void await_resume() { }
};
}

template<UE5CORO_AWAITABLE... T>
UE5Coro::Private::FAnyAwaiter UE5Coro::WhenAny(T&&... Args)
{
	static_assert(
		std::conjunction_v<std::is_constructible<std::remove_reference_t<T>, T&&>...>,
		"Attempted to copy a noncopyable awaiter, move it instead");
	return {sizeof...(Args) ? 1 : 0, std::forward<T>(Args)...};
}

template<UE5CORO_AWAITABLE... T>
UE5Coro::Private::FAllAwaiter UE5Coro::WhenAll(T&&... Args)
{
	static_assert(
		std::conjunction_v<std::is_constructible<std::remove_reference_t<T>, T&&>...>,
		"Attempted to copy a noncopyable awaiter, move it instead");
	return {sizeof...(Args), std::forward<T>(Args)...};
}

template<typename T>
UE5Coro::TCoroutine<> UE5Coro::Private::FAggregateAwaiter::Consume(
	std::shared_ptr<FData> Data, int Index, T&& Awaiter)
{
	auto AwaiterCopy = std::forward<T>(Awaiter); // If this line doesn't compile,
	// you'll need to fix your usage of WhenAny/WhenAll and move the affected
	// noncopyable parameter into the call with MoveTemp/std::move/etc.

	ON_SCOPE_EXIT
	{
		UE::TScopeLock _(Data->Lock);
		if (--Data->Count != 0)
			return;
		Data->Index = Index; // Mark that this index was the one reaching 0
		auto* Promise = Data->Promise;
		_.Unlock();

		// Not co_awaited yet if this is nullptr, await_ready deals with this
		if (Promise != nullptr)
			Promise->Resume();
	};

	co_await std::move(AwaiterCopy);
}

#undef UE5CORO_AWAITABLE
