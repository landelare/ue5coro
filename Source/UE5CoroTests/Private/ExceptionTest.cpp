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

#include <exception>
#include "TestWorld.h"
#include "Misc/AutomationTest.h"
#include "UE5Coro.h"
#include "UE5CoroTestObject.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

// Enable exceptions for this module and UE5Coro itself to test this
#if !PLATFORM_EXCEPTIONS_DISABLED

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExceptionTest, "UE5Coro.Exceptions",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

bool FExceptionTest::RunTest(const FString& Parameters)
{
	FTestWorld World;

	try
	{
		auto Fn = []() -> TGenerator<int>
		{
			co_yield 1;
			throw 123;
		};
		auto Gen = Fn();
		TestEqual("Generator init value", Gen.Current(), 1);
		Gen.Resume();
		AddError("Generator unreachable code");
	}
	catch (int Value)
	{
		TestEqual("Generator exception", Value, 123);
	}
	catch (...)
	{
		AddError("Generator unexpected exception");
	}

	std::optional<TCoroutine<>> Coro;
	try
	{
		auto Fn = [&]() -> TCoroutine<>
		{
			co_await std::suspend_never();
			Coro = static_cast<TCoroutinePromise<void, FAsyncPromise>&>(
				FPromise::Current()).get_return_object();
			throw 456;
		};
		Coro = std::nullopt;
		Fn();
		AddError("Async unreachable code");
	}
	catch (int Value)
	{
		TestEqual("Async exception", Value, 456);
	}
	catch (...)
	{
		AddError("Async unexpected exception");
	}
	TestTrue("Handle captured", Coro.has_value());
	TestTrue("Done", Coro->IsDone());
	TestFalse("Not successful", Coro->WasSuccessful());

	try
	{
		auto Fn = [&](FLatentActionInfo) -> TCoroutine<>
		{
			co_await std::suspend_never();
			Coro = static_cast<TCoroutinePromise<void, FLatentPromise>&>(
				FPromise::Current()).get_return_object();
			throw 789;
		};
		FLatentActionInfo Info(0, 0, nullptr,
		                       NewObject<UUE5CoroTestObject>(World));
		Coro = std::nullopt;
		Fn(Info);
		AddError("Latent unreachable code");
	}
	catch (int Value)
	{
		TestEqual("Latent exception", Value, 789);
	}
	catch (...)
	{
		AddError("Latent unexpected exception");
	}
	TestTrue("Handle captured", Coro.has_value());
	TestTrue("Done", Coro->IsDone());
	TestFalse("Not successful", Coro->WasSuccessful());

	// Check if FLatentPromise detached correctly, i.e., these don't crash
	World.Tick();
	World.Tick();
	World.Tick();

	return true;
}

#endif
