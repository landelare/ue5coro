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
#include "UE5CoroTestObject.h"
#include "Misc/AutomationTest.h"
#include "UE5Coro/Cancellation.h"
#include "UE5Coro/LatentAwaiters.h"
#include "UE5Coro/LatentCallbacks.h"

using namespace UE5Coro;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentCallbackTest, "UE5Coro.Latent.Callbacks",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

FAsyncCoroutine UUE5CoroTestObject::ObjectDestroyedTest(
	int& State, bool& bAbnormal, bool& bCanceled, FLatentActionInfo)
{
	State = 1;
	Latent::FOnActionAborted A([&] { State = 2; });
	Latent::FOnObjectDestroyed B([&] { State = 3; });
	Latent::FOnAbnormalExit C([&] { bAbnormal = true; });
	FOnCoroutineCanceled D([&] { bCanceled = true; });
	co_await Latent::Ticks(10);
	State = 10;
}

bool FLatentCallbackTest::RunTest(const FString& Parameters)
{
	int State = 0;
	bool bCanceled = false;
	{
		FTestWorld World;
		World.Run([&](FLatentActionInfo) -> TCoroutine<>
		{
			FOnCoroutineCanceled _([&] { bCanceled = true; });
			ON_SCOPE_EXIT { State = 2; };
			State = 1;
			co_await Latent::NextTick();
		});
		TestEqual(TEXT("Initial state"), State, 1);
	}
	TestEqual(TEXT("On scope exit"), State, 2);
	TestTrue(TEXT("Canceled"), bCanceled);

	{
		FTestWorld World;
		bool bAbnormal = false;
		bCanceled = false;
		auto* Object = NewObject<UUE5CoroTestObject>();
		Object->ObjectDestroyedTest(State, bAbnormal, bCanceled,
		                            {0, 0, TEXT("Empty"), Object});
		World.EndTick();
		TestEqual(TEXT("Initial state"), State, 1);
		for (int i = 0; i < 10; ++i)
		{
			TestEqual(TEXT("No early resume"), State, 1);
			World.Tick();
		}
		TestEqual(TEXT("Resumed state"), State, 10);
		World.Tick();
		TestFalse(TEXT("Normal exit"), bAbnormal);
		TestFalse(TEXT("Not canceled"), bCanceled);
	}

	{
		FTestWorld World;
		bool bAbnormal = false;
		bCanceled = false;
		auto* Object = NewObject<UUE5CoroTestObject>();
		Object->ObjectDestroyedTest(State, bAbnormal, bCanceled,
		                            {0, 0, TEXT("Empty"), Object});
		TestEqual(TEXT("Initial state"), State, 1);
		auto& LAM = World->GetLatentActionManager();
		LAM.RemoveActionsForObject(Object);
		World.Tick();
		TestEqual(TEXT("On action aborted"), State, 2);
		TestTrue(TEXT("Abnormal exit"), bAbnormal);
		TestTrue(TEXT("Implicitly canceled"), bCanceled);
	}

	{
		FTestWorld World;
		bool bAbnormal = false;
		bCanceled = false;
		auto* Object = NewObject<UUE5CoroTestObject>();
		Object->ObjectDestroyedTest(State, bAbnormal, bCanceled,
		                            {0, 0, TEXT("Empty"), Object});
		TestEqual(TEXT("Initial state"), State, 1);
		Object->MarkAsGarbage();
		CollectGarbage(RF_NoFlags);
		World.Tick();
		TestEqual(TEXT("On object destroyed"), State, 3);
		TestTrue(TEXT("Abnormal exit"), bAbnormal);
		TestTrue(TEXT("Implicitly canceled"), bCanceled);
	}
	return true;
}
