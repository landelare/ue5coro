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
#include <tuple>
#include "Async/Future.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/IQueuedWork.h"
#include "Misc/QueuedThreadPool.h"
#include "UE5Coro/Private.h"
#include "UE5Coro/Promise.h"

namespace UE5Coro::Async
{
/** Returns an object that, when co_awaited, suspends the calling coroutine and
 *  resumes it on the provided named thread.
 *
 *  If the coroutine is already running on that kind of named thread, nothing
 *  happens. See Yield() for enforcing a suspension.
 *
 *  The return value of this function is reusable. Repeated co_awaits will keep
 *	moving back into the provided thread. */
UE5CORO_API auto MoveToThread(ENamedThreads::Type) noexcept
	-> Private::FAsyncAwaiter;

/** Convenience function to resume on the game thread.
 *  Equivalent to calling Async::MoveToThread(ENamedThreads::GameThread).
 *
 *  As such, its return value is reusable and will keep co_awaiting back into
 *  the game thread. */
UE5CORO_API auto MoveToGameThread() noexcept -> Private::FAsyncAwaiter;

/** Convenience function to resume on the same kind of named thread that this
 *  function was called on.
 *
 *  co_await MoveToSimilarThread() is not useful and will do nothing.
 *  The return value should be stored to "remember" the original thread, then
 *  co_awaited later. See Yield() for a forced suspension on your own thread. */
UE5CORO_API auto MoveToSimilarThread() -> Private::FAsyncAwaiter;

/** Returns an object that, when co_awaited, unconditionally suspends the
 *  calling coroutine, and resumes it in a UE::Tasks::TTask.
 *
 *  The return value of this function is reusable.
 *  Repeated co_awaits will keep resuming in a new TTask every time. */
UE5CORO_API auto MoveToTask(const TCHAR* DebugName = nullptr)
	-> Private::FTaskAwaiter;

/** Returns an object that, when co_awaited, unconditionally suspends the
 *  calling coroutine, and queues it to resume on the provided thread pool with
 *  the given priority.
 *
 *  The result of the await expression is true if the thread pool scheduled the
 *  work normally, false if it was abandoned.
 *
 *  The return value of this function is reusable.
 *  Repeated co_awaits will keep moving back to the same thread pool at the
 *  originally-provided priority. */
UE5CORO_API auto MoveToThreadPool(
	FQueuedThreadPool& ThreadPool = *GThreadPool,
	EQueuedWorkPriority Priority = EQueuedWorkPriority::Normal)
	-> Private::FThreadPoolAwaiter;

/** Returns an object that, when co_awaited, unconditionally suspends its caller
 *  and resumes it on the same kind of named thread that it's currently running
 *  on, or AnyThread if it's not running on a detectable named thread.
 *
 *  The return value of this function is reusable and always refers to the
 *  current thread, even if the coroutine has moved threads since this function
 *  was called. */
UE5CORO_API auto Yield() noexcept -> Private::FAsyncYieldAwaiter;

/** Returns an object that, when co_awaited, starts a new thread with the
 *  provided parameters and resumes the coroutine there.
 *  Intended for long-running operations before the next co_await or co_return.
 *  For the parameters, see the engine function FRunnableThread::Create().
 *
 *  The return value of this function is reusable.
 *  Every co_await will start a new thread with the same parameters. */
UE5CORO_API auto MoveToNewThread(
	EThreadPriority Priority = TPri_Normal,
	uint64 Affinity = FPlatformAffinity::GetNoAffinityMask(),
	EThreadCreateFlags Flags = EThreadCreateFlags::None) noexcept
	-> Private::FNewThreadAwaiter;

/** Returns an object that, when co_awaited, resumes the coroutine after the
 *  specified amount of time has elapsed since the call of this function (not
 *  the co_await!), based on FPlatformTime.
 *
 *  The coroutine will resume on the same kind of named thread as it was running
 *  on when it was suspended. */
UE5CORO_API auto PlatformSeconds(double Seconds) noexcept
	-> Private::FAsyncTimeAwaiter;

/** Returns an object that, when co_awaited, resumes the coroutine after the
 *  specified amount of time has elapsed since the call of this function (not
 *  the co_await!), based on FPlatformTime.
 *
 *  The coroutine will resume on an unspecified worker thread. */
UE5CORO_API auto PlatformSecondsAnyThread(double Seconds) noexcept
	-> Private::FAsyncTimeAwaiter;

/** Returns an object that, when co_awaited, resumes the coroutine after
 *  FPlatformTime::Seconds has reached or passed the specified value.
 *
 *  The coroutine will resume on the same kind of named thread as it was running
 *  on when it was suspended. */
UE5CORO_API auto UntilPlatformTime(double Time) noexcept
	-> Private::FAsyncTimeAwaiter;

/** Returns an object that, when co_awaited, resumes the coroutine after
 *  FPlatformTime::Seconds has reached or passed the specified value.
 *
 *  The coroutine will resume on an unspecified worker thread. */
UE5CORO_API auto UntilPlatformTimeAnyThread(double Time) noexcept
	-> Private::FAsyncTimeAwaiter;
}

