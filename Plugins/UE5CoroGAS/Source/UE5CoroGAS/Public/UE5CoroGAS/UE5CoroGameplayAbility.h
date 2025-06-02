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
#include "Abilities/GameplayAbility.h"
#include "UE5CoroGAS/AbilityPromises.h"
#include "UE5CoroGameplayAbility.generated.h"

/**
 * Usage summary:
 * - Override ExecuteAbility instead of ActivateAbility
 * - Call CommitAbility like usual but do *not* call EndAbility
 * - Any other method not mentioned above can be overridden and used normally
 * - Every instancing policy is supported
 */
UCLASS(Abstract, NotBlueprintable)
class UE5COROGAS_API UUE5CoroGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
	friend class UUE5CoroTaskCallbackTarget;
	friend UE5Coro::Private::TAbilityPromise<ThisClass>;

	// One shared per class to support every instancing policy including derived
	// classes changing their minds at runtime. The real one is on the CDO.
	TMap<UE5Coro::Private::GAS::FActivationKey,
	     UE5Coro::Private::TAbilityPromise<ThisClass>*>* Activations;

	UPROPERTY()
	TSet<TObjectPtr<UUE5CoroTaskCallbackTarget>> Tasks;

public:
	UUE5CoroGameplayAbility();
	virtual ~UUE5CoroGameplayAbility() override;

protected:
	/** If true when ExecuteAbility co_returns, the ability's end will be
	 *  replicated. This value may be freely changed at any time. */
	UPROPERTY(BlueprintReadWrite)
	bool bReplicateAbilityEnd = true;

	/** Override this with a coroutine instead of ActivateAbility.
	 *  The returned coroutine's completion will call EndAbility, and will be
	 *  canceled if the ability's execution is canceled.
	 *  The coroutine will run in latent mode and can self-cancel.
	 *  Do not call directly. */
	virtual UE5Coro::GAS::FAbilityCoroutine
	ExecuteAbility(FGameplayAbilitySpecHandle Handle,
	               const FGameplayAbilityActorInfo* ActorInfo,
	               FGameplayAbilityActivationInfo ActivationInfo,
	               const FGameplayEventData* TriggerEventData)
		PURE_VIRTUAL(UUE5CoroGameplayAbility::ExecuteAbility, co_return;);

	/** Given a UObject* with a single BlueprintAssignable UPROPERTY,
	 *  the return value of this function lets you co_await that delegate safely,
	 *  handling cancellations even if the delegate never Broadcasts.<br>
	 *  If the parameter is a UGameplayTask or UBlueprintAsyncActionBase, it
	 *  will be activated before returning. */
	UE5Coro::Private::FLatentAwaiter Task(UObject*);

private:
	/** Override ExecuteAbility instead. */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle,
	                             const FGameplayAbilityActorInfo*,
	                             FGameplayAbilityActivationInfo,
	                             const FGameplayEventData*) final override;

	/** Do not use. */
	virtual void EndAbility(FGameplayAbilitySpecHandle,
	                        const FGameplayAbilityActorInfo*,
	                        FGameplayAbilityActivationInfo, bool,
	                        bool) final override;

	void CoroutineStarting(UE5Coro::Private::TAbilityPromise<ThisClass>*);

	static bool ShouldResumeTask(void*, bool);
};
