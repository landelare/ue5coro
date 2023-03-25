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

#include "UE5Coro/UE5CoroSubsystem.h"
#include "UE5Coro/UE5CoroChainCallbackTarget.h"

using namespace UE5Coro::Private;

bool FTwoLives::Release()
{
	// The <= 2 part should help catch use-after-free bugs in full debug builds.
	checkf(RefCount > 0 && RefCount <= 2,
	       TEXT("Internal error: misused two-lives tracker"));
	if (--RefCount == 0)
	{
		delete this;
		return false;
	}
	return true;
}

bool FTwoLives::ShouldResume(void*& State, bool bCleanup)
{
	auto* This = static_cast<FTwoLives*>(State);
	if (UNLIKELY(bCleanup))
	{
		This->Release();
		return false;
	}
	return This->RefCount < 2;
}

FLatentActionInfo UUE5CoroSubsystem::MakeLatentInfo()
{
	checkf(IsInGameThread(), TEXT("Unexpected latent info off the game thread"));
	// Using INDEX_NONE linkage and next as the UUID is marginally faster due
	// to an early exit in FLatentActionManager::TickLatentActionForObject.
	return {INDEX_NONE, NextLinkage++, TEXT("None"), this};
}

FLatentActionInfo UUE5CoroSubsystem::MakeLatentInfo(FTwoLives* State)
{
	checkf(IsInGameThread(), TEXT("Unexpected latent info off the game thread"));

	// Lazy delegate binding in order to not affect
	// projects that never use Chain/ChainEx.
	if (UNLIKELY(!LatentActionsChangedHandle.IsValid()))
		LatentActionsChangedHandle =
			FLatentActionManager::OnLatentActionsChanged().AddUObject(
				this, &ThisClass::LatentActionsChanged);

	int32 Linkage = NextLinkage++;
	checkf(!ChainCallbackTargets.Contains(Linkage),
	       TEXT("Unexpected linkage collision"));
	// Pooling these objects was found to be consistently slower
	// than making new ones every time.
	auto* Target = NewObject<UUE5CoroChainCallbackTarget>(this);
	Target->Activate(Linkage, State);
	ChainCallbackTargets.Add(Linkage, Target);
	return {Linkage, Linkage, TEXT("ExecuteLink"), Target};
}

void UUE5CoroSubsystem::Deinitialize()
{
	Super::Deinitialize();

	if (LatentActionsChangedHandle.IsValid())
		FLatentActionManager::OnLatentActionsChanged().Remove(
			LatentActionsChangedHandle);
}

void UUE5CoroSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// ProcessLatentActions refuses to work on non-BP classes.
	GetClass()->ClassFlags |= CLASS_CompiledFromBlueprint;
	GetWorld()->GetLatentActionManager().ProcessLatentActions(this, DeltaTime);
	GetClass()->ClassFlags &= ~CLASS_CompiledFromBlueprint;
}

TStatId UUE5CoroSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UUE5CoroSubsystem, STATGROUP_Tickables);
}

void UUE5CoroSubsystem::LatentActionsChanged(UObject* Object,
                                             ELatentActionChangeType Change)
{
	checkf(IsInGameThread(),
	       TEXT("Unexpected latent action update off the game thread"));

	if (Change != ELatentActionChangeType::ActionsRemoved)
		return;

	if (auto* Target = Cast<UUE5CoroChainCallbackTarget>(Object);
	    IsValid(Target) && Target->GetOuter() == this)
	{
		verify(ChainCallbackTargets.Remove(Target->GetExpectedLink()) == 1);
		Target->Deactivate();
	}
}
