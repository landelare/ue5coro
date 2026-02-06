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
using namespace UE5Coro::Async;
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCancelTestAsync, "UE5Coro.Cancel.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCancelTestLatent, "UE5Coro.Cancel.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	IF_CORO_LATENT
	{
		bool bCanceled = false, bAborted = false, bDestroyed = false;
		auto Coro = World.Run(CORO_R(int)
		{
			FOnCoroutineCanceled _1([&]
			{
				bCanceled = true;
				Test.TestTrue("Read cancellation from within",
				              IsCurrentCoroutineCanceled());
			});
			FOnActionAborted _2([&] { bAborted = true; });
			FOnObjectDestroyed _3([&] { bDestroyed = true; });
			Test.TestFalse("Not canceled yet", IsCurrentCoroutineCanceled());
			co_await FSelfCancellation();
			co_return 1;
		});
		Test.TestTrue("Done", Coro.IsDone());
		Test.TestTrue("Canceled", bCanceled);
		Test.TestFalse("Not aborted", bAborted);
		Test.TestFalse("Not destroyed", bDestroyed);
		Test.TestFalse("Not successful", Coro.WasSuccessful());
		Test.TestEqual("No return value", Coro.GetResult(), 0);
	}

	IF_CORO_LATENT
	{
		std::atomic<bool> bCanceled = false, bAborted = false,
		                  bDestroyed = false;
		auto Coro = World.Run(CORO_R(int)
		{
			FOnCoroutineCanceled _1([&]
			{
				Test.TestTrue("Back on the game thread", IsInGameThread());
				bCanceled = true;
			});
			FOnActionAborted _2([&] { bAborted = true; });
			FOnObjectDestroyed _3([&] { bDestroyed = true; });
			Test.TestFalse("Not canceled yet", IsCurrentCoroutineCanceled());
			co_await MoveToNewThread();
			co_await FSelfCancellation();
			co_return 1;
		});
		FTestHelper::PumpGameThread(World, [&] { return Coro.IsDone(); });
		Test.TestTrue("Canceled", bCanceled);
		Test.TestFalse("Not aborted", bAborted);
		Test.TestFalse("Not destroyed", bDestroyed);
		Test.TestFalse("Not successful", Coro.WasSuccessful());
		Test.TestEqual("No return value", Coro.GetResult(), 0);
	}

	{
		bool bCanceled = false, bDestroyed = false;
		auto Coro = World.Run(CORO
		{
			FOnCoroutineCanceled _([&]
			{
				bCanceled = true;
				Test.TestFalse("Not canceled", IsCurrentCoroutineCanceled());
			});
			ON_SCOPE_EXIT
			{
				bDestroyed = true;
				Test.TestFalse("Not canceled", IsCurrentCoroutineCanceled());
			};
			Test.TestFalse("Not canceled yet", IsCurrentCoroutineCanceled());
			co_return;
		});
		Test.TestTrue("Destroyed", bDestroyed);
		Test.TestFalse("Not canceled", bCanceled);
		Test.TestTrue("Successful", Coro.WasSuccessful());
	}

	{
		bool bCanceled = false, bDestroyed = false;
		std::optional<TCoroutine<>> Coro;
		{
			FTestWorld World2;
			Coro = World2.Run(CORO
			{
				FOnCoroutineCanceled _([&]
				{
					bCanceled = true;
					Test.TestTrue("Read cancellation from within 1",
					              IsCurrentCoroutineCanceled());
				});
				ON_SCOPE_EXIT
				{
					bDestroyed = true;
					Test.TestTrue("Read cancellation from within 2",
					              IsCurrentCoroutineCanceled());
				};
				Test.TestFalse("Not canceled yet",
				               IsCurrentCoroutineCanceled());
				co_await NextTick();
			});
			Test.TestFalse("Still running", Coro->WasSuccessful());
		} // Indirectly cancel by destroying the world during a latent co_await
		Test.TestTrue("Destroyed", bDestroyed);
		Test.TestTrue("Canceled", bCanceled);
		Test.TestFalse("Not successful", Coro->WasSuccessful());
	}

	{
		bool bCanceled = false, bDestroyed = false;
		auto Coro = World.Run(CORO
		{
			FOnCoroutineCanceled _([&]
			{
				bCanceled = true;
				Test.TestTrue("Read cancellation from within 1",
				              IsCurrentCoroutineCanceled());
			});
			ON_SCOPE_EXIT
			{
				bDestroyed = true;
				Test.TestTrue("Read cancellation from within 2",
				              IsCurrentCoroutineCanceled());
			};
			Test.TestFalse("Not canceled yet", IsCurrentCoroutineCanceled());
			co_await Ticks(1000);
		});
		World.EndTick();
		Test.TestFalse("Active", bCanceled);
		Test.TestFalse("Active", bDestroyed);
		Test.TestFalse("Not done yet", Coro.IsDone());
		Test.TestFalse("Still running", Coro.WasSuccessful());
		Coro.Cancel();
		Test.TestFalse("Not destroyed yet", bDestroyed);
		World.Tick(); // This processes the cancellation
		Test.TestTrue("Canceled", bCanceled);
		Test.TestTrue("Canceled", bDestroyed);
		Test.TestFalse("Not successful", Coro.WasSuccessful());
	}

	{
		std::atomic<bool> bDone = false;
		auto Coro = World.Run(CORO
		{
			ON_SCOPE_EXIT
			{
				bDone = true;
				IF_CORO_LATENT
					Test.TestTrue("Back on the game thread", IsInGameThread());
			};
			Test.TestFalse("Not canceled yet",
			               IsCurrentCoroutineCanceled());
			co_await MoveToThread(ENamedThreads::AnyThread);
			for (;;)
				co_await Yield();
		});
		for (int i = 0; i < 10; ++i)
		{
			World.Tick();
			Test.TestFalse("Still running", bDone);
		}
		Coro.Cancel();
		// Also acts as a busy wait for the async test
		FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
		Test.TestTrue("Canceled", bDone);
		Test.TestFalse("Not successful", Coro.WasSuccessful());
	}

	{
		bool bDone = false, bContinue = false;
		auto Coro = World.Run(CORO
		{
			ON_SCOPE_EXIT { bDone = true; };
			// First, run with cancellations blocked
			{
				FCancellationGuard _;
				while (!bContinue)
					co_await NextTick();
				Test.TestTrue("Incoming guarded cancellation",
				              IsCurrentCoroutineCanceled());
			}
			// Then, allow cancellations
			Test.TestTrue("Incoming unguarded cancellation",
			              IsCurrentCoroutineCanceled());
			for (;;)
				co_await NextTick();
		});
		Coro.Cancel();
		for (int i = 0; i < 10; ++i)
		{
			World.Tick();
			Test.TestFalse("Still running", bDone);
		}
		bContinue = true;
		World.Tick();
		Test.TestTrue("Canceled", bDone);
		Test.TestFalse("Not successful", Coro.WasSuccessful());
	}

	{
		bool bDone = false;
		auto Coro = World.Run(CORO
		{
			Test.TestFalse("Not canceled yet", IsCurrentCoroutineCanceled());
			co_await MoveToNewThread();
			ON_SCOPE_EXIT
			{
				bDone = true;
				IF_CORO_LATENT
					Test.TestTrue("Back on the game thread", IsInGameThread());
			};
			for (;;)
				co_await FinishNowIfCanceled();
		});
		for (int i = 0; i < 10; ++i)
		{
			World.Tick();
			Test.TestFalse("Still running", bDone);
		}
		Coro.Cancel();
		FTestHelper::PumpGameThread(World, [&] { return bDone; });
	}
}
}

bool FCancelTestAsync::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FCancelTestLatent::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