#pragma region Private
namespace UE5Coro::Private
{
// Bits used to identify a kind of thread, without the scheduling flags
constexpr auto ThreadTypeMask = ENamedThreads::ThreadIndexMask |
                                ENamedThreads::ThreadPriorityMask;

class [[nodiscard]] UE5CORO_API FAsyncAwaiter : public TAwaiter<FAsyncAwaiter>
{
	ENamedThreads::Type Thread;

public:
	explicit FAsyncAwaiter(ENamedThreads::Type Thread) noexcept
		: Thread(Thread) { }

	[[nodiscard]] bool await_ready();
	void Suspend(FPromise&);
};

class [[nodiscard]] UE5CORO_API FAsyncTimeAwaiter
	: public TCancelableAwaiter<FAsyncTimeAwaiter>
{
	friend class FTimerThread;

	double TargetTime;
	union
	{
		bool bAnyThread; // Before suspension
		ENamedThreads::Type Thread; // After suspension
	};
	std::atomic<FPromise*> Promise = nullptr;

public:
	explicit FAsyncTimeAwaiter(double TargetTime, bool bAnyThread) noexcept
		: TCancelableAwaiter(&Cancel), TargetTime(TargetTime),
		  bAnyThread(bAnyThread) { }
	FAsyncTimeAwaiter(const FAsyncTimeAwaiter&);
	~FAsyncTimeAwaiter();

	[[nodiscard]] bool await_ready() noexcept;
	void Suspend(FPromise&);

private:
	static void Cancel(void*, FPromise&);
	void Resume();

	[[nodiscard]] auto operator<=>(const FAsyncTimeAwaiter& Other) const noexcept
	{
		return TargetTime <=> Other.TargetTime;
	}
};

class [[nodiscard]] UE5CORO_API FAsyncYieldAwaiter
	: public TAwaiter<FAsyncYieldAwaiter>
{
public:
	static void Suspend(FPromise&);
};

template<typename T>
class [[nodiscard]] TFutureAwaiter final : public TAwaiter<TFutureAwaiter<T>>
{
	TFuture<T> Future;
	std::remove_reference_t<T>* Result = nullptr; // Dangerous!

public:
	explicit TFutureAwaiter(TFuture<T>&& Future) : Future(std::move(Future)) { }
	UE_NONCOPYABLE(TFutureAwaiter);

	[[nodiscard]] bool await_ready()
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

