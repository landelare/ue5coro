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
#include "UE5Coro/AsyncCoroutine.h"
#include "UE5Coro/Private.h"

namespace UE5Coro
{
namespace Private
{
struct FAwaitingPromise;
}
/**
 * Awaitable event. co_awaiting this object suspends the coroutine if the event
 * is not triggered, and resumes it at the next call to Trigger().
 * AutoReset events will only resume one awaiter, ManualReset all of them.
 */
class UE5CORO_API FAwaitableEvent final
	: public Private::TAwaiter<FAwaitableEvent>
{
	const EEventMode Mode;

	Private::FMutex Lock;
	bool bActive;
	Private::FAwaitingPromise* Awaiters = nullptr;

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
	[[nodiscard]] bool IsManualReset() const;

	bool await_ready() noexcept;
	void Suspend(Private::FPromise&);
	void await_resume() noexcept { }

private:
	void ResumeOne();
	void TryResumeAll();
};

/**
 * Awaitable semaphore. co_awaiting this object will attempt to lock/acquire one
 * count and suspend the coroutine if this was not possible, resuming it when
 * the semaphore is next Unlock()ed (released).
 */
class UE5CORO_API FAwaitableSemaphore final
	: public Private::TAwaiter<FAwaitableSemaphore>
{
	const int Capacity;
	Private::FMutex Lock;
	int Count;
	Private::FAwaitingPromise* Awaiters = nullptr;

public:
	/** Initializes the semaphore to the given capacity and initial count.<br>
	 *  Defaults to being an unlocked mutex. */
	explicit FAwaitableSemaphore(int Capacity = 1, int InitialCount = 1);
	UE_NONCOPYABLE(FAwaitableSemaphore);
#if UE5CORO_DEBUG
	~FAwaitableSemaphore();
#endif

	/** Unlocks (releases) the semaphore the specified amount of times. */
	void Unlock(int Count = 1);

	bool await_ready();
	void Suspend(Private::FPromise&);
	void await_resume() noexcept { }

private:
	void TryResumeAll();
};

namespace Private
{
struct FAwaitingPromise
{
	FPromise* Promise;
	FAwaitingPromise* Next;
};
}
}
