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
#include <forward_list>
#include "UE5Coro/Private.h"
#include "UE5Coro/Promise.h"

namespace UE5Coro
{
/** Awaitable event. co_awaiting this object suspends the coroutine if the event
 *  is not triggered, and resumes it at the next call to Trigger().
 *  AutoReset events will only resume one awaiter, ManualReset all of them. */
class UE5CORO_API FAwaitableEvent final
{
	friend Private::FEventAwaiter;
	friend Private::Test::FTestHelper;

	UE::FMutex Lock;
	bool bActive;
	const EEventMode Mode;
	std::forward_list<Private::FPromise*> AwaitingPromises;

public:
	/** Initializes this event to be in the given mode and state. */
	explicit FAwaitableEvent(EEventMode Mode = EEventMode::AutoReset,
	                         bool bInitialState = false);
	UE_NONCOPYABLE(FAwaitableEvent);
#if UE5CORO_DEBUG
	~FAwaitableEvent();
#endif

	/** Resumes one or more coroutines awaiting this event, depending on Mode. */
	void Trigger();
	/** Clears this event, making subsequent co_awaits suspend. */
	void Reset();
	/** @return true if this object was made as ManualReset. */
	[[nodiscard]] bool IsManualReset() const noexcept;

	auto operator co_await() -> Private::FEventAwaiter;

private:
	[[nodiscard]] bool TryResumeOne();
	void TryResumeAll();
};

/** Awaitable semaphore. co_awaiting this object will attempt to lock/acquire
 *  one count and suspend the coroutine if this was not possible, resuming it
 *  when the semaphore is next Unlock()ed (released). */
class UE5CORO_API FAwaitableSemaphore final
{
	friend Private::FSemaphoreAwaiter;
	friend Private::Test::FTestHelper;

	UE::FMutex Lock;
	const int Capacity;
	int Count;
	std::forward_list<Private::FPromise*> AwaitingPromises;

public:
	/** Initializes the semaphore to the given capacity and initial count.
	 *  Defaults to being an unlocked mutex. */
	explicit FAwaitableSemaphore(int Capacity = 1, int InitialCount = 1);
	UE_NONCOPYABLE(FAwaitableSemaphore);
#if UE5CORO_DEBUG
	~FAwaitableSemaphore();
#endif

	/** Unlocks (releases) the semaphore the specified amount of times. */
	void Unlock(int InCount = 1);

	auto operator co_await() -> Private::FSemaphoreAwaiter;

private:
	void TryResumeAll();
};
}

#pragma region Private
namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FEventAwaiter final
	: public TCancelableAwaiter<FEventAwaiter>
{
	FAwaitableEvent& Event;

public:
	explicit FEventAwaiter(FAwaitableEvent& Event)
		: TCancelableAwaiter(&Cancel), Event(Event) { }

	[[nodiscard]] bool await_ready() noexcept;
	void Suspend(FPromise&);
	void await_resume() noexcept { }

private:
	static void Cancel(void*, FPromise&);
};

class [[nodiscard]] UE5CORO_API FSemaphoreAwaiter final
	: public TCancelableAwaiter<FSemaphoreAwaiter>
{
	FAwaitableSemaphore& Semaphore;

public:
	explicit FSemaphoreAwaiter(FAwaitableSemaphore& Semaphore)
		: TCancelableAwaiter(&Cancel), Semaphore(Semaphore) { }

	[[nodiscard]] bool await_ready();
	void Suspend(FPromise&);
	void await_resume() noexcept { }

private:
	static void Cancel(void*, FPromise&);
};
}
#pragma endregion
