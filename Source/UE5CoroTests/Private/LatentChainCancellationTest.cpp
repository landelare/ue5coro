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
#include "UE5Coro/LatentAwaiters.h"
#include "UE5Coro/UE5CoroChainCallbackTarget.h"

using namespace std::placeholders;
using namespace UE5Coro;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncChainCancelTest, "UE5Coro.Chain.Cancel.Async",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentChainCancelTest, "UE5Coro.Chain.Cancel.Latent",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

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
		Test.TestEqual(TEXT("Latent state"), State, ExpectedState);
	};

	auto ExpectFail = [&](bool bValue)
	{
		Test.TestFalse(TEXT("Chain aborted"), bValue);
	};

	{
		TSet<UUE5CoroChainCallbackTarget*> Targets;
		for (auto* Target : TObjectRange<UUE5CoroChainCallbackTarget>())
			Targets.Add(Target);

		World.Run(CORO
		{
			State = 1;
#if UE5CORO_PRIVATE_LATENT_CHAIN_IS_OK
			ExpectFail(co_await Latent::Chain(&UKismetSystemLibrary::Delay, 1));
#else
			ExpectFail(co_await Latent::ChainEx(&UKismetSystemLibrary::Delay,
				_1, 1, _2));
#endif
			State = 2;
		});
		Test.TestEqual(TEXT("Started"), State, 1);
		UUE5CoroChainCallbackTarget* NewTarget = nullptr;
		for (auto* Target : TObjectRange<UUE5CoroChainCallbackTarget>())
			if (!Targets.Contains(Target))
			{
				NewTarget = Target;
				break;
			}
		Test.TestNotNull(TEXT("Callback target found"), NewTarget);
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
