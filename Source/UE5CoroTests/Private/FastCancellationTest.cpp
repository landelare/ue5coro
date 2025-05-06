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
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFastCancelTestAsyncST,
                                 "UE5Coro.Cancel.Fast.Async.ST",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFastCancelTestAsyncMT,
                                 "UE5Coro.Cancel.Fast.Async.MT",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFastCancelTestLatentST,
                                 "UE5Coro.Cancel.Fast.Latent.ST",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFastCancelTestLatentMT,
                                 "UE5Coro.Cancel.Fast.Latent.MT",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
class FTestCancelableAwaiter
	: public Private::TCancelableAwaiter<FTestCancelableAwaiter>
{
	FAutomationTestBase& Test;
	std::atomic<bool> bCanceled = false;

public:
	explicit FTestCancelableAwaiter(FAutomationTestBase& Test)
		: TCancelableAwaiter(&Cancel), Test(Test) { }

	FTestCancelableAwaiter(FTestCancelableAwaiter&& Other)
		: FTestCancelableAwaiter(Other.Test) { Other.bCanceled = true; }

	~FTestCancelableAwaiter() { Test.TestTrue("Canceled", bCanceled); }

	void Suspend(FPromise& Promise)
	{
		UE::TUniqueLock Lock(Promise.GetLock());
		if (!Promise.RegisterCancelableAwaiter(this))
			FAsyncYieldAwaiter::Suspend(Promise);
	}

	static void Cancel(void* This, FPromise& Promise)
	{
		auto* Awaiter = static_cast<FTestCancelableAwaiter*>(This);
		if (Promise.UnregisterCancelableAwaiter<false>())
		{
			Awaiter->bCanceled = true;
			FAsyncYieldAwaiter::Suspend(Promise);
		}
	}

	void await_resume() { Test.AddError("Awaiter was resumed"); }
};

