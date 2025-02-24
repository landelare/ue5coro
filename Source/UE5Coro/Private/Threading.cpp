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

#include "UE5Coro/Threading.h"
#include "UE5Coro/AsyncAwaiter.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
void SuspendCore(void* This, FPromise& Promise, UE::FMutex& Lock,
                 std::forward_list<FPromise*>& List)
{
	UE::TUniqueLock L(Promise.GetLock()); // The promise's lock
	checkf(Lock.IsLocked(), // The awaiter's lock
	       TEXT("Internal error: unguarded suspension"));
	if (Promise.RegisterCancelableAwaiter(This))
		List.push_front(&Promise);
	else
		FAsyncYieldAwaiter::Suspend(Promise);
	Lock.Unlock();
}

void CancelCore(FPromise& Promise, UE::FMutex& Lock,
                std::forward_list<FPromise*>& List)
{
	checkf(Promise.GetLock().IsLocked(), // The promise's lock
	       TEXT("Internal error: expected guarded cancellation"));
	UE::TUniqueLock L(Lock); // The awaiter's lock
	for (auto i = List.before_begin();;)
	{
		auto Before = i++;
		if (i == List.end())
			// This is OK, TryResumeAll might have cleared the list
			break;
		else if (*i == &Promise)
		{
			List.erase_after(Before);
			break;
		}
	}
	checkfSlow(std::ranges::find(List, &Promise) == List.end(),
	           TEXT("Internal error: multiple promises found"));
	// Calling Resume() synchronously from Cancel() would complicate things
	FAsyncYieldAwaiter::Suspend(Promise);
}
}

FAwaitableEvent::FAwaitableEvent(EEventMode Mode, bool bInitialState)
	: bActive(bInitialState), Mode(Mode)
{
	checkf(Mode == EEventMode::AutoReset || Mode == EEventMode::ManualReset,
	       TEXT("Invalid event mode"));
}

#if UE5CORO_DEBUG
FAwaitableEvent::~FAwaitableEvent()
{
	UE::TUniqueLock L(Lock);
	checkf(AwaitingPromises.empty(),
	       TEXT("Event destroyed with active awaiters"));
}
#endif

void FAwaitableEvent::Trigger()
{
	Lock.Lock();
	if (Mode == EEventMode::ManualReset)
	{
		bActive = true;
		TryResumeAll();
	}
	else
retry:
		if (!AwaitingPromises.empty())
		{
			if (!TryResumeOne()) // AutoReset: don't set bActive
				goto retry;
			// TryResumeOne unlocks the lock before returning true
		}
		else
		{
			bActive = true;
			Lock.Unlock();
		}
}

void FAwaitableEvent::Reset()
{
	UE::TUniqueLock L(Lock);
	bActive = false;
}

bool FAwaitableEvent::IsManualReset() const noexcept
{
	return Mode == EEventMode::ManualReset;
}

FEventAwaiter FAwaitableEvent::operator co_await()
{
	return FEventAwaiter(*this);
}

bool FAwaitableEvent::TryResumeOne()
{
	checkf(Lock.IsLocked(), TEXT("Internal error: resuming without lock"));
	checkf(!AwaitingPromises.empty(),
	       TEXT("Internal error: attempting to resume nothing"));
	auto* Promise = AwaitingPromises.front();
	AwaitingPromises.pop_front();
	if (Promise->UnregisterCancelableAwaiter<true>())
	{
		Lock.Unlock();
		Promise->Resume();
		return true;
	}
	else
		// This promise is getting canceled; try another, still holding the lock
		return false;
}

void FAwaitableEvent::TryResumeAll()
{
	checkf(Lock.IsLocked(), TEXT("Internal error: resuming without lock"));

	// Start a new list to make sure every current awaiter gets resumed,
	// even if the event is reset during one of the Resume() calls
	auto List = std::exchange(AwaitingPromises, {});
	Lock.Unlock();

	for (auto* Promise : List)
		if (Promise->UnregisterCancelableAwaiter<true>())
			Promise->Resume();
}

bool FEventAwaiter::await_ready() noexcept
{
	Event.Lock.Lock();
	bool bValue = Event.bActive;
	if (Event.Mode == EEventMode::AutoReset)
		Event.bActive = false;
	if (bValue)
	{
		Event.Lock.Unlock();
		return true;
	}
	else // Leave it locked
		return false;
}

void FEventAwaiter::Suspend(FPromise& Promise)
{
	checkf(!Event.bActive, TEXT("Internal error: suspending with active event"));
	SuspendCore(this, Promise, Event.Lock, Event.AwaitingPromises);
}

void FEventAwaiter::Cancel(void* This, FPromise& Promise)
{
	if (Promise.UnregisterCancelableAwaiter<false>())
	{
		auto& Event = static_cast<FEventAwaiter*>(This)->Event;
		CancelCore(Promise, Event.Lock, Event.AwaitingPromises);
	}
}

FAwaitableSemaphore::FAwaitableSemaphore(int Capacity, int InitialCount)
	: Capacity(Capacity), Count(InitialCount)
{
	checkf(Capacity > 0 && InitialCount >= 0 && InitialCount <= Capacity,
	       TEXT("Initial semaphore values out of range"));
}

#if UE5CORO_DEBUG
FAwaitableSemaphore::~FAwaitableSemaphore()
{
	UE::TUniqueLock L(Lock);
	checkf(AwaitingPromises.empty(),
	       TEXT("Semaphore destroyed with active awaiters"));
}
#endif

void FAwaitableSemaphore::Unlock(int InCount)
{
	checkf(InCount > 0, TEXT("Invalid count"));
	Lock.Lock();
	verifyf((Count += InCount) <= Capacity,
	        TEXT("Semaphore unlocked above maximum"));
	TryResumeAll();
}

FSemaphoreAwaiter FAwaitableSemaphore::operator co_await()
{
	return FSemaphoreAwaiter(*this);
}

void FAwaitableSemaphore::TryResumeAll()
{
	checkf(Lock.IsLocked(), TEXT("Internal error: resuming without lock held"));
	while (!AwaitingPromises.empty() && Count > 0)
	{
		auto* Promise = AwaitingPromises.front();
		AwaitingPromises.pop_front();
		if (!Promise->UnregisterCancelableAwaiter<true>())
			continue; // This promise is getting canceled, try another
		verifyf(--Count >= 0, TEXT("Internal error: semaphore went negative"));
		Lock.Unlock();
		Promise->Resume(); // The coroutine might want the lock
		Lock.Lock();
	}
	Lock.Unlock();
}

bool FSemaphoreAwaiter::await_ready()
{
	Semaphore.Lock.Lock();
	if (Semaphore.Count > 0)
	{
		verifyf(--Semaphore.Count >= 0,
		        TEXT("Internal error: semaphore went negative"));
		Semaphore.Lock.Unlock();
		return true;
	}
	else // Leave it locked
		return false;
}

void FSemaphoreAwaiter::Suspend(FPromise& Promise)
{
	SuspendCore(this, Promise, Semaphore.Lock, Semaphore.AwaitingPromises);
}

void FSemaphoreAwaiter::Cancel(void* This, FPromise& Promise)
{
	if (Promise.UnregisterCancelableAwaiter<false>())
	{
		auto& Semaphore = static_cast<FSemaphoreAwaiter*>(This)->Semaphore;
		CancelCore(Promise, Semaphore.Lock, Semaphore.AwaitingPromises);
	}
}
