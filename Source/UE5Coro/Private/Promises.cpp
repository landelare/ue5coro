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
#include "LatentExitReason.h"
#include "UE5Coro/LatentAwaiter.h"
#include "UE5Coro/Promise.h"

using namespace UE5Coro::Private;

namespace UE5Coro::Private
{
class [[nodiscard]] FPendingLatentCoroutine final : public FPendingLatentAction
{
	// The coroutine may move to other threads, but this object only interacts
	// with its promise on the game thread.
	// Since latent promises are destroyed on the game thread, there's nothing
	// to synchronize and the lock is not used to access Extras->Promise.
	std::shared_ptr<FPromiseExtras> Extras;
	bool bTriggerLink = false;
	FLatentActionInfo LatentInfo;
	FLatentAwaiter CurrentAwaiter; // latent->latent await fast path

public:
	explicit FPendingLatentCoroutine(std::shared_ptr<FPromiseExtras> Extras,
	                                 FLatentActionInfo LatentInfo)
		: Extras(std::move(Extras)), LatentInfo(std::move(LatentInfo))
		, CurrentAwaiter(nullptr, nullptr, std::false_type()) { }

	UE_NONCOPYABLE(FPendingLatentCoroutine);

	virtual ~FPendingLatentCoroutine() override
	{
		checkf(IsInGameThread(),
		       TEXT("Unexpected latent action off the game thread"));
		if (auto* LatentPromise = static_cast<FLatentPromise*>(Extras->Promise))
			[[likely]]
		{
			LatentPromise->LatentActionDestroyed();
			// This call force-canceled the promise, and unregistered the
			// pending latent action from it.

			// If the promise was detached from the game thread, calling
			// Resume() will reattach it, and calling Resume() again will handle
			// cleanup. One call is this one, the other comes from the awaiter,
			// either the current one, or the next one if the coroutine is
			// running. There is always a next one, due to final_suspend.
			// The order doesn't matter.

			// If the promise was not detached, it must be awaiting a
			// FLatentAwaiter, otherwise it would be blocking this destructor.
			// The pending latent action will no longer tick the awaiter, so
			// Resume() must be called now to clean up.
			LatentPromise->Resume();
		}
		// CurrentAwaiter is a non-owning copy, disarm its destructor
		CurrentAwaiter.Clear();
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		checkf(IsInGameThread(),
		       TEXT("Internal error: expected game thread update"));
		auto* LatentPromise = static_cast<FLatentPromise*>(Extras->Promise);

		if (!LatentPromise) [[unlikely]]
		{
			FinishNow(Response);
			return;
		}

		if (CurrentAwaiter.IsValid() && CurrentAwaiter.ShouldResume())
		{
			CurrentAwaiter.Clear();
			// This might set the awaiter for next time
			LatentPromise->Resume();
		}

		// Resume() might have deleted LatentPromise, check it again
		if (Extras->Promise) [[likely]]
		{
			checkf(Extras->Promise == LatentPromise,
			       TEXT("Internal error: unexpected promise corruption"));
			checkf(!Extras->IsComplete(),
			       TEXT("Internal error: completed promise was not cleared"));

			// If ownership is with the game thread, check if the promise is
			// waiting to be completed
			if (LatentPromise->IsOnGameThread())
			{
				using FLatentHandle = std::coroutine_handle<FLatentPromise>;
				if (auto Handle = FLatentHandle::from_promise(*LatentPromise);
				    Handle.done() || LatentPromise->ShouldCancel(false))
					FinishNow(Response);
			}
		}
		else
			FinishNow(Response);
	}

	virtual void NotifyActionAborted() override
	{
		checkf(IsInGameThread(),
		       TEXT("Internal error: expected callback from the game thread"));
		if (auto* LatentPromise = static_cast<FLatentPromise*>(Extras->Promise))
			[[likely]]
			LatentPromise->SetExitReason(ELatentExitReason::ActionAborted);
	}

	virtual void NotifyObjectDestroyed() override
	{
		checkf(IsInGameThread(),
		       TEXT("Internal error: expected callback from the game thread"));
		if (auto* LatentPromise = static_cast<FLatentPromise*>(Extras->Promise))
			[[likely]]
			LatentPromise->SetExitReason(ELatentExitReason::ObjectDestroyed);
	}

	const FLatentActionInfo& GetLatentInfo() const { return LatentInfo; }

	void RequestLink() { bTriggerLink = true; }

	void FinishNow(FLatentResponse& Response)
	{
		if (bTriggerLink)
			Response.TriggerLink(LatentInfo.ExecutionFunction,
			                     LatentInfo.Linkage, LatentInfo.CallbackTarget);
		Response.DoneIf(true);
	}

