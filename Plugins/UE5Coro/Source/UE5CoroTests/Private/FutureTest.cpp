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
#include "UE5Coro/AggregateAwaiters.h"
#include "UE5Coro/AsyncAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFutureAsync, "UE5Coro.Future.Async",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFutureLatent, "UE5Coro.Future.Latent",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

#ifdef _MSC_VER
// MSVC workaround - DoTest is not a coroutine but it won't compile without this
template<>
struct stdcoro::coroutine_traits<void, FAutomationTestBase&>
{
	using promise_type = UE5Coro::Private::TCoroutinePromise<void, FAsyncPromise>;
};
#endif

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	{
		TPromise<int> Promise;
		Promise.SetValue(1);
		auto Coro = World.Run(CORO_R(int)
		{
			co_return co_await Promise.GetFuture();
		});
		Test.TestTrue(TEXT("Already done"), Coro.IsDone());
		Test.TestEqual(TEXT("Value"), Coro.GetResult(), 1);
	}

	{
		int State = 0;
		TPromise<void> Promise;
		World.Run(CORO
		{
			State = 1;
			co_await Promise.GetFuture();
			State = 2;
		});
		Test.TestEqual(TEXT("Before"), State, 1);
		Promise.SetValue();
		Test.TestEqual(TEXT("After"), State, 2);
	}

	{
		int State = 0;
		TPromise<int> Promise;
		World.Run(CORO
		{
			State = 1;
			decltype(auto) Value = co_await Promise.GetFuture();
			static_assert(std::is_same_v<decltype(Value), int>);
			State = Value;
		});
		Test.TestEqual(TEXT("Before"), State, 1);
		Promise.SetValue(2);
		Test.TestEqual(TEXT("After"), State, 2);
	}

	{
		int State = 0;
		TPromise<int&> Promise;
		World.Run(CORO
		{
			State = 1;
			decltype(auto) Value = co_await Promise.GetFuture();
			static_assert(std::is_same_v<decltype(Value), int&>);
			State = Value;
		});
		Test.TestEqual(TEXT("Before"), State, 1);
		int Two = 2;
		Promise.SetValue(Two);
		Test.TestEqual(TEXT("After"), State, 2);
	}

	{
		int State = 0;
		TPromise<int> Promise1;
		TPromise<int> Promise2;
		World.Run(CORO
		{
			State = co_await WhenAny(Promise1.GetFuture(), Promise2.GetFuture());
		});
		Test.TestEqual(TEXT("Before"), State, 0);
		int One = 1;
		Promise2.SetValue(One);
		Test.TestEqual(TEXT("After"), State, 1);
		Promise1.SetValue(One);
	}
}
} // namespace

bool FFutureAsync::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FFutureLatent::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
