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
#include <coroutine>
#include "Async/TaskGraphInterfaces.h"
#include "UE5Coro/AsyncCoroutine.h"

namespace UE5Coro::Private
{
class FAsyncAwaiter;
class FAsyncPromise;
class FLatentPromise;
class FNewThreadAwaiter;
}

namespace UE5Coro::Async
{
/** Suspends the coroutine and resumes it on the provided named thread. */
UE5CORO_API Private::FAsyncAwaiter MoveToThread(ENamedThreads::Type);

/** Convenience function to resume on the game thread.<br>
 *  Equivalent to calling Async::MoveToThread(ENamedThreads::GameThread). */
UE5CORO_API Private::FAsyncAwaiter MoveToGameThread();

/** Starts a new thread with additional control over priority, affinity, etc.
 *  and resumes the coroutine there.<br>
 *  Intended for long-running operations before the next co_await or co_return.
 *  For parameters see the engine function FRunnableThread::Create(). */
UE5CORO_API Private::FNewThreadAwaiter MoveToNewThread(
	EThreadPriority Priority = TPri_Normal,
	uint64 Affinity = FPlatformAffinity::GetNoAffinityMask(),
	EThreadCreateFlags Flags = EThreadCreateFlags::None);
}

namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FAsyncAwaiter final
{
	ENamedThreads::Type Thread;
	FHandle ResumeAfter;

public:
	explicit FAsyncAwaiter(ENamedThreads::Type Thread,
	                       FHandle ResumeAfter = nullptr)
		: Thread(Thread), ResumeAfter(ResumeAfter) { }
	FAsyncAwaiter(FAsyncAwaiter&&) = default;

	bool await_ready() { return false; }
	void await_resume() { }

	void await_suspend(FAsyncHandle Handle);
	void await_suspend(FLatentHandle Handle);
};

template<typename T>
class [[nodiscard]] TFutureAwaiter final
{
	TFuture<T> Future;
	std::remove_reference_t<T>* Result = nullptr; // Dangerous!

public:
	explicit TFutureAwaiter(TFuture<T>&& Future)
		: Future(std::move(Future))
	{
		ensureMsgf(this->Future.IsValid(), TEXT("Awaiting invalid future"));
	}
	UE_NONCOPYABLE(TFutureAwaiter);

	bool await_ready() { return Future.IsReady(); }

	T await_resume()
	{
		if constexpr (std::is_lvalue_reference_v<T>)
			return *Result;
		else if constexpr (!std::is_void_v<T>)
			return std::move(*Result);
	}

	template<typename P>
	void await_suspend(std::coroutine_handle<P> Handle)
	{
		checkf(!Result, TEXT("Attempting to reuse spent TFutureAwaiter"));

		if constexpr (std::is_same_v<P, FLatentPromise>)
			Handle.promise().DetachFromGameThread();

		Future.Then([this, Handle](auto Future)
		{
			// TFuture<T&> will pass T* for Value, TFuture<void> an int
			if constexpr (std::is_lvalue_reference_v<T>)
			{
				static_assert(std::is_pointer_v<decltype(Future.Get())>);
				Result = Future.Get();
				Handle.promise().Resume();
			}
			else
			{
				// It's normally dangerous to expose a pointer to a local, but
				auto Value = Future.Get(); // This will be alive while...
				Result = &Value;
				Handle.promise().Resume(); // ...await_resume moves from it here
			}
		});
	}
};

template<typename P, typename T>
struct TAwaitTransform<P, TFuture<T>>
{
	TFutureAwaiter<T> operator()(TFuture<T>&& Future)
	{
		return TFutureAwaiter<T>(std::move(Future));
	}

	// co_awaiting a TFuture consumes it, use MoveTemp/std::move
	TFutureAwaiter<T> operator()(TFuture<T>&) = delete;
};

class [[nodiscard]] UE5CORO_API FNewThreadAwaiter
{
	EThreadPriority Priority;
	uint64 Affinity;
	EThreadCreateFlags Flags;

public:
	explicit FNewThreadAwaiter(
		EThreadPriority Priority, uint64 Affinity, EThreadCreateFlags Flags)
		: Priority(Priority), Affinity(Affinity), Flags(Flags) { }
	FNewThreadAwaiter(FNewThreadAwaiter&&) = default;

	bool await_ready() { return false; }
	void await_resume() { }

	void await_suspend(FAsyncHandle);
	void await_suspend(FLatentHandle);
};
}
