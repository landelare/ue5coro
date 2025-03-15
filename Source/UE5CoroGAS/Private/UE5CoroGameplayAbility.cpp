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

#include "UE5CoroGAS/UE5CoroGameplayAbility.h"
#include "GameplayTask.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "UE5CoroTaskCallbackTarget.h"
#include "UE5Coro/LatentAwaiter.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;
using namespace UE5CoroGAS::Private;

namespace
{
bool GCoroutineEnded = false;
FPredictionKey GCurrentPredictionKey;

using FCallbackTargetPtr = TStrongObjectPtr<UUE5CoroTaskCallbackTarget>;

// Workaround for member IsTemplate being unreliable in destructors
bool IsTemplate(UObject* Object)
{
	// Deliberately not using IsValid here
	checkf(Object, TEXT("Internal error: corrupted object"));
	// Outer is explicitly not checked, it might be destroyed already
	return Object->HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject);
}

FMulticastDelegateProperty* FindDelegate(UClass* Class)
{
	// The class is already validated to have 1 property if there is a cache hit
	static TMap<TWeakObjectPtr<UClass>, FMulticastDelegateProperty*> Cache;
	if (auto* Property = Cache.FindRef(Class))
		return Property;

	// Find BlueprintAssignable properties
	FProperty* Property = nullptr;
	for (auto* i = Class->PropertyLink; i; i = i->NextRef)
		if (i->HasAnyPropertyFlags(CPF_BlueprintAssignable))
		{
			checkf(!Property,
			       TEXT("Only one BlueprintAssignable UPROPERTY is supported"));
			Property = i;
			if constexpr (!UE5CORO_DEBUG) // Keep looking for others in debug
				break;
		}
	checkf(Property, TEXT("A BlueprintAssignable UPROPERTY is required"));
	auto* DelegateProp = CastFieldChecked<FMulticastDelegateProperty>(Property);
	Cache.Add(Class, DelegateProp);
	return DelegateProp;
}
}

namespace UE5CoroGAS::Private
{
struct FStrictPredictionKey : FPredictionKey
{
	FStrictPredictionKey(const FPredictionKey& Key) noexcept
		: FPredictionKey(Key) { }

	bool operator==(const FStrictPredictionKey& Other) const noexcept
	{
		return FPredictionKey::operator==(Other) && Base == Other.Base;
	}
};
static_assert(sizeof(FStrictPredictionKey) == sizeof(FPredictionKey));

int32 GetTypeHash(const FStrictPredictionKey& Key) noexcept // ADL
{
	return GetTypeHash(static_cast<const FPredictionKey&>(Key)) ^
	       Key.Base << 16;
}
}

UUE5CoroGameplayAbility::UUE5CoroGameplayAbility()
{
	if (::IsTemplate(this))
		Activations = new TMap<FStrictPredictionKey, TAbilityPromise<ThisClass>*>;
	else
		Activations = GetDefault<ThisClass>(GetClass())->Activations;
	checkf(Activations, TEXT("Internal error: non-template object before CDO"));

	// For consistency.
	// Super::ActivateAbility is not called, so these aren't really used.
	bHasBlueprintActivate = false;
	bHasBlueprintActivateFromEvent = false;
}

UUE5CoroGameplayAbility::~UUE5CoroGameplayAbility()
{
	if (::IsTemplate(this))
		delete Activations;
#if UE5CORO_DEBUG
	Activations = reinterpret_cast<decltype(Activations)>(0xEEEEEEEEEEEEEEEE);
#endif
}

FLatentAwaiter UUE5CoroGameplayAbility::Task(UObject* Object, bool bAutoActivate)
{
	checkf(IsInGameThread(),
	       TEXT("This method is only available on the game thread"));
	checkf(IsValid(Object), TEXT("Attempting to await invalid object"));

	auto* Target = NewObject<UUE5CoroTaskCallbackTarget>(this);
	FScriptDelegate Delegate;
	Delegate.BindUFunction(Target, NAME_Core);
	FindDelegate(Object->GetClass())->AddDelegate(std::move(Delegate), Object);

	// Activate some well-known base classes (IsValid was checked above)
	if (bAutoActivate)
	{
		if (auto* Task = Cast<UGameplayTask>(Object))
			Task->ReadyForActivation();
		else if (auto* Action = Cast<UBlueprintAsyncActionBase>(Object))
			Action->Activate();
	}

	// The comment above TStrongObjectPtr promises that it is trivially movable
	static_assert(sizeof(FCallbackTargetPtr) <= sizeof(void*));
	void* Data;
	new (&Data) FCallbackTargetPtr(Target);
	return FLatentAwaiter(Data, &ShouldResumeTask, std::true_type());
}

