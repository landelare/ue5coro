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

#include "UE5CoroGASTestAbilityTask.h"

using namespace UE5Coro;
using namespace UE5Coro::Latent;

auto UUE5CoroGASTestAbilityTask::Run(UGameplayAbility* InAbility) -> ThisClass*
{
	return NewAbilityTask<ThisClass>(InAbility);
}

GAS::FAbilityCoroutine UUE5CoroGASTestAbilityTask::Execute()
{
	// Some tests want to execute code with the coroutine on the call stack
	if (CoroutineCallback)
		CoroutineCallback();

	State = 1;
	{
		FCancellationGuard _;
		ON_SCOPE_EXIT { State = 2; };
		co_await NextTick();
	}
	State = 3;
	if (bSoftCancel)
		co_await FSelfCancellation();
	co_await NextTick();
	State = 4;

	ON_SCOPE_EXIT { State = 5; };
	co_await PerformLastStep;
}

void UUE5CoroGASTestAbilityTask::Succeeded()
{
	State = 10;
}

void UUE5CoroGASTestAbilityTask::Failed()
{
	State = 11;
}
