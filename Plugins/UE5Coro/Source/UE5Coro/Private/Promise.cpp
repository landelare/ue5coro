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

#include "UE5Coro/AsyncCoroutine.h"
#include "Misc/ScopeExit.h"

using namespace UE5Coro::Private;

#if UE5CORO_DEBUG
std::atomic<int> UE5Coro::Private::GLastDebugID = -1; // -1 = no coroutines yet
#endif

thread_local FPromise* UE5Coro::Private::GCurrentPromise = nullptr;
thread_local bool UE5Coro::Private::GDestroyedEarly = false;

namespace
{
struct FCoroutineScope
{
	FPromise* Promise;
	FPromise* PreviousPromise;

	explicit FCoroutineScope(FPromise* Promise)
		: Promise(Promise),
		  PreviousPromise(std::exchange(GCurrentPromise, Promise))
	{
	}

	~FCoroutineScope()
	{
		verifyf(std::exchange(GCurrentPromise, PreviousPromise) == Promise,
		        TEXT("Internal error: coroutine tracking derailed"));
	}
};
}

bool FPromiseExtras::IsComplete() const
{
	return Completed->Wait(0, true);
}

bool FCancellationTracker::ShouldCancel(bool bBypassHolds) const
{
	return bCanceled && (bBypassHolds || CancellationHolds == 0);
}

FPromise::FPromise(std::shared_ptr<FPromiseExtras> InExtras,
                   const TCHAR* PromiseType)
	: Extras(std::move(InExtras))
{
#if UE5CORO_DEBUG
	Extras->DebugID = ++GLastDebugID;
	Extras->DebugPromiseType = PromiseType;
#endif
}

FPromise::~FPromise()
{
	// Expecting the lock to be taken by a derived destructor
	checkf(!Extras->Lock.try_lock(), TEXT("Internal error: lock not held"));
	checkf(!Extras->IsComplete(),
	       TEXT("Unexpected late/double coroutine destruction"));
#if PLATFORM_EXCEPTIONS_DISABLED
	Extras->bWasSuccessful = !GDestroyedEarly;
#else
	Extras->bWasSuccessful = !GDestroyedEarly && !bUnhandledException;
#endif
	GDestroyedEarly = false;

	// The coroutine is considered completed NOW
	auto* ReturnValuePtr = std::exchange(Extras->ReturnValuePtr, nullptr);
	Extras->Completed->Trigger();
	Extras->Lock.unlock();

	for (auto& Fn : OnCompleted)
		Fn(ReturnValuePtr);
}

void FPromise::ThreadSafeDestroy()
{
	auto Handle = stdcoro::coroutine_handle<FPromise>::from_promise(*this);
	GDestroyedEarly = IsEarlyDestroy();
	{
		FCoroutineScope Scope(this);
		Handle.destroy(); // counts as delete this;
	}
	checkf(!GDestroyedEarly,
	       TEXT("Internal error: early destroy flag not reset"));
}

FPromise& FPromise::Current()
{
	checkf(GCurrentPromise,
	       TEXT("This operation is only available from inside a TCoroutine"));
	return *GCurrentPromise;
}

void FPromise::Cancel()
{
	CancellationTracker.Cancel();
}

bool FPromise::ShouldCancel(bool bBypassHolds) const
{
	return CancellationTracker.ShouldCancel(bBypassHolds);
}

void FPromise::HoldCancellation()
{
	CancellationTracker.Hold();
}

void FPromise::ReleaseCancellation()
{
	CancellationTracker.Release();
}

void FPromise::Resume(bool bBypassCancellationHolds)
{
	checkf(this, TEXT("Corruption")); // Still useful on some compilers
	checkf(!Extras->IsComplete(),
	       TEXT("Attempting to resume completed coroutine"));

	// Self-destruct instead of resuming if a cancellation was received.
	// As an exception, the latent action manager destroying the latent action
	// bypasses cancellation holds.
	if (UNLIKELY(ShouldCancel(bBypassCancellationHolds)))
		ThreadSafeDestroy();
	else
	{
		FCoroutineScope Scope(this);
		stdcoro::coroutine_handle<FPromise>::from_promise(*this).resume();
	}
}

void FPromise::ResumeFast()
{
	checkf(!Extras->IsComplete() && !ShouldCancel(true),
	       TEXT("Internal error: Fast resume preconditions not met"));
	// If this is a FLatentPromise, !LF_Detached is also assumed
	FCoroutineScope Scope(this);
	stdcoro::coroutine_handle<FPromise>::from_promise(*this).resume();
}

void FPromise::AddContinuation(std::function<void(void*)> Fn)
{
	// Expecting a non-empty function and the lock to be held by the caller
	checkf(!Extras->Lock.try_lock(), TEXT("Internal error: lock not held"));
	checkf(Fn, TEXT("Internal error: adding empty function as continuation"));

	OnCompleted.Add(std::move(Fn));
}

void FPromise::unhandled_exception()
{
#if PLATFORM_EXCEPTIONS_DISABLED
	// Hitting this can be a result of the coroutine itself invoking undefined
	// behavior, e.g., by using a bad pointer.
	// On Windows, SEH exceptions can end up here if C++ exceptions are disabled.
	// If this hinders debugging, feel free to remove it!
	checkSlow(!"Unhandled exception from coroutine!");
#else
	bUnhandledException = true;
	throw;
#endif
}
