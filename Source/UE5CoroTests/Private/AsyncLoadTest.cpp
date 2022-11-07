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
#include "UE5Coro/LatentAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncLoadTestLatent, "UE5Coro.AsyncLoad.Latent",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncLoadTestAsync, "UE5Coro.AsyncLoad.Async",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
#define CORO [&](T...) -> FAsyncCoroutine
	FTestWorld World;

	{
		TStrongObjectPtr<UWorld> Object(World.operator->());
		UWorld* Result;
		FEventRef CoroToTest;
		World.Run(CORO
		{
			co_await Latent::NextTick();
			TSoftObjectPtr<UWorld> Soft = Object.Get();
			Result = co_await Latent::AsyncLoadObject(Soft);
			CoroToTest->Trigger();
		});
		FTestHelper::PumpGameThread(World, [&] { return CoroToTest->Wait(0); });
		Test.TestEqual(TEXT("Loaded"), Result, Object.Get());
	}

	{
		UClass* Result;
		FEventRef CoroToTest;
		World.Run(CORO
		{
			co_await Latent::NextTick();
			TSoftClassPtr<UObject> Soft = UObject::StaticClass();
			Result = co_await Latent::AsyncLoadClass(Soft);
			CoroToTest->Trigger();
		});
		FTestHelper::PumpGameThread(World, [&] { return CoroToTest->Wait(0); });
		Test.TestEqual(TEXT("Loaded"), Result, UObject::StaticClass());
	}

	{
		constexpr auto RawPath = TEXT("/Engine/BasicShapes/Cube");
		FPackagePath Path;
		bool bSuccess = FPackagePath::TryFromPackageName(RawPath, Path);
		Test.TestTrue(TEXT("Package path"), bSuccess);
		UPackage* Package = nullptr;
		FEventRef CoroToTest;
		World.Run(CORO
		{
			co_await Latent::NextTick();
			Package = co_await Latent::AsyncLoadPackage(Path);
			CoroToTest->Trigger();
		});
		FTestHelper::PumpGameThread(World, [&] { return CoroToTest->Wait(0); });
		Test.TestEqual(TEXT("Package"), Package->GetName(), RawPath);
	}

#undef CORO
}
}

bool FAsyncLoadTestLatent::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}

bool FAsyncLoadTestAsync::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}
