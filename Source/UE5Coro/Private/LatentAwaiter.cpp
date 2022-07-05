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

#include "LatentActions.h"
#include "UE5Coro/AsyncCoroutine.h"
#include "UE5Coro/LatentAwaiters.h"
#include "UE5Coro/UE5CoroSubsystem.h"

using namespace UE5Coro::Private;

namespace
{
struct [[nodiscard]] FPendingAsyncCoroutine : FPendingLatentAction
{
	FAsyncHandle Handle;
	FLatentAwaiter* Awaiter;

	FPendingAsyncCoroutine(FAsyncHandle Handle, FLatentAwaiter* Awaiter)
		: Handle(Handle), Awaiter(Awaiter) { }
	UE_NONCOPYABLE(FPendingAsyncCoroutine);

	virtual ~FPendingAsyncCoroutine() override
	{
		if (Handle)
			Handle.destroy();
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		if (!Awaiter->ShouldResume())
			return;

		Response.DoneIf(true);

		// Ownership moves back to the task graph tasks
		auto Original = Handle;
		Handle = nullptr;
		Original.resume();
	}
};
}

void FLatentCancellation::await_suspend(FLatentHandle Handle)
{
	ensureMsgf(IsInGameThread(),
	           TEXT("Latent awaiters may only be used on the game thread"));
	Handle.promise().LatentCancel();
}

FLatentAwaiter::~FLatentAwaiter()
{
	(*Resume)(State, true);
	State = nullptr;
	Resume = nullptr;
}

void FLatentAwaiter::await_suspend(FAsyncHandle Handle)
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));

	// Prepare a latent action on the subsystem and transfer ownership to that
	auto* Sys = GWorld->GetSubsystem<UUE5CoroSubsystem>();
	auto* Latent = new FPendingAsyncCoroutine(Handle, this);
	auto LatentInfo = Sys->MakeLatentInfo();
	GWorld->GetLatentActionManager().AddNewAction(
		LatentInfo.CallbackTarget, LatentInfo.UUID, Latent);
}

void FLatentAwaiter::await_suspend(FLatentHandle Handle)
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	auto& Promise = Handle.promise();
	checkCode(
		auto CurrentState = Promise.GetLatentState();
		checkf(CurrentState == FLatentPromise::LatentRunning,
		       TEXT("Unexpected state in latent coroutine %d"), CurrentState);
	);

	Promise.SetCurrentAwaiter(this);
}
