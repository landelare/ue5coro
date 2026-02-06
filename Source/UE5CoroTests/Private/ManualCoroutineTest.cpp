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
// WHETHER IN CONTCoroT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "Misc/AutomationTest.h"
#include "TestWorld.h"

using namespace UE5Coro;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FManualCoroutineTest, "UE5Coro.Manual",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	{
		TManualCoroutine<T> Coro;
		Test.TestFalse("Active", Coro.IsDone());
		if constexpr (std::is_void_v<T>)
			Test.TestTrue("Success", Coro.TrySetResult());
		else
			Test.TestTrue("Success", Coro.TrySetResult(TEXT("X")));
		Test.TestTrue("Done", Coro.IsDone());
		Test.TestTrue("Successful", Coro.WasSuccessful());
		if constexpr (!std::is_void_v<T>)
			Test.TestEqual("Result", Coro.GetResult(), TEXT("X"));

		if constexpr (std::is_void_v<T>)
			Test.TestFalse("Already done", Coro.TrySetResult());
		else
		{
			Test.TestFalse("Already done", Coro.TrySetResult(TEXT("Y")));
			Test.TestEqual("Same result", Coro.GetResult(), TEXT("X"));
		}
	}

	for (int i = 0; i <= 1; ++i)
	{
		std::optional<TManualCoroutine<T>> Coro1 = TManualCoroutine<T>();
		TCoroutine<T> Coro2 = *Coro1;
		if constexpr (std::is_void_v<T>)
		{
			Test.TestTrue("Success 1", Coro1->TrySetResult());
			if (i)
				Coro1.reset();
			Test.TestTrue("Success 2", Coro2.WasSuccessful());
		}
		else
		{
			Test.TestTrue("Success 1", Coro1->TrySetResult(TEXT("X")));
			if (i)
				Coro1.reset();
			Test.TestTrue("Success 2", Coro2.WasSuccessful());
			Test.TestEqual("Result", Coro2.GetResult(), TEXT("X"));
		}
	}

	{
		TManualCoroutine<T> Coro;
		Coro.Cancel();
		FTestHelper::PumpGameThread(World, [&] { return Coro.IsDone(); });
		Test.TestFalse("Canceled", Coro.WasSuccessful());
		if constexpr (std::is_void_v<T>)
		{
			Test.TestFalse("Already canceled", Coro.TrySetResult());
			Test.TestFalse("Still canceled", Coro.WasSuccessful());
		}
		else
		{
			Test.TestEqual("Result 1", Coro.GetResult(), FString());
			Test.TestFalse("Already canceled", Coro.TrySetResult(TEXT("X")));
			Test.TestEqual("Result 2", Coro.GetResult(), FString());
			Test.TestFalse("Still canceled", Coro.WasSuccessful());
		}
	}

	{
		std::optional<TManualCoroutine<T>> Coro1 = TManualCoroutine<T>();
		Test.TestFalse("Not canceled 1A", Coro1->IsDone());
		TCoroutine<T> Coro2 = *Coro1;
		Test.TestFalse("Not canceled 1B", Coro1->IsDone());
		Test.TestFalse("Not canceled 2A", Coro2.IsDone());
		Coro1.reset(); // Should cancel
		// Cancellation processing is expedited, but not instant
		FTestHelper::PumpGameThread(World, [&] { return Coro2.IsDone(); });
		Test.TestFalse("Canceled", Coro2.WasSuccessful());
	}

	{
		TManualCoroutine<T> Coro;
		Coro.Cancel();
		FTestHelper::PumpGameThread(World, [&] { return Coro.IsDone(); });
		Test.TestFalse("Canceled", Coro.WasSuccessful());
		if constexpr (std::is_void_v<T>)
			Test.TestFalse("Can't set result", Coro.TrySetResult());
		else
			Test.TestFalse("Can't set result", Coro.TrySetResult(TEXT("X")));
	}

	{
		TManualCoroutine<T> Coro1;
		auto Coro2 = World.Run([&]() -> TCoroutine<T>
		{
			co_return co_await Coro1;
		});
		Test.TestFalse("Not done yet 1", Coro1.IsDone());
		Test.TestFalse("Not done yet 2", Coro2.IsDone());
		if constexpr (std::is_void_v<T>)
		{
			Test.TestTrue("Success 1A", Coro1.TrySetResult());
			Test.TestTrue("Success 1B", Coro1.WasSuccessful());
			Test.TestTrue("Success 2", Coro2.WasSuccessful());
		}
		else
		{
			Test.TestTrue("Success", Coro1.TrySetResult(TEXT("X")));
			Test.TestEqual("Result 1", Coro1.GetResult(), TEXT("X"));
			Test.TestEqual("Result 2", Coro2.GetResult(), TEXT("X"));
		}
	}

	// This test case needs -stompmalloc to be truly useful
	{
		TManualCoroutine<T>* InnerPtr;
		bool bDone = false;
		World.Run([&]() -> TCoroutine<>
		{
			ON_SCOPE_EXIT { bDone = true; };
			TManualCoroutine<T> Inner;
			InnerPtr = &Inner;
			if constexpr (std::is_void_v<T>)
			{
				co_await Inner;
				Test.TestTrue("Success", Inner.WasSuccessful());
			}
			else
			{
				Test.TestEqual("Result", co_await Inner, TEXT("X"));
				Test.TestTrue("Success", Inner.WasSuccessful());
			}
		});
		if constexpr (std::is_void_v<T>)
			InnerPtr->SetResult();
		else
			InnerPtr->SetResult(TEXT("X"));
		FTestHelper::PumpGameThread(World, [&] { return bDone; });
	}

	// This test case needs -stompmalloc to be truly useful
	{
		TManualCoroutine<T>* InnerPtr;
		bool bDone = false;
		World.Run([&]() -> TCoroutine<>
		{
			ON_SCOPE_EXIT { bDone = true; };
			TManualCoroutine<T> Inner;
			InnerPtr = &Inner;
			co_await Inner;
			Test.TestTrue("Canceled 1", Inner.IsDone());
			Test.TestFalse("Canceled 2", Inner.WasSuccessful());
		});
		InnerPtr->Cancel();
		FTestHelper::PumpGameThread(World, [&] { return bDone; });
	}
}
}

bool FManualCoroutineTest::RunTest(const FString& Parameters)
{
	DoTest<void>(*this);
	DoTest<FString>(*this);
	return true;
}
