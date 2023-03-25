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
#include "Async/TaskGraphInterfaces.h"
#include "UE5Coro/Coroutine.h"
#include "UE5Coro/AsyncAwaiters.h"
#include "UE5Coro/LatentAwaiters.h"

namespace UE5Coro::Private
{
template<typename T>
class TAsyncCoroutineAwaiter : public FAsyncAwaiter
{
public:
	explicit TAsyncCoroutineAwaiter(TCoroutine<T> Antecedent)
		: FAsyncAwaiter(FTaskGraphInterface::Get().GetCurrentThreadIfKnown(),
		                std::move(Antecedent)) { }

	// Prevent surprises with `co_await SomeCoroutine();` by making a copy.
	// This cannot be moved as there could be another TCoroutine still owning it
	T await_resume()
	{
		auto& Coro = static_cast<TCoroutine<T>&>(*ResumeAfter);
		checkf(Coro.IsDone(), TEXT("Internal error: resuming too early"));
		return Coro.GetResult();
	}
};

template<>
class TAsyncCoroutineAwaiter<void> : public FAsyncAwaiter
{
public:
	explicit TAsyncCoroutineAwaiter(TCoroutine<> Antecedent)
		: FAsyncAwaiter(FTaskGraphInterface::Get().GetCurrentThreadIfKnown(),
		                std::move(Antecedent)) { }
};

template<typename T>
bool ShouldResumeLatentCoroutine(void*& State, bool bCleanup)
{
	auto* This = static_cast<TCoroutine<T>*>(State);
	if (UNLIKELY(bCleanup))
	{
		delete This;
		return false;
	}
	return This->IsDone();
}

template<typename T>
class TLatentCoroutineAwaiter : public FLatentAwaiter
{
public:
	explicit TLatentCoroutineAwaiter(TCoroutine<T> Antecedent)
		: FLatentAwaiter(new TCoroutine<T>(std::move(Antecedent)),
		                 &ShouldResumeLatentCoroutine<T>) { }

	// Prevent surprises with `co_await SomeCoroutine();` by making a copy.
	// This cannot be moved as there could be another TCoroutine still owning it
	T await_resume()
	{
		auto* Coro = static_cast<TCoroutine<T>*>(State);
		checkf(Coro->IsDone(), TEXT("Internal error: resuming too early"));
		return Coro->GetResult();
	}
};

template<>
class TLatentCoroutineAwaiter<void> : public FLatentAwaiter
{
public:
	explicit TLatentCoroutineAwaiter(TCoroutine<> Antecedent)
		: FLatentAwaiter(new TCoroutine(std::move(Antecedent)),
		                 &ShouldResumeLatentCoroutine<void>) { }
};

template<typename T>
auto TAwaitTransform<FAsyncPromise, TCoroutine<T>>::operator()(TCoroutine<T> Coro)
	-> TAsyncCoroutineAwaiter<T>
{
	return TAsyncCoroutineAwaiter<T>(std::move(Coro));
}

template<typename T>
auto TAwaitTransform<FLatentPromise, TCoroutine<T>>::operator()(TCoroutine<T> Coro)
	-> TLatentCoroutineAwaiter<T>
{
	return TLatentCoroutineAwaiter<T>(std::move(Coro));
}
}
