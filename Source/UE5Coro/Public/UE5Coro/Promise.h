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
#include <coroutine>
#include <functional>
#include "Engine/LatentActionManager.h"
#define UE5CORO_PRIVATE_SUPPRESS_COROUTINE_INL
#include "UE5Coro/Coroutine.h"
#include "UE5Coro/Private.h"

namespace UE5Coro
{
/** Things that can be co_awaited in a coroutine returning TCoroutine. */
template<typename T>
concept TAwaitable = requires
{
	// FLatentPromise supports more things than FAsyncPromise
	Private::TAwaitTransform<Private::FLatentPromise,
	                         std::remove_cvref_t<T>>()(std::declval<T>())
	.await_suspend(std::declval<std::coroutine_handle<Private::FLatentPromise>>());
};

/** Types that get special treatment when awaited from a coroutine (async:
 *  higher overhead, latent: fast path).
 *  This concept is mainly provided for documentation purposes to expose an
 *  internal, but important implementation detail on the public API. */
template<typename T>
concept TLatentAwaiter = std::derived_from<T, Private::FLatentAwaiter>;

/** Types that are not TLatentAwaiters, but support expedited cancellation.
 *  This concept is mainly provided for documentation purposes. */
template<typename T>
concept TCancelableAwaiter =
	std::derived_from<T, Private::TCancelableAwaiter<T>> ||
	requires(T t) { { t.operator co_await() } -> std::derived_from<
		Private::TCancelableAwaiter<decltype(t.operator co_await())>>; };
	// TAwaitTransform is not handled by this concept
}

#pragma region Private
namespace UE5Coro::Private
{
template<typename T>
struct [[nodiscard]] TAwaiter
{
	[[nodiscard]] bool await_ready() noexcept { return false; }

	template<std::derived_from<FPromise> P>
	void await_suspend(std::coroutine_handle<P> Handle)
	{
		if constexpr (std::derived_from<P, FLatentPromise>)
			Handle.promise().DetachFromGameThread();
		static_cast<T*>(this)->Suspend(Handle.promise());
	}

	void await_resume() noexcept { }
};

template<typename T>
class [[nodiscard]] TCancelableAwaiter : public TAwaiter<T>
{
	void (*fn_)(void*, FPromise&);

protected:
	explicit TCancelableAwaiter(void (*Cancel)(void*, FPromise&))
		: fn_(Cancel) { }

public:
	template<std::derived_from<FPromise> P>
	void await_suspend(std::coroutine_handle<P> Handle)
	{
		static_assert(STRUCT_OFFSET(T, fn_) == 0, "Unexpected object layout");
		TAwaiter<T>::await_suspend(Handle);
	}
};

struct UE5CORO_API FCoroutineScope final
{
	FPromise* Promise;
	FPromise* PreviousPromise;

	explicit FCoroutineScope(FPromise*);
	~FCoroutineScope();
};

struct FInitialSuspend final
{
	enum EAction
	{
		Resume,
		Destroy,
	} Action;

	[[nodiscard]] bool await_ready() noexcept { return false; }

	template<std::derived_from<FPromise> P>
	void await_suspend(std::coroutine_handle<P> Handle)
	{
		FCoroutineScope Scope(&Handle.promise());
		switch (Action)
		{
			case Resume: Handle.promise().ResumeFast(); break;
			// This is very early and doesn't yet count as cancellation
			case Destroy: Handle.destroy(); break;
		}
	}

	void await_resume() noexcept { }
};

struct FLatentFinalSuspend final
{
	bool bDestroy;

	[[nodiscard]] bool await_ready() noexcept { return false; }

	template<std::derived_from<FLatentPromise> P>
	void await_suspend(std::coroutine_handle<P> Handle) noexcept
	{
		if (bDestroy)
			Handle.promise().ThreadSafeDestroy();
	}

	void await_resume() noexcept { }
};

/** Fields of FPromise that may be alive after the coroutine is done. */
struct [[nodiscard]] UE5CORO_API FPromiseExtras
{
#if UE5CORO_DEBUG
	int DebugID = -1;
	const TCHAR* DebugPromiseType = nullptr;
	const TCHAR* DebugName = nullptr;
#endif

