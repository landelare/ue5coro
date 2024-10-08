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

using namespace UE5Coro;
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLatentReturnTest, "UE5Coro.Return.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncReturnTest, "UE5Coro.Return.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
struct FCopyCounter
{
	int C = 0;
	FCopyCounter() = default;
	FCopyCounter(const FCopyCounter& Other) : C(Other.C + 1) { }
	FCopyCounter(FCopyCounter&& Other) : C(std::exchange(Other.C, -99)) { }
	void operator=(const FCopyCounter& Other) { C = Other.C + 1; }
	void operator=(FCopyCounter&& Other) { C = std::exchange(Other.C, -99); }
};

template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	{
		auto A = World.Run(CORO_R(int) { co_return 1; });
		auto B = World.Run(CORO_R(int) { co_return 1.0; });
		Test.TestEqual("Return value passthrough", A.MoveResult(), 1);
		Test.TestEqual("Implicit conversion", B.MoveResult(), 1);
	}

	{
		bool bSuccess = false;
		World.Run(CORO
		{
			// Always async inner
			bSuccess = co_await World.Run([&]() -> TCoroutine<bool>
			{
				co_return true;
			});
		});
		Test.TestTrue("co_await result", bSuccess);
	}

	{
		bool bSuccess = false;
		// Always async outer
		World.Run([&]() -> TCoroutine<>
		{
			bSuccess = co_await World.Run(CORO_R(bool)
			{
				co_return true;
			});
		});
		Test.TestTrue("co_await result", bSuccess);
	}

	{
		bool bSuccess = false, bInnerReturned = false;
		World.Run(CORO
		{
			bSuccess = co_await World.Run(CORO_R(bool)
			{
				ON_SCOPE_EXIT { bInnerReturned = true; };
				co_return true;
			});
		});
		Test.TestTrue("Inner returned", bInnerReturned);
		Test.TestTrue("co_await result", bSuccess);
	}

	{
		bool bInnerComplete = false;
		auto Coro = World.Run(CORO_R(TArray<int>)
		{
			auto InnerCoro = World.Run(CORO_R(TArray<int>)
			{
				ON_SCOPE_EXIT { bInnerComplete = true; };
				co_await NextTick();
				co_return {1, 2, 3};
			});

			for (int i = 0; i < 2; ++i) // Test double await
			{
				decltype(auto) Array = co_await InnerCoro; // lvalue
				static_assert(std::same_as<decltype(Array), TArray<int>>);
				Test.TestEqual("Array Num", Array.Num(), 3);
				Test.TestEqual("Array[0]", Array[0], 1);
				Test.TestEqual("Array[1]", Array[1], 2);
				Test.TestEqual("Array[2]", Array[2], 3);
			}

			co_return {4};
		});
		World.EndTick();
		Test.TestFalse("Inner not complete yet", bInnerComplete);
		World.Tick(); // NextTick
		IF_CORO_LATENT
		{
			Test.TestFalse("Outer not complete yet", Coro.IsDone());
			Test.TestTrue("Inner complete", bInnerComplete);
			World.Tick(); // Outer completion
		}
		Test.TestTrue("Outer complete", Coro.IsDone());
		Test.TestTrue("Outer successful", Coro.WasSuccessful());
		auto Array = Coro.MoveResult();
		Test.TestEqual("Outer array Num", Array.Num(), 1);
		Test.TestEqual("Outer array[0]", Array[0], 4);
	}

	{
		bool bDone = false;
		World.Run(CORO
		{
			auto Coro1 = World.Run(CORO_R(FCopyCounter) { co_return {}; });
			auto Coro2 = World.Run(CORO_R(FCopyCounter) { co_return {}; });
			auto Copied = co_await Coro1;
			auto Moved = co_await std::move(Coro2);
			// These values are intentionally very strict and assume perfect RVO
			Test.TestEqual("Copied", Copied.C, 1);
			Test.TestEqual("Moved", Moved.C, 0);
			bDone = true;
		});
		IF_CORO_LATENT
			FTestHelper::PumpGameThread(World, [&] { return bDone; });
		else
			Test.TestTrue("Instant async completion", bDone);
	}
}
}

bool FAsyncReturnTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FLatentReturnTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
