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

#include "UE5CoroGASTestGameplayAbility.h"
#include "Misc/EngineVersionComparison.h"
#include "Tasks/GameplayTask_WaitDelay.h"

using namespace UE5Coro;
using namespace UE5Coro::GAS;
using namespace UE5Coro::Latent;

void UUE5CoroGASTestGameplayAbility::SetInstancingPolicy(
	EGameplayAbilityInstancingPolicy::Type Policy)
{
	GetMutableDefault<ThisClass>()->InstancingPolicy = Policy;
}

void UUE5CoroGASTestGameplayAbility::Reset()
{
	State = 0;
	Handle = {};
	ActorInfo = nullptr;
	ActivationInfo = {};
	TriggerEventData = nullptr;
}

FAbilityCoroutine UUE5CoroGASTestGameplayAbility::ExecuteAbility(
	FGameplayAbilitySpecHandle InHandle,
	const FGameplayAbilityActorInfo* InActorInfo,
	FGameplayAbilityActivationInfo InActivationInfo,
	const FGameplayEventData* InTriggerEventData)
{
	Handle = InHandle;
	ActorInfo = InActorInfo;
	ActivationInfo = InActivationInfo;
	TriggerEventData = InTriggerEventData;

	CommitAbility(InHandle, InActorInfo, InActivationInfo);

	State = 1;
	{
		FCancellationGuard _;
		// Instanced abilities remove latent actions when canceled, so the guard
		// will be ignored if the ability is canceled here
		ON_SCOPE_EXIT { State = 2; };
		co_await NextTick();
	}
	State = 3;
	co_await NextTick();
	State = 4;

	// UGameplayTask_WaitDelay only works on instanced abilities
#if UE_VERSION_OLDER_THAN(5, 5, 0)
	if (GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::NonInstanced)
#else
	if constexpr (true)
#endif
	{
		// UGameplayTask_WaitDelay is MinimalAPI
		auto* Class = UGameplayTask_WaitDelay::StaticClass();
		auto* Fn = Class->FindFunctionByName("TaskWaitDelay");
		TTuple<TScriptInterface<IGameplayTaskOwnerInterface>, float, uint8,
		       UGameplayTask_WaitDelay*> Params{this, 1, 192, nullptr};
		GetMutableDefault<UGameplayTask_WaitDelay>()->ProcessEvent(Fn, &Params);
		co_await Task(Params.Get<UGameplayTask_WaitDelay*>());
		State = 5;
	}
	ON_SCOPE_EXIT { State = 6; };
	co_await PerformLastStep;
}
