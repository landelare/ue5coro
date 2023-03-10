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

/******************************************************************************
 *          This file only contains private implementation details.           *
 *             #include "UE5Coro/Coroutine.h" for the public API.             *
 ******************************************************************************/

#include "CoreMinimal.h"
#include "UE5Coro/Definitions.h"
#include <functional>
#define UE5CORO_SUPPRESS_COROUTINE_INL
#include "UE5Coro/Coroutine.h"
#include "Misc/SpinLock.h"

namespace UE5Coro::Private
{
enum class ELatentExitReason : uint8;
class FAsyncAwaiter;
class FAsyncPromise;
class FLatentAwaiter;
class FLatentPromise;
class FPromise;
class FPromiseExtras;
template<typename> class TFutureAwaiter;
template<typename> class TTaskAwaiter;
namespace Test { class FTestHelper; }

template<typename P, typename A>
struct TAwaitTransform
{
	// Default passthrough
	A& operator()(A& Awaitable) { return Awaitable; }
	A&& operator()(A&& Awaitable) { return std::move(Awaitable); }
};
}

namespace UE5Coro
{
#if UE5CORO_CPP20
/** Things that can be co_awaited in a TCoroutine. */
template<typename T>
concept TAwaitable = requires
{
	// FLatentPromise supports more things than FAsyncPromise
	Private::TAwaitTransform<Private::FLatentPromise,
	                         std::remove_reference_t<T>>()(std::declval<T>())
	.await_suspend(std::declval<Private::stdcoro::coroutine_handle<
		Private::FLatentPromise>>());
};
#endif
}

namespace UE5Coro::Private
{
template<typename T>
struct [[nodiscard]] TAwaiter
{
	bool await_ready() { return false; }

	template<typename P>
	std::enable_if_t<std::is_base_of_v<FPromise, P>>
	await_suspend(stdcoro::coroutine_handle<P> Handle)
	{
		if constexpr (std::is_base_of_v<FLatentPromise, P>)
			Handle.promise().DetachFromGameThread();
		static_cast<T*>(this)->Suspend(Handle.promise());
	}

	void await_resume() { }
};

struct FInitialSuspend
{
	enum EAction
	{
		Resume,
		Destroy,
	} Action;

	bool await_ready() { return false; }

	template<typename P>
	void await_suspend(stdcoro::coroutine_handle<P> Handle)
	{
		switch (Action)
		{
			case Resume: Handle.promise().Resume(); break;
			case Destroy: Handle.destroy(); break;
		}
	}

	void await_resume() { }
};

/** Fields of FPromise that may be alive after the coroutine is done. */
class [[nodiscard]] UE5CORO_API FPromiseExtras
{
public:
#if UE5CORO_DEBUG
	int DebugID = -1;
	const TCHAR* DebugPromiseType = nullptr;
	const TCHAR* DebugName = nullptr;
#endif

	FEventRef Completed{EEventMode::ManualReset};
	UE::FSpinLock Lock;
	TMulticastDelegate<void()> Continuations_DEPRECATED;
	std::function<void()> OnCompleted = [] { };

	FPromiseExtras() = default;
	virtual ~FPromiseExtras() = default; // Virtual for warning suppression only
	UE_NONCOPYABLE(FPromiseExtras);

	bool IsComplete() const;
	virtual void Complete();
};

template<typename T>
struct [[nodiscard]] TPromiseExtras : FPromiseExtras
{
#if UE5CORO_DEBUG
	std::atomic<bool> bMoveUsed = false;
#endif
	T ReturnValue;
	std::function<void(const T&)> OnCompletedT = [](const T&) { };

	virtual void Complete() override
	{
		FPromiseExtras::Complete();
		OnCompletedT(ReturnValue);
		OnCompletedT = nullptr;
	}
};

class [[nodiscard]] FPromise
{
#if UE5CORO_DEBUG
	static std::atomic<int> LastDebugID;
	static thread_local TArray<FPromise*> ResumeStack;

	friend void TCoroutine<>::SetDebugName(const TCHAR*);
#endif

protected:
	std::shared_ptr<FPromiseExtras> Extras;

	UE5CORO_API explicit FPromise(std::shared_ptr<FPromiseExtras>,
	                              const TCHAR* PromiseType);
	UE_NONCOPYABLE(FPromise);

public:
	UE5CORO_API virtual ~FPromise(); // Virtual for warning suppression only
	UE5CORO_API virtual void Resume();

	UE5CORO_API void unhandled_exception();

	// co_yield is not allowed in async coroutines
	template<typename T>
	stdcoro::suspend_never yield_value(T&&) = delete;
};

class [[nodiscard]] UE5CORO_API FAsyncPromise : public FPromise
{
public:
	template<typename... A>
	explicit FAsyncPromise(std::shared_ptr<FPromiseExtras> Extras, A&&...)
		: FPromise(std::move(Extras), TEXT("Async")) { }

	FInitialSuspend initial_suspend() { return {FInitialSuspend::Resume}; }
	stdcoro::suspend_never final_suspend() noexcept { return {}; }

	template<typename T>
	decltype(auto) await_transform(T&& Awaitable)
	{
		return TAwaitTransform<FAsyncPromise, std::remove_reference_t<T>>()
			(std::forward<T>(Awaitable));
	}
};

class [[nodiscard]] UE5CORO_API FLatentPromise : public FPromise
{
public:
	enum ELatentState
	{
		LatentRunning,
		AsyncRunning,
		DeferredDestroy,
		Canceled,
		Done,
	};

private:
	UWorld* World = nullptr;
	void* PendingLatentCoroutine = nullptr;
	std::atomic<ELatentState> LatentState = LatentRunning;
	ELatentExitReason ExitReason = static_cast<ELatentExitReason>(0);

