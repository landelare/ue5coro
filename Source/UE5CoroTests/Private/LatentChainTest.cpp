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
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/AutomationTest.h"
#include "UE5Coro/AggregateAwaiters.h"
#include "UE5Coro/LatentAwaiters.h"

using namespace std::placeholders;
using namespace UE5Coro;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncChainTest, "UE5Coro.Chain.Async",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentChainTest, "UE5Coro.Chain.Latent",
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

	auto ExpectSuccess = [&](bool bValue)
	{
		Test.TestTrue(TEXT("Chain not aborted"), bValue);
	};

	{
		State = 0;
		World.Run(CORO
		{
			State = 1;
#if UE5CORO_PRIVATE_LATENT_CHAIN_IS_OK
			ExpectSuccess(co_await Latent::Chain(
				&UKismetSystemLibrary::DelayUntilNextTick));
#else
			ExpectSuccess(co_await Latent::ChainEx(
				&UKismetSystemLibrary::DelayUntilNextTick, _1, _2));
#endif
			State = 2;
			ExpectSuccess(co_await Latent::ChainEx(
				&UKismetSystemLibrary::DelayUntilNextTick, _1, _2));
			State = 3;
		});
		Test.TestEqual(TEXT("Initial state"), State, 1);
		DoubleTick(2, 0);
		DoubleTick(3, 0);
	}

	{
		State = 0;
		World.Run(CORO
		{
			State = 1;
			ExpectSuccess(co_await Latent::ChainEx(
				&UKismetSystemLibrary::Delay, _1, 1, _2));
			State = 2;
#if UE5CORO_PRIVATE_LATENT_CHAIN_IS_OK
			ExpectSuccess(co_await Latent::Chain(&UKismetSystemLibrary::Delay,
				1));
#else
			ExpectSuccess(co_await Latent::ChainEx(&UKismetSystemLibrary::Delay,
				_1, 1, _2));
#endif
			State = 3;
		});
		Test.TestEqual(TEXT("Initial state"), State, 1);
		World.Tick(0.5);
		Test.TestEqual(TEXT("Half state"), State, 1);
		DoubleTick(2, 1);
		DoubleTick(3, 1.01);
	}

	{
		State = 0;
		World.Run(CORO
		{
			State = 1;
			auto* Obj = NewObject<UUE5CoroTestObject>();
			TStrongObjectPtr<UObject> KeepAlive(Obj);
#if UE5CORO_PRIVATE_LATENT_CHAIN_IS_OK
			ExpectSuccess(co_await Latent::Chain(&UUE5CoroTestObject::Latent,
				Obj));
#else
			ExpectSuccess(co_await Latent::ChainEx(&UUE5CoroTestObject::Latent,
				Obj, _2));
#endif
			State = 2;
			ExpectSuccess(co_await Latent::ChainEx(&UUE5CoroTestObject::Latent,
				Obj, _2));
			State = 3;
		});
		Test.TestEqual(TEXT("Initial state"), State, 1);
		DoubleTick(2, 0);
		DoubleTick(3, 0);
	}

	{
		State = -1;
		World.Run(CORO
		{
#if UE5CORO_PRIVATE_LATENT_CHAIN_IS_OK
			// The next line passes 0 instead of 1 on older versions of MSVC
			auto Chain1 = Latent::Chain(&UKismetSystemLibrary::Delay, 1);
			auto Chain2 =
				Latent::Chain(&UKismetSystemLibrary::DelayUntilNextTick);
#else
			auto Chain1 =
				Latent::ChainEx(&UKismetSystemLibrary::Delay, _1, 1, _2);
			auto Chain2 = Latent::ChainEx(
				&UKismetSystemLibrary::DelayUntilNextTick, _1, _2);
#endif
			State = 0;
			State = co_await WhenAny(std::move(Chain1), std::move(Chain2));
		});
		World.EndTick();
		Test.TestEqual(TEXT("Initial state"), State, 0);
		DoubleTick(1, 0);
		World.Tick(2);
		World.Tick(2);
	}
}
}

bool FAsyncChainTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FLatentChainTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
