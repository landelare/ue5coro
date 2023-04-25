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
#include <optional>
#include <variant>
#include "UE5Coro/AsyncCoroutine.h"
#include "UE5CoroAnimCallbackTarget.generated.h"

/**
 * Internal class supporting some async coroutine functionality.<br>
 * You never need to interact with it directly.
 */
UCLASS(Hidden)
class UE5CORO_API UUE5CoroAnimCallbackTarget : public UObject,
                                               public FTickableGameObject
{
	GENERATED_BODY()

	TWeakObjectPtr<UAnimInstance> WeakInstance;
	UE5Coro::Private::FPromise* Promise = nullptr;
	// UPlayMontageCallbackProxy uses this value as the default
	int32 MontageIDFilter = INDEX_NONE;
	std::optional<FName> NotifyFilter; // "None" is a valid name for a notify

	void TryResume();

public:
	// Void's result is indicated by this holding a bool, not monostate
	std::variant<std::monostate, bool, const FBranchingPointNotifyPayload*,
	             TTuple<FName, const FBranchingPointNotifyPayload*>> Result;

	void ListenForMontageEvent(UAnimInstance*, UAnimMontage*, bool);
	void ListenForNotify(UAnimInstance*, UAnimMontage*, FName);
	void ListenForPlayMontageNotify(UAnimInstance*, UAnimMontage*,
	                                std::optional<FName>, bool);
	void RequestResume(UE5Coro::Private::FPromise&);
	void CancelResume();

#pragma region Callbacks
	UFUNCTION()
	void NotifyCallback();
	UFUNCTION()
	void MontageCallbackBool(UAnimMontage* Montage, bool bInterrupted);
	UFUNCTION()
	void MontageCallbackNameAndPayload(
		FName NotifyName, const FBranchingPointNotifyPayload& Payload);
#pragma endregion

#pragma region FTickableGameObject overrides
	// These are needed to catch the anim instance getting destroyed without
	// a callback. Editor tick is needed to handle Persona and the end of PIE.
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
#pragma endregion
};