			if constexpr (std::is_lvalue_reference_v<T>)
			{
				// The return type of TFuture<T&>::Get() is T*, T*&, or T&,
				// depending on the version of Unreal...
				if constexpr (std::is_pointer_v<
				              std::remove_reference_t<decltype(InFuture.Get())>>)
					Result = InFuture.Get();
				else
					Result = &InFuture.Get();
				Promise.Resume();
			}
			else if constexpr (!std::is_void_v<T>)
			{
				// It's normally dangerous to expose a pointer to a local, but
				auto Value = InFuture.Get(); // This will be alive while...
				Result = &Value;
				Promise.Resume(); // ...await_resume moves from it here
			}
			else
			{
				// await_resume expects a non-null pointer from Then
				Result = reinterpret_cast<decltype(Result)>(-1);
				Promise.Resume();
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

template<typename>
struct TDelegateAwaiterFor;
template<typename T, typename R, typename... A>
struct TDelegateAwaiterFor<R (T::*)(A...) const>
{
	using type = std::conditional_t<TIsDynamicDelegate<T>,
	                                TDynamicDelegateAwaiter<R, A...>,
	                                TDelegateAwaiter<R, A...>>;
};

template<typename P, TIsDelegate T>
struct TAwaitTransform<P, T>
{
	static_assert(!std::is_reference_v<T>);
	static constexpr auto ExecutePtr()
	{
		if constexpr (TIsSparseDelegate<T>)
		{
			using Ptr = decltype(std::declval<T>().GetShared().Get());
			return &std::remove_pointer_t<Ptr>::Broadcast;
		}
		else if constexpr (TIsMulticastDelegate<T>)
			return &T::Broadcast;
		else
			return &T::Execute;
	}
	using FAwaiter = typename TDelegateAwaiterFor<decltype(ExecutePtr())>::type;

	FAwaiter operator()(T& Delegate) { return FAwaiter(Delegate); }

	// The delegate needs to live longer than the awaiter. Use lvalues only.
	FAwaiter operator()(T&& Delegate) = delete;
};

class [[nodiscard]] UE5CORO_API FThreadPoolAwaiter final
	: public IQueuedWork, public TAwaiter<FThreadPoolAwaiter>
{
	std::atomic<FPromise*> Promise = nullptr;
	FQueuedThreadPool& Pool;
	EQueuedWorkPriority Priority;
	bool bAbandoned = false;

	virtual void DoThreadedWork() override;
	virtual void Abandon() override;

public:
	explicit FThreadPoolAwaiter(FQueuedThreadPool& Pool,
	                            EQueuedWorkPriority Priority)
		: Pool(Pool), Priority(Priority) { }
	FThreadPoolAwaiter(const FThreadPoolAwaiter&);
	void Suspend(FPromise&);
	bool await_resume() { return !bAbandoned; }
};

class [[nodiscard]] UE5CORO_API FNewThreadAwaiter
	: public TAwaiter<FNewThreadAwaiter>
{
	EThreadPriority Priority;
	EThreadCreateFlags Flags;
	uint64 Affinity;

public:
	explicit FNewThreadAwaiter(EThreadPriority Priority, uint64 Affinity,
	                           EThreadCreateFlags Flags)
		: Priority(Priority), Flags(Flags), Affinity(Affinity) { }

	void Suspend(FPromise&);
};

// Stores references as values. Structured bindings will give references.
// Used with DYNAMIC delegates.
// The memory layout has to match TBaseUFunctionDelegateInstance::Execute!
template<typename... T>
class TDecayedPayload : private TPayload<void(std::decay_t<T>...)>
{
	template<size_t N>
	using TType = std::tuple_element_t<N, std::tuple<T...>>;

public:
	// Objects of this type are never made, it's only used to reinterpret params
	TDecayedPayload() = delete;

	template<size_t N>
	TType<N>& get() { return this->Values.template Get<N>(); }
};

class [[nodiscard]] UE5CORO_API FDelegateAwaiter
	: public TCancelableAwaiter<FDelegateAwaiter>
{
	static void Cancel(void*, FPromise&);

protected:
	std::atomic<FPromise*> Promise = nullptr;
	std::function<void()> Cleanup;

	FDelegateAwaiter();
	void Resume();
	UObject* SetupCallbackTarget(std::function<void(void*)>);

public:
	UE_NONCOPYABLE(FDelegateAwaiter);
#if UE5CORO_DEBUG
	~FDelegateAwaiter();
#endif
	void Suspend(FPromise& InPromise);
};

template<typename R, typename... A>
class [[nodiscard]] TDelegateAwaiter : public FDelegateAwaiter
{
	using ThisClass = TDelegateAwaiter;
	using FResult = std::conditional_t<sizeof...(A) != 0, TTuple<A...>, void>;
	TTuple<A...>* Result = nullptr;

public:
	template<typename T>
	explicit TDelegateAwaiter(T& Delegate)
	{
		static_assert(!TIsDynamicDelegate<T>);
		if constexpr (TIsMulticastDelegate<T>)
		{
			auto Handle = Delegate.AddRaw(this,
			                              &ThisClass::template ResumeWith<A...>);
			Cleanup = [Handle, &Delegate] { Delegate.Remove(Handle); };
		}
		else
		{
			Delegate.BindRaw(this, &ThisClass::template ResumeWith<A...>);
			Cleanup = [&Delegate] { Delegate.Unbind(); };
		}
	}
	UE_NONCOPYABLE(TDelegateAwaiter);

	template<typename... T>
	R ResumeWith(T... Args)
	{
		TTuple<T...> Values(std::forward<T>(Args)...);
		Result = &Values; // This exposes a pointer to a local, but...
		Resume(); // ...it's only read by await_resume, right here
		// The coroutine might have completed, destroying this object
		return R();
	}

	FResult await_resume()
	{
		checkf(Result, TEXT("Internal error: resumed without a result"));
		if constexpr (sizeof...(A) != 0)
			return std::move(*Result);
	}
};

template<typename R, typename... A>
class [[nodiscard]] TDynamicDelegateAwaiter : public FDelegateAwaiter
{
	using FResult = std::conditional_t<sizeof...(A) != 0,
	                                   TDecayedPayload<A...>&, void>;
	using FPayload = std::conditional_t<std::is_void_v<R>, TDecayedPayload<A...>,
	                                    TDecayedPayload<A..., R>>;
	TDecayedPayload<A...>* Result; // Missing R, for the coroutine

public:
	template<typename T>
	explicit TDynamicDelegateAwaiter(T& InDelegate)
	{
		static_assert(TIsDynamicDelegate<T>);
		// SetupCallbackTarget sets Cleanup and ties Target's lifetime to this
		auto* Target = SetupCallbackTarget([this](void* Params)
		{
			// This matches the hack in TBaseUFunctionDelegateInstance::Execute
			Result = static_cast<TDecayedPayload<A...>*>(Params);
			Resume();
			// The coroutine might have completed, deleting the awaiter
			if constexpr (!std::is_void_v<R>)
				static_cast<FPayload*>(Params)->template get<sizeof...(A)>() = R();
		});

		if constexpr (TIsMulticastDelegate<T>)
		{
			FScriptDelegate Delegate;
			Delegate.BindUFunction(Target, NAME_Core);
			InDelegate.Add(Delegate);
		}
		else
			InDelegate.BindUFunction(Target, NAME_Core);
	}
	UE_NONCOPYABLE(TDynamicDelegateAwaiter);

	FResult await_resume()
	{
		if constexpr (sizeof...(A) != 0)
		{
			checkf(Result, TEXT("Internal error: resumed without a result"));
			return *Result;
		}
	}
};
}

template<typename... T>
struct std::tuple_size<UE5Coro::Private::TDecayedPayload<T...>>
{
	static constexpr size_t value = sizeof...(T);
};

template<size_t N, typename... T>
struct std::tuple_element<N, UE5Coro::Private::TDecayedPayload<T...>>
{
	using type = std::tuple_element_t<N, std::tuple<T...>>;
};
#pragma endregion
