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
#include "Misc/EngineVersionComparison.h"
#include "UE5CoroGASTestAbilityTask.h"
#include "UE5CoroGASTestGameplayAbility.h"

using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityTaskTest, "UE5Coro.GAS.AbilityTask",
                                 EAutomationTestFlags_ApplicationContextMask |
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
	TestNotNull("Ability found", Ability);

	// This test relies on accessing garbage-but-uncollected UObjects
#if UE_VERSION_OLDER_THAN(5, 4, 0)
	if (UObjectBaseUtility::IsPendingKillEnabled())
	{
		AddWarning("Skipping tests incompatible with pending kill");
		return true;
	}
#else
	bool bOld = UObjectBaseUtility::IsGarbageEliminationEnabled();
	ON_SCOPE_EXIT { UObjectBaseUtility::SetGarbageEliminationEnabled(bOld); };
	UObjectBaseUtility::SetGarbageEliminationEnabled(false);
#endif

	// This is called from within an executing coroutine for FPromise::Current
	auto CheckWorld = [&] { FTestHelper::CheckWorld(*this, World); };

	{
		TStrongObjectPtr Ptr(UUE5CoroGASTestAbilityTask::Run(Ability));
		auto* Task = Ptr.Get(); // Avoid a check() in operator->...
		Task->CoroutineCallback = CheckWorld;
		TestEqual("Not started", Task->State, 0);
		Task->ReadyForActivation();
		World.EndTick();
		TestEqual("Started", Task->State, 1);
		World.Tick();
		TestEqual("Waited 1", Task->State, 3);
		World.Tick();
		TestEqual("Waited 2", Task->State, 4);
		TestTrue("Not garbage yet", IsValid(Task));
		Task->PerformLastStep.Execute();
		TestEqual("Finished", Task->State, 10); // ...here
		TestFalse("Garbage", IsValid(Task));
	}

	{
		TStrongObjectPtr Ptr(UUE5CoroGASTestAbilityTask::Run(Ability));
		auto* Task = Ptr.Get();
		Task->CoroutineCallback = CheckWorld;
		TestEqual("Not started", Task->State, 0);
		Task->ReadyForActivation();
		TestTrue("Not garbage yet", IsValid(Task));
		Task->TaskOwnerEnded();
		TestFalse("Garbage", IsValid(Task));
		World.EndTick();
		TestEqual("Started", Task->State, 1);
		World.Tick();
		TestEqual("Waited", Task->State, 2); // Forced cancellation
		for (int i = 0; i < 10; ++i)
			World.Tick();
		TestEqual("Still no progress", Task->State, 2);
		TestFalse("Still garbage", IsValid(Task));
	}

	{
		TStrongObjectPtr Ptr(UUE5CoroGASTestAbilityTask::Run(Ability));
		auto* Task = Ptr.Get();
		Task->CoroutineCallback = CheckWorld;
		TestEqual("Not started", Task->State, 0);
		Task->ReadyForActivation();
		Task->bSoftCancel = true;
		World.EndTick();
		TestEqual("Started", Task->State, 1);
		TestTrue("Not garbage yet", IsValid(Task));
		World.Tick();
		TestEqual("Failed", Task->State, 11);
		TestFalse("Garbage", IsValid(Task));
	}

	return true;
}
