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

#include "UE5CoroGAS.h"
#include "UE5CoroGASTestGameplayAbility.generated.h"

UCLASS(Hidden, MinimalAPI)
class UUE5CoroGASTestGameplayAbility : public UUE5CoroGameplayAbility
{
	GENERATED_BODY()

public:
	static void SetInstancingPolicy(EGameplayAbilityInstancingPolicy::Type);
	static void Reset();

	static inline int State;
	static inline TDelegate<void()> PerformLastStep;

	static inline FGameplayAbilitySpecHandle Handle;
	static inline const FGameplayAbilityActorInfo* ActorInfo;
	static inline FGameplayAbilityActivationInfo ActivationInfo;
	static inline const FGameplayEventData* TriggerEventData;

protected:
	virtual UE5Coro::GAS::FAbilityCoroutine
	ExecuteAbility(FGameplayAbilitySpecHandle InHandle,
	               const FGameplayAbilityActorInfo* InActorInfo,
	               FGameplayAbilityActivationInfo InActivationInfo,
	               const FGameplayEventData* InTriggerEventData) override;
};