template<bool bMultithreaded, typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	auto EventTest = [&](EEventMode Mode)
	{
		FAwaitableEvent Event(Mode);
		FEventRef CoroToTest;
		std::atomic<int> State = 0;
		auto Coro = World.Run(CORO
		{
			ON_SCOPE_EXIT { State = 99; };
			if constexpr (bMultithreaded)
				co_await Async::MoveToTask();
			for (int i = 1; i < 10; ++i)
			{
				State = i;
				CoroToTest->Trigger();
				co_await Event;
				if (Mode == EEventMode::ManualReset)
					Event.Reset();
			}
		});
		World.EndTick();
		CoroToTest->Wait();
		Test.TestEqual("Start", State, 1);
		CoroToTest->Reset();
		Event.Trigger();
		CoroToTest->Wait();
		Test.TestEqual("Coroutine resumes immediately", State, 2);
		Test.TestFalse("Event is reset", FTestHelper::ReadEvent(Event));
		CoroToTest->Reset();
		Coro.Cancel();
		FTestHelper::PumpGameThread(World, [&] { return State == 99; });
		Test.TestTrue("Coroutine done", Coro.IsDone());
		Event.Trigger();
		Test.TestEqual("State no longer changes", State, 99);
		Test.TestTrue("Event is still triggered",
		              FTestHelper::ReadEvent(Event));
	};
	EventTest(EEventMode::AutoReset);
	EventTest(EEventMode::ManualReset);

	for (int i = 1; i <= 2; ++i)
	{
		FAwaitableSemaphore Semaphore(i, 0);
		FEventRef CoroToTest;
		std::atomic<int> State = 0;
		auto Coro = World.Run(CORO
		{
			ON_SCOPE_EXIT { State = 99; };
			if constexpr (bMultithreaded)
				co_await Async::MoveToTask();
			for (int j = 1; j < 10; ++j)
			{
				State = j;
				CoroToTest->Trigger();
				co_await Semaphore;
			}
		});
		World.EndTick();
		CoroToTest->Wait();
		Test.TestEqual("Start", State, 1);
		CoroToTest->Reset();
		Semaphore.Unlock();
		CoroToTest->Wait();
		Test.TestEqual("Coroutine resumes immediately", State, 2);
		Test.TestEqual("Semaphore is used",
		               FTestHelper::ReadSemaphore(Semaphore), 0);
		CoroToTest->Reset();
		Coro.Cancel();
		FTestHelper::PumpGameThread(World, [&] { return State == 99; });
		Test.TestTrue("Coroutine done", Coro.IsDone());
		Test.TestEqual("Immediate cancellation", State, 99);
		Semaphore.Unlock();
		Test.TestEqual("State no longer changes", State, 99);
		Test.TestEqual("Semaphore is still available",
		               FTestHelper::ReadSemaphore(Semaphore), 1);
	}

	{
		FEventRef CoroToTest;
		std::atomic<bool> bDone = false, bWrong = false;
		auto Coro = World.Run(CORO
		{
			ON_SCOPE_EXIT { bDone = true; };
			if constexpr (bMultithreaded)
				co_await Async::MoveToTask();
			CoroToTest->Trigger();
			co_await Async::PlatformSeconds(10000);
			bWrong = true;
		});
		World.EndTick();
		CoroToTest->Wait();
		for (int i = 0; i < 10; ++i)
		{
			Test.TestFalse("Still active", bDone);
			World.Tick();
		}
		Coro.Cancel();
		FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
		Test.TestTrue("Coroutine done", Coro.IsDone());
		Test.TestFalse("Coroutine was canceled", bWrong);
	}

	IF_CORO_ASYNC_OR(!bMultithreaded) // Latent + bMultithreaded is invalid
	{
		FAwaitableEvent Coro1Event;
		auto Coro1 = World.Run(CORO
		{
			co_await Coro1Event;
		});
		FEventRef CoroToTest;
		std::atomic<bool> bDone = false, bWrong = false;
		auto Coro2 = World.Run(CORO
		{
			ON_SCOPE_EXIT { bDone = true; };
			if constexpr (bMultithreaded)
				co_await Async::MoveToTask();
			CoroToTest->Trigger();
			co_await Coro1; // This is a different awaiter in async and latent
			bWrong = true;
		});
		World.EndTick();
		CoroToTest->Wait();
		for (int i = 0; i < 10; ++i)
		{
			Test.TestFalse("Still active", bDone);
			World.Tick();
		}
		Coro2.Cancel();
		FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
		Test.TestTrue("Coroutine done", Coro2.IsDone());
		Coro1Event.Trigger();
		Test.TestTrue("Coro1 done", Coro1.IsDone());
		Test.TestFalse("Coroutine was canceled", bWrong);
	}

	for (int i = 0; i <= 1; ++i)
	{
		FAwaitableEvent Event;
		std::atomic<bool> bDone = false, bWrong = false;
		auto Coro = World.Run(CORO
		{
			ON_SCOPE_EXIT { bDone = true; };
			if constexpr (bMultithreaded)
				co_await Async::MoveToTask();
			if (i % 2)
				co_await WhenAll(Event, FTestCancelableAwaiter(Test));
			else
				co_await WhenAny(Event, FTestCancelableAwaiter(Test));
			bWrong = true;
		});
		World.EndTick();
		World.Tick();
		Test.TestFalse("Not done yet 1", bDone);
		Test.TestFalse("Not done yet 2", bWrong);
		Coro.Cancel();
		FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
		Test.TestTrue("Coroutine done", Coro.IsDone());
		Event.Trigger();
		World.Tick();
		Test.TestFalse("Coroutine was canceled", bWrong);
	}

	{
		FEventRef CoroToTest;
		std::atomic<bool> bDone = false, bInnerCancel = false;
		auto Inner = World.Run(CORO
		{
			FOnCoroutineCanceled _([&] { bInnerCancel = true; });
			co_await Latent::Seconds(2);
		});
		auto Coro = World.Run(CORO
		{
			ON_SCOPE_EXIT { bDone = true; };
			if constexpr (bMultithreaded)
				co_await Async::MoveToTask();
			CoroToTest->Trigger();
			co_await Race(Inner);
		});
		World.EndTick();
		CoroToTest->Wait();
		World.Tick();
		Test.TestFalse("Not done yet 1", bDone);
		Test.TestFalse("Not done yet 2", bInnerCancel);
		Coro.Cancel();
		FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
		Test.TestTrue("Coroutine done", Coro.IsDone());
		World.Tick();
		Test.TestTrue("Inner coroutine was canceled", bInnerCancel);
	}

	{
		FEventRef CoroToTest;
		bool bCanceled = false;
		std::atomic<bool> bWrong = false;
		auto Coro1 = World.Run(CORO
		{
			FOnCoroutineCanceled _([&] { bCanceled = true; });
			for (;;)
				co_await Latent::NextTick();
		});
		auto Coro2 = World.Run(CORO
		{
			if constexpr (bMultithreaded)
				co_await Async::MoveToTask();
			CoroToTest->Trigger();
			co_await Race(Coro1);
			bWrong = true;
		});
		World.EndTick();
		CoroToTest->Wait();
		Coro2.Cancel();
		Test.TestFalse("Cancellation not processed yet", bCanceled);
		World.Tick();
		FTestHelper::PumpGameThread(World, [&] { return bCanceled; });
		Test.TestFalse("Direct cancel", bWrong);
	}
}
}

bool FFastCancelTestAsyncST::RunTest(const FString& Parameters)
{
	DoTest<false>(*this);
	return true;
}

bool FFastCancelTestAsyncMT::RunTest(const FString& Parameters)
{
	DoTest<true>(*this);
	return true;
}

bool FFastCancelTestLatentST::RunTest(const FString& Parameters)
{
	DoTest<false, FLatentActionInfo>(*this);
	return true;
}

bool FFastCancelTestLatentMT::RunTest(const FString& Parameters)
{
	DoTest<true, FLatentActionInfo>(*this);
	return true;
}
