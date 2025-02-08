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

#include "UE5Coro/HttpAwaiter.h"
#include "UE5Coro/AsyncAwaiter.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

FHttpAwaiter Http::ProcessAsync(FHttpRequestRef Request)
{
	return FHttpAwaiter(std::move(Request));
}

FHttpAwaiter::FState::FState(FHttpRequestRef&& Request)
	: Thread(Request->GetDelegateThreadPolicy() ==
	             EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread
	         ? ENamedThreads::UnusedAnchor
	         : FTaskGraphInterface::Get().GetCurrentThreadIfKnown())
	, Request(std::move(Request))
{
}

FHttpAwaiter::FHttpAwaiter(FHttpRequestRef&& Request)
	: State(new FState(std::move(Request)))
{
	State->Request->OnProcessRequestComplete().BindSP(State.ToSharedRef(),
	                                                  &FState::RequestComplete);
	State->Request->ProcessRequest();
}

bool FHttpAwaiter::await_ready()
{
	State->Lock.Lock();

	// Skip suspension if the request finished first
	if (State->Result.has_value())
	{
		State->Lock.Unlock();
		return true;
	}

	// Carry the lock into Suspend()
	checkf(!State->bSuspended, TEXT("Attempted second concurrent co_await"));
	State->bSuspended = true;
	return false;
}

void FHttpAwaiter::Suspend(FPromise& Promise)
{
	// This should be locked from await_ready
	checkf(State->Lock.IsLocked(), TEXT("Internal error: lock wasn't taken"));
	State->Promise = &Promise;
	State->Lock.Unlock();
}

void FHttpAwaiter::FState::Resume()
{
	// The default HTTP thread policy is to resume on the GT
	ensureMsgf(Thread == ENamedThreads::UnusedAnchor || IsInGameThread(),
	           TEXT("Internal error: expected HTTP callback on the game thread"));
	// leave bSuspended true to prevent any further suspensions (not co_awaits)

	// Fast path if the target thread is the current thread
	if (auto ThisThread = FTaskGraphInterface::Get().GetCurrentThreadIfKnown();
	    Thread == ENamedThreads::UnusedAnchor || // Indicates HTTP thread
	    (Thread & ThreadTypeMask) == (ThisThread & ThreadTypeMask))
		Promise->Resume();
	else
		AsyncTask(Thread, [Promise = Promise] { Promise->Resume(); });
}

void FHttpAwaiter::FState::RequestComplete(FHttpRequestPtr,
                                           FHttpResponsePtr Response,
                                           bool bConnectedSuccessfully)
{
	UE::TDynamicUniqueLock L(Lock);
	Result = {std::move(Response), bConnectedSuccessfully};
	if (bSuspended)
	{
		L.Unlock();
		Resume();
	}
}

TTuple<FHttpResponsePtr, bool> FHttpAwaiter::await_resume()
{
	checkf(State->Result.has_value(),
	       TEXT("Internal error: resuming with no value"));
	return *State->Result;
}
