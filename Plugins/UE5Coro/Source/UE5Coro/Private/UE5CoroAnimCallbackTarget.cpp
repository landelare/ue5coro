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

#include "UE5Coro/UE5CoroAnimCallbackTarget.h"
#include "UE5Coro/AnimationAwaiters.h"

using namespace UE5Coro::Private;

namespace
{
using FPrivateMapPtr = TMap<FName, FSimpleMulticastDelegate> UAnimInstance::*;

FPrivateMapPtr GExternalNotifyHandlersPtr;

template<FPrivateMapPtr Ptr>
class TPrivateSpy
{
	TPrivateSpy() { GExternalNotifyHandlersPtr = Ptr; }
	static TPrivateSpy Instance; // MSVC doesn't compile this as inline
};

template<FPrivateMapPtr Ptr>
TPrivateSpy<Ptr> TPrivateSpy<Ptr>::Instance;

template class TPrivateSpy<&UAnimInstance::ExternalNotifyHandlers>;
}

void UUE5CoroAnimCallbackTarget::TryResume()
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: attempting to resume from wrong thread"));
	if (Promise) // Is there anything suspended?
	{
		WeakInstance = nullptr; // Stop watching the instance
		std::exchange(Promise, nullptr)->Resume(); // Resume exactly once
	}
}

void UUE5CoroAnimCallbackTarget::ListenForMontageEvent(UAnimInstance* Instance,
                                                       UAnimMontage* Montage,
                                                       bool bEnd)
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: animation montage event received outside GT"));
	checkf(Instance,
	       TEXT("Internal error: anim montage event without anim instance"));
	WeakInstance = Instance;
	auto Callback = FOnMontageEnded::CreateUObject(
		this, &ThisClass::MontageCallbackBool);
	if (bEnd)
		Instance->Montage_SetEndDelegate(Callback, Montage);
	else
		Instance->Montage_SetBlendingOutDelegate(Callback, Montage);
}

void UUE5CoroAnimCallbackTarget::ListenForNotify(UAnimInstance* Instance,
                                                 UAnimMontage* Montage,
                                                 FName NotifyName)
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: notify event received outside GT"));
	checkf(Instance,
	       TEXT("Internal error: anim montage event without anim instance"));
	checkf(GExternalNotifyHandlersPtr,
	       TEXT("Internal error: anim instance spy failed"));
	WeakInstance = Instance;

	// UAnimInstance::AddExternalNotifyHandler() ties the notify name and the
	// called UFUNCTION's name together. :(
	auto& ExternalNotifyHandlers = Instance->*GExternalNotifyHandlersPtr;
	FName HandlerName = *(TEXT("AnimNotify_") + NotifyName.ToString());
	auto& Delegate = ExternalNotifyHandlers.FindOrAdd(HandlerName);
	Delegate.AddUObject(this, &ThisClass::NotifyCallback);
}

void UUE5CoroAnimCallbackTarget::ListenForPlayMontageNotify(
	UAnimInstance* Instance, UAnimMontage* Montage,
	std::optional<FName> NotifyName, bool bEnd)
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: play montage event received outside GT"));
	checkf(Instance,
	       TEXT("Internal error: play montage event without anim instance"));
	checkf(MontageIDFilter == INDEX_NONE && !NotifyFilter.has_value(),
	       TEXT("Internal error: montage filter already set up"));
	WeakInstance = Instance;

	if (auto* MontageInstance = Instance->GetActiveInstanceForMontage(Montage))
		MontageIDFilter = MontageInstance->GetInstanceID();
	NotifyFilter = NotifyName;

	(bEnd ? Instance->OnPlayMontageNotifyEnd
	      : Instance->OnPlayMontageNotifyBegin)
		.AddDynamic(this, &ThisClass::MontageCallbackNameAndPayload);
}

void UUE5CoroAnimCallbackTarget::RequestResume(FPromise& InPromise)
{
	checkf(!Promise, TEXT("Attempted second concurrent co_await"));
	// await_ready should've prevented suspension if there's already a result
	checkf(IsInGameThread(), TEXT("Internal error: suspending on wrong thread"));
	checkf(std::holds_alternative<std::monostate>(Result),
	       TEXT("Internal error: reused callback target"));
	Promise = &InPromise;
}

void UUE5CoroAnimCallbackTarget::CancelResume()
{
	checkf(IsInGameThread(), TEXT("Internal error: canceling on wrong thread"));
	// Promise can be nullptr already if this is a deferred destruction
	Promise = nullptr;
}

void UUE5CoroAnimCallbackTarget::MontageCallbackBool(UAnimMontage* Montage,
                                                     bool bInterrupted)
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: expected montage callback on game thread"));

	Result = bInterrupted;
	TryResume();
}

void UUE5CoroAnimCallbackTarget::MontageCallbackNameAndPayload(
	FName NotifyName, const FBranchingPointNotifyPayload& Payload)
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: expected montage callback on game thread"));

	// Apply filters
	if (MontageIDFilter != INDEX_NONE &&
	    Payload.MontageInstanceID != MontageIDFilter)
		return;
	if (NotifyFilter.has_value() && *NotifyFilter != NotifyName)
		return;

	// This callback passed all filters. Store the result, then resume.
	if (NotifyFilter.has_value())
		Result = &Payload;
	else
		Result = FPayloadTuple(NotifyName, &Payload);
	TryResume();
}

void UUE5CoroAnimCallbackTarget::NotifyCallback()
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: expected notify callback on game thread"));

	Result = true; // This is for the void awaiter
	TryResume();
}

ETickableTickType UUE5CoroAnimCallbackTarget::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}

void UUE5CoroAnimCallbackTarget::Tick(float DeltaTime)
{
	// Keep trying to resume on tick, because it's possible that an animation
	// awaiter exists but it will only get co_awaited in the future.
	// A successful resume will explicitly null WeakInstance.
	// TAnimAwaiter is dealing with unexpected/early resumptions from this
	// code path, mainly by checking if Result is still std::monostate.
	if (WeakInstance.IsStale())
		TryResume();
}

TStatId UUE5CoroAnimCallbackTarget::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UUE5CoroAnimCallbackTarget,
	                                STATGROUP_Tickables);
}
