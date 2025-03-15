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

#include "UE5Coro/Promise.h"
#include "Misc/ScopeExit.h"

using namespace UE5Coro::Private;

thread_local FPromise* UE5Coro::Private::GCurrentPromise = nullptr;
thread_local bool UE5Coro::Private::GDestroyedEarly = false;

FCoroutineScope::FCoroutineScope(FPromise* Promise)
	: Promise(Promise),
	  PreviousPromise(std::exchange(GCurrentPromise, Promise))
{
}

FCoroutineScope::~FCoroutineScope()
{
	verifyf(std::exchange(GCurrentPromise, PreviousPromise) == Promise,
	        TEXT("Internal error: coroutine tracking derailed"));
}

bool FPromiseExtras::IsComplete() const
{
	return Completed->Wait(0, true);
}

FPromise::FPromise(std::shared_ptr<FPromiseExtras> InExtras,
                   const TCHAR* PromiseType)
	: Extras(std::move(InExtras))
{
#if UE5CORO_DEBUG
	verifyf(++Debug::GActiveCoroutines > 0,
	        TEXT("Internal error: promise tracking derailed"));
	Extras->DebugID = ++Debug::GLastDebugID;
	Extras->DebugPromiseType = PromiseType;
#endif
}

FPromise::~FPromise()
{
#if UE5CORO_DEBUG
	verifyf(--Debug::GActiveCoroutines >= 0,
	        TEXT("Internal error: promise tracking derailed"));
#endif
	// Expecting the lock to be taken by a derived destructor
	checkf(Extras->Lock.IsLocked(), TEXT("Internal error: lock not held"));
	checkf(!Extras->IsComplete(),
	       TEXT("Internal error: unexpected late/double coroutine destruction"));
#if PLATFORM_EXCEPTIONS_DISABLED
	Extras->bWasSuccessful = !GDestroyedEarly;
#else
	Extras->bWasSuccessful = !GDestroyedEarly && !bUnhandledException;
#endif
	GDestroyedEarly = false;

	// The coroutine is considered completed NOW
	auto* ReturnValuePtr = std::exchange(Extras->ReturnValuePtr, nullptr);
	Extras->Completed->Trigger();
	Extras->Lock.Unlock();

	for (auto& Fn : OnCompleted)
		Fn(ReturnValuePtr);
}

void FPromise::ResumeInternal(bool bBypassCancellationHolds)
{
	checkf(this, TEXT("Corruption")); // UB, but still useful on some compilers
	checkf(!Extras->IsComplete(),
	       TEXT("Attempting to resume completed coroutine"));
	checkCode(
		UE::TUniqueLock Lock(Extras->Lock);
		checkf(!CancelableAwaiter,
		       TEXT("Internal error: resumed with a registered awaiter"));
	);

	// Self-destruct instead of resuming if a cancellation was received
	if (ShouldCancel(bBypassCancellationHolds)) [[unlikely]]
		ThreadSafeDestroy();
	else
	{
		FCoroutineScope Scope(this);
		std::coroutine_handle<FPromise>::from_promise(*this).resume();
	}
}

void FPromise::ThreadSafeDestroy()
{
	auto Handle = std::coroutine_handle<FPromise>::from_promise(*this);
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

UE::FMutex& FPromise::GetLock()
{
	return Extras->Lock;
}

bool FPromise::RegisterCancelableAwaiter(void* Awaiter)
{
	checkf(Extras->Lock.IsLocked(),
	       TEXT("Internal error: unguarded awaiter registration"));
	checkf(!CancelableAwaiter,
	       TEXT("Internal error: overlapping awaiter registration"));
	if (ShouldCancel(false))
		return false;
	else
	{
		CancelableAwaiter = Awaiter;
		return true;
	}
}

template<bool bLock>
bool FPromise::UnregisterCancelableAwaiter()
{
	if constexpr (bLock)
	{
		UE::TUniqueLock Lock(Extras->Lock);
		return std::exchange(CancelableAwaiter, nullptr) != nullptr;
	}
	else
	{
		checkf(Extras->Lock.IsLocked(),
		       TEXT("Internal error: unguarded awaiter registration"));
		return std::exchange(CancelableAwaiter, nullptr) != nullptr;
	}
}
template UE5CORO_API bool FPromise::UnregisterCancelableAwaiter<false>();
template UE5CORO_API bool FPromise::UnregisterCancelableAwaiter<true>();

void FPromise::Cancel(bool bBypassCancellationHolds)
{
	checkf(Extras->Lock.IsLocked(),
	       TEXT("Internal error: unguarded cancellation"));
	CancellationTracker.Cancel();
	if (CancelableAwaiter && ShouldCancel(bBypassCancellationHolds))
		(**static_cast<void (**)(void*, FPromise&)>(CancelableAwaiter))(
			CancelableAwaiter, *this);
}

bool FPromise::ShouldCancel(bool bBypassCancellationHolds) const
{
	return CancellationTracker.ShouldCancel(bBypassCancellationHolds);
}

void FPromise::HoldCancellation()
{
	CancellationTracker.Hold();
}

void FPromise::ReleaseCancellation()
{
	CancellationTracker.Release();
}

void FPromise::Resume()
{
	ResumeInternal(false);
}

void FPromise::ResumeFast()
{
	checkf(!Extras->IsComplete() && !Extras->Lock.IsLocked() &&
	       !CancelableAwaiter && !ShouldCancel(true),
	       TEXT("Internal error: fast resume preconditions not met"));
	// If this is a FLatentPromise, !LF_Detached is also assumed

	checkf(GCurrentPromise == this,
	       TEXT("Internal error: expected to run inside a coroutine scope"));
	std::coroutine_handle<FPromise>::from_promise(*this).resume();
}

void FPromise::AddContinuation(std::function<void(void*)> Fn)
{
	// Expecting a non-empty function and the lock to be held by the caller
	checkf(Extras->Lock.IsLocked(), TEXT("Internal error: lock not held"));
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

#if UE5CORO_PRIVATE_USE_DEBUG_ALLOCATOR
#include "Windows/WindowsHWrapper.h"

void* FPromise::operator new(size_t Size)
{
	auto* Memory = VirtualAlloc(nullptr, Size, MEM_COMMIT | MEM_RESERVE,
	                            PAGE_READWRITE);
	checkf(Memory, TEXT("VirtualAlloc failed"));
	memset(Memory, 0xAA, Size);
	return Memory;
}

void FPromise::operator delete(void* Memory)
{
	// Keep the memory reserved, so that future promises don't recycle addresses
	verifyf(VirtualFree(Memory, 0, MEM_DECOMMIT), TEXT("VirtualFree failed"));
}
#endif
