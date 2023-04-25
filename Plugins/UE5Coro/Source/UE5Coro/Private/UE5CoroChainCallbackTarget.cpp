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

#include "UE5Coro/UE5CoroChainCallbackTarget.h"
#include "UE5Coro/UE5CoroSubsystem.h"

using namespace UE5Coro::Private;

void UUE5CoroChainCallbackTarget::Activate(int32 InExpectedLink,
                                           FTwoLives* InState)
{
	check(IsInGameThread());
	checkf(!State, TEXT("Unexpected double activation"));
	ExpectedLink = InExpectedLink;
	State = InState;
}

void UUE5CoroChainCallbackTarget::Deactivate()
{
	check(IsInGameThread());
	checkf(State, TEXT("Unexpected deactivation while not active"));
	// Leave ExpectedLink stale for the check in ExecuteLink
	if (!State->Release())
		State = nullptr; // The other side is not interested anymore
}

int32 UUE5CoroChainCallbackTarget::GetExpectedLink() const
{
	check(IsInGameThread());
	checkf(State, TEXT("Unexpected linkage query on inactive object"));
	return ExpectedLink;
}

void UUE5CoroChainCallbackTarget::ExecuteLink(int32 Link)
{
	check(IsInGameThread());
	checkf(Link == ExpectedLink, TEXT("Unexpected linkage"));
	if (State)
	{
		State->UserData = 1;
		State = nullptr;
	}
}

ETickableTickType UUE5CoroChainCallbackTarget::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}

void UUE5CoroChainCallbackTarget::Tick(float DeltaTime)
{
	if (!State)
		return;

	// ProcessLatentActions refuses to work on non-BP classes.
	GetClass()->ClassFlags |= CLASS_CompiledFromBlueprint;
	GetWorld()->GetLatentActionManager().ProcessLatentActions(this, DeltaTime);
	GetClass()->ClassFlags &= ~CLASS_CompiledFromBlueprint;
}

TStatId UUE5CoroChainCallbackTarget::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UUE5CoroChainCallbackTarget,
	                                STATGROUP_Tickables);
}
