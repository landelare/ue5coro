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

using namespace UE5Coro;
using namespace UE5Coro::Private;

FAwaitableEvent::FAwaitableEvent(EEventMode Mode, bool bInitialState)
	: bActive(bInitialState), Mode(Mode)
{
	checkf(Mode == EEventMode::AutoReset || Mode == EEventMode::ManualReset,
	       TEXT("Invalid event mode"));
}

#if UE5CORO_DEBUG
FAwaitableEvent::~FAwaitableEvent()
{
	ensureMsgf(AwaitingPromises.empty(),
	           TEXT("Destroyed early, remaining awaiters will never resume!"));
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
	else if (!AwaitingPromises.empty())
		ResumeOne(); // AutoReset: don't set bActive
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

void FAwaitableEvent::ResumeOne()
{
	checkf(Lock.IsLocked(), TEXT("Internal error: resuming without lock"));
	checkf(!AwaitingPromises.empty(),
	       TEXT("Internal error: attempting to resume nothing"));
	auto* Promise = AwaitingPromises.front();
	AwaitingPromises.pop_front();
	Lock.Unlock(); // The coroutine might want the lock

	Promise->Resume();
}

void FAwaitableEvent::TryResumeAll()
{
	checkf(Lock.IsLocked(), TEXT("Internal error: resuming without lock"));

	// Start a new list to make sure every current awaiter gets resumed,
	// even if the event is reset during one of the Resume() calls
	auto List = std::exchange(AwaitingPromises, {});
	Lock.Unlock();

	for (auto* Promise : List)
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
	checkf(Event.Lock.IsLocked(),
	       TEXT("Internal error: suspension without lock"));
	checkf(!Event.bActive, TEXT("Internal error: suspending with active event"));
	Event.AwaitingPromises.push_front(&Promise);
	Event.Lock.Unlock();
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
	ensureMsgf(AwaitingPromises.empty(),
	           TEXT("Destroyed early, remaining awaiters will never resume!"));
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
	checkf(Semaphore.Lock.IsLocked(),
	       TEXT("Internal error: suspension without lock"));
	Semaphore.AwaitingPromises.push_front(&Promise);
	Semaphore.Lock.Unlock();
}
