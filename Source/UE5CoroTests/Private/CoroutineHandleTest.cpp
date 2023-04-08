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

#include <map>
#include "TestWorld.h"
#include "Misc/AutomationTest.h"
#include "UE5CoroTestObject.h"
#include "UE5Coro/Coroutine.h"
#include "UE5Coro/AsyncAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandleTestAsync, "UE5Coro.Handle.Async",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandleTestLatent, "UE5Coro.Handle.Latent",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter);

namespace
{
template<typename U>
using TThreadSafeSharedPtr = TSharedPtr<U, ESPMode::ThreadSafe>;

template<template<typename> typename S, typename... T>
void DoTestSharedPtr(FTestWorld& World, FAutomationTestBase& Test)
{
	{
		S<int> Ptr(new int(0));
		auto Coro = World.Run(CORO { co_await Latent::NextTick(); });
		Coro.ContinueWithWeak(Ptr, [](int* Value) { *Value = 1; });
		World.EndTick();
		Test.TestEqual(TEXT("Not completed yet"), *Ptr, 0);
		World.Tick();
		Test.TestEqual(TEXT("Completed"), *Ptr, 1);
	}

	{
		S<int> Ptr(new int(0));
		bool bContinued = false;
		auto Coro = World.Run(CORO { co_await Latent::NextTick(); });
		Coro.ContinueWithWeak(Ptr, [&] { bContinued = true; });
		World.EndTick();
		Test.TestFalse(TEXT("Not completed yet"), bContinued);
		Ptr = nullptr;
		World.Tick();
		Test.TestFalse(TEXT("No continuation"), bContinued);
	}

	struct FBoolSetter
	{
		bool* Ptr;
		void Set() { *Ptr = true; }
	};

	{
		bool bContinued = false;
		S<FBoolSetter> Ptr(new FBoolSetter{&bContinued});
		auto Coro = World.Run(CORO { co_await Latent::NextTick(); });
		Coro.ContinueWithWeak(Ptr, &FBoolSetter::Set);
		World.EndTick();
		Test.TestFalse(TEXT("Not completed yet"), bContinued);
		World.Tick();
		Test.TestTrue(TEXT("Completed"), bContinued);
	}

	{
		bool bContinued = false;
		S<FBoolSetter> Ptr(new FBoolSetter{&bContinued});
		auto Coro = World.Run(CORO { co_await Latent::NextTick(); });
		Coro.ContinueWithWeak(Ptr, &FBoolSetter::Set);
		World.EndTick();
		Test.TestFalse(TEXT("Not completed yet"), bContinued);
		Ptr = nullptr;
		World.Tick();
		Test.TestFalse(TEXT("No continuation"), bContinued);
	}

	struct FIntSetter
	{
		int* Ptr;
		void Set(int Value) const { *Ptr = Value; }
	};

	{
		int State = 0;
		S<const FIntSetter> Ptr(new FIntSetter{&State});
		auto Coro = World.Run(CORO_R(int)
		{
			co_await Latent::NextTick();
			co_return 1;
		});
		Coro.ContinueWithWeak(Ptr, &FIntSetter::Set);
		World.EndTick();
		Test.TestEqual(TEXT("Not completed yet"), State, 0);
		World.Tick();
		Test.TestEqual(TEXT("Completed"), State, 1);
	}

	{
		int State = 0;
		S<FIntSetter> Ptr(new FIntSetter{&State});
		auto Coro = World.Run(CORO_R(int)
		{
			co_await Latent::NextTick();
			co_return 1;
		});
		Coro.ContinueWithWeak(Ptr, &FIntSetter::Set);
		World.EndTick();
		Test.TestEqual(TEXT("Not completed yet"), State, 0);
		Ptr = nullptr;
		World.Tick();
		Test.TestEqual(TEXT("No continuation"), State, 0);
	}
}

template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	{
		FEventRef StartTest;
		auto Coro = World.Run(CORO
		{
			co_await Async::MoveToNewThread();
			StartTest->Wait();
			FPlatformProcess::Sleep(0.1f);
		});

		// Waiting itself has to run on another thread.
		// In the latent case, not doing this would deadlock the game thread.
		std::atomic<bool> bDone;
		World.Run(CORO
		{
			co_await Async::MoveToNewThread();
			StartTest->Trigger();
			Test.TestFalse(TEXT("Timeout"), Coro.Wait(1));
			Test.TestTrue(TEXT("Waited enough"), Coro.Wait());
			bDone = true;
		});
		FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
	}

	{
		int Value = 0;
		World.Run(CORO_R(int) { co_return 1; })
		     .ContinueWith([&](int InValue) { Value = InValue; });
		Test.TestEqual(TEXT("Value"), Value, 1);
	}

