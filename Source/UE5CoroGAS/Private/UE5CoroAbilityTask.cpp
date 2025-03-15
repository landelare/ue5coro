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

#include "UE5CoroGAS/UE5CoroAbilityTask.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

void UUE5CoroAbilityTask::Activate()
{
	Super::Activate();

	// Is there a use for this? PerformActivation seems to guard against it.
	ensureMsgf(!Promise, TEXT("Multiple overlapping activations"));

	checkf(!TAbilityPromise<ThisClass>::bCalledFromActivate,
	       TEXT("Internal error: Activate() recursion"));
	TAbilityPromise<ThisClass>::bCalledFromActivate = true;
	TCoroutine Coroutine = Execute();
	checkf(!TAbilityPromise<ThisClass>::bCalledFromActivate,
	       TEXT("Did you implement Execute() with a coroutine?"));
	Coroutine.ContinueWithWeak(this, [=, this]
	{
		checkf(IsInGameThread(),
		       TEXT("Internal error: expected to continue on the game thread"));
		checkf(Promise,
		       TEXT("Internal error: expected to be the active coroutine"));
		Promise = nullptr;
		Super::EndTask();
		Coroutine.WasSuccessful() ? Succeeded() : Failed();
	});
}

void UUE5CoroAbilityTask::EndTask()
{
	check(!"Do not call EndTask() manually");
}

void UUE5CoroAbilityTask::OnDestroy(bool bInOwnerFinished)
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: expected to be destroyed on the game thread"));

	// GAS itself relies on this hack in TaskOwnerEnded... :(
	if (!IsValid(this))
		return;

	// A forced cancellation would be more appropriate because this is a
	// destruction, but the coroutine might be running (and NOT suspended!)
	if (Promise)
	{
		UE::TUniqueLock Lock(Promise->GetLock());
		Promise->Cancel(false);
	}

	Super::OnDestroy(bInOwnerFinished);
	checkf(!IsValid(this), TEXT("Internal error: expected MarkAsGarbage()"));
}

void UUE5CoroAbilityTask::CoroutineStarting(TAbilityPromise<ThisClass>* InPromise)
{
	Promise = InPromise;
}

void UUE5CoroSimpleAbilityTask::Succeeded()
{
	OnSucceeded.Broadcast();
}

void UUE5CoroSimpleAbilityTask::Failed()
{
	OnFailed.Broadcast();
}