	FEventRef Completed{EEventMode::ManualReset};
	// This could be read from another thread
	std::atomic<bool> bWasSuccessful = false;

	UE::FMutex Lock; // Used for the union below and by FLatentPromise
	union
	{
		FPromise* Promise; // nullptr once destroyed
		void* ReturnValuePtr; // in the destructor only
	};

	explicit FPromiseExtras(FPromise& Promise) noexcept : Promise(&Promise) { }
	UE_NONCOPYABLE(FPromiseExtras);
	// This class deliberately does not have a virtual destructor

	bool IsComplete() const;
	template<typename T> void ContinueWith(auto Fn);
};

template<typename T>
struct [[nodiscard]] TPromiseExtras final : FPromiseExtras
{
#if UE5CORO_DEBUG
	std::atomic<bool> bMoveUsed = false;
#endif
	T ReturnValue{};

	explicit TPromiseExtras(FPromise& InPromise) noexcept
		: FPromiseExtras(InPromise) { }
};

class [[nodiscard]] FCancellationTracker
{
	std::atomic<bool> bCanceled = false;
	std::atomic<int> CancellationHolds = 0;

public:
	void Cancel() { bCanceled = true; }
	void Hold() { verify(++CancellationHolds >= 0); }
	void Release() { verify(--CancellationHolds >= 0); }
	bool ShouldCancel(bool bBypassHolds) const
	{
		return bCanceled && (bBypassHolds || CancellationHolds == 0);
	}
};

extern thread_local FPromise* GCurrentPromise;

class [[nodiscard]] UE5CORO_API FPromise
{
	friend void TCoroutine<>::SetDebugName(const TCHAR*);

	FCancellationTracker CancellationTracker;

protected:
	void* CancelableAwaiter = nullptr;

	std::shared_ptr<FPromiseExtras> Extras;
	TArray<std::function<void(void*)>> OnCompleted;
#if !PLATFORM_EXCEPTIONS_DISABLED
	std::atomic<bool> bUnhandledException = false;
#endif

	explicit FPromise(std::shared_ptr<FPromiseExtras>, const TCHAR* PromiseType);
	UE_NONCOPYABLE(FPromise);
	virtual ~FPromise(); // Virtual for warning suppression only
	void ResumeInternal(bool bBypassCancellationHolds);
	virtual bool IsEarlyDestroy() const = 0;
	virtual void ThreadSafeDestroy();

public:
	static FPromise& Current();
	UE::FMutex& GetLock();

	[[nodiscard]] bool RegisterCancelableAwaiter(void*);
	template<bool bLock> [[nodiscard]] bool UnregisterCancelableAwaiter();
	void Cancel(bool bBypassCancellationHolds);
	bool ShouldCancel(bool bBypassCancellationHolds) const;
	void HoldCancellation();
	void ReleaseCancellation();
	virtual void Resume();
	void ResumeFast();
	void AddContinuation(std::function<void(void*)>);

	void unhandled_exception();

	// co_yield is not allowed in these types of coroutines
	std::suspend_never yield_value(auto&&) = delete;

#if UE5CORO_PRIVATE_USE_DEBUG_ALLOCATOR
	void* operator new(size_t);
	void operator delete(void*);
#endif
};

class [[nodiscard]] UE5CORO_API FAsyncPromise : public FPromise
{
protected:
	virtual bool IsEarlyDestroy() const override;

	explicit FAsyncPromise(std::shared_ptr<FPromiseExtras> InExtras, auto&&...)
		: FPromise(std::move(InExtras), TEXT("Async")) { }

public:
	FInitialSuspend initial_suspend() noexcept
	{
		return {FInitialSuspend::Resume};
	}

	std::suspend_never final_suspend() noexcept { return {}; }

	template<typename T>
	decltype(auto) await_transform(T&& Awaitable)
	{
		TAwaitTransform<FAsyncPromise, std::remove_cvref_t<T>> Transform;
		return Transform(std::forward<T>(Awaitable));
	}
};

class [[nodiscard]] UE5CORO_API FLatentPromise : public FPromise
{
	friend FLatentFinalSuspend;
	friend Test::FTestHelper;

