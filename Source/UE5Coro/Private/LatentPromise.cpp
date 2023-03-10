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

#include <exception>
#include "LatentActions.h"
#include "LatentExitReason.h"
#include "UE5Coro/AsyncAwaiters.h"
#include "UE5Coro/AsyncCoroutine.h"
#include "UE5Coro/LatentAwaiters.h"

using namespace UE5Coro::Private;

namespace
{
class [[nodiscard]] FPendingLatentCoroutine : public FPendingLatentAction
{
	// The coroutine may move to other threads, but this object only interacts
	// with it on the game thread.
	FLatentPromise* Promise;
	FLatentActionInfo LatentInfo;
	FLatentAwaiter* CurrentAwaiter = nullptr;

public:
	explicit FPendingLatentCoroutine(FLatentPromise& Promise,
	                                 FLatentActionInfo LatentInfo)
		: Promise(&Promise), LatentInfo(LatentInfo) { }

	UE_NONCOPYABLE(FPendingLatentCoroutine);

	virtual ~FPendingLatentCoroutine() override
	{
		checkf(IsInGameThread(),
		       TEXT("Unexpected latent action off the game thread"));
		if (LIKELY(Promise))
			Promise->ThreadSafeDestroy();
	}

#if !PLATFORM_EXCEPTIONS_DISABLED
	/** Called in ~FLatentPromise if it was automatically called due to an
	 *  uncaught exception, to prevent a second destruction from the LAM. */
	void Detach()
	{
		if (IsInGameThread())
		{
			checkf(Promise, TEXT("Internal error"));
			Promise = nullptr;
		}
		else
		{
			// Promise (the pointer) is not thread safe, so perform everything
			// on the game thread and block this thread until it's done.
			// Performance is not a concern, this only happens with an uncaught
			// exception to begin with.
			FEventRef Done;
			AsyncTask(ENamedThreads::GameThread, [&]
			{
				Detach();
				Done->Trigger();
			});
			Done->Wait();
		}
	}
#endif

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		checkf(IsInGameThread(), TEXT("Internal error"));
		if (UNLIKELY(!Promise))
		{
			Response.DoneIf(true);
			return;
		}

		if (CurrentAwaiter && CurrentAwaiter->ShouldResume())
		{
			CurrentAwaiter = nullptr;
			// This might set the awaiter for next time
			Promise->Resume();
		}

		// Did the coroutine finish?
		auto State = Promise->GetLatentState();
		if (State >= FLatentPromise::Canceled)
			Response.DoneIf(true);
		if (State == FLatentPromise::Done)
			Response.TriggerLink(LatentInfo.ExecutionFunction, LatentInfo.Linkage,
			                     LatentInfo.CallbackTarget);
	}

	virtual void NotifyActionAborted() override
	{
		checkf(IsInGameThread(), TEXT("Internal error"));
		if (LIKELY(Promise))
			Promise->SetExitReason(ELatentExitReason::ActionAborted);
	}

