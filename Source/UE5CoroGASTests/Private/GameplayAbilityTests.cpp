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
#include "UE5CoroGASTestGameplayAbility.h"

using namespace UE5Coro::Private::Test;

#if UE_VERSION_OLDER_THAN(5, 5, 0)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayAbilityTestNonInstanced,
                                 "UE5Coro.GAS.GameplayAbility.NonInstanced",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayAbilityTestPerActor,
                                 "UE5Coro.GAS.GameplayAbility.PerActor",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayAbilityTestPerExecution,
                                 "UE5Coro.GAS.GameplayAbility.PerExecution",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
void DoTest(FAutomationTestBase& Test,
            EGameplayAbilityInstancingPolicy::Type Policy)
{
#if UE_VERSION_OLDER_THAN(5, 5, 0)
	bool bInstanced = Policy != EGameplayAbilityInstancingPolicy::NonInstanced;
#else
	constexpr bool bInstanced = true;
#endif
	auto* CDO = GetMutableDefault<UUE5CoroGASTestGameplayAbility>();
	UUE5CoroGASTestGameplayAbility::SetInstancingPolicy(Policy);
	int& State = UUE5CoroGASTestGameplayAbility::State;

	{
		FGASTestWorld World;
		UUE5CoroGASTestGameplayAbility::Reset();
		Test.TestEqual("Clean", State, 0);
		World.Run(UUE5CoroGASTestGameplayAbility::StaticClass());
		Test.TestEqual("Started", State, 1);
		World.Tick();
		Test.TestEqual("Waited 1", State, 3);
		World.Tick();
		Test.TestEqual("Waited 2", State, 4);
		if (bInstanced)
		{
			World.Tick(2);
			// Give the latent action an opportunity to poll
			if (State != 5)
				World.Tick(0);
			Test.TestEqual("Task ran", State, 5);
		}
		UUE5CoroGASTestGameplayAbility::PerformLastStep.Execute();
		Test.TestEqual("Task completed", State, 6);
	}

	{
		FGASTestWorld World;
		UUE5CoroGASTestGameplayAbility::Reset();
		Test.TestEqual("Clean", State, 0);
		World.Run(UUE5CoroGASTestGameplayAbility::StaticClass());
		UUE5CoroGASTestGameplayAbility* Ability = nullptr;
		if (!bInstanced)
			Ability = CDO;
		else
			for (auto* Obj : TObjectRange<UUE5CoroGASTestGameplayAbility>())
				Ability = Obj;
		Test.TestNotNull("Ability found", Ability);
		Test.TestEqual("Started", State, 1);
		Ability->CancelAbility(UUE5CoroGASTestGameplayAbility::Handle,
		                       UUE5CoroGASTestGameplayAbility::ActorInfo,
		                       UUE5CoroGASTestGameplayAbility::ActivationInfo,
		                       false);
		Test.TestEqual("Cancellation not processed yet", State, 1);
		World.Tick();
		// Instanced force cancels (2), non-instanced is a regular cancel (2->3)
		Test.TestEqual("Canceled", State, bInstanced ? 2 : 3);
	}

	{
		FGASTestWorld World;
		UUE5CoroGASTestGameplayAbility::Reset();
		Test.TestEqual("Clean", State, 0);
		World.Run(UUE5CoroGASTestGameplayAbility::StaticClass());
	} // Force cancel by destroying the world
	Test.TestEqual("Canceled", State, 2);
}
}

#if UE_VERSION_OLDER_THAN(5, 5, 0)
bool FGameplayAbilityTestNonInstanced::RunTest(const FString& Parameters)
{
	DoTest(*this, EGameplayAbilityInstancingPolicy::NonInstanced);
	return true;
}
#endif

bool FGameplayAbilityTestPerActor::RunTest(const FString& Parameters)
{
	DoTest(*this, EGameplayAbilityInstancingPolicy::InstancedPerActor);
	return true;
}

bool FGameplayAbilityTestPerExecution::RunTest(const FString& Parameters)
{
	DoTest(*this, EGameplayAbilityInstancingPolicy::InstancedPerExecution);
	return true;
}
