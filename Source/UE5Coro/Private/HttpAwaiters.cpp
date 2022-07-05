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

#include "UE5Coro/HttpAwaiters.h"
#include "UE5Coro/AsyncAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

FHttpAwaiter Http::ProcessAsync(FHttpRequestRef Request)
{
	return FHttpAwaiter(std::move(Request));
}

FHttpAwaiter::FHttpAwaiter(FHttpRequestRef&& Request)
	: Thread(FTaskGraphInterface::Get().GetCurrentThreadIfKnown())
	, Request(std::move(Request))
{
	Request->OnProcessRequestComplete().BindRaw(
		this, &FHttpAwaiter::RequestComplete);
	Request->ProcessRequest();
}

bool FHttpAwaiter::await_ready()
{
	Lock.Lock();

#if DO_CHECK
	std::visit([](std::coroutine_handle<> Handle)
	{
		checkf(!Handle, TEXT("Attempting to reuse HTTP awaiter"));
	}, Handle);
#endif

	// Skip suspension if the request finished first
	if (Result.has_value())
	{
		Lock.Unlock();
		return true;
	}
	else
	{
		// Lock is deliberately left locked
		bSuspended = true;
		return false;
	}
}

void FHttpAwaiter::await_suspend(FLatentHandle InHandle)
{
	// Even if the entire co_await starts and ends on the game thread we need
	// to take temporary ownership in case the latent action manager decides to
	// delete the latent action.
	InHandle.promise().DetachFromGameThread();
	SetHandleAndUnlock(InHandle);
}

void FHttpAwaiter::await_suspend(FAsyncHandle InHandle)
{
	SetHandleAndUnlock(InHandle);
}

template<typename T>
void FHttpAwaiter::SetHandleAndUnlock(std::coroutine_handle<T> InHandle)
{
	// This should be locked from await_ready
	checkf(!Lock.TryLock(), TEXT("Internal error"));
	Handle = InHandle;
	Lock.Unlock();
}

void FHttpAwaiter::Resume()
{
	// Don't needlessly dispatch AsyncTasks to the GT from the GT
	ensureMsgf(IsInGameThread(), TEXT("Internal error"));
	auto Dispatch = [this](std::invocable auto&& Fn)
	{
		bSuspended = false; // Technically not needed since this is not reusable
		if (Thread == ENamedThreads::GameThread)
			Fn();
		else
			AsyncTask(Thread, std::move(Fn));
	};

	if (std::holds_alternative<FLatentHandle>(Handle))
		Dispatch([this] { std::get<FLatentHandle>(Handle).promise()
		                                                 .ThreadSafeResume(); });
	else
		Dispatch([this] { std::get<FAsyncHandle>(Handle).resume(); });
}

void FHttpAwaiter::RequestComplete(FHttpRequestPtr, FHttpResponsePtr Response,
                                   bool bConnectedSuccessfully)
{
	UE::TScopeLock _(Lock);
	Result = {std::move(Response), bConnectedSuccessfully};
	if (bSuspended)
		Resume();
}

TTuple<FHttpResponsePtr, bool> FHttpAwaiter::await_resume()
{
	checkf(Result.has_value(), TEXT("Internal error"));
	return std::move(Result).value();
}
