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

#include "UE5Coro/LatentAwaiter.h"
#include "LatentActions.h"
#include "UE5Coro/Promise.h"
#include "UE5Coro/UE5CoroSubsystem.h"

using namespace UE5Coro::Private;

namespace UE5Coro::Private
{
class [[nodiscard]] FPendingAsyncCoroutine final : public FPendingLatentAction
{
	FAsyncPromise* Promise;
	FLatentAwaiter Awaiter;

public:
	FPendingAsyncCoroutine(FAsyncPromise& Promise,
	                       const FLatentAwaiter& InAwaiter)
		: Promise(&Promise), Awaiter(InAwaiter)
	{
	}

	UE_NONCOPYABLE(FPendingAsyncCoroutine);

	virtual ~FPendingAsyncCoroutine() override
	{
		Awaiter.Clear(); // This is a non-owning copy, disarm its destructor
		if (!Promise)
			return;
		// This class doesn't own the coroutine (its Latent counterpart does),
		// no need for special forced cancellation to propagate destruction
		{
			UE::TUniqueLock Lock(Promise->GetLock());
			Promise->Cancel(false);
		}
		Promise->Resume(); // The latent action ended, which is a kind of result
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		checkf(Promise, TEXT("Internal error: update on null promise"));

		// React to cancellations and the awaiter completing
		if (Promise->ShouldCancel(false) || Awaiter.ShouldResume())
		{
			Response.DoneIf(true);

			// Ownership moves back to the coroutine itself
			std::exchange(Promise, nullptr)->Resume();
		}
	}
};
}

FLatentAwaiter::FLatentAwaiter(void* State, bool (*Resume)(void*, bool),
                               auto WorldSensitive) noexcept(!UE5CORO_DEBUG)
	: State(State), Resume(Resume)
#if UE5CORO_DEBUG
	, OriginalWorld(WorldSensitive.value ? GWorld : nullptr)
#endif
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be created on the game thread"));
}
template UE5CORO_API FLatentAwaiter::FLatentAwaiter(
	void*, bool (*)(void*, bool), std::false_type) noexcept(!UE5CORO_DEBUG);
template UE5CORO_API FLatentAwaiter::FLatentAwaiter(
	void*, bool (*)(void*, bool), std::true_type) noexcept(!UE5CORO_DEBUG);

FLatentAwaiter::FLatentAwaiter(FLatentAwaiter&& Other) noexcept
	: State(std::exchange(Other.State, nullptr))
	, Resume(std::exchange(Other.Resume, nullptr))
#if UE5CORO_DEBUG
	, OriginalWorld(Other.OriginalWorld)
#endif
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be moved on the game thread"));
}

FLatentAwaiter::~FLatentAwaiter()
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be destroyed on the game thread"));
	if (Resume) [[likely]]
		(*Resume)(State, true);
#if UE5CORO_DEBUG
	State = reinterpret_cast<void*>(0xEEEEEEEEEEEEEEEE);
	Resume = reinterpret_cast<bool (*)(void*, bool)>(0xEEEEEEEEEEEEEEEE);
	OriginalWorld = reinterpret_cast<UWorld*>(0xEEEEEEEEEEEEEEEE);
#endif
}

bool FLatentAwaiter::ShouldResume()
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	checkf(State, TEXT("Attempting to poll invalid latent awaiter"));
#if UE5CORO_DEBUG
	// If you hit this ensure, the awaiter will probably misbehave.
	// Use an async awaiter instead (if possible), or ensure that the co_await
	// finishes before changing worlds by, e.g., canceling its coroutine.
	ensureMsgf(!OriginalWorld || OriginalWorld == GWorld,
	           TEXT("World changed since awaiter creation"));
#endif
	return (*Resume)(State, false);
}

void FLatentAwaiter::Suspend(FAsyncPromise& Promise)
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	checkf(::IsValid(GWorld),
	       TEXT("Awaiting this can only be done in the context of a valid world"));

	// Prepare a latent action on the subsystem and transfer ownership to that
	auto* Sys = GWorld->GetSubsystem<UUE5CoroSubsystem>();
	auto* Latent = new FPendingAsyncCoroutine(Promise, *this);
	auto LatentInfo = Sys->MakeLatentInfo();
	GWorld->GetLatentActionManager().AddNewAction(LatentInfo.CallbackTarget,
	                                              LatentInfo.UUID, Latent);
}

void FLatentAwaiter::Suspend(FLatentPromise& Promise)
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	Promise.SetCurrentAwaiter(*this);
}
