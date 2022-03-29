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

using namespace UE5Coro::Private;

namespace
{
class [[nodiscard]] FPendingLatentCoroutine : public FPendingLatentAction
{
	std::coroutine_handle<FLatentPromise> Handle;
	FLatentActionInfo LatentInfo;
	FLatentAwaiter* CurrentAwaiter = nullptr;

public:
	explicit FPendingLatentCoroutine(std::coroutine_handle<FLatentPromise> Handle,
	                                 FLatentActionInfo LatentInfo)
		: Handle(Handle), LatentInfo(LatentInfo) { }

	UE_NONCOPYABLE(FPendingLatentCoroutine);

	virtual ~FPendingLatentCoroutine() override
	{
		checkf(IsInGameThread(),
		       TEXT("Unexpected latent action off the game thread"));

		auto& Promise = Handle.promise();
		auto& State = Promise.GetMutableLatentState();

		// Destroy the coroutine unless it's currently running an AsyncTask.
		// In that case, the responsibility will transfer to the async awaiter.
		if (auto Old = FLatentPromise::AsyncRunning;
			!State.compare_exchange_strong(Old, FLatentPromise::DeferredDestroy))
			Handle.destroy();
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		if (CurrentAwaiter && CurrentAwaiter->ShouldResume())
		{
			CurrentAwaiter = nullptr;
			Handle.resume(); // This might set the awaiter for next time
		}

		auto& Promise = Handle.promise();
		auto State = Promise.GetMutableLatentState().load();

		if (State >= FLatentPromise::Aborted)
			Response.DoneIf(true);
		if (State == FLatentPromise::Done)
			Response.TriggerLink(LatentInfo.ExecutionFunction, LatentInfo.Linkage,
			                     LatentInfo.CallbackTarget);
	}

	const FLatentActionInfo& GetLatentInfo() const { return LatentInfo; }

	void SetCurrentAwaiter(FLatentAwaiter* Awaiter)
	{
		checkf(IsInGameThread(),
		       TEXT("Latent awaiters may only be used on the game thread"));
		if (Awaiter)
			ensureMsgf(!CurrentAwaiter, TEXT("Unexpected double await"));

		CurrentAwaiter = Awaiter;
	}
};
}

// This is a separate function so that template Init() doesn't need the type
void FLatentPromise::CreateLatentAction(FLatentActionInfo&& LatentInfo)
{
	// The static_assert on coroutine_traits prevents this
	checkf(!PendingLatentCoroutine, TEXT("Internal error"));

	PendingLatentCoroutine = new FPendingLatentCoroutine(
		std::coroutine_handle<FLatentPromise>::from_promise(*this),
		std::move(LatentInfo));
}

void FLatentPromise::Init()
{
	// This should have been an async coroutine without a LatentActionInfo
	checkf(PendingLatentCoroutine, TEXT("Internal error"));

	// Last resort if we got this far without a world
	if (!World)
	{
		World = GWorld;
		checkf(World, TEXT("Could not determine world for latent coroutine"));
	}
}

void FLatentPromise::SetCurrentAwaiter(FLatentAwaiter* Awaiter)
{
	auto* Pending = static_cast<FPendingLatentCoroutine*>(PendingLatentCoroutine);
	Pending->SetCurrentAwaiter(Awaiter);
}

FInitialSuspend FLatentPromise::initial_suspend()
{
	auto& LAM = World->GetLatentActionManager();
	auto* Pending = static_cast<FPendingLatentCoroutine*>(PendingLatentCoroutine);
	auto& LatentInfo = Pending->GetLatentInfo();

	// Don't let the coroutine run and clean up if this is a duplicate
	if (LAM.FindExistingAction<FPendingLatentCoroutine>(LatentInfo.CallbackTarget,
	                                                    LatentInfo.UUID))
		return {FInitialSuspend::Destroy};

	World->GetLatentActionManager().AddNewAction(LatentInfo.CallbackTarget,
	                                             LatentInfo.UUID, Pending);

	// Let the coroutine start immediately on its calling thread
	return {FInitialSuspend::Ready};
}

void FLatentPromise::return_void()
{
	ensureMsgf(IsInGameThread(),
	           TEXT("Latent coroutines must end on the game thread"));
	// Only this should be possible if co_returning cleanly on the game thread,
	// ~FPendingLatentCoroutine is blocked from running and Abort doesn't resume
	checkCode(
		auto State = LatentState.load();
		ensureMsgf(State == LatentRunning,
		           TEXT("Unexpected coroutine state %d"), State);
	);
	LatentState = Done;
}
