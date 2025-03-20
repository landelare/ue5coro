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

using namespace std::placeholders;
using namespace UE5Coro;
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncAwaitTest, "UE5Coro.Handle.Await.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentAwaitTest, "UE5Coro.Handle.Await.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

// For testing UntilCoroutine
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(disable:4996)
#endif

namespace
{
TCoroutine<> Wait5(TLatentContext<> Context)
{
	co_await Ticks(5);
}

template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	{
		std::optional<TCoroutine<>> Inner;
		auto Coro = World.Run(CORO
		{
			Inner = Wait5(static_cast<UWorld*>(World));
			co_await *Inner;
		});
		World.EndTick();
		Inner->Cancel();
		Test.TestFalse("Not done yet", Coro.IsDone());
		Test.TestFalse("Inner not done yet", Inner->IsDone());
		World.Tick(); // Inner will complete here
		IF_CORO_LATENT
			World.Tick(); // Latent-latent might need an extra tick
		Test.TestTrue("Cancellation processed", Coro.IsDone());
	}

	{
		auto Coro = World.Run(CORO
		{
			co_await Wait5(static_cast<UWorld*>(World));
		});
		World.EndTick();
		Coro.Cancel();
		// Expedited async cancellation will behave identically to latent
		// cancellation, because it happens asynchronously on the game thread
		Test.TestFalse("Not done yet", Coro.IsDone());
		World.Tick();
		Test.TestTrue("Done", Coro.IsDone());
	}

	{
		auto Coro = World.Run(CORO
		{
			co_await UntilCoroutine(Wait5(static_cast<UWorld*>(World)));
		});
		World.EndTick();
		Coro.Cancel();
		Test.TestFalse("Not done yet", Coro.IsDone());
		World.Tick();
		// Expecting identical behavior in async and latent
		Test.TestTrue("Done", Coro.IsDone());
	}
}
}

bool FAsyncAwaitTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FLatentAwaitTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