	void SetCurrentAwaiter(const FLatentAwaiter& Awaiter)
	{
		checkf(IsInGameThread(),
		       TEXT("Latent awaiters may only be used on the game thread"));
		ensureMsgf(!CurrentAwaiter.IsValid(), TEXT("Unexpected double await"));

		CurrentAwaiter = Awaiter;
	}
};
}

bool FAsyncPromise::IsEarlyDestroy() const
{
	return ShouldCancel(false);
}

int FLatentPromise::UUID = 0;

void FLatentPromise::CreateLatentAction(const UObject* Owner)
{
	checkf(IsInGameThread(),
	       TEXT("Latent coroutines may only be started on the game thread"));
	checkf(IsValid(Owner),
	       TEXT("Attempted to start latent coroutine with invalid owner"));
	checkf(World.IsValid(),
	       TEXT("Could not determine world for latent coroutine"));

	CreateLatentAction(
		{INDEX_NONE, UUID++, TEXT("None"), const_cast<UObject*>(Owner)});
}

// This is a separate function so that template Init() doesn't need the type
void FLatentPromise::CreateLatentAction(const FLatentActionInfo& LatentInfo)
{
	// The static_assert on coroutine_traits and Init() logic prevent this
	checkf(!LatentAction,
	       TEXT("Internal error: multiple latent action creations on start"));

	// It's possible to name a UFUNCTION None, but the engine uses that as an
	// invalid/placeholder value, and it leads to all kinds of bugs.
	// Sanity check that there isn't one that might receive the fake callback.
	// This check is technically needed for the other overload, but it's in this
	// one to cover every latent coroutine equally.
	checkf(!LatentInfo.CallbackTarget->GetClass()->FindFunctionByName(NAME_None),
	       TEXT("Having a UFUNCTION named None is not supported (by Unreal)"));

	LatentAction = new FPendingLatentCoroutine(Extras, LatentInfo);
}

FLatentPromise::~FLatentPromise()
{
	checkf(IsInGameThread(),
	       TEXT("Unexpected latent coroutine destruction off the game thread"));
	GLatentExitReason = ELatentExitReason::Normal;
}

bool FLatentPromise::IsEarlyDestroy() const
{
	// Destruction can come before or after final_suspend, but the only reason
	// it can come before is a cancellation, both regular and forced
	return !(LatentFlags & LF_Successful);
}

void FLatentPromise::ThreadSafeDestroy()
{
	// Latent coroutines always end on the game thread by definition
	if (!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [this] { ThreadSafeDestroy(); });
		return;
	}

	// Since we're on the game thread now, there's no possibility of a race with
	// ~FPendingLatentCoroutine requesting another deletion
	GLatentExitReason = ExitReason;
	FPromise::ThreadSafeDestroy(); // Counts as delete this;
	checkf(GLatentExitReason == ELatentExitReason::Normal,
	       TEXT("Internal error: latent exit reason not restored"));
}

void FLatentPromise::Resume()
{
	// Is the latent action gone, but ownership extended?
	// In this case, another Resume() call is guaranteed to arrive later.
	// Re-attach the coroutine now, and handle destruction in the next Resume().
	// This will usually be the call from ~FPendingLatentCoroutine, but it
	// could arrive second.
	if (!LatentAction && LatentFlags.fetch_and(~LF_Detached) & LF_Detached)
		[[unlikely]]
		return;

	// In the more common case, if the coroutine is resuming on the game thread,
	// return ownership to the latent action manager.
	// In this case, there's no possible race with ~FPendingLatentCoroutine.
	if (LatentFlags & LF_Detached && IsInGameThread())
		AttachToGameThread();

	// Still being detached suggests that the latent coroutine is co_awaiting
	// two async awaiters back to back. This is OK; lifetime remains extended.

	// Not having a latent action bypasses cancellation holds, and
	// ThreadSafeDestroy will marshal the destruction back to the game thread
	ResumeInternal(!LatentAction);
}

void FLatentPromise::LatentActionDestroyed()
{
	UE::TUniqueLock Lock(Extras->Lock);
	verifyf(std::exchange(LatentAction, nullptr),
	        TEXT("Internal error: double latent action destruction"));
	Cancel(true);
	checkf(ShouldCancel(true),
	       TEXT("Internal error: forced cancellation not received"));
}

void FLatentPromise::CancelFromWithin()
{
	// Force move the coroutine back to the game thread
	AttachToGameThread(true);

	{
		UE::TUniqueLock Lock(Extras->Lock);
		Cancel(false);
		checkf(ShouldCancel(false),
		       TEXT("Coroutines may only be canceled from within if no "
		            "FCancellationGuards are active"));
	}

	// If the self-cancellation arrived on the game thread, don't wait for the
	// next FPendingLatentCoroutine tick to start cleaning up
	if (IsInGameThread())
		ThreadSafeDestroy();
}

