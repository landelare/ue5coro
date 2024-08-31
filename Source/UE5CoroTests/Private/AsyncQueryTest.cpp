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
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncQueryTestAsync, "UE5Coro.AsyncQuery.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncQueryTestLatent, "UE5Coro.AsyncQuery.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	{
		int State = -1;
		World.Run(CORO
		{
			auto Result = co_await AsyncLineTraceByChannel(
				World, EAsyncTraceType::Single, FVector::ZeroVector,
				FVector::UpVector, ECC_WorldStatic);
			State = Result.Num();
		});
		World.Tick(); // This queries the async trace on the game thread
		Test.TestEqual("No results yet", State, -1);
		World.Tick(); // This completes it
		Test.TestEqual("Results", State, 0);
	}

	{
		int State = -1;
		World.Run(CORO
		{
			auto Result = co_await AsyncOverlapByChannel(
				World, FVector::ZeroVector, FQuat::Identity, ECC_WorldStatic,
				FCollisionShape::MakeBox(FVector::OneVector));
			State = Result.Num();
		});
		World.Tick(); // This queries the async trace on the game thread
		Test.TestEqual("No results yet", State, -1);
		World.Tick(); // This completes it
		Test.TestEqual("Results", State, 0);
	}

	{
		int State = -1;
		World.Run(CORO
		{
			auto Awaiter = AsyncLineTraceByChannel(
				World, EAsyncTraceType::Single, FVector::ZeroVector,
				FVector::UpVector, ECC_WorldStatic);
			co_await Ticks(2); // Make sure the async query is complete
			Test.TestTrue("Ready", Awaiter.await_ready());
			State = (co_await Awaiter).Num();
		});
		World.Tick(); // This queries the async trace on the game thread
		Test.TestEqual("No results yet", State, -1);
		World.Tick(); // This completes it
		Test.TestEqual("No results yet", State, -1);
		World.Tick(); // This will end Ticks(2)
		Test.TestEqual("Results", State, 0);
	}

	{
		int State = -1;
		World.Run(CORO
		{
			auto Awaiter = AsyncOverlapByChannel(
				World, FVector::ZeroVector, FQuat::Identity, ECC_WorldStatic,
				FCollisionShape::MakeBox(FVector::OneVector));
			co_await Ticks(2); // Make sure the async query is complete
			Test.TestTrue("Ready", Awaiter.await_ready());
			State = (co_await std::move(Awaiter)).Num();
		});
		World.Tick(); // This queries the async trace on the game thread
		Test.TestEqual("No results yet", State, -1);
		World.Tick(); // This completes it
		Test.TestEqual("No results yet", State, -1);
		World.Tick(); // This will end Ticks(2)
		Test.TestEqual("Results", State, 0);
	}
}
}

bool FAsyncQueryTestAsync::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FAsyncQueryTestLatent::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
