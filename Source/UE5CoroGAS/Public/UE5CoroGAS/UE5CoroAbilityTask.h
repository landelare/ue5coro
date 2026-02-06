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
#include "UE5CoroGAS/Definition.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "UE5CoroGAS/AbilityPromise.h"
#include "UE5CoroAbilityTask.generated.h"

/** Usage summary:
 *  - Add the necessary static UFUNCTION in your subclass
 *  - Add your own delegates, or use UUE5CoroSimpleAbilityTask
 *  - Override Execute with a coroutine instead of Activate
 *  - Run to completion to succeed, cancel the coroutine to fail
 *  - Invoke your delegates from Succeeded or Failed, not from Execute */
UCLASS(Abstract, NotBlueprintable)
class UE5COROGAS_API UUE5CoroAbilityTask : public UAbilityTask
{
	GENERATED_BODY()
	friend UE5Coro::Private::TAbilityPromise<ThisClass>;

	UE5Coro::Private::TAbilityPromise<ThisClass>* Promise = nullptr;

protected:
	/** Override this with a coroutine instead of Activate. Do not call directly.
	 *  The coroutine's completion will call Succeeded or Failed.
	 *  The coroutine will run in latent mode and can self-cancel to indicate
	 *  failure. */
	virtual UE5Coro::GAS::FAbilityCoroutine Execute()
		PURE_VIRTUAL(UUE5CoroAbilityTask::Execute, co_return;);

	/** Called if Execute successfully runs to completion. */
	virtual void Succeeded() { }

	/** Called if Execute completes unsuccessfully, e.g., due to cancellation. */
	virtual void Failed() { }

private:
	/** Do not use. */
	virtual void Activate() final override;
	/** Do not call. Let the coroutine complete to end the task.
	 *  Calling EndTask on the base type is legal, but likely wrong if not done
	 *  by engine code. */
	void EndTask();
	virtual void OnDestroy(bool bInOwnerFinished) final override;
	void CoroutineStarting(UE5Coro::Private::TAbilityPromise<ThisClass>*);
};

UCLASS(Abstract)
class UE5COROGAS_API UUE5CoroSimpleAbilityTask : public UUE5CoroAbilityTask
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FGenericGameplayTaskDelegate OnSucceeded;

	UPROPERTY(BlueprintAssignable)
	FGenericGameplayTaskDelegate OnFailed;

protected:
	virtual void Succeeded() override;
	virtual void Failed() override;
};
