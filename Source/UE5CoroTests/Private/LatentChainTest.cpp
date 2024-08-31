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
#include "UE5Coro.h"

using namespace std::placeholders;
using namespace UE5Coro;
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncChainTest, "UE5Coro.Chain.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentChainTest, "UE5Coro.Chain.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
TCoroutine<> ChainTest(FAutomationTestBase& Test, FLatentActionInfo, int Value1,
                       int& Value2)
{
	Test.TestEqual("Value1", Value1, 1);
	Test.TestEqual("Value2", Value2, 2);
	co_await NextTick();
	Value2 = 3;
}

template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;
	int State = 0;

	// The order between Chain and the chained latent actions' Ticks is
	// determined by the latent action manager, so allow one extra tick if
	// they happen to be processed backwards
	auto DoubleTick = [&](int ExpectedState, float DeltaSeconds)
	{
		World.Tick(DeltaSeconds);
		if (State != ExpectedState)
			World.Tick(0);
		Test.TestEqual("Latent state", State, ExpectedState);
	};

	auto ExpectSuccess = [&](bool bValue)
	{
		Test.TestTrue("Chain not aborted", bValue);
	};

	{
		State = 0;
		World.Run(CORO
		{
			State = 1;
			ExpectSuccess(co_await Chain(
				&UKismetSystemLibrary::DelayUntilNextTick));
			State = 2;
			ExpectSuccess(co_await ChainEx(
				&UKismetSystemLibrary::DelayUntilNextTick, _1, _2));
			State = 3;
		});
		Test.TestEqual("Initial state", State, 1);
		DoubleTick(2, 0);
		DoubleTick(3, 0);
	}

	{
		State = 0;
		World.Run(CORO
		{
			State = 1;
			ExpectSuccess(co_await ChainEx(
				&UKismetSystemLibrary::Delay, _1, 1, _2));
			State = 2;
			ExpectSuccess(co_await Chain(&UKismetSystemLibrary::Delay, 1));
			State = 3;
		});
		Test.TestEqual("Initial state", State, 1);
		World.Tick(0.5);
		Test.TestEqual("Half state", State, 1);
		DoubleTick(2, 1);
		DoubleTick(3, 1.01);
	}

	{
		State = 0;
		World.Run(CORO
		{
			State = 1;
			auto* Obj = NewObject<UUE5CoroTestObject>(World);
			TStrongObjectPtr<UObject> KeepAlive(Obj);
			ExpectSuccess(co_await Chain(Obj, &UUE5CoroTestObject::Latent));
			State = 2;
			ExpectSuccess(co_await ChainEx(&UUE5CoroTestObject::Latent, Obj, _2));
			State = 3;
		});
		Test.TestEqual("Initial state", State, 1);
		DoubleTick(2, 0);
		DoubleTick(3, 0);
	}

	{
		State = -1;
		World.Run(CORO
		{
			auto Chain1 = Chain(&UKismetSystemLibrary::Delay, 1);
			auto Chain2 = Chain(&UKismetSystemLibrary::DelayUntilNextTick);
			State = 0;
			State = co_await WhenAny(std::move(Chain1), std::move(Chain2));
		});
		World.EndTick();
		Test.TestEqual("Initial state", State, 0);
		DoubleTick(1, 0);
		World.Tick(2);
		World.Tick(2);
	}

	{
		State = -1;
		World.Run(CORO
		{
			int Value = 2;
			auto Awaiter = Chain(&ChainTest, Test, 1, Value);
			Test.TestEqual("No lvalue write yet", Value, 2);
			State = 0;
			ExpectSuccess(co_await Awaiter);
			Test.TestEqual("Lvalue changed", Value, 3);
			State = 1;
		});
		World.EndTick();
		Test.TestEqual("Initial state", State, 0);
		DoubleTick(1, 0);
	}

	{
		State = -1;
		World.Run(CORO
		{
			int Value = 2;
			auto Awaiter = ChainEx(&ChainTest, std::ref(Test), _2, 1,
			                       std::ref(Value));
			Test.TestEqual("No lvalue write yet", Value, 2);
			State = 0;
			ExpectSuccess(co_await Awaiter);
			Test.TestEqual("Lvalue changed", Value, 3);
			State = 1;
		});
		World.EndTick();
		Test.TestEqual("Initial state", State, 0);
		DoubleTick(1, 0);
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
