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

#pragma once

#include "CoreMinimal.h"
#include "UE5Coro/Definitions.h"
#include "UE5Coro/UE5CoroAnimCallbackTarget.h"

namespace UE5Coro::Private
{
using FPayloadPtr = const FBranchingPointNotifyPayload*;
using FPayloadTuple = TTuple<FName, const FBranchingPointNotifyPayload*>;
template<typename> class TAnimAwaiter;
using FAnimAwaiterVoid = TAnimAwaiter<std::monostate>;
using FAnimAwaiterBool = TAnimAwaiter<bool>;
using FAnimAwaiterPayload = TAnimAwaiter<FPayloadPtr>;
using FAnimAwaiterTuple = TAnimAwaiter<FPayloadTuple>;
}

namespace UE5Coro::Anim
{
/** Waits for the provided montage's current instance to blend out on the given
 *  anim instance.<br>
 *  The result of the co_await expression is true if this was caused by an
 *  interruption, false otherwise.<br>
 *  The return value of this function is copyable but only one copy may be
 *  co_awaited at the same time.<br>
 *  The anim instance getting destroyed early counts as an interruption.
 *  Use IsValid(Instance) to handle this separately, if desired.
 *  @see FOnMontageBlendingOutStarted */
UE5CORO_API Private::FAnimAwaiterBool MontageBlendingOut(UAnimInstance* Instance,
                                                         UAnimMontage* Montage);

/** Waits for the provided montage's current instance to end on the given
 *  anim instance.<br>
 *  The result of the co_await expression is true if this was caused by an
 *  interruption, false otherwise.<br>
 *  The return value of this function is copyable but only one copy may be
 *  co_awaited at the same time.<br>
 *  The anim instance getting destroyed early counts as an interruption.
 *  Use IsValid(Instance) to handle this separately, if desired.
 *  @see FOnMontageEnded */
UE5CORO_API Private::FAnimAwaiterBool MontageEnded(UAnimInstance* Instance,
                                                   UAnimMontage* Montage);

/** Waits for the anim notify to happen on the provided anim instance.<br>
 *  The anim instance getting destroyed early counts as the notify having
 *  happened.<br>
 *  The return value of this function is copyable but only one copy may be
 *  co_awaited at the same time.<br>
 *  Use IsValid(Instance) after the co_await to detect this, if desired. */
UE5CORO_API Private::FAnimAwaiterVoid NextNotify(UAnimInstance* Instance,
                                                 FName NotifyName);

/** Waits for any PlayMontageNotify or PlayMontageNotifyWindow to begin on the
 *  montage's currently-playing instance.<br>
 *  The result of co_await is a TTuple of the name of the montage that began
 *  along with a pointer to its notify payload.<br>
 *  The return value of this function is copyable but only one copy may be
 *  co_awaited at the same time.<br>
 *  The payload pointer is only valid until the next co_await, and it might be
 *  nullptr in case the notify happened before co_awaiting the returned value of
 *  this function or (rarely) if the anim instance got destroyed.
 *  Use IsValid(AnimInstance) if you need to handle this separately.
 *  @see FPlayMontageAnimNotifyDelegate */
UE5CORO_API auto PlayMontageNotifyBegin(UAnimInstance* Instance,
                                        UAnimMontage* Montage)
	-> Private::FAnimAwaiterTuple;

/** Waits for any PlayMontageNotify or PlayMontageNotifyWindow to end on the
 *  montage's currently-playing instance.<br>
 *  The result of co_await is a TTuple of the name of the montage that ended
 *  along with a pointer to its notify payload.<br>
 *  The return value of this function is copyable but only one copy may be
 *  co_awaited at the same time.<br>
 *  The payload pointer is only valid until the next co_await, and it might be
 *  nullptr in case the notify happened before co_awaiting the returned value of
 *  this function or (rarely) if the anim instance got destroyed.
 *  Use IsValid(AnimInstance) if you need to handle this separately.
 *  @see FPlayMontageAnimNotifyDelegate */
UE5CORO_API auto PlayMontageNotifyEnd(UAnimInstance* Instance,
                                      UAnimMontage* Montage)
	-> Private::FAnimAwaiterTuple;

/** Waits for the PlayMontageNotify or PlayMontageNotifyWindow of the given name
 *  to begin on the montage's currently-playing instance.<br>
 *  The result of co_await is a pointer to the notify payload.<br>
 *  The return value of this function is copyable but only one copy may be
 *  co_awaited at the same time.<br>
 *  The payload pointer is only valid until the next co_await, and it might be
 *  nullptr in case the notify happened before co_awaiting the returned value of
 *  this function or (rarely) if the anim instance got destroyed.
 *  Use IsValid(AnimInstance) if you need to handle this separately.
 *  @see FPlayMontageAnimNotifyDelegate */
UE5CORO_API auto PlayMontageNotifyBegin(UAnimInstance* Instance,
                                        UAnimMontage* Montage, FName NotifyName)
	-> Private::FAnimAwaiterPayload;

/** Waits for the PlayMontageNotify or PlayMontageNotifyWindow of the given name
 *  to end on the montage's currently-playing instance.<br>
 *  The result of co_await is a pointer to the notify payload.<br>
 *  The return value of this function is copyable but only one copy may be
 *  co_awaited at the same time.<br>
 *  The payload pointer is only valid until the next co_await, and it might be
 *  nullptr in case the notify happened before co_awaiting the returned value of
 *  this function or (rarely) if the anim instance got destroyed.
 *  Use IsValid(AnimInstance) if you need to handle this separately.
 *  @see FPlayMontageAnimNotifyDelegate */
UE5CORO_API auto PlayMontageNotifyEnd(UAnimInstance* Instance,
                                      UAnimMontage* Montage, FName NotifyName)
	-> Private::FAnimAwaiterPayload;
}

namespace UE5Coro::Private
{
class [[nodiscard]] FAnimAwaiter : public TAwaiter<FAnimAwaiter>
{
protected:
	TStrongObjectPtr<UUE5CoroAnimCallbackTarget> Target;
	bool bSuspended = false;

	FAnimAwaiter(UAnimInstance*, UAnimMontage*);
	~FAnimAwaiter();

public:
	UE5CORO_API void Suspend(FPromise&);
};

template<typename T>
class [[nodiscard]] TAnimAwaiter : public FAnimAwaiter
{
	friend UUE5CoroAnimCallbackTarget;

	static constexpr enum
	{
		Void,
		Bool,
		Payload,
		NameAndPayload,
	} Type = std::is_same_v<T, std::monostate> ? Void
	       : std::is_same_v<T, bool>           ? Bool
	       : std::is_pointer_v<T>              ? Payload
	                                           : NameAndPayload;

public:
	template<typename TEnd>
	TAnimAwaiter(TEnd, UAnimInstance*, UAnimMontage*);
	template<typename TEnd>
	TAnimAwaiter(TEnd, UAnimInstance*, UAnimMontage*, FName);
	UE5CORO_API ~TAnimAwaiter();

	UE5CORO_API bool await_ready();
	UE5CORO_API std::conditional_t<Type == Void, void, T> await_resume();
};
}
