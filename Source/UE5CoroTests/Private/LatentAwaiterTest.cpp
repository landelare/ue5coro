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
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "UE5Coro.h"

using namespace UE5Coro;
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentAwaiterTest, "UE5Coro.Latent.TrueLatent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentInAsyncTest, "UE5Coro.Latent.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	IF_CORO_LATENT
	{
		bool bStarted = false, bDone = false;
		auto Fn = CORO
		{
			ON_SCOPE_EXIT { bDone = true; };
			bStarted = true;
			co_return;
		};
		World.Run(Fn);

		Test.TestTrue("Trivial latent coroutine started", bStarted);
		Test.TestTrue("Trivial latent coroutine finished", bDone);
	}

	{
		int State = 0;
		World.Run(CORO
		{
			State = 1;
			co_await NextTick();
			State = 2;
		});
		World.EndTick();
		Test.TestEqual("NextTick 1", State, 1);
		World.Tick();
		Test.TestEqual("NextTick 2", State, 2);
	}

	{
		int State = 0;
		World.Run(CORO
		{
			State = 1;
			co_await Ticks(2);
			State = 2;
		});
		World.EndTick();
		Test.TestEqual("Ticks 1-1", State, 1);
		World.Tick();
		Test.TestEqual("Ticks 1-2", State, 1);
		World.Tick();
		Test.TestEqual("Ticks 2", State, 2);
	}

	{
		int State = 0, RealState = 0;
		World.Run(CORO
		{
			State = 1;
			co_await Seconds(1);
			State = 2;
		});
		World.Run(CORO
		{
			RealState = 1;
			co_await RealSeconds(1);
			RealState = 2;
		});
		World.EndTick();
		UGameplayStatics::SetGlobalTimeDilation(World, 0.1);
		Test.TestEqual("Seconds 1-1", State, 1);
		Test.TestEqual("RealSeconds 1-1", RealState, 1);
		World.Tick(0.95);
		Test.TestEqual("Seconds 1-2", State, 1);
		Test.TestEqual("RealSeconds 1-2", RealState, 1);
		World.Tick(0.1); // Crossing 1.0s here
		World->bDebugPauseExecution = false;
		Test.TestEqual("Seconds 1-3", State, 1);
		Test.TestEqual("RealSeconds 2", RealState, 2);
		UGameplayStatics::SetGlobalTimeDilation(World, 1);
		// The dilated coroutine only had around 0.1 seconds, let it complete
		World.Tick(1);
		Test.TestEqual("Seconds 2", State, 2);
	}

	{
		int State = 0, RealState = 0;
		World.Run(CORO
		{
			State = 1;
			auto Time = World->GetTimeSeconds();
			co_await UntilTime(Time + 1);
			State = 2;
		});
		World.Run(CORO
		{
			RealState = 1;
			auto Time = World->GetRealTimeSeconds();
			co_await UntilRealTime(Time + 1);
			RealState = 2;
		});
		World.EndTick();
		UGameplayStatics::SetGlobalTimeDilation(World, 0.1);
		Test.TestEqual("UntilTime 1-1", State, 1);
		Test.TestEqual("UntilRealTime 1-1", RealState, 1);
		World.Tick(0.95);
		Test.TestEqual("UntilTime 1-2", State, 1);
		Test.TestEqual("UntilRealTime 1-2", RealState, 1);
		World.Tick(0.1); // Crossing 1.0s here
		World->bDebugPauseExecution = false;
		Test.TestEqual("UntilTime 1-3", State, 1);
		Test.TestEqual("UntilRealTime 2", RealState, 2);
		UGameplayStatics::SetGlobalTimeDilation(World, 1);
		// The dilated coroutine only had around 0.1 seconds, let it complete
		World.Tick(1);
		Test.TestEqual("UntilTime 2", State, 2);
	}
}
}

bool FLatentAwaiterTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}

bool FLatentInAsyncTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}
