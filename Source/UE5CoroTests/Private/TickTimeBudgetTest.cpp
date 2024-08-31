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
#include "Misc/AutomationTest.h"
#include "UE5Coro.h"

using namespace UE5Coro;
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTickTimeBudgetAsyncTest,
                                 "UE5Coro.Latent.TickTimeBudget.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTickTimeBudgetLatentTest,
                                 "UE5Coro.Latent.TickTimeBudget.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	// Since these test cases involve real time, they're not deterministic :(
	{
		int State = 0;
		World.Run(CORO
		{
			auto Budget = FTickTimeBudget::Milliseconds(1);
			FPlatformProcess::Sleep(0.002);
			co_await Budget;
			State = 1;
		});
		World.EndTick();
		Test.TestEqual("State remained", State, 0);
		World.Tick();
		Test.TestEqual("State changed", State, 1);
	}

	{
		int State = 0;
		TSet<int> Observed;
		constexpr int Count = 20;
		World.Run(CORO
		{
			auto Budget = FTickTimeBudget::Milliseconds(100);
			for (; State < Count; ++State)
			{
				// Hopefully the platform can handle a sleep of roughly 20 ms...
				FPlatformProcess::Sleep(20 / 1000.0f);
				co_await Budget;
			}
		});
		World.EndTick();
		for (int i = 0; i < Count; ++i)
		{
			World.Tick();
			Observed.Add(State);
		}
		Test.TestFalse("No immediate suspension 0", Observed.Contains(0));
		Test.TestFalse("No immediate suspension 1", Observed.Contains(1));
		// This was observed to pass with ±3, take one away for safety
		Test.TestTrue("Execution was between the two extremes",
		              Observed.Num() > 2 && Observed.Num() <= Count - 2);
	}
}
}

bool FTickTimeBudgetAsyncTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FTickTimeBudgetLatentTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
