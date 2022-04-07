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
#include <atomic>
#include <coroutine>
#include "Engine/LatentActionManager.h"
#include "AsyncCoroutine.generated.h"

namespace UE5Coro::Private
{
class FAsyncPromise;
class FLatentAwaiter;
class FLatentPromise;
}

// This type does nothing but it has to be a USTRUCT in the global namespace to
// support latent UFUNCTIONs without wrappers.

/**
 * Asynchronous coroutine. Return this type from a function and it will be able to
 * co_await various awaiters without blocking the calling thread.<br>
 * Objects of this type are meaningless and should not be stored.
 */
USTRUCT(BlueprintInternalUseOnly, Meta=(HiddenByDefault))
struct FAsyncCoroutine
{
	GENERATED_BODY()
};

template<typename... Args>
struct std::coroutine_traits<FAsyncCoroutine, Args...>
{
	static constexpr int LatentInfoCount =
		(... + std::is_convertible_v<Args, FLatentActionInfo>);
	static_assert(LatentInfoCount <= 1,
		"Multiple FLatentActionInfo parameters found in coroutine");
	using promise_type = std::conditional_t<LatentInfoCount,
	                                        UE5Coro::Private::FLatentPromise,
	                                        UE5Coro::Private::FAsyncPromise>;
};

namespace UE5Coro::Private
{
struct FInitialSuspend
{
	enum EAction
	{
		Ready,
		Suspend,
		Destroy,
	} Action;

	bool await_ready() noexcept { return Action == Ready; }
	void await_resume() noexcept { }
	void await_suspend(std::coroutine_handle<FLatentPromise> Handle) noexcept
	{
		if (Action == Destroy)
			Handle.destroy();
	}
};

class [[nodiscard]] FPromise
{
protected:
	FPromise() = default;
	UE_NONCOPYABLE(FPromise);

public:
	FAsyncCoroutine get_return_object() { return {}; }
	void unhandled_exception() { check(!"Exceptions are not supported"); }

	// co_yield is not allowed in async coroutines
	std::suspend_never yield_value(auto&&) = delete;
};

class [[nodiscard]] FAsyncPromise : public FPromise
{
public:
	std::suspend_never initial_suspend() { return {}; }
	std::suspend_never final_suspend() noexcept { return {}; }
	void return_void() { }
};

class [[nodiscard]] UE5CORO_API FLatentPromise : public FPromise
{
public:
	enum ELatentState
	{
		LatentRunning,
		AsyncRunning,
		DeferredDestroy,
		Aborted,
		Done,
	};

private:
	UWorld* World = nullptr;
	void* PendingLatentCoroutine = nullptr;
	std::atomic<ELatentState> LatentState = LatentRunning;

	void CreateLatentAction(FLatentActionInfo&&);
	void Init();
	void Init(const UObject*, auto&...);
	void Init(FLatentActionInfo, auto&...);
	void Init(auto&, auto&...);

public:
	explicit FLatentPromise(auto&&...);

	std::atomic<ELatentState>& GetMutableLatentState() { return LatentState; }
	void SetCurrentAwaiter(FLatentAwaiter*);

	FInitialSuspend initial_suspend();
	std::suspend_always final_suspend() noexcept { return {}; }
	void return_void();
};

FLatentPromise::FLatentPromise(auto&&... Args)
{
	checkf(IsInGameThread(),
	       TEXT("Latent coroutines may only be started on the game thread"));

	Init(Args...); // Deliberately not forwarding to force lvalue references
}

void FLatentPromise::Init(const UObject* WorldContext, auto&... Args)
{
	// Keep trying to find a world from the UObjects passed in
	if (!World && WorldContext)
		World = WorldContext->GetWorld(); // null is fine

	Init(Args...);
}

void FLatentPromise::Init(FLatentActionInfo LatentInfo, auto&... Args)
{
	// The static_assert on coroutine_traits prevents this
	check(!PendingLatentCoroutine);
	CreateLatentAction(std::move(LatentInfo));

	Init(Args...);
}

void FLatentPromise::Init(auto& First, auto&... Args)
{
	// Convert UObject& to UObject* for world context
	if constexpr (std::is_convertible_v<decltype(First), const UObject&>)
		Init(static_cast<const UObject*>(std::addressof(First)), Args...);
	else
		Init(Args...);
}
}