	static int UUID;

	TWeakObjectPtr<UWorld> World;
	void* LatentAction = nullptr; // Use Extras->Lock for destruction
	enum ELatentFlags : int
	{
		LF_Detached = 1,
		LF_Successful = 2,
	};
	std::atomic<int> LatentFlags = 0; // int to get the bitwise operators
	ELatentExitReason ExitReason = static_cast<ELatentExitReason>(0); // private

	void CreateLatentAction(const UObject*);
	void CreateLatentAction(const FLatentActionInfo&);

protected:
	explicit FLatentPromise(std::shared_ptr<FPromiseExtras>, const auto&...);
	virtual ~FLatentPromise() override;
	virtual bool IsEarlyDestroy() const override;
	virtual void ThreadSafeDestroy() final override;

public:
	virtual void Resume() override;
	void LatentActionDestroyed();
	void CancelFromWithin();

	void AttachToGameThread(bool bFromAnyThread = false);
	void DetachFromGameThread();
	bool IsOnGameThread() const;

	ELatentExitReason GetExitReason() const { return ExitReason; }
	void SetExitReason(ELatentExitReason Reason);
	void SetCurrentAwaiter(const FLatentAwaiter&);

	FInitialSuspend initial_suspend();
	FLatentFinalSuspend final_suspend() noexcept;

	template<typename T>
	decltype(auto) await_transform(T&& Awaitable)
	{
		TAwaitTransform<FLatentPromise, std::remove_cvref_t<T>> Transform;
		return Transform(std::forward<T>(Awaitable));
	}
};

template<typename T, typename Base>
class TCoroutinePromise : public Base
{
public:
	explicit TCoroutinePromise(const auto&... Args)
		: Base(std::make_shared<TPromiseExtras<T>>(*this), Args...) { }
	UE_NONCOPYABLE(TCoroutinePromise);

	~TCoroutinePromise()
	{
		auto* ExtrasT = static_cast<TPromiseExtras<T>*>(this->Extras.get());
		ExtrasT->Lock.Lock(); // This will be held until the end of ~FPromise
		checkf(ExtrasT->Promise, TEXT("Unexpected double promise destruction"));
		ExtrasT->ReturnValuePtr = &ExtrasT->ReturnValue;
	}

	void return_value(T Value)
	{
		auto* ExtrasT = static_cast<TPromiseExtras<T>*>(this->Extras.get());
		UE::TUniqueLock Lock(ExtrasT->Lock);
		check(!ExtrasT->IsComplete()); // Completion is after a value is returned
		ExtrasT->ReturnValue = std::move(Value);
	}

	TCoroutine<T> get_return_object() noexcept
	{
		return TCoroutine<T>(this->Extras);
	}
};

template<typename Base>
class TCoroutinePromise<void, Base> : public Base
{
public:
	explicit TCoroutinePromise(const auto&... Args)
		: Base(std::make_shared<FPromiseExtras>(*this), Args...) { }
	UE_NONCOPYABLE(TCoroutinePromise);

	~TCoroutinePromise()
	{
		// This will be held until the end of ~FPromise
		this->Extras->Lock.Lock();
		checkf(this->Extras->Promise,
		       TEXT("Unexpected double promise destruction"));
		this->Extras->ReturnValuePtr = nullptr;
	}

	void return_void() noexcept { }

