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
#include "Misc/EngineVersionComparison.h"
#include "UE5Coro.h"

using namespace UE5Coro;
using namespace UE5Coro::Async;
using namespace UE5Coro::Http;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHttpAsyncTest, "UE5Coro.HTTP.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHttpLatentTest, "UE5Coro.HTTP.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
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
		auto Awaiter = ProcessAsync(Request);
		auto AwaiterCopy = Awaiter;
		auto [Response, bSuccess] = co_await AwaiterCopy;
		Test.TestFalse("Failed copy", bSuccess);
		Test.TestFalse("No response from copy", static_cast<bool>(Response));
		bSuccess = true;
		Tie(Response, bSuccess) = co_await Awaiter;
		Test.TestFalse("Failed original", bSuccess);
		Test.TestFalse("No response from original", static_cast<bool>(Response));
		bDone = true;
	});
	FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });

	bDone = false;
	World.Run(CORO
	{
		co_await MoveToTask();
		FPlatformMisc::MemoryBarrier();
		Test.TestFalse("Not in game thread 1", IsInGameThread());
		auto Request = FHttpModule::Get().CreateRequest();
		// We're not testing HTTP, just the awaiter
		Request->SetURL(TEXT(".invalid"));
		Request->SetTimeout(0.01);
		auto [Response, bSuccess] = co_await ProcessAsync(Request);
		Test.TestFalse("Not in game thread 2", IsInGameThread());
		Test.TestFalse("Success", bSuccess);
		Test.TestFalse("Response", static_cast<bool>(Response));
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
		[[maybe_unused]] auto Unused = ProcessAsync(Request);
		co_return;
	});
	World.Tick(); // Nothing to test here besides not crashing

	// To patch the bug in 5.3, make sure the HTTP thread is ticked
#if UE_VERSION_NEWER_THAN(5, 3, 2)
	bDone = false;
	World.Run(CORO
	{
		auto Request = FHttpModule::Get().CreateRequest();
		Request->SetDelegateThreadPolicy(
			EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		Request->SetURL(TEXT(".invalid"));
		Request->SetTimeout(0.01);
		co_await Http::ProcessAsync(Request);
		FPlatformMisc::MemoryBarrier();
		Test.TestFalse("Not GT", IsInGameThread());
		FPlatformMisc::MemoryBarrier();
		bDone = true;
	});
	FTestHelper::PumpGameThread(World, [&] { return bDone.load(); });
#endif
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