	void CreateLatentAction();
	void CreateLatentAction(FLatentActionInfo&&);
	void Init();
	template<typename... T> void Init(const UObject*, T&...);
	template<typename... T> void Init(FForceLatentCoroutine, T&...);
	template<typename... T> void Init(FLatentActionInfo, T&...);
	template<typename T, typename... A> void Init(T&, A&...);

public:
	template<typename... T>
	explicit FLatentPromise(std::shared_ptr<FPromiseExtras>, T&&...);

	virtual ~FLatentPromise() override;
	virtual void Resume() override;
	void ThreadSafeDestroy();

	ELatentState GetLatentState() const { return LatentState.load(); }
	void AttachToGameThread(); // AsyncRunning -> LatentRunning
	void DetachFromGameThread(); // LatentRunning -> AsyncRunning
	void LatentCancel(); // LatentRunning -> Canceled

	ELatentExitReason GetExitReason() const { return ExitReason; }
	void SetExitReason(ELatentExitReason Reason);
	void SetCurrentAwaiter(FLatentAwaiter*);

	FInitialSuspend initial_suspend();
	stdcoro::suspend_always final_suspend() noexcept;

	template<typename T>
	decltype(auto) await_transform(T&& Awaitable)
	{
		return TAwaitTransform<FLatentPromise, std::remove_reference_t<T>>()
			(std::forward<T>(Awaitable));
	}
};

template<typename T, typename Base>
class TCoroutinePromise : public Base
{
public:
	template<typename... A>
	explicit TCoroutinePromise(A&&... Args)
		: Base(std::make_shared<TPromiseExtras<T>>(), std::forward<A>(Args)...)
	{ }

	void return_value(T Value)
	{
		auto* ExtrasT = static_cast<TPromiseExtras<T>*>(this->Extras.get());
		UE::TScopeLock _(ExtrasT->Lock);
		check(!ExtrasT->IsComplete()); // Completion is after a value is returned
		ExtrasT->ReturnValue = std::move(Value);
	}

	TCoroutine<T> get_return_object() { return TCoroutine<T>(this->Extras); }
};

template<typename Base>
class TCoroutinePromise<void, Base> : public Base
{
public:
	template<typename... A>
	explicit TCoroutinePromise(A&&... Args)
		: Base(std::make_shared<FPromiseExtras>(), std::forward<A>(Args)...) { }
	void return_void() { }
	TCoroutine<> get_return_object() { return TCoroutine<>(this->Extras); }
};

template<typename... T>
FLatentPromise::FLatentPromise(std::shared_ptr<FPromiseExtras> Extras,
                               T&&... Args)
	: FPromise(std::move(Extras), TEXT("Latent"))
{
	checkf(IsInGameThread(),
	       TEXT("Latent coroutines may only be started on the game thread"));

	Init(Args...); // Deliberately not forwarding to force lvalue references
}

template<typename... T>
void FLatentPromise::Init(const UObject* WorldContext, T&... Args)
{
	// Keep trying to find a world from the UObjects passed in
	if (!World && WorldContext)
		World = WorldContext->GetWorld(); // null is fine

	Init(Args...);
}

template<typename... T>
void FLatentPromise::Init(FForceLatentCoroutine, T&... Args)
{
	// The static_assert on coroutine_traits prevents this
	check(!PendingLatentCoroutine);
	CreateLatentAction();

	Init(Args...);
}

template<typename... T>
void FLatentPromise::Init(FLatentActionInfo LatentInfo, T&... Args)
{
	// The static_assert on coroutine_traits prevents this
	check(!PendingLatentCoroutine);
	CreateLatentAction(std::move(LatentInfo));

	Init(Args...);
}

template<typename T, typename... A>
void FLatentPromise::Init(T& First, A&... Args)
{
	// Convert UObject& to UObject* for world context
	if constexpr (std::is_convertible_v<T&, const UObject&>)
		Init(static_cast<const UObject*>(std::addressof(First)), Args...);
	else
		Init(Args...);
}
}

template<typename T, typename... Args>
struct UE5Coro::Private::stdcoro::coroutine_traits<UE5Coro::TCoroutine<T>, Args...>
{
	static constexpr int LatentInfoCount =
		(0 + ... + std::is_convertible_v<Args, FLatentActionInfo>);
	static constexpr bool LatentForceCount =
		(0 + ... + std::is_same_v<Args, FForceLatentCoroutine>);
	static_assert(LatentInfoCount + LatentForceCount <= 1,
	              "Multiple latent info/force parameters found in coroutine");
	static constexpr bool bUseLatent = LatentInfoCount || LatentForceCount;
	using promise_type = UE5Coro::Private::TCoroutinePromise<
	    T, std::conditional_t<bUseLatent, UE5Coro::Private::FLatentPromise,
	                                      UE5Coro::Private::FAsyncPromise>>;
};

template<typename T, typename... Args>
struct UE5Coro::Private::stdcoro::coroutine_traits<const UE5Coro::TCoroutine<T>,
                                                   Args...>
{
	using promise_type = typename coroutine_traits<UE5Coro::TCoroutine<T>,
	                                               Args...>::promise_type;
};

template<typename... Args>
struct UE5Coro::Private::stdcoro::coroutine_traits<FAsyncCoroutine, Args...>
{
	using promise_type = typename coroutine_traits<UE5Coro::TCoroutine<>,
	                                               Args...>::promise_type;
};

template<typename... Args>
struct UE5Coro::Private::stdcoro::coroutine_traits<const FAsyncCoroutine,
                                                   Args...>
{
	using promise_type = typename coroutine_traits<UE5Coro::TCoroutine<>,
	                                               Args...>::promise_type;
};

#include "UE5Coro/Coroutine.inl"
