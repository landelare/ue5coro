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
#include <tuple>
#include "Async/TaskGraphInterfaces.h"
#include "UE5Coro/AsyncCoroutine.h"
#include "UE5Coro/Private.h"

namespace UE5Coro::Private
{
class FAsyncAwaiter;
class FAsyncPromise;
class FAsyncTimeAwaiter;
class FAsyncYieldAwaiter;
class FLatentPromise;
class FNewThreadAwaiter;
template<typename, typename...> class TDelegateAwaiter;
template<typename, typename...> class TDynamicDelegateAwaiter;
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

/** Convenience function to resume on the same kind of named thread that this
 *  function was called on.<br>
 *  co_await MoveToSimilarThread() is not useful. The return value should be
 *  stored to "remember" the original thread, then co_awaited later. */
UE5CORO_API Private::FAsyncAwaiter MoveToSimilarThread();

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

/** Resumes the coroutine after the specified amount of time has elapsed, based
 *  on FPlatformTime.<br>
 *  The coroutine will resume on the same kind of named thread as it was running
 *  on when it was suspended. */
UE5CORO_API Private::FAsyncTimeAwaiter PlatformSeconds(double Seconds);

/** Resumes the coroutine after the specified amount of time has elapsed, based
 *  on FPlatformTime.<br>
 *  The coroutine will resume on an unspecified worker thread. */
UE5CORO_API Private::FAsyncTimeAwaiter PlatformSecondsAnyThread(double Seconds);

/** Resumes the coroutine after FPlatformTime::Seconds has reached the specified
 *  amount.<br>
 *  The coroutine will resume on the same kind of named thread as it was running
 *  on when it was suspended. */
UE5CORO_API Private::FAsyncTimeAwaiter UntilPlatformTime(double Time);

/** Resumes the coroutine after FPlatformTime::Seconds has reached the specified
 *  amount.<br>
 *  The coroutine will resume on an unspecified worker thread. */
UE5CORO_API Private::FAsyncTimeAwaiter UntilPlatformTimeAnyThread(double Time);
}

namespace UE5Coro::Private
{
// Bits used to identify a kind of thread, without the scheduling flags
constexpr auto ThreadTypeMask = ENamedThreads::ThreadIndexMask |
                                ENamedThreads::ThreadPriorityMask;

class [[nodiscard]] UE5CORO_API FAsyncAwaiter : public TAwaiter<FAsyncAwaiter>
{
	ENamedThreads::Type Thread;

public:
	explicit FAsyncAwaiter(ENamedThreads::Type Thread)
		: Thread(Thread) { }

	bool await_ready();
	void Suspend(FPromise&);
};

class [[nodiscard]] UE5CORO_API FAsyncTimeAwaiter
	: public TAwaiter<FAsyncTimeAwaiter>
{
	friend class FTimerThread;

	const double TargetTime;
	union FState
	{
		bool bAnyThread; // Before suspension
		ENamedThreads::Type Thread; // After suspension
		explicit FState(bool bAnyThread) : bAnyThread(bAnyThread) { }
	} U;
	std::atomic<FPromise*> Promise = nullptr;

public:
	explicit FAsyncTimeAwaiter(double TargetTime, bool bAnyThread)
		: TargetTime(TargetTime), U(bAnyThread) { }
	FAsyncTimeAwaiter(const FAsyncTimeAwaiter&);
	~FAsyncTimeAwaiter();

	bool await_ready();
	void Suspend(FPromise&);

	bool operator<(const FAsyncTimeAwaiter& Other) const noexcept
	{
		return TargetTime < Other.TargetTime;
	}

private:
	void Resume();
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

template<typename P, typename T>
struct TAwaitTransform<P, T, std::enable_if_t<TIsDelegate<T>>>
{
	static constexpr auto ExecutePtr(T)
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
	using FExecutePtr = decltype(ExecutePtr(std::declval<T>()));
	using FAwaiter = typename TDelegateAwaiterFor<FExecutePtr>::type;

	FAwaiter operator()(T& Delegate) { return FAwaiter(Delegate); }

	// The delegate needs to live longer than the awaiter. Use lvalues only.
	FAwaiter operator()(T&& Delegate) = delete;
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
	: public TAwaiter<FDelegateAwaiter>
{
protected:
	FPromise* Promise = nullptr;
	std::function<void()> Cleanup;
	void TryResumeOnce();
	UObject* SetupCallbackTarget(std::function<void(void*)>);

public:
	~FDelegateAwaiter();
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
			auto Handle = Delegate.AddRaw(this, &ThisClass::ResumeWith<A...>);
			Cleanup = [Handle, &Delegate] { Delegate.Remove(Handle); };
		}
		else
		{
			Delegate.BindRaw(this, &ThisClass::ResumeWith<A...>);
			Cleanup = [&Delegate] { Delegate.Unbind(); };
		}
	}
	UE_NONCOPYABLE(TDelegateAwaiter);

	template<typename... T>
	R ResumeWith(T... Args)
	{
		TTuple<T...> Values(std::forward<T>(Args)...);
		Result = &Values; // This exposes a pointer to a local, but...
		TryResumeOnce(); // ...it's only read by await_resume, right here
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
			TryResumeOnce();
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