	virtual void NotifyObjectDestroyed() override
	{
		checkf(IsInGameThread(), TEXT("Internal error"));
		if (LIKELY(Promise))
			Promise->SetExitReason(ELatentExitReason::ObjectDestroyed);
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

void FLatentPromise::CreateLatentAction()
{
	// We're still scanning for the world, so use what we have right now
	auto* WorldNow = World ? World : GWorld;
	auto* Sys = WorldNow->GetSubsystem<UUE5CoroSubsystem>();
	CreateLatentAction(Sys->MakeLatentInfo());
}

// This is a separate function so that template Init() doesn't need the type
void FLatentPromise::CreateLatentAction(FLatentActionInfo&& LatentInfo)
{
	// The static_assert on coroutine_traits prevents this
	checkf(!PendingLatentCoroutine, TEXT("Internal error"));

	PendingLatentCoroutine = new FPendingLatentCoroutine(*this, LatentInfo);
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

FLatentPromise::~FLatentPromise()
{
	checkf(IsInGameThread(),
	       TEXT("Unexpected latent coroutine destruction off the game thread"));
	GLatentExitReason = ELatentExitReason::Normal;
#if !PLATFORM_EXCEPTIONS_DISABLED
	if (UNLIKELY(std::uncaught_exceptions()))
		// Destroyed early. Prevent the normal destruction from the world's LAM.
		static_cast<FPendingLatentCoroutine*>(PendingLatentCoroutine)->Detach();
#endif
}

void FLatentPromise::Resume()
{
	// Return to latent running on the game thread, even if it's an async task.
	bool bIsInGameThread = IsInGameThread();
	if (bIsInGameThread)
		AttachToGameThread();

	// Was there a deferred deletion request?
	if (UNLIKELY(LatentState == DeferredDestroy))
	{
		// Finish on the game thread: exit reason, destructors, etc.
		if (bIsInGameThread)
			ThreadSafeDestroy();
		else
			AsyncTask(ENamedThreads::GameThread,
			          std::bind(&FLatentPromise::ThreadSafeDestroy, this));
	}
	else
		// If this promise is async running, we're committed to a resumption at
		// this point (DeferredDestroy arrived since the if above).
		// Deletion requests will end up in the next call to Resume
		// since the coroutine must return to the game thread in a future
		// co_await.

		// If this is the game thread, ~FPendingLatentCoroutine cannot run and
		// the coroutine will either co_await or return_void.

		// Therefore, this can safely run on any thread now.
		FPromise::Resume();
}

void FLatentPromise::ThreadSafeDestroy()
{
	// If the coroutine is async running, request destruction from the awaiter.
	if (auto Old = AsyncRunning;
		LatentState.compare_exchange_strong(Old, DeferredDestroy))
		return;

	checkf(IsInGameThread(),
	       TEXT("Unexpected latent coroutine destruction off the game thread"));
	auto Handle = stdcoro::coroutine_handle<FLatentPromise>::from_promise(*this);

	GLatentExitReason = ExitReason;
	Handle.destroy(); // Counts as delete this;
	checkf(GLatentExitReason == ELatentExitReason::Normal,
	       TEXT("Internal error"));
}

void FLatentPromise::AttachToGameThread()
{
	// This might fail to exchange if State == DeferredDestroy which is OK
	auto Old = AsyncRunning;
	LatentState.compare_exchange_strong(Old, LatentRunning);
	checkCode(
		auto State = LatentState.load();
		checkf(State == FLatentPromise::LatentRunning ||
		       State == FLatentPromise::DeferredDestroy,
		       TEXT("Unexpected state when returning to game thread"));
	);
}

void FLatentPromise::DetachFromGameThread()
{
	// Calling this method "pins" the promise and coroutine state, deferring any
	// destruction requests from the latent action manager.
	// This is useful for threading or callback-based awaiters to ensure that
	// there will be a valid promise and coroutine state to return to.
	// FLatentAwaiters use a dedicated code path and do not call this, as they
	// support destruction while being co_awaited.

	if (auto Old = LatentRunning;
		LatentState.compare_exchange_strong(Old, AsyncRunning) ||
		Old == AsyncRunning || Old == DeferredDestroy)
		; // Done
	else
		checkf(false, TEXT("Unexpected latent coroutine state %d"), Old);
}

void FLatentPromise::LatentCancel()
{
	checkCode(
		auto CurrentState = LatentState.load();
		ensureMsgf(CurrentState == FLatentPromise::LatentRunning,
		           TEXT("Unexpected latent coroutine state %d"), CurrentState);
	);
	LatentState = Canceled;
}

void FLatentPromise::SetExitReason(ELatentExitReason Reason)
{
	checkf(ExitReason == ELatentExitReason::Normal, TEXT("Internal error"));
	ExitReason = Reason;
}

void FLatentPromise::SetCurrentAwaiter(FLatentAwaiter* Awaiter)
{
	auto* Pending = static_cast<FPendingLatentCoroutine*>(PendingLatentCoroutine);
	Pending->SetCurrentAwaiter(Awaiter);
}

FInitialSuspend FLatentPromise::initial_suspend()
{
	checkf(IsInGameThread(),
	       TEXT("Latent coroutines may only be started on the game thread"));

	auto& LAM = World->GetLatentActionManager();
	auto* Pending = static_cast<FPendingLatentCoroutine*>(PendingLatentCoroutine);
	auto& LatentInfo = Pending->GetLatentInfo();

	// Don't let the coroutine run and clean up if this is a duplicate
	if (LAM.FindExistingAction<FPendingLatentCoroutine>(LatentInfo.CallbackTarget,
	                                                    LatentInfo.UUID))
		return {FInitialSuspend::Destroy};

	// Also refuse to run if there's no callback target
	if (!ensureMsgf(IsValid(LatentInfo.CallbackTarget),
	                TEXT("Not starting latent coroutine with invalid target")))
		return {FInitialSuspend::Destroy};

	LAM.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, Pending);

	// Let the coroutine start immediately on its calling thread
	return {FInitialSuspend::Resume};
}

stdcoro::suspend_always FLatentPromise::final_suspend() noexcept
{
	ensureMsgf(IsInGameThread(),
	           TEXT("Latent coroutines must end on the game thread"));
	// Only this should be possible if co_returning cleanly on the game thread,
	// ~FPendingLatentCoroutine is blocked from running and Latent::Cancel()
	// doesn't resume.
	checkCode(
		auto State = LatentState.load();
		ensureMsgf(State == LatentRunning,
		           TEXT("Unexpected coroutine state %d"), State);
	);
	LatentState = Done;
	return {};
}
