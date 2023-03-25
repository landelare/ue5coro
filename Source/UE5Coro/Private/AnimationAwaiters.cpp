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

#include "UE5Coro/AnimationAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

FAnimAwaiterBool Anim::MontageBlendingOut(UAnimInstance* Instance,
                                          UAnimMontage* Montage)
{
	return {std::false_type(), Instance, Montage};
}

FAnimAwaiterBool Anim::MontageEnded(UAnimInstance* Instance,
                                    UAnimMontage* Montage)
{
	return {std::true_type(), Instance, Montage};
}

FAnimAwaiterVoid Anim::NextNotify(UAnimInstance* Instance, FName NotifyName)
{
	return {std::monostate(), Instance, nullptr, NotifyName};
}

FAnimAwaiterTuple Anim::PlayMontageNotifyBegin(UAnimInstance* Instance,
                                               UAnimMontage* Montage)
{
	return {std::false_type(), Instance, Montage};
}

FAnimAwaiterTuple Anim::PlayMontageNotifyEnd(UAnimInstance* Instance,
                                             UAnimMontage* Montage)
{
	return {std::true_type(), Instance, Montage};
}

FAnimAwaiterPayload Anim::PlayMontageNotifyBegin(UAnimInstance* Instance,
                                                 UAnimMontage* Montage,
                                                 FName NotifyName)
{
	return {std::false_type(), Instance, Montage, NotifyName};
}

FAnimAwaiterPayload Anim::PlayMontageNotifyEnd(UAnimInstance* Instance,
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

	auto* Obj = NewObject<UUE5CoroAnimCallbackTarget>();
	Target = TStrongObjectPtr(Obj);
}

FAnimAwaiter::~FAnimAwaiter()
{
	checkf(IsInGameThread(),
	       TEXT("Unexpected anim awaiter destruction off the game thread"));
	if (UNLIKELY(bSuspended))
		Target->CancelResume();
}

void FAnimAwaiter::Suspend(FPromise& Promise)
{
	bSuspended = true;
	Target->RequestResume(Promise);
}

template<typename T>
template<typename TEnd>
TAnimAwaiter<T>::TAnimAwaiter(TEnd, UAnimInstance* Instance,
                              UAnimMontage* Montage)
	: FAnimAwaiter(Instance, Montage)
{
	if constexpr (Type == Bool)
		Target->ListenForMontageEvent(Instance, Montage, TEnd::value);
	else
	{
		static_assert(Type == NameAndPayload);
		Target->ListenForPlayMontageNotify(Instance, Montage, {}, TEnd::value);
	}
}

template<typename T>
template<typename TEnd>
TAnimAwaiter<T>::TAnimAwaiter(TEnd, UAnimInstance* Instance,
                              UAnimMontage* Montage, FName NotifyName)
	: FAnimAwaiter(Instance, Montage)
{
	if constexpr (Type == Void)
	{
		static_assert(std::is_same_v<TEnd, std::monostate>);
		Target->ListenForNotify(Instance, Montage, NotifyName);
	}
	else
	{
		static_assert(Type == Payload);
		Target->ListenForPlayMontageNotify(Instance, Montage, NotifyName,
		                                   TEnd::value);
	}
}

template<typename T>
TAnimAwaiter<T>::~TAnimAwaiter() = default;

template<typename T>
bool TAnimAwaiter<T>::await_ready()
{
	checkf(IsInGameThread(),
	       TEXT("Animation awaiters may only be used on the game thread"));
	auto* Obj = Target.Get();
	if (!std::holds_alternative<std::monostate>(Obj->Result))
	{
		// If Result is or contains a payload pointer, that has expired by now
		if constexpr (Type == Payload)
			std::get<FPayloadPtr>(Obj->Result) = nullptr;
		else if constexpr (Type == NameAndPayload)
			std::get<FPayloadTuple>(Obj->Result).Value = nullptr;
		return true;
	}
	return false;
}

template<typename T>
auto TAnimAwaiter<T>::await_resume()
	-> std::conditional_t<Type == Void, void, T>
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
	// Usually, the caller is also getting destroyed and the stack will be
	// unwound (by coroutine_handle::destroy()) instead of resuming.
	auto& Result = Target->Result;
	bool bDestroyed = std::holds_alternative<std::monostate>(Result);

	if constexpr (Type == Bool)
		return std::get<bool>(bDestroyed ? true : Result);
	else if constexpr (Type == Payload)
		return bDestroyed ? nullptr : std::get<FPayloadPtr>(Result);
	else if constexpr (Type == NameAndPayload)
		return bDestroyed ? FPayloadTuple(NAME_None, nullptr)
		                  : std::get<FPayloadTuple>(Result);
}

namespace UE5Coro::Private
{
template class TAnimAwaiter<std::monostate>;
template class TAnimAwaiter<bool>;
template class TAnimAwaiter<FPayloadPtr>;
template class TAnimAwaiter<FPayloadTuple>;
}
