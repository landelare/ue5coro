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

#include "GASTestWorld.h"
#include "Misc/AutomationTest.h"
#include "UE5CoroGASTestAbilityTask.h"
#include "UE5CoroGASTestGameplayAbility.h"

using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityTaskTest, "UE5Coro.GAS.AbilityTask",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

bool FAbilityTaskTest::RunTest(const FString& Parameters)
{
	FGASTestWorld World;
	UUE5CoroGASTestGameplayAbility::SetInstancingPolicy(
		EGameplayAbilityInstancingPolicy::InstancedPerExecution);
	UUE5CoroGASTestGameplayAbility::Reset();
	World.Run(UUE5CoroGASTestGameplayAbility::StaticClass());
	UUE5CoroGASTestGameplayAbility* Ability = nullptr;
	for (auto* Obj : TObjectRange<UUE5CoroGASTestGameplayAbility>())
		Ability = Obj;
	TestNotNull(TEXT("Ability found"), Ability);

	{
		auto* Task = UUE5CoroGASTestAbilityTask::Run(Ability);
		Task->ReadyForActivation();
		World.EndTick();
		TestEqual(TEXT("Started"), Task->State, 1);
		World.Tick();
		TestEqual(TEXT("Waited 1"), Task->State, 3);
		World.Tick();
		TestEqual(TEXT("Waited 2"), Task->State, 4);
		Task->PerformLastStep.Execute();
		TestEqual(TEXT("Finished"), Task->State, 10);
		TestFalse(TEXT("Garbage"), IsValid(Task));
	}

	{
		auto* Task = UUE5CoroGASTestAbilityTask::Run(Ability);
		Task->ReadyForActivation();
		Task->TaskOwnerEnded();
		World.EndTick();
		TestEqual(TEXT("Started"), Task->State, 1);
		World.Tick();
		TestEqual(TEXT("Waited"), Task->State, 2); // Forced cancellation
		World.Tick();
		World.Tick();
		TestEqual(TEXT("No progress"), Task->State, 2);
		TestFalse(TEXT("Garbage"), IsValid(Task));
	}

	{
		auto* Task = UUE5CoroGASTestAbilityTask::Run(Ability);
		Task->ReadyForActivation();
		Task->bSoftCancel = true;
		World.EndTick();
		TestEqual(TEXT("Started"), Task->State, 1);
		World.Tick();
		TestEqual(TEXT("Failed"), Task->State, 11);
		TestFalse(TEXT("Garbage"), IsValid(Task));
	}

	return true;
}
