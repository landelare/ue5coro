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

#include "Misc/AutomationTest.h"
#include "TestWorld.h"
#include "UE5Coro.h"

using namespace UE5Coro;
using namespace UE5Coro::Async;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTaskCreateAsync, "UE5Coro.Tasks.CreateAsync",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTaskCreateLatent, "UE5Coro.Tasks.CreateLatent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTaskConsumeAsync, "UE5Coro.Tasks.ConsumeAsync",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTaskConsumeLatent, "UE5Coro.Tasks.ConsumeLatent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoCreateTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	{
		FEventRef TestToCoro, CoroToTest;
		int State = 0;
		World.Run(CORO
		{
			co_await MoveToTask(UE_SOURCE_LOCATION);
			TestToCoro->Wait();
			State = 1;
			CoroToTest->Trigger();
		});
		Test.TestEqual("Initial state", State, 0);
		TestToCoro->Trigger();
		CoroToTest->Wait();
		Test.TestEqual("Final state", State, 1);
	}

	{
		FEventRef TestToCoro, CoroToTest;
		int State = 0;
		World.Run(CORO
		{
			co_await MoveToTask();
			TestToCoro->Wait();
			State = 1;
			CoroToTest->Trigger();
		});
		Test.TestEqual("Initial state", State, 0);
		TestToCoro->Trigger();
		CoroToTest->Wait();
		Test.TestEqual("Final state", State, 1);
	}

	{
		FEventRef TestToCoro, CoroToTest;
		World.Run(CORO
		{
			co_await MoveToTask();
			TestToCoro->Wait(); // Required initial suspension and wait for Run
			co_await WhenAll(MoveToTask(TEXT("Test1")),
			                 MoveToTask(TEXT("Test2")));
			CoroToTest->Trigger();
		});
		TestToCoro->Trigger();
		Test.TestTrue("Triggered", CoroToTest->Wait());
	}
}

template<typename... T>
void DoConsumeTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	{
		FEventRef TestToCoro, CoroToTest;
		std::atomic<int> State = 0;
		World.Run(CORO
		{
			co_await UE::Tasks::Launch(UE_SOURCE_LOCATION, [&]
			{
				TestToCoro->Wait();
				++State;
			});
			++State;
			CoroToTest->Trigger();
		});
		TestToCoro->Trigger();
		CoroToTest->Wait();
		Test.TestEqual("Final state", State, 2);
	}

	{
		FEventRef TestToCoro, CoroToTest;
		int State = 0, Retval = 0;
		World.Run(CORO
		{
			auto Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&]
			{
				TestToCoro->Wait();
				++State;
				return 3;
			});
			Test.TestTrue("Still in game thread", IsInGameThread());
			decltype(auto) Value1 = co_await Task;
			Test.TestFalse("Moved out of game thread 1", IsInGameThread());
			decltype(auto) Value2 = co_await std::move(Task);
			Test.TestFalse("Moved out of game thread 2", IsInGameThread());
			// TTask<T>::GetResult() returns T&
			static_assert(std::is_lvalue_reference_v<decltype(Value1)>);
			static_assert(std::is_lvalue_reference_v<decltype(Value2)>);
			Test.TestEqual("Values", Value1, Value2);
			Retval = Value1;
			++State;
			CoroToTest->Trigger();
		});
		TestToCoro->Trigger();
		CoroToTest->Wait();
		Test.TestEqual("Final state", State, 2);
		Test.TestEqual("Return value", Retval, 3);
	}
}
}

bool FTaskCreateAsync::RunTest(const FString& Parameters)
{
	DoCreateTest<>(*this);
	return true;
}

bool FTaskCreateLatent::RunTest(const FString& Parameters)
{
	DoCreateTest<FLatentActionInfo>(*this);
	return true;
}

bool FTaskConsumeAsync::RunTest(const FString& Parameters)
{
	DoConsumeTest<>(*this);
	return true;
}

bool FTaskConsumeLatent::RunTest(const FString& Parameters)
{
	DoConsumeTest<FLatentActionInfo>(*this);
	return true;
}
