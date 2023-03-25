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
#include "UE5CoroTestObject.h"
#include "UE5Coro/AsyncAwaiters.h"
#include "UE5Coro/Generator.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

// Enable exceptions for this module and UE5Coro itself to test this
#if !PLATFORM_EXCEPTIONS_DISABLED

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExceptionTest, "UE5Coro.Exceptions",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
class FTestException final : public std::exception
{
	const char* What;

public:
	explicit FTestException(const char* What) : What(What) { }
	virtual const char* what() const noexcept override { return What; }
};
}

bool FExceptionTest::RunTest(const FString& Parameters)
{
	FTestWorld World; // This is not used but it needs to exist in this scope

	try
	{
		auto Fn = []() -> TGenerator<int>
		{
			co_yield 1;
			throw FTestException("test");
		};
		auto Gen = Fn();
		TestEqual(TEXT("Generator init value"), Gen.Current(), 1);
		Gen.Resume();
		TestTrue(TEXT("Generator unreachable code"), false);
	}
	catch (const FTestException& Ex)
	{
		TestEqual(TEXT("Generator exception"), Ex.what(), "test");
	}
	catch (...)
	{
		TestTrue(TEXT("Generator unexpected exception"), false);
	}

	try
	{
		auto Fn = [&]() -> TCoroutine<>
		{
			co_await stdcoro::suspend_never();
			throw FTestException("async");
		};
		auto Coro = Fn();
		TestTrue(TEXT("Async unreachable code"), false);
	}
	catch (const FTestException& Ex)
	{
		TestEqual(TEXT("Async exception"), Ex.what(), "async");
	}
	catch (...)
	{
		TestTrue(TEXT("Async unexpected exception"), false);
	}

	try
	{
		auto Fn = [&](FLatentActionInfo) -> TCoroutine<>
		{
			co_await stdcoro::suspend_never();
			throw FTestException("latent");
		};
		FLatentActionInfo Info(0, 0, nullptr, NewObject<UUE5CoroTestObject>());
		auto Coro = Fn(Info);
		TestTrue(TEXT("Latent unreachable code"), false);
	}
	catch (const FTestException& Ex)
	{
		TestEqual(TEXT("Latent exception"), Ex.what(), "latent");
	}
	catch (...)
	{
		TestTrue(TEXT("Latent unexpected exception"), false);
	}

	// Check if FLatentPromise detached correctly
	World.Tick();
	World.Tick();
	World.Tick();

	return true;
}

#endif
