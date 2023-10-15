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
#include "UE5Coro/AggregateAwaiters.h"
#include "UE5Coro/AsyncAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncAwaiterTest, "UE5Coro.Async.TrueAsync",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncInLatentTest, "UE5Coro.Async.Latent",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncStressTest, "UE5Coro.Async.Stress",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::MediumPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	IF_CORO_LATENT
	{
		FEventRef TestToCoro(EEventMode::AutoReset);
		FEventRef CoroToTest(EEventMode::AutoReset);
		bool bStarted = false;
		bool bDone = false;
		World.Run(CORO
		{
			bStarted = true;
			co_await UE5Coro::Async::MoveToThread(ENamedThreads::AnyThread);
			TestToCoro->Wait();
			bDone = true;
			co_await UE5Coro::Async::MoveToGameThread();
			CoroToTest->Trigger();
		});
		Test.TestTrue(TEXT("Started"), bStarted);
		Test.TestFalse(TEXT("Not done yet 1"), bDone);
		TestToCoro->Trigger();

		// This test is running on the game thread so MoveToGameThread() needs
		// a little help
		FTestHelper::PumpGameThread(World, [&] { return CoroToTest->Wait(0); });
		Test.TestTrue(TEXT("Done"), bDone);
	}

	{
		FEventRef TestToCoro(EEventMode::AutoReset);
		FEventRef CoroToTest(EEventMode::AutoReset);
		int State = 0;
		World.Run(CORO
		{
			State = 1;
			CoroToTest->Trigger();
			co_await Async::MoveToThread(ENamedThreads::AnyThread);
			TestToCoro->Wait();
			State = 2;
			CoroToTest->Trigger();
		});
		Test.TestEqual(TEXT("Initial state"), State, 1);
		Test.TestTrue(TEXT("Wait 1"), CoroToTest->Wait());
		Test.TestEqual(TEXT("First event, original thread"), State, 1);
		TestToCoro->Trigger();
		Test.TestTrue(TEXT("Wait 2"), CoroToTest->Wait());
		Test.TestEqual(TEXT("Second event, new thread"), State, 2);
	}

	{
		FEventRef TestToCoro(EEventMode::AutoReset);
		FEventRef CoroToTest(EEventMode::AutoReset);
		int State = 0;
		World.Run(CORO
		{
			State = 1;
			CoroToTest->Trigger();
			co_await UE5Coro::Async::MoveToNewThread();
			TestToCoro->Wait();
			State = 2;
			CoroToTest->Trigger();
		});
		Test.TestEqual(TEXT("Initial state"), State, 1);
		Test.TestTrue(TEXT("Wait 1"), CoroToTest->Wait());
		Test.TestEqual(TEXT("First event, original thread"), State, 1);
		TestToCoro->Trigger();
		Test.TestTrue(TEXT("Wait 2"), CoroToTest->Wait());
		Test.TestEqual(TEXT("Second event, new thread"), State, 2);
	}

	{
		FEventRef CoroToTest;
		World.Run(CORO
		{
			co_await WhenAll(
				UE5Coro::Async::MoveToNewThread(),
				UE5Coro::Async::MoveToThread(ENamedThreads::AnyThread));
			CoroToTest->Trigger();
		});
		Test.TestTrue(TEXT("Triggered"), CoroToTest->Wait());
	}

	{
		FEventRef CoroToTest;
		std::atomic<bool> bMovedOut = false;
		std::atomic<bool> bMovedIn = false;
		World.Run(CORO
		{
			auto Return = Async::MoveToSimilarThread();
			co_await Async::MoveToNewThread();
			bMovedOut = !IsInGameThread();
			co_await Return;
			bMovedIn = IsInGameThread();
			CoroToTest->Trigger();
		});
		FTestHelper::PumpGameThread(World, [&] { return CoroToTest->Wait(0); });
		Test.TestTrue(TEXT("Moved out"), bMovedOut);
		Test.TestTrue(TEXT("Moved back in"), bMovedIn);
	}

	{
		std::atomic<int> State = 0;
		World.Run(CORO
		{
			co_await UE5Coro::Async::PlatformSecondsAnyThread(0.05);
			++State;
			co_await UE5Coro::Async::PlatformSecondsAnyThread(0.05);
			++State;
		});
		Test.TestEqual(TEXT("Initial state"), State, 0);
		auto Start = FPlatformTime::Seconds();
		FPlatformProcess::Sleep(0.07);
		Test.TestTrue(TEXT("Sleep is reliable 1"),
		              FPlatformTime::Seconds() >= Start + 0.05);
		Test.TestTrue(TEXT("State has increased"), State >= 1);
		FPlatformProcess::Sleep(0.05);
		Test.TestTrue(TEXT("Sleep is reliable 2"),
		              FPlatformTime::Seconds() >= Start + 0.1);
		Test.TestEqual(TEXT("Final state"), State, 2);
	}
}
}

bool FAsyncAwaiterTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FAsyncInLatentTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}

bool FAsyncStressTest::RunTest(const FString& Parameters)
{
	FTestWorld World;

	std::atomic<int> Count = 0;
	World.Run([&]() -> TCoroutine<>
	{
		TArray<FAsyncTimeAwaiter> Awaiters;
		auto GetValue = [](int i)
		{
			// Map hashes to -0.001..0.001
			auto Div = 1000 *
			           static_cast<double>(std::numeric_limits<size_t>::max());
			return (static_cast<double>(std::hash<int>()(i)) / Div * 2 - 1);
		};
		for (int i = 0; i < 1000; ++i)
		{
			Awaiters.Add(Async::PlatformSecondsAnyThread(GetValue(i)));
			if (i > 500)
				co_await Async::PlatformSecondsAnyThread(GetValue(-i));
			++Count;
		}
		for (auto& Awaiter : Awaiters)
		{
			co_await Awaiter;
			++Count;
		}
	});
	while (Count < 2000)
		;
	FPlatformProcess::Sleep(0.01);
	TestEqual(TEXT("Final count"), Count, 2000);

	return true;
}
