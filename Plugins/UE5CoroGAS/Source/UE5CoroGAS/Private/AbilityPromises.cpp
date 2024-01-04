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

#include "UE5CoroGAS/AbilityPromises.h"
#include "UE5CoroGAS/UE5CoroAbilityTask.h"
#include "UE5CoroGAS/UE5CoroGameplayAbility.h"

using namespace UE5Coro;
using namespace UE5Coro::GAS;
using namespace UE5Coro::Private;

FAbilityCoroutine::FAbilityCoroutine(std::shared_ptr<FPromiseExtras> Extras)
	: TCoroutine(std::move(Extras))
{
}

FLatentActionInfo FAbilityPromise::MakeLatentInfo(UObject& Task)
{
	static int DummyId = 0;
	return {0, DummyId++, TEXT("None"), &Task};
}

FAbilityPromise::FAbilityPromise(UObject& Target, UWorld* MaybeWorld)
	: Super(Target, MaybeWorld, MakeLatentInfo(Target))
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: Expected to start on the game thread"));
}

FAbilityCoroutine FAbilityPromise::get_return_object() noexcept
{
	return FAbilityCoroutine(Extras);
}

FFinalSuspend FAbilityPromise::final_suspend() noexcept
{
	// Skip triggering a BP link because there isn't one
	return Super::final_suspend<false>();
}

template<typename T>
void TAbilityPromise<T>::Init(T& Target)
{
	checkf(bCalledFromActivate, TEXT("Do not call Execute coroutines directly!"));
	bCalledFromActivate = false;
	Target.CoroutineStarting(this);
}

UWorld* FAbilityPromise::TryGetWorld(FGameplayAbilitySpecHandle Handle,
                                     const FGameplayAbilityActorInfo* ActorInfo,
                                     FGameplayAbilityActivationInfo ActivationInfo,
                                     const FGameplayEventData* TriggerEventData)
{
	// UAbilitySystemComponent::InternalTryActivateAbility should prevent these
	checkf(ActorInfo, TEXT("Expected ability activation with valid actor info"));
	checkf(IsValid(ActorInfo->OwnerActor.Get()),
	       TEXT("Expected ability activation with valid owner"));
	checkf(IsValid(ActorInfo->AvatarActor.Get()),
	       TEXT("Expected ability activation with valid avatar"));

	if (auto* World = ActorInfo->OwnerActor.Get()->GetWorld();
	    ensureMsgf(IsValid(World),
	               TEXT("Expected ability activation in valid world")))
		return World;
	else
		return nullptr;
}

namespace UE5Coro::Private
{
template<typename T>
bool TAbilityPromise<T>::bCalledFromActivate = false;
template class UE5COROGAS_API TAbilityPromise<UUE5CoroAbilityTask>;
template class UE5COROGAS_API TAbilityPromise<UUE5CoroGameplayAbility>;
}
