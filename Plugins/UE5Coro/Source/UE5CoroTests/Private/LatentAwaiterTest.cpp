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
#include "UE5Coro/LatentAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentAwaiterTest, "UE5Coro.Latent.TrueLatent",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentInAsyncTest, "UE5Coro.Latent.Async",
                                 EAutomationTestFlags::ApplicationContextMask |
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
		bool bStarted = false;
		bool bDone = false;
		auto Fn = CORO
		{
			ON_SCOPE_EXIT
			{
				bDone = true;
			};
			bStarted = true;
			co_return;
		};
		World.Run(Fn);

		Test.TestTrue(TEXT("Null latent coroutine started"), bStarted);
		Test.TestTrue(TEXT("Null latent coroutine finished"), bDone);
	}

	{
		int State = 0;
		World.Run(CORO
		{
			State = 1;
			co_await Latent::NextTick();
			State = 2;
		});
		World.EndTick();
		Test.TestEqual(TEXT("NextTick 1"), State, 1);
		World.Tick();
		Test.TestEqual(TEXT("NextTick 2"), State, 2);
	}

	{
		int State = 0;
		World.Run(CORO
		{
			State = 1;
			co_await Latent::Ticks(2);
			State = 2;
		});
		World.EndTick();
		Test.TestEqual(TEXT("Ticks 1-1"), State, 1);
		World.Tick();
		Test.TestEqual(TEXT("Ticks 1-2"), State, 1);
		World.Tick();
		Test.TestEqual(TEXT("Ticks 2"), State, 2);
	}

	{
		// Having this here works around a compiler issue (MSVC 19.33.31629)
		// causing the lambdas to "mis-capture" State and RealState
		volatile int msvc_workaround = 0;

		int State = 0;
		int RealState = 0;
		World.Run(CORO
		{
			State = 1;
			co_await Latent::Seconds(1);
			State = 2;
		});
		World.Run(CORO
		{
			RealState = 1;
			co_await Latent::RealSeconds(1);
			RealState = 2;
		});
		World.EndTick();
		UGameplayStatics::SetGlobalTimeDilation(GWorld, 0.1);
		Test.TestEqual(TEXT("Seconds 1-1"), State, 1);
		Test.TestEqual(TEXT("RealSeconds 1-1"), RealState, 1);
		World.Tick(0.95);
		Test.TestEqual(TEXT("Seconds 1-2"), State, 1);
		Test.TestEqual(TEXT("RealSeconds 1-2"), RealState, 1);
		World.Tick(0.1); // Crossing 1.0s here
		GWorld->bDebugPauseExecution = false;
		Test.TestEqual(TEXT("Seconds 1-3"), State, 1);
		Test.TestEqual(TEXT("RealSeconds 2"), RealState, 2);
		UGameplayStatics::SetGlobalTimeDilation(GWorld, 1);
		// The dilated coroutine only had around 0.1 seconds, let it complete
		World.Tick(1);
		Test.TestEqual(TEXT("Seconds 2"), State, 2);
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
