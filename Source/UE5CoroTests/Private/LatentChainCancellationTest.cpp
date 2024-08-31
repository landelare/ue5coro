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

#include "TestWorld.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/AutomationTest.h"
#include "UE5Coro.h"

using namespace std::placeholders;
using namespace UE5Coro;
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncChainCancelTest, "UE5Coro.Chain.Cancel.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentChainCancelTest, "UE5Coro.Chain.Cancel.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace UE5Coro::Private
{
extern UE5CORO_API UClass* ChainCallbackTarget_StaticClass();
}

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;
	int State = 0;

	// The order between Chain and the chained latent actions' Ticks is not
	// fixed, so allow one extra tick if needed
	auto DoubleTick = [&](int ExpectedState, float DeltaSeconds)
	{
		World.Tick(DeltaSeconds);
		if (State != ExpectedState)
			World.Tick(0);
		Test.TestEqual("Latent state", State, ExpectedState);
	};

	auto ExpectFail = [&](bool bValue)
	{
		Test.TestFalse("Chain aborted", bValue);
	};

	{
		TSet<UObject*> Targets;
		for (auto* Target : TObjectRange<UObject>())
			if (Target->IsA(ChainCallbackTarget_StaticClass()))
				Targets.Add(Target);

		World.Run(CORO
		{
			State = 1;
			ExpectFail(co_await Chain(&UKismetSystemLibrary::Delay, 1));
			State = 2;
		});
		Test.TestEqual("Started", State, 1);
		UObject* NewTarget = nullptr;
		for (auto* Target : TObjectRange<UObject>())
			if (Target->IsA(ChainCallbackTarget_StaticClass()) &&
			    !Targets.Contains(Target))
			{
				NewTarget = Target;
				break;
			}
		Test.TestNotNull("Callback target found", NewTarget);
		World->GetLatentActionManager().RemoveActionsForObject(NewTarget);
		DoubleTick(2, 0); // Removals are only processed on the next tick
	}
}
}

bool FAsyncChainCancelTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FLatentChainCancelTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
