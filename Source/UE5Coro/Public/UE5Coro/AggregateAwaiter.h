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
#include "UE5Coro/CoroutineAwaiter.h"
#include "UE5Coro/Private.h"
#include "UE5Coro/Promise.h"

namespace UE5Coro
{
/** co_awaits all parameters, returns an object that when co_awaited, resumes
 *  its own awaiting coroutine when the first one of the parameters finishes.
 *
 *  The result of the await expression is the index of the parameter that
 *  finished first. */
auto WhenAny(TAwaitable auto&&...) -> Private::FAnyAwaiter;

/** Returns an object that when co_awaited, resumes its awaiting coroutine when
 *  any of the provided coroutines have completed.
 *
 *  The result of the await expression is the array index of the coroutine that
 *  finished first. */
UE5CORO_API auto WhenAny(const TArray<TCoroutine<>>&) -> Private::FAnyAwaiter;

/** Sets up the provided coroutines so that the first one to complete cancels
 *  all the others, and returns an object that when co_awaited, will resume its
 *  caller once this has happened.
 *
 *  The result of the await expression is the array index of the coroutine that
 *  finished first. */
UE5CORO_API auto Race(TArray<TCoroutine<>>) -> Private::FRaceAwaiter;

/** Sets up the provided coroutines so that the first one to complete cancels
 *  all the others, and returns an object that when co_awaited, will resume its
 *  caller once this has happened.
 *
 *  The result of the await expression is the index of the parameter that
 *  finished first. */
template<typename... T>
auto Race(TCoroutine<T>... Args) -> Private::FRaceAwaiter;

/** co_awaits all parameters, returns an object that when co_awaited resumes its
 *  awaiting coroutine once all of them have finished. */
auto WhenAll(TAwaitable auto&&...) -> Private::FAllAwaiter;

/** Returns an object that when co_awaited, resumes its awaiting coroutine once
 *  all the provided coroutines have finished. */
UE5CORO_API auto WhenAll(const TArray<TCoroutine<>>&) -> Private::FAllAwaiter;
}

#pragma region Private
namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FAggregateAwaiter
	: public TAwaiter<FAggregateAwaiter>
{
	struct FData
	{
		UE::FMutex Lock;
		int Count;
		int Index = -1;
		FPromise* Promise = nullptr;

		explicit FData(int Count) : Count(Count) { }
	};

	std::shared_ptr<FData> Data;

	static TCoroutine<> Consume(std::shared_ptr<FData>, int, TAwaitable auto&&);

protected:
	int GetResumerIndex() const;

public:
	explicit FAggregateAwaiter(int Count, TAwaitable auto&&... Awaiters)
		: Data(std::make_shared<FData>(Count))
	{
		int Idx = 0;
		(Consume(Data, Idx++, std::forward<decltype(Awaiters)>(Awaiters)), ...);
	}

	explicit FAggregateAwaiter(auto, const TArray<TCoroutine<>>& Coroutines);

	[[nodiscard]] bool await_ready();
	void Suspend(FPromise&);
};

class [[nodiscard]] FAnyAwaiter : public FAggregateAwaiter
{
public:
	explicit FAnyAwaiter(auto&&... Args)
		: FAggregateAwaiter(std::forward<decltype(Args)>(Args)...) { }
	int await_resume() { return GetResumerIndex(); }
};

class [[nodiscard]] FAllAwaiter : public FAggregateAwaiter
{
public:
	explicit FAllAwaiter(auto&&... Args)
		: FAggregateAwaiter(std::forward<decltype(Args)>(Args)...) { }
	void await_resume() noexcept { }
};

class [[nodiscard]] UE5CORO_API FRaceAwaiter : public TAwaiter<FRaceAwaiter>
{
	struct FData
	{
		UE::FMutex Lock;
		int Index = -1;
		TArray<TCoroutine<>> Handles;
		FPromise* Promise = nullptr;

		explicit FData(TArray<TCoroutine<>>&& Array)
			: Handles(std::move(Array)) { }
	};
	std::shared_ptr<FData> Data;

public:
	explicit FRaceAwaiter(TArray<TCoroutine<>>&&);
	[[nodiscard]] bool await_ready();
	void Suspend(FPromise&);
	int await_resume() noexcept;
};
}

auto UE5Coro::WhenAny(TAwaitable auto&&... Args) -> Private::FAnyAwaiter
{
	return Private::FAnyAwaiter(sizeof...(Args) ? 1 : 0,
	                            std::forward<decltype(Args)>(Args)...);
}

template<typename... T>
auto UE5Coro::Race(TCoroutine<T>... Args) -> Private::FRaceAwaiter
{
	return Race(TArray<TCoroutine<>>{std::move(Args)...});
}

auto UE5Coro::WhenAll(TAwaitable auto&&... Args) -> Private::FAllAwaiter
{
	return Private::FAllAwaiter(sizeof...(Args),
	                            std::forward<decltype(Args)>(Args)...);
}

template<UE5Coro::TAwaitable T>
UE5Coro::TCoroutine<> UE5Coro::Private::FAggregateAwaiter::Consume(
	std::shared_ptr<FData> Data, int Index, T&& Awaiter)
{
	ON_SCOPE_EXIT // Handle both success and cancellation
	{
		UE::TDynamicUniqueLock Lock(Data->Lock);
		if (--Data->Count != 0)
			return;
		Data->Index = Index; // Mark that this index was the one reaching 0
		auto* Promise = Data->Promise;
		Lock.Unlock();

		// Not co_awaited yet if this is nullptr, await_ready deals with this
		if (Promise != nullptr)
			Promise->Resume();
	};

	TAwaitTransform<FAsyncPromise, std::remove_cvref_t<T>> Transform;
	// Extend the awaiter's life, in case Transform was a reference passthrough
	auto TransformedAwaiter = Transform(std::forward<T>(Awaiter));
	co_await std::move(TransformedAwaiter);
}
#pragma endregion
