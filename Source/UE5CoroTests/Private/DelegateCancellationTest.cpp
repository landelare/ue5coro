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
#include "TestDelegates.h"
#include "TestWorld.h"
#include "UE5Coro.h"
#include "UE5CoroTestObject.h"

using namespace UE5Coro;
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDelegateCancellationTestAsync,
                                 "UE5Coro.Cancel.Fast.Delegate.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDelegateCancellationTestLatent,
                                 "UE5Coro.Cancel.Fast.Delegate.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseDelegateCancellationTestAsync,
                                 "UE5Coro.Cancel.Fast.Delegate.Sparse.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseDelegateCancellationTestLatent,
                                 "UE5Coro.Cancel.Fast.Delegate.Sparse.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<int N, typename... T>
void DoTest(FAutomationTestBase& Test)
{
	using TSelector = TDelegateSelector<N>;
	constexpr bool bMultithreaded = (N & 16) != 0;
	using TTestDelegate = typename TSelector::type;

	if constexpr (!std::is_void_v<TTestDelegate>)
	{
		FTestWorld World;

		std::atomic<bool> bDone = false, bWrong = false;
		FEventRef CoroToTest;
		TTestDelegate Delegate;
		auto Coro = World.Run(CORO
		{
			ON_SCOPE_EXIT { bDone = true; };
			if constexpr (bMultithreaded)
				co_await Async::MoveToTask();
			CoroToTest->Trigger();
			co_await Delegate;
			bWrong = true;
		});
		World.EndTick();
		CoroToTest->Wait();
		Coro.Cancel();
		FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
		Test.TestTrue("Coroutine done", Coro.IsDone());
		Test.TestFalse("Coroutine was canceled", bWrong);
	}
}

template<int N, typename... T>
void DoTests(FAutomationTestBase& Test)
{
	if constexpr (N <= 31)
	{
		DoTest<N, T...>(Test);
		DoTests<N + 1, T...>(Test);
	}
}

template<bool bMultithreaded, bool bParams, typename... T>
void DoSparseTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	FEventRef CoroToTest;
	std::atomic<bool> bDone = false, bWrong = false;
	auto* Object = NewObject<UUE5CoroTestObject>(World);
	auto Coro = World.Run(CORO
	{
		ON_SCOPE_EXIT { bDone = true; };
		if constexpr (bMultithreaded)
			co_await Async::MoveToTask();
		CoroToTest->Trigger();
		if constexpr (bParams)
			co_await Object->SparseParamsDelegate;
		else
			co_await Object->SparseDelegate;
		bWrong = true;
	});
	World.EndTick();
	CoroToTest->Wait();
	Coro.Cancel();
	FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
	Test.TestTrue("Coroutine done", Coro.IsDone());
	int Two = 2;
	Object->SparseDelegate.Broadcast();
	Object->SparseParamsDelegate.Broadcast(1, Two);
	Test.TestFalse("Coroutine was canceled", bWrong);
}
}

bool FDelegateCancellationTestAsync::RunTest(const FString& Parameters)
{
	DoTests<0>(*this);
	return true;
}

bool FDelegateCancellationTestLatent::RunTest(const FString& Parameters)
{
	DoTests<0, FLatentActionInfo>(*this);
	return true;
}

template<int N, typename... T>
void DoSparseTests(FAutomationTestBase& Test)
{
	if constexpr (N <= 3)
	{
		DoSparseTest<(N & 2) != 0, (N & 1) != 0, T...>(Test);
		DoSparseTests<N + 1, T...>(Test);
	}
}

bool FSparseDelegateCancellationTestAsync::RunTest(const FString& Parameters)
{
	DoSparseTests<0>(*this);
	return true;
}

bool FSparseDelegateCancellationTestLatent::RunTest(const FString& Parameters)
{
	DoSparseTests<0, FLatentActionInfo>(*this);
	return true;
}
