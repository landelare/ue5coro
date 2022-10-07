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

#include <optional>
#include "TestWorld.h"
#include "Misc/AutomationTest.h"
#include "UE5Coro/AggregateAwaiters.h"
#include "UE5Coro/AsyncAwaiters.h"
#include "UE5Coro/LatentAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAggregateAsyncTest, "UE5Coro.Aggregate.Async",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAggregateLatentTest, "UE5Coro.Aggregate.Latent",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
#define CORO [&](T...) -> FAsyncCoroutine
	FTestWorld World;

	{
		int State = 0;
		World.Run(CORO
		{
			++State;
			auto A = World.Run(CORO
			{
				++State;
				co_await Latent::NextTick();
				++State;
			});
			auto B = World.Run(CORO
			{
				++State;
				co_await Latent::Ticks(2);
				++State;
			});
			++State;
			co_await WhenAll(A, B);
			++State;
		});
		World.EndTick();
		Test.TestEqual("Initial state", State, 4); // All 3 coroutines suspended
		World.Tick();
		Test.TestEqual("First tick", State, 5); // A resumed
		World.Tick();
		Test.TestEqual("Second tick", State, 7); // B and outer resumed
	}

	{
		int State = 0;
		std::optional<int> First;
		World.Run(CORO
		{
			++State;
			auto A = World.Run(CORO
			{
				++State;
				co_await Latent::NextTick();
				++State;
			});
			auto B = World.Run(CORO
			{
				++State;
				co_await Latent::Ticks(2);
				++State;
			});
			++State;
			First = co_await WhenAny(A, B);
			++State;
		});
		World.EndTick();
		Test.TestEqual("Initial state", State, 4); // All 3 coroutines suspended
		Test.TestFalse("Not resumed yet", First.has_value());
		World.Tick();
		Test.TestEqual("First tick", State, 6); // A and outer resumed
		Test.TestEqual("Resumer index", First.value(), 0);
		World.Tick();
		Test.TestEqual("Second tick", State, 7); // B resumed
	}

	{
		std::optional<int> First;
		World.Run(CORO
		{
			auto A = World.Run(CORO { co_await Latent::Ticks(3); });
			auto B = World.Run(CORO { co_await Latent::Ticks(4); });
			auto C = World.Run(CORO { co_await Latent::Ticks(1); });
			auto D = World.Run(CORO { co_await Latent::Ticks(2); });
			First = co_await WhenAny(A, B, C, D);
		});
		World.EndTick();
		Test.TestFalse("Not resumed yet", First.has_value());
		World.Tick();
		Test.TestEqual("Resumer index", First.value(), 2);
	}

	{
		std::optional<int> First;
		World.Run(CORO
		{
			auto A = Latent::Ticks(1);
			auto B = Latent::Ticks(2);
			co_await Latent::Ticks(3);
			First = co_await WhenAny(A, B);
		});
		World.EndTick();
		Test.TestFalse("Not resumed yet", First.has_value());
		World.Tick();
		World.Tick();
		World.Tick();
		Test.TestEqual("Resumer index", First.value(), 0);
	}

#undef CORO
}
}

bool FAggregateAsyncTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FAggregateLatentTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
