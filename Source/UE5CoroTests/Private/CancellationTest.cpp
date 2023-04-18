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
#include "UE5Coro/AsyncAwaiters.h"
#include "UE5Coro/Cancellation.h"
#include "UE5Coro/LatentAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCancelTestAsync, "UE5Coro.Cancel.Async",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCancelTestLatent, "UE5Coro.Cancel.Latent",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter);

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	IF_CORO_LATENT
	{
		bool bCanceled = false;
		auto Coro = World.Run(CORO_R(int)
		{
			FOnCoroutineCanceled _([&]
			{
				bCanceled = true;
				Test.TestTrue(TEXT("Read cancellation from within"),
				              IsCurrentCoroutineCanceled());
			});
			co_await Latent::Cancel();
			co_return 1;
		});
		Test.TestTrue(TEXT("Done"), Coro.IsDone());
		Test.TestTrue(TEXT("Canceled"), bCanceled);
		Test.TestEqual(TEXT("No return value"), Coro.GetResult(), 0);
	}

	IF_CORO_LATENT
	{
		std::atomic<bool> bCanceled = false;
		auto Coro = World.Run(CORO_R(int)
		{
			FOnCoroutineCanceled _([&] { bCanceled = true; });
			Test.TestFalse(TEXT("Not canceled yet"),
			               IsCurrentCoroutineCanceled());
			co_await Async::MoveToNewThread();
			co_await Latent::Cancel();
			co_return 1;
		});
		FTestHelper::PumpGameThread(World, [&] { return Coro.IsDone(); });
		Test.TestTrue(TEXT("Canceled"), bCanceled);
		Test.TestEqual(TEXT("No return value"), Coro.GetResult(), 0);
	}

	{
		bool bCanceled = false;
		bool bDestroyed = false;
		World.Run(CORO
		{
			FOnCoroutineCanceled _([&]
			{
				bCanceled = true;
				Test.TestFalse(TEXT("Not canceled"),
				               IsCurrentCoroutineCanceled());
			});
			ON_SCOPE_EXIT
			{
				bDestroyed = true;
				Test.TestFalse(TEXT("Not canceled"),
				               IsCurrentCoroutineCanceled());
			};
			Test.TestFalse(TEXT("Not canceled yet"),
			               IsCurrentCoroutineCanceled());
			co_return;
		});
		Test.TestTrue(TEXT("Destroyed"), bDestroyed);
		Test.TestFalse(TEXT("Not canceled"), bCanceled);
	}

	{
		bool bCanceled = false;
		bool bDestroyed = false;
		{
			FTestWorld World2;
			World2.Run(CORO
			{
				FOnCoroutineCanceled _([&]
				{
					bCanceled = true;
					Test.TestTrue(TEXT("Read cancellation from within"),
					              IsCurrentCoroutineCanceled());
				});
				ON_SCOPE_EXIT
				{
					bDestroyed = true;
					Test.TestTrue(TEXT("Read cancellation from within"),
					              IsCurrentCoroutineCanceled());
				};
				Test.TestFalse(TEXT("Not canceled yet"),
				               IsCurrentCoroutineCanceled());
				co_await Latent::NextTick();
			});
		} // Indirectly cancel by destroying the world during a latent co_await
		Test.TestTrue(TEXT("Destroyed"), bDestroyed);
		Test.TestTrue(TEXT("Canceled"), bCanceled);
	}

	{
		bool bCanceled = false;
		bool bDestroyed = false;
		auto Coro = World.Run(CORO
		{
			FOnCoroutineCanceled _([&]
			{
				bCanceled = true;
				Test.TestTrue(TEXT("Read cancellation from within"),
				              IsCurrentCoroutineCanceled());
			});
			ON_SCOPE_EXIT
			{
				bDestroyed = true;
				Test.TestTrue(TEXT("Read cancellation from within"),
				              IsCurrentCoroutineCanceled());
			};
			Test.TestFalse(TEXT("Not canceled yet"),
			               IsCurrentCoroutineCanceled());
			co_await Latent::Ticks(5);
		});
		World.EndTick();
		Test.TestFalse(TEXT("Active"), bCanceled);
		Test.TestFalse(TEXT("Active"), bDestroyed);
		Coro.Cancel();
		for (int i = 0; i < 5; ++i) // Async needs to attempt to resume
		{
			IF_NOT_CORO_LATENT // Latent->latent sees the cancellation right away
				Test.TestFalse(TEXT("Not canceled yet"), bDestroyed);
			World.Tick();
		}
		Test.TestTrue(TEXT("Canceled"), bCanceled);
		Test.TestTrue(TEXT("Canceled"), bDestroyed);
	}

	{
		std::atomic<bool> bDone = false;
		auto Coro = World.Run(CORO
		{
			ON_SCOPE_EXIT { bDone = true; };
			Test.TestFalse(TEXT("Not canceled yet"),
			               IsCurrentCoroutineCanceled());
			co_await Async::MoveToThread(ENamedThreads::AnyThread);
			for (;;)
				co_await Async::Yield();
		});
		for (int i = 0; i < 10; ++i)
		{
			World.Tick();
			Test.TestFalse(TEXT("Still running"), bDone);
		}
		Coro.Cancel();
		// Also acts as a busy wait for the async test
		FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
		Test.TestTrue(TEXT("Canceled"), bDone);
	}

	{
		bool bDone = false;
		bool bContinue = false;
		auto Coro = World.Run(CORO
		{
			ON_SCOPE_EXIT { bDone = true; };
			// First, run with cancellations blocked
			{
				FCancellationGuard _;
				while (!bContinue)
					co_await Latent::NextTick();
				Test.TestTrue(TEXT("Incoming guarded cancellation"),
				              IsCurrentCoroutineCanceled());
			}
			// Then, allow cancellations
			Test.TestTrue(TEXT("Incoming unguarded cancellation"),
			              IsCurrentCoroutineCanceled());
			for (;;)
				co_await Latent::NextTick();
		});
		Coro.Cancel();
		for (int i = 0; i < 10; ++i)
		{
			World.Tick();
			Test.TestFalse(TEXT("Still running"), bDone);
		}
		bContinue = true;
		World.Tick();
		// Async->Latent await needs an extra tick to figure this out
		IF_NOT_CORO_LATENT
			World.Tick();
		Test.TestTrue(TEXT("Canceled"), bDone);
	}

	{
		bool bDone = false;
		auto Coro = World.Run(CORO
		{
			Test.TestFalse(TEXT("Not canceled yet"),
			               IsCurrentCoroutineCanceled());
			co_await Async::MoveToNewThread();
			ON_SCOPE_EXIT { bDone = true; };
			for (;;)
				co_await FinishNowIfCanceled();
		});
		for (int i = 0; i < 10; ++i)
		{
			World.Tick();
			Test.TestFalse(TEXT("Still running"), bDone);
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
