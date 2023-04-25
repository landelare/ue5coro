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
#include <optional>
#include "Async/TaskGraphInterfaces.h"
#include "UE5Coro/AsyncCoroutine.h"

namespace UE5Coro::Private
{
class FAsyncAwaiter;
class FAsyncPromise;
class FAsyncYieldAwaiter;
class FLatentPromise;
class FNewThreadAwaiter;
}

namespace UE5Coro::Async
{
/** Suspends the coroutine and resumes it on the provided named thread, if it's
 *  not already on that thread. If it is, nothing happens.<br>
 *  The return value of this function is reusable. Repeated co_awaits will keep
 *	moving back into the provided thread. */
UE5CORO_API Private::FAsyncAwaiter MoveToThread(ENamedThreads::Type);

/** Convenience function to resume on the game thread.<br>
 *  Equivalent to calling Async::MoveToThread(ENamedThreads::GameThread).<br>
 *  As such, its return value is reusable and will keep co_awaiting back into
 *  the game thread. */
UE5CORO_API Private::FAsyncAwaiter MoveToGameThread();

/** Always suspends the coroutine and resumes it on the same kind of named
 *  thread that it's currently running on, or AnyThread otherwise.<br>
 *  The return value of this function is reusable and always refers to the
 *  current thread, even if the coroutine has moved threads since this function
 *  was called. */
UE5CORO_API Private::FAsyncYieldAwaiter Yield();

/** Starts a new thread with additional control over priority, affinity, etc.
 *  and resumes the coroutine there.<br>
 *  Intended for long-running operations before the next co_await or co_return.
 *  For parameters see the engine function FRunnableThread::Create().<br>
 *  The return value of this function is reusable. Every co_await will start a
 *  new thread. */
UE5CORO_API Private::FNewThreadAwaiter MoveToNewThread(
	EThreadPriority Priority = TPri_Normal,
	uint64 Affinity = FPlatformAffinity::GetNoAffinityMask(),
	EThreadCreateFlags Flags = EThreadCreateFlags::None);
}

namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FAsyncAwaiter : public TAwaiter<FAsyncAwaiter>
{
	ENamedThreads::Type Thread;

protected:
	std::optional<TCoroutine<>> ResumeAfter;

public:
	explicit FAsyncAwaiter(ENamedThreads::Type Thread,
	                       std::optional<TCoroutine<>> ResumeAfter)
		: Thread(Thread), ResumeAfter(std::move(ResumeAfter)) { }

	bool await_ready();
	void Suspend(FPromise&);
};

class [[nodiscard]] UE5CORO_API FAsyncYieldAwaiter
	: public TAwaiter<FAsyncYieldAwaiter>
{
public:
	void Suspend(FPromise&);
};

template<typename T>
class [[nodiscard]] TFutureAwaiter final : public TAwaiter<TFutureAwaiter<T>>
{
	TFuture<T> Future;
	std::remove_reference_t<T>* Result = nullptr; // Dangerous!

public:
	explicit TFutureAwaiter(TFuture<T>&& Future) : Future(std::move(Future)) { }
	UE_NONCOPYABLE(TFutureAwaiter);

	bool await_ready()
	{
		checkf(!Result, TEXT("Attempting to reuse spent TFutureAwaiter"));
		checkf(Future.IsValid(),
		       TEXT("Awaiting invalid/spent future will never resume"));
		return Future.IsReady();
	}

	void Suspend(FPromise& Promise)
	{
		// Extremely rarely, Then will run synchronously because Future
		// finished after IsReady but before Suspend.
		// This is OK and will result in the caller coroutine resuming itself.

		Future.Then([this, &Promise](auto InFuture)
		{
			checkf(!Future.IsValid(),
			       TEXT("Internal error: future was not consumed"));

			// TFuture<T&> will pass T* for Value, TFuture<void> an int
			if constexpr (std::is_lvalue_reference_v<T>)
			{
				static_assert(std::is_pointer_v<decltype(InFuture.Get())>);
				Result = InFuture.Get();
				Promise.Resume();
			}
			else
			{
				// It's normally dangerous to expose a pointer to a local, but
				auto Value = InFuture.Get(); // This will be alive while...
				Result = &Value;
				Promise.Resume(); // ...await_resume moves from it here
			}
		});
	}

	T await_resume()
	{
		if (!Result)
		{
			// Result being nullptr indicates that await_ready returned true,
			// Then has not and will not run, and Future is still valid
			checkf(Future.IsValid(), TEXT("Internal error: future was consumed"));
			static_assert(std::is_same_v<T, decltype(Future.Get())>);
			Result = reinterpret_cast<decltype(Result)>(-1); // Mark as spent
			return Future.Get();
		}
		else
		{
			// Otherwise, we're being called from Then, and Future is spent
			checkf(!Future.IsValid(),
			       TEXT("Internal error: future was not consumed"));
			if constexpr (std::is_lvalue_reference_v<T>)
				return *Result;
			else if constexpr (!std::is_void_v<T>)
				return std::move(*Result); // This will move from Then's local
		}
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
	: public TAwaiter<FNewThreadAwaiter>
{
	EThreadPriority Priority;
	uint64 Affinity;
	EThreadCreateFlags Flags;

public:
	explicit FNewThreadAwaiter(EThreadPriority Priority, uint64 Affinity,
	                           EThreadCreateFlags Flags)
		: Priority(Priority), Affinity(Affinity), Flags(Flags) { }

	void Suspend(FPromise&);
};
}
