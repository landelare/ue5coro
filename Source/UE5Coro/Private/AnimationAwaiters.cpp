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

#include "UE5Coro/AnimationAwaiter.h"
#include "UE5CoroAnimCallbackTarget.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
using FPayloadPtr = const FBranchingPointNotifyPayload*;
using FPayloadTuple = TTuple<FName, const FBranchingPointNotifyPayload*>;
}

TAnimAwaiter<bool> Anim::MontageBlendingOut(UAnimInstance* Instance,
                                          UAnimMontage* Montage)
{
	return {std::false_type(), Instance, Montage};
}

TAnimAwaiter<bool> Anim::MontageEnded(UAnimInstance* Instance,
                                    UAnimMontage* Montage)
{
	return {std::true_type(), Instance, Montage};
}

TAnimAwaiter<void> Anim::NextNotify(UAnimInstance* Instance, FName NotifyName)
{
	return {std::monostate(), Instance, nullptr, NotifyName};
}

TAnimAwaiter<FPayloadTuple> Anim::PlayMontageNotifyBegin(UAnimInstance* Instance,
                                                         UAnimMontage* Montage)
{
	return {std::false_type(), Instance, Montage};
}

TAnimAwaiter<FPayloadTuple> Anim::PlayMontageNotifyEnd(UAnimInstance* Instance,
                                                       UAnimMontage* Montage)
{
	return {std::true_type(), Instance, Montage};
}

TAnimAwaiter<FPayloadPtr> Anim::PlayMontageNotifyBegin(UAnimInstance* Instance,
                                                       UAnimMontage* Montage,
                                                       FName NotifyName)
{
	return {std::false_type(), Instance, Montage, NotifyName};
}

TAnimAwaiter<FPayloadPtr> Anim::PlayMontageNotifyEnd(UAnimInstance* Instance,
                                                     UAnimMontage* Montage,
                                                     FName NotifyName)
{
	return {std::true_type(), Instance, Montage, NotifyName};
}

FAnimAwaiter::FAnimAwaiter(UAnimInstance* Instance, UAnimMontage*)
{
	checkf(IsInGameThread(),
	       TEXT("Animation awaiters may only be used on the game thread"));
	checkf(Instance, TEXT("Attempting to wait on a null anim instance"));
	// A null montage is valid, meaning "any montage"

	Target = TStrongObjectPtr(NewObject<UUE5CoroAnimCallbackTarget>());
}

FAnimAwaiter::~FAnimAwaiter()
{
	checkf(IsInGameThread(),
	       TEXT("Unexpected anim awaiter destruction off the game thread"));
	if (bSuspended) [[unlikely]]
		Target->CancelResume();
}

// These cannot be defaulted in the .h, they use UUE5CoroAnimCallbackTarget
FAnimAwaiter::FAnimAwaiter(const FAnimAwaiter&) = default;
FAnimAwaiter& FAnimAwaiter::operator=(const FAnimAwaiter&) = default;

void FAnimAwaiter::Suspend(FPromise& Promise)
{
	bSuspended = true;
	Target->RequestResume(Promise);
}

template<typename T>
TAnimAwaiter<T>::TAnimAwaiter(auto End, UAnimInstance* Instance,
                              UAnimMontage* Montage)
	: FAnimAwaiter(Instance, Montage)
{
	if constexpr (std::same_as<T, bool>)
		Target->ListenForMontageEvent(Instance, Montage, End.value);
	else
	{
		static_assert(std::same_as<T, FPayloadTuple>);
		Target->ListenForPlayMontageNotify(Instance, Montage, NAME_None,
		                                   End.value);
	}
}

template<typename T>
TAnimAwaiter<T>::TAnimAwaiter(auto End, UAnimInstance* Instance,
                              UAnimMontage* Montage, FName NotifyName)
	: FAnimAwaiter(Instance, Montage)
{
	if constexpr (std::is_void_v<T>)
	{
		static_assert(std::same_as<decltype(End), std::monostate>);
		Target->ListenForNotify(Instance, Montage, NotifyName);
	}
	else
	{
		static_assert(std::same_as<T, FPayloadPtr>);
		Target->ListenForPlayMontageNotify(Instance, Montage, NotifyName,
		                                   End.value);
	}
}

template<typename T>
TAnimAwaiter<T>::~TAnimAwaiter() = default;

template<typename T>
bool TAnimAwaiter<T>::await_ready()
{
	checkf(IsInGameThread(),
	       TEXT("Animation awaiters may only be used on the game thread"));
	if (auto* Obj = Target.Get();
	    !std::holds_alternative<std::monostate>(Obj->Result))
	{
		// If Result is or contains a payload pointer, that has expired by now
		if constexpr (std::same_as<T, FPayloadPtr>)
			std::get<FPayloadPtr>(Obj->Result) = nullptr;
		else if constexpr (std::same_as<T, FPayloadTuple>)
			std::get<FPayloadTuple>(Obj->Result).Value = nullptr;
		return true;
	}
	return false;
}

template<typename T>
T TAnimAwaiter<T>::await_resume()
{
	// bSuspended can be false
	checkf(Target, TEXT("Internal error: resuming without a callback target"));
	bSuspended = false;

	// The only reason we get here without a result is that the anim instance
	// was destroyed early.
	// The only clean way without to communicate this back to the caller without
	// memory management nightmares or forcing everything through a TOptional is
	// through an exception, which won't work for most UE projects.
	// Interested callers can use IsValid(Instance) after co_await to determine
	// if this happened, but this is expected to be a very rare situation.
	// Usually, the caller is also getting destroyed and the coroutine state
	// will be destroyed (by coroutine_handle::destroy()) instead of resuming.
	auto& Result = Target->Result;
	bool bDestroyed = std::holds_alternative<std::monostate>(Result);

	if constexpr (std::same_as<T, bool>)
		return bDestroyed ? true : std::get<bool>(Result);
	else if constexpr (std::same_as<T, FPayloadPtr>)
		return bDestroyed ? nullptr : std::get<FPayloadPtr>(Result);
	else if constexpr (std::same_as<T, FPayloadTuple>)
		return bDestroyed ? FPayloadTuple(NAME_None, nullptr)
		                  : std::get<FPayloadTuple>(Result);
	else static_assert(std::is_void_v<T>);
}

namespace UE5Coro::Private
{
template struct TAnimAwaiter<void>;
template struct TAnimAwaiter<bool>;
template struct TAnimAwaiter<FPayloadPtr>;
template struct TAnimAwaiter<FPayloadTuple>;
}
