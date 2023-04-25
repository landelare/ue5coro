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

#include "HttpModule.h"
#include "TestWorld.h"
#include "Misc/AutomationTest.h"
#include "UE5Coro/AsyncAwaiters.h"
#include "UE5Coro/HttpAwaiters.h"
#include "UE5Coro/LatentAwaiters.h"
#include "UE5Coro/TaskAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHttpAsyncTest, "UE5Coro.Http.Async",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHttpLatentTest, "UE5Coro.Http.Latent",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	std::atomic<bool> bDone = false;
	World.Run(CORO
	{
		auto Request = FHttpModule::Get().CreateRequest();
		// We're not testing HTTP, just the awaiter
		Request->SetURL(TEXT(".invalid"));
		Request->SetTimeout(0.01);
		auto Awaiter = Http::ProcessAsync(Request);
		auto AwaiterCopy = Awaiter;
		auto [Response, bSuccess] = co_await AwaiterCopy;
		Test.TestFalse(TEXT("Success"), bSuccess);
		Test.TestTrue(TEXT("Response"), static_cast<bool>(Response));
		bSuccess = true;
		Tie(Response, bSuccess) = co_await Awaiter;
		Test.TestFalse(TEXT("Success"), bSuccess);
		Test.TestTrue(TEXT("Response"), static_cast<bool>(Response));
		bDone = true;
	});
	FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });

	bDone = false;
	World.Run(CORO
	{
		co_await Tasks::MoveToTask();
		FPlatformMisc::MemoryBarrier();
		Test.TestFalse(TEXT("Not in game thread 1"), IsInGameThread());
		auto Request = FHttpModule::Get().CreateRequest();
		// We're not testing HTTP, just the awaiter
		Request->SetURL(TEXT(".invalid"));
		Request->SetTimeout(0.01);
		auto [Response, bSuccess] = co_await Http::ProcessAsync(Request);
		Test.TestFalse(TEXT("Not in game thread 2"), IsInGameThread());
		Test.TestFalse(TEXT("Success"), bSuccess);
		Test.TestTrue(TEXT("Response"), static_cast<bool>(Response));
		FPlatformMisc::MemoryBarrier();
		bDone = true;
	});
	// Test is being used by the coroutine on another thread here
	FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });

	World.Run(CORO
	{
		auto Request = FHttpModule::Get().CreateRequest();
		// We're not testing HTTP, just the awaiter
		Request->SetURL(TEXT(".invalid"));
		Request->SetTimeout(0.01);
		[[maybe_unused]] auto Unused = Http::ProcessAsync(Request);
		co_await Latent::NextTick(); // Run() requires some co_await
	});
	World.Tick(); // Nothing to test here besides not crashing
}
}

bool FHttpAsyncTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FHttpLatentTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