void UUE5CoroGameplayAbility::ActivateAbility(
	FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// In some engine versions, Super::ActivateAbility has a weird piece of
	// example code in it that forcibly commits the ability, so it's not called

	checkf(IsInGameThread(),
	       TEXT("Internal error: expected GA activation on the game thread"));
	GCurrentPredictionKey = ActivationInfo.GetActivationPredictionKey();
	checkf(!TAbilityPromise<ThisClass>::bCalledFromActivate,
	       TEXT("Internal error: ActivateAbility recursion"));
	TAbilityPromise<ThisClass>::bCalledFromActivate = true;
	TCoroutine Coroutine = ExecuteAbility(Handle, ActorInfo, ActivationInfo,
	                                      TriggerEventData);
	checkf(!TAbilityPromise<ThisClass>::bCalledFromActivate,
	       TEXT("Did you implement ExecuteAbility with a coroutine?"));

	Coroutine.ContinueWithWeak(this, [=, this, ActorInfoCopy = *ActorInfo]
	{
		checkf(IsInGameThread(),
		       TEXT("Internal error: expected to continue on the game thread"));
		GCoroutineEnded = true;
		EndAbility(Handle, &ActorInfoCopy, ActivationInfo, bReplicateAbilityEnd,
		           !Coroutine.WasSuccessful());
		checkf(!GCoroutineEnded, TEXT("Internal error: unexpected state"));
	});
}

void UUE5CoroGameplayAbility::EndAbility(
	FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
	bool bWasCanceled)
{
	checkf(IsInGameThread(), TEXT("Abilities may only end on the game thread"));

	// CancelAbility might also call this, but it's still externally initiated
	bool bCoroutineEnded = GCoroutineEnded;
	// Prepare for the Super call possibly ending something else recursively
	GCoroutineEnded = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
	                  bWasCanceled);

	auto PredictionKey = ActivationInfo.GetActivationPredictionKey();
	if (!PredictionKey.IsValidKey())
		return;

	TAbilityPromise<ThisClass>* Promise;
	bool bFound = Activations->RemoveAndCopyValue(PredictionKey, Promise);

	// Nothing to do if the coroutine has ended already
	if (bCoroutineEnded)
		return;

	// If the coroutine hasn't ended, why was it not in the map?
	checkf(bFound, TEXT("Internal error: unexpected EndAbility call"));

	// Cancel the coroutine. Depending on instancing policy, there will be a
	// second, forced cancellation coming when the latent action manager
	// processes the action's removal.
	UE::TUniqueLock Lock(Promise->GetLock());
	checkf(!Promise->get_return_object().IsDone(),
	       TEXT("Internal error: unexpected coroutine state"));
	Promise->Cancel(false);
}

void UUE5CoroGameplayAbility::CoroutineStarting(TAbilityPromise<ThisClass>* Promise)
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: expected coroutine on the game thread"));
	checkf(GCurrentPredictionKey.IsValidKey(),
	       TEXT("Attempting to start ability with invalid prediction key"));
	checkf(!Activations->Contains(GCurrentPredictionKey),
	       TEXT("Overlapping ability activations with the same prediction key"));
	// The promise object is not fully constructed yet, but its address is known
	Activations->Add(GCurrentPredictionKey, Promise);
}

bool UUE5CoroGameplayAbility::ShouldResumeTask(void* State, bool bCleanup)
{
	static_assert(sizeof(FCallbackTargetPtr) <= sizeof(void*));
	auto& Ptr = reinterpret_cast<FCallbackTargetPtr&>(State);
	if (bCleanup) [[unlikely]]
	{
		Ptr.~FCallbackTargetPtr();
		return false;
	}
	return Ptr->bExecuted;
}