	{
		FEventRef TestToCoro, CoroToTest;
		TStrongObjectPtr Object(NewObject<UUE5CoroTestObject>());
		auto Coro = World.Run(CORO
		{
			co_await Async::MoveToNewThread();
			TestToCoro->Wait();
			// Unconditionally move to the GT, ContinueWithWeak is on a UObject
			co_await Async::MoveToGameThread();
		});
		Coro.ContinueWithWeak(Object.Get(), [&] { CoroToTest->Trigger(); });
		Test.TestFalse(TEXT("Not triggered yet"), CoroToTest->Wait(0));
		TestToCoro->Trigger();
		FTestHelper::PumpGameThread(World, [&] { return Coro.IsDone(); });
		CoroToTest->Wait();
		Test.TestTrue(TEXT("Done"), true);
	}

	{
		FEventRef TestToCoro, CoroToTest;
		TStrongObjectPtr Object(NewObject<UUE5CoroTestObject>());
		Object->Callback = [&] { CoroToTest->Trigger(); };
		auto Coro = World.Run(CORO
		{
			co_await Async::MoveToNewThread();
			TestToCoro->Wait();
			// Unconditionally move to the GT, ContinueWithWeak is on a UObject
			co_await Async::MoveToGameThread();
		});
		Coro.ContinueWithWeak(Object.Get(), &UUE5CoroTestObject::RunCallback);
		Test.TestFalse(TEXT("Not triggered yet"), CoroToTest->Wait(0));
		TestToCoro->Trigger();
		FTestHelper::PumpGameThread(World, [&] { return Coro.IsDone(); });
		CoroToTest->Wait();
		Test.TestTrue(TEXT("Done"), true);
	}

	{
		FEventRef TestToCoro;
		std::atomic<bool> bContinued = false;
		TWeakObjectPtr Object(NewObject<UUE5CoroTestObject>());
		auto Coro = World.Run(CORO
		{
			co_await Async::MoveToNewThread();
			TestToCoro->Wait();
			// Unconditionally move to the GT, ContinueWithWeak is on a UObject
			co_await Async::MoveToGameThread();
		});
		Coro.ContinueWithWeak(Object.Get(), [&] { bContinued = true; });
		Object->MarkAsGarbage();
		CollectGarbage(RF_NoFlags);
		Test.TestFalse(TEXT("Object destroyed"), Object.IsValid());
		Test.TestFalse(TEXT("Coroutine still running"), Coro.IsDone());
		TestToCoro->Trigger();
		FTestHelper::PumpGameThread(World, [&] { return Coro.IsDone(); });
		// There's a data race with IsDone() when async, wait a little
		for (int i = 0; i < 10; ++i)
			World.Tick();
		Test.TestFalse(TEXT("Continuation not called"), bContinued);
	}

	{
		auto Coro = TCoroutine<>::CompletedCoroutine;
		Test.TestTrue(TEXT("Completed"), Coro.IsDone());

		auto Ptr = std::make_unique<int>(1); // move-only
		auto Coro1 = TCoroutine<>::FromResult(std::move(Ptr));
		auto Coro2 = TCoroutine<>::FromResult(2);
		auto Coro3 = TCoroutine<int>::FromResult(3);
		Test.TestTrue(TEXT("Completed 1"), Coro1.IsDone());
		Test.TestTrue(TEXT("Completed 2"), Coro2.IsDone());
		Test.TestTrue(TEXT("Completed 3"), Coro3.IsDone());
		Test.TestNull(TEXT("Moved from"), Ptr.get());
		Test.TestEqual(TEXT("Moved to"), *Coro1.GetResult(), 1);
		Test.TestEqual(TEXT("Coro2"), Coro2.GetResult(), 2);
		Test.TestEqual(TEXT("Coro3"), Coro3.MoveResult(), 3);
	}

	{
		TMap<FAsyncCoroutine, int> Map1;
		TSortedMap<TCoroutine<int>, int> Map2;
		std::unordered_map<TCoroutine<>, int> Map3;
		std::map<TCoroutine<int>, int> Map4;
		for (int i = 0; i < 5; ++i)
		{
			Map1.Add(World.Run(CORO_R(int) { co_return i; }), i);
			Map2.Add(World.Run(CORO_R(int) { co_return i; }), i);
			Map3[World.Run(CORO_R(int) { co_return i; })] = i;
			Map4[World.Run(CORO_R(int) { co_return i; })] = i;
		}
		auto TestMap = [&](auto& Map)
		{
			for (auto& [Key, Value] : Map)
				Test.TestEqual(TEXT("Value"), Map[Key], Value);
		};
		TestMap(Map1);
		TestMap(Map2);
		TestMap(Map3);
		TestMap(Map4);
	}

	DoTestSharedPtr<TThreadSafeSharedPtr, T...>(World, Test);
	DoTestSharedPtr<std::shared_ptr, T...>(World, Test);
}
}

bool FHandleTestAsync::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FHandleTestLatent::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