	TCoroutine<> get_return_object() noexcept
	{
		return TCoroutine<>(this->Extras);
	}
};

template<typename T>
void FPromiseExtras::ContinueWith(auto Fn)
{
	UE::TDynamicUniqueLock L(Lock);
	if (IsComplete()) // Already completed?
	{
		L.Unlock();
		if constexpr (std::is_void_v<T>)
			Fn();
		else // T is controlled by TCoroutine<T>, safe to cast
			Fn(static_cast<const TPromiseExtras<T>*>(this)->ReturnValue);
		return;
	}

	checkf(Promise,
	       TEXT("Internal error: attaching continuation to a complete promise"));
	Promise->AddContinuation([Fn = std::move(Fn)](void* Data)
	{
		if constexpr (std::is_void_v<T>)
			Fn();
		else
			Fn(*static_cast<const T*>(Data));
	});
}

FLatentPromise::FLatentPromise(std::shared_ptr<FPromiseExtras> InExtras,
                               const auto&... Args)
	: FPromise(std::move(InExtras), TEXT("Latent"))
{
	checkf(IsInGameThread(),
	       TEXT("Latent coroutines may only be started on the game thread"));

	// The coroutine_traits specialization guarantees that at most one of the
	// special types is provided
	constexpr bool bExplicitContext =
		(... || std::same_as<std::decay_t<decltype(Args)>, FLatentActionInfo>) ||
		(... || bIsLatentContext<std::decay_t<decltype(Args)>>);

	if constexpr (bExplicitContext)
		// World context is determined by the special struct
		([this]<typename T>(const T& Context)
		{
			if constexpr (std::same_as<std::decay_t<T>, FLatentActionInfo>)
			{
				checkf(IsValid(Context.CallbackTarget),
			           TEXT("FLatentActionInfo callback target not valid"));
				World = Context.CallbackTarget->GetWorld();
				CreateLatentAction(Context);
			}
			else if constexpr (bIsLatentContext<std::decay_t<T>>)
			{
				checkf(IsValid(Context.Target) && IsValid(Context.World),
				       TEXT("Invalid override used for latent coroutine"));
				World = Context.World;
				CreateLatentAction(Context.Target);
			}
		}(Args), ...);
	else
	{
		// The world context is the first parameter
		static_assert(sizeof...(Args) >= 1, "Provide a world context parameter");
		using First = std::tuple_element_t<0, std::tuple<decltype(Args)...>>;
		static_assert(std::convertible_to<First, const UObject*> ||
		              std::convertible_to<First, const UObject&>,
		              "Provide a world context as the first parameter");
		[this]<typename T>(const T& WorldContext, const auto&...)
		{
			if constexpr (std::is_pointer_v<T>)
			{
				World = WorldContext->GetWorld();
				CreateLatentAction(WorldContext);
			}
			else
			{
				World = WorldContext.GetWorld();
				CreateLatentAction(&WorldContext);
			}
		}(Args...);
	}
	checkf(World.IsValid(),
	       TEXT("Could not determine world for latent coroutine"));
}
}

template<typename T, typename... Args>
struct std::coroutine_traits<UE5Coro::TCoroutine<T>, Args...>
{
	static constexpr int LatentInfoCount =
		(0 + ... + std::same_as<std::decay_t<Args>, FLatentActionInfo>);
	static constexpr int LatentForceCount =
		(0 + ... + std::same_as<std::decay_t<Args>, FForceLatentCoroutine>);
	static constexpr int LatentContextCount =
		(0 + ... + UE5Coro::Private::bIsLatentContext<std::decay_t<Args>>);

	// You may only specify one of these special parameters per coroutine
	static_assert(LatentInfoCount + LatentForceCount + LatentContextCount <= 1,
	              "Conflicting latent parameters found in coroutine");
	static constexpr bool bUseLatent = LatentInfoCount || LatentForceCount ||
	                                   LatentContextCount;

	using promise_type = UE5Coro::Private::TCoroutinePromise<
		T, std::conditional_t<bUseLatent, UE5Coro::Private::FLatentPromise,
		                                  UE5Coro::Private::FAsyncPromise>>;
};

template<typename T, typename... Args>
struct std::coroutine_traits<const UE5Coro::TCoroutine<T>, Args...>
{
	using promise_type = typename coroutine_traits<UE5Coro::TCoroutine<T>,
	                                               Args...>::promise_type;
};

template<typename... Args>
struct std::coroutine_traits<FVoidCoroutine, Args...>
{
	using promise_type = typename coroutine_traits<UE5Coro::TCoroutine<>,
	                                               Args...>::promise_type;
};

template<typename... Args>
struct std::coroutine_traits<const FVoidCoroutine, Args...>
{
	using promise_type = typename coroutine_traits<UE5Coro::TCoroutine<>,
	                                               Args...>::promise_type;
};

#include "UE5Coro/Coroutine.inl"
#pragma endregion