void FLatentPromise::AttachToGameThread(bool bFromAnyThread)
{
	checkf(bFromAnyThread || IsInGameThread(),
	       TEXT("Internal error: expected to be on the game thread"));
	checkf([this]
	{
		UE::TUniqueLock Lock(Extras->Lock);
		return !CancelableAwaiter;
	}(), TEXT("Internal error: cannot reattach with a registered awaiter"));

	LatentFlags &= ~LF_Detached;
}

/** Calling this method "pins" the promise and coroutine state, deferring any
 *  destruction requests from the latent action manager.
 *  This is useful for threading or callback-based awaiters to ensure that
 *  there will be a valid promise and coroutine state to return to.
 *  FLatentAwaiters use a dedicated code path and do not call this, as they
 *  support destruction on game thread Tick while being co_awaited. */
void FLatentPromise::DetachFromGameThread()
{
	// Multiple detachments in a row are OK, but the first one must be on the GT
	checkf(LatentFlags & LF_Detached || IsInGameThread(),
	       TEXT("Internal error: expected first detachment on the GT"));

	LatentFlags |= LF_Detached;
}

bool FLatentPromise::IsOnGameThread() const
{
	return !(LatentFlags & LF_Detached);
}

void FLatentPromise::SetExitReason(ELatentExitReason Reason)
{
	checkf(ExitReason == ELatentExitReason::Normal,
	       TEXT("Internal error: setting conflicting exit reasons"));
	ExitReason = Reason;
}

void FLatentPromise::SetCurrentAwaiter(const FLatentAwaiter& Awaiter)
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	// How is a new latent awaiter getting added in these states?
	checkf(LatentFlags == 0,
	       TEXT("Internal error: unexpected state in latent coroutine"));
	checkCode(
		UE::TUniqueLock Lock(Extras->Lock);
		checkf(LatentAction,
		       TEXT("Internal error: unexpected awaiter without latent action"));
	);

	auto* Pending = static_cast<FPendingLatentCoroutine*>(LatentAction);
	Pending->SetCurrentAwaiter(Awaiter);
}

FInitialSuspend FLatentPromise::initial_suspend()
{
	checkf(IsInGameThread(),
	       TEXT("Latent coroutines may only be started on the game thread"));
	checkf(World.IsValid(),
	       TEXT("Internal error: latent coroutine starts in invalid/stale world"));

	auto* Pending = static_cast<FPendingLatentCoroutine*>(LatentAction);
	auto& LatentInfo = Pending->GetLatentInfo();
	auto* Target = LatentInfo.CallbackTarget.Get();
	// Expecting the same world that was determined in the constructor
	checkf(IsValid(Target) &&
	       (Target->IsTemplate() || Target->GetWorld() == World.Get()),
	       TEXT("Internal error: coroutine suspending in invalid state"));
	auto& LAM = World->GetLatentActionManager();

	// Don't let the coroutine run and clean up if this is a duplicate
	if (LAM.FindExistingAction<FPendingLatentCoroutine>(Target, LatentInfo.UUID))
		return {FInitialSuspend::Destroy};

	// Also refuse to run if there's no callback target
	if (!ensureMsgf(IsValid(LatentInfo.CallbackTarget),
	                TEXT("Not starting latent coroutine with invalid target")))
		return {FInitialSuspend::Destroy};

	// Make the latent action owned by the context instead of the callback
	// target to provide a better match for the "latent `this` protection"
	// offered by FLatentActionManager.
	// These will usually be the same object for latent UFUNCTIONs, but
	// FForceLatentCoroutine uses UUE5CoroSubsystem as a helper callback target.
	LAM.AddNewAction(Target, LatentInfo.UUID, Pending);

	// Let the coroutine start immediately on its calling thread
	return {FInitialSuspend::Resume};
}

FLatentFinalSuspend FLatentPromise::final_suspend() noexcept
{
	UE::TUniqueLock Lock(Extras->Lock); // Block incoming destruction requests
	bool bDestroy;

	// It is possible that the latent action was deleted somewhere between
	// the last co_await and final_suspend if the coroutine was running off
	// the game thread.
	if (auto* Pending = static_cast<FPendingLatentCoroutine*>(LatentAction))
	{
		Pending->RequestLink(); // Nothing to cancel anymore, link back to BP

		// Self-destruct instantly if on the game thread, so that
		// ContinueWith runs in this tick, not the next one.
		// Otherwise, FPendingLatentCoroutine will see
		// coroutine_handle::done() and trigger cleanup on the next tick.
		bDestroy = IsInGameThread();
	}
	else
	{
		checkf(LatentFlags & LF_Detached, // How can this happen on the GT?
		       TEXT("Internal error: unexpected state without latent action"));
		bDestroy = true; // The latent action manager no longer owns this
	}

	// Flags are overwritten, i.e., the coroutine is unconditionally reattached
	LatentFlags = LF_Successful;

	// Release the lock, then process the flag in await_suspend
	return {bDestroy};
}
