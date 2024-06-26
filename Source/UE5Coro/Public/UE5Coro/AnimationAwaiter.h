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
#include "UE5Coro/Definition.h"
#include <variant>
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "UE5Coro/Private.h"
#include "UE5Coro/Promise.h"

namespace UE5Coro::Anim
{
/** Asynchronously waits for the provided montage's current instance to blend
 *  out on the given anim instance.
 *
 *  The result of the await expression is true if this was caused by an
 *  interruption, false otherwise.
 *
 *  @see FOnMontageBlendingOutStarted */
UE5CORO_API auto MontageBlendingOut(UAnimInstance* Instance,
                                    UAnimMontage* Montage)
	-> Private::TAnimAwaiter<bool>;

/** Asynchronously waits for the provided montage's current instance to end on
 *  the given anim instance.
 *
 *  The result of the await expression is true if this was caused by an
 *  interruption, false otherwise.
 *
 *  @see FOnMontageEnded */
UE5CORO_API auto MontageEnded(UAnimInstance* Instance, UAnimMontage* Montage)
	-> Private::TAnimAwaiter<bool>;

/** Asynchronously waits for the anim notify to happen on the provided anim
 *  instance. */
UE5CORO_API auto NextNotify(UAnimInstance* Instance, FName NotifyName)
	-> Private::TAnimAwaiter<void>;

/** Asynchronously waits for any PlayMontageNotify or PlayMontageNotifyWindow to
 *  begin on the montage's current instance.
 *
 *  The result of the await expression is a TTuple of the name of the montage
 *  that began, along with a pointer to its notify payload.
 *
 *  The payload pointer is only valid until the next co_await, and it might be
 *  nullptr in case the notify happened before co_awaiting the returned value of
 *  this function or (rarely) if the anim instance got destroyed.
 *  Use IsValid(AnimInstance) if you need to handle this separately.
 *
 *  @see FPlayMontageAnimNotifyDelegate */
UE5CORO_API auto PlayMontageNotifyBegin(UAnimInstance* Instance,
                                        UAnimMontage* Montage)
	-> Private::TAnimAwaiter<TTuple<FName, const FBranchingPointNotifyPayload*>>;

/** Asynchronously waits for any PlayMontageNotify or PlayMontageNotifyWindow to
 *  end on the montage's currently-playing instance.
 *
 *  The result of the await expression is a TTuple of the name of the montage
 *  that ended, along with a pointer to its notify payload.
 *
 *  The payload pointer is only valid until the next co_await, and it might be
 *  nullptr in case the notify happened before co_awaiting the returned value of
 *  this function or (rarely) if the anim instance got destroyed.
 *  Use IsValid(AnimInstance) if you need to handle this separately.
 *
 *  @see FPlayMontageAnimNotifyDelegate */
UE5CORO_API auto PlayMontageNotifyEnd(UAnimInstance* Instance,
                                      UAnimMontage* Montage)
	-> Private::TAnimAwaiter<TTuple<FName, const FBranchingPointNotifyPayload*>>;

/** Asynchronously waits for the PlayMontageNotify or PlayMontageNotifyWindow of
 *  the given name to begin on the montage's currently-playing instance.
 *
 *  The result of the await expression is a pointer to the notify payload.
 *
 *  The payload pointer is only valid until the next co_await, and it might be
 *  nullptr in case the notify happened before co_awaiting the returned value of
 *  this function or (rarely) if the anim instance got destroyed.
 *  Use IsValid(AnimInstance) if you need to handle this separately.
 *
 *  @see FPlayMontageAnimNotifyDelegate */
UE5CORO_API auto PlayMontageNotifyBegin(UAnimInstance* Instance,
                                        UAnimMontage* Montage, FName NotifyName)
	-> Private::TAnimAwaiter<const FBranchingPointNotifyPayload*>;

/** Asynchronously waits for the PlayMontageNotify or PlayMontageNotifyWindow of
 *  the given name to end on the montage's currently-playing instance.
 *
 *  The result of the await expression is a pointer to the notify payload.
 *
 *  The payload pointer is only valid until the next co_await, and it might be
 *  nullptr in case the notify happened before co_awaiting the returned value of
 *  this function or (rarely) if the anim instance got destroyed.
 *  Use IsValid(AnimInstance) if you need to handle this separately.
 *
 *  @see FPlayMontageAnimNotifyDelegate */
UE5CORO_API auto PlayMontageNotifyEnd(UAnimInstance* Instance,
                                      UAnimMontage* Montage, FName NotifyName)
	-> Private::TAnimAwaiter<const FBranchingPointNotifyPayload*>;
}

#pragma region Private
namespace UE5Coro::Private
{
class [[nodiscard]] FAnimAwaiter : public TAwaiter<FAnimAwaiter>
{
protected:
	TStrongObjectPtr<UUE5CoroAnimCallbackTarget> Target;
	bool bSuspended = false;

	FAnimAwaiter(UAnimInstance*, UAnimMontage*);
	~FAnimAwaiter();

	UE5CORO_API FAnimAwaiter(const FAnimAwaiter&);
	UE5CORO_API FAnimAwaiter& operator=(const FAnimAwaiter&);

public:
	UE5CORO_API void Suspend(FPromise&);
};

template<typename T>
struct [[nodiscard]] TAnimAwaiter : FAnimAwaiter
{
	TAnimAwaiter(auto, UAnimInstance*, UAnimMontage*);
	TAnimAwaiter(auto, UAnimInstance*, UAnimMontage*, FName);
	UE5CORO_API ~TAnimAwaiter();

	[[nodiscard]] UE5CORO_API bool await_ready();
	UE5CORO_API T await_resume();
};
}
#pragma endregion
