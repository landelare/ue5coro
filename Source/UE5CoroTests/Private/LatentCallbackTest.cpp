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
#include "UE5Coro.h"

using namespace UE5Coro;
using namespace UE5Coro::Async;
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentCallbackTest, "UE5Coro.Latent.Callbacks",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentCompletionTest, "UE5Coro.Latent.Completion",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)


FVoidCoroutine UUE5CoroTestObject::ObjectDestroyedTest(
	int& State, bool& bAbnormal, bool& bCanceled, FLatentActionInfo)
{
	State = 1;
	FOnActionAborted A([&] { State = 2; });
	FOnObjectDestroyed B([&] { State = 3; });
	FOnAbnormalExit C([&] { bAbnormal = true; });
	FOnCoroutineCanceled D([&] { bCanceled = true; });
	co_await Ticks(10);
	State = 10;
}

bool FLatentCallbackTest::RunTest(const FString& Parameters)
{
	FTestWorld World;

	{
		int State = 0;
		bool bCanceled = false;
		{
			FTestWorld World2;
			World2.Run([&](FLatentActionInfo) -> TCoroutine<>
			{
				FOnCoroutineCanceled _([&] { bCanceled = true; });
				ON_SCOPE_EXIT { State = 2; };
				State = 1;
				co_await NextTick();
			});
			TestEqual("Initial state", State, 1);
		} // Destroy the world, force canceling the coroutine in it
		TestEqual("On scope exit", State, 2);
		TestTrue("Canceled", bCanceled);
	}

	{
		int State = 0;
		bool bAbnormal = false, bCanceled = false;
		TStrongObjectPtr Object(NewObject<UUE5CoroTestObject>(World));
		Object->ObjectDestroyedTest(State, bAbnormal, bCanceled,
		                            {0, 0, TEXT("Core"), Object.Get()});
		World.EndTick();
		TestEqual("Initial state", State, 1);
		for (int i = 0; i < 10; ++i)
		{
			TestEqual("No early resume", State, 1);
			World.Tick();
		}
		TestEqual("Resumed state", State, 10);
		World.Tick();
		TestFalse("Normal exit", bAbnormal);
		TestFalse("Not canceled", bCanceled);
	}

	{
		int State = 0;
		bool bAbnormal = false, bCanceled = false;
		TStrongObjectPtr Object(NewObject<UUE5CoroTestObject>(World));
		Object->ObjectDestroyedTest(State, bAbnormal, bCanceled,
		                            {0, 0, TEXT("Core"), Object.Get()});
		TestEqual("Initial state", State, 1);
		auto& LAM = World->GetLatentActionManager();
		LAM.RemoveActionsForObject(Object.Get());
		World.Tick();
		TestEqual("On action aborted", State, 2);
		TestTrue("Abnormal exit", bAbnormal);
		TestTrue("Implicitly canceled", bCanceled);
	}

	{
		int State = 0;
		bool bAbnormal = false, bCanceled = false;
		{
			TStrongObjectPtr Object(NewObject<UUE5CoroTestObject>(World));
			Object->ObjectDestroyedTest(State, bAbnormal, bCanceled,
			                            {0, 0, TEXT("Core"), Object.Get()});
			TestEqual("Initial state", State, 1);
			Object->MarkAsGarbage();
		}
		CollectGarbage(RF_NoFlags, true);
		World.Tick();
		TestEqual("On object destroyed", State, 3);
		TestTrue("Abnormal exit", bAbnormal);
		TestTrue("Implicitly canceled", bCanceled);
	}
	return true;
}

bool FLatentCompletionTest::RunTest(const FString& Parameters)
{
	FTestWorld World;

	{
		std::atomic<bool> bDone = false;
		auto Coro = World.Run([&](FLatentActionInfo) -> TCoroutine<>
		{
			ON_SCOPE_EXIT
			{
				TestFalse("Cleanup off the game thread", IsInGameThread());
				bDone = true;
			};
			co_await MoveToNewThread();
			TestFalse("Moved to another thread", IsInGameThread());
		});
		FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
	}

	{
		FAwaitableEvent TestToCoro;
		FEventRef CoroToTest;
		bool bDone = false;
		auto Coro = World.Run([&](FLatentActionInfo) -> TCoroutine<>
		{
			ON_SCOPE_EXIT
			{
				TestTrue("Cleanup on game thread", IsInGameThread());
				bDone = true;
			};
			co_await MoveToNewThread();
			TestFalse("Moved to another thread", IsInGameThread());
			CoroToTest->Trigger();
			// There's an extremely rare, but valid order of events, where
			// TestToCoro.Trigger() arrives before await_ready, making this
			// event take the fast path and not suspend the coroutine.
			// Make sure that the cancellation is explicitly processed,
			// otherwise the ON_SCOPE_EXIT will run on the wrong thread.
			co_await TestToCoro;
			co_await FinishNowIfCanceled();
		});
		CoroToTest->Wait();
		Coro.Cancel();
		TestToCoro.Trigger();
		FTestHelper::PumpGameThread(World,
		                            [&] { return bDone && Coro.IsDone(); });
		TestFalse("Canceled", Coro.WasSuccessful());
	}

	{
		FEventRef TestToCoro, CoroToTest;
		bool bDone = false;
		auto Coro = World.Run([&](FLatentActionInfo) -> TCoroutine<>
		{
			co_await MoveToNewThread();
			CoroToTest->Trigger();
			TestToCoro->Wait(); // Blocking wait
			// final_suspend
		});
		Coro.ContinueWith([&]
		{
			TestTrue(TEXT("Continuing on the game thread"), IsInGameThread());
			bDone = true;
		});
		CoroToTest->Wait();
		Coro.Cancel();
		TestToCoro->Trigger();
		// ContinueWith is expected to run on the game thread
		FTestHelper::PumpGameThread(World, [&] { return bDone; });
		// The cancellation is not processed before final_suspend is reached
		TestTrue(TEXT("Successful completion"), Coro.WasSuccessful());
	}

	return true;
}
