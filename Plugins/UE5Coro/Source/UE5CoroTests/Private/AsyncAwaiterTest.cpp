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
