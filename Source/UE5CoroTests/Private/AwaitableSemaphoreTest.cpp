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
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSemaAsyncTest, "UE5Coro.Threading.Semaphore.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::CriticalPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSemaLatentTest, "UE5Coro.Threading.Semaphore.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::CriticalPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	{
		int State = 0;
		FAwaitableSemaphore Semaphore(10, 0);
		World.Run(CORO
		{
			++State;
			co_await Semaphore;
			++State;
			co_await Semaphore;
			++State;
			co_await Semaphore;
			++State;
			co_await Semaphore;
			++State;
		});

		Test.TestEqual("Initial state", State, 1);
		Semaphore.Unlock();
		Test.TestEqual("Unlock 1", State, 2);
		Semaphore.Unlock(2);
		Test.TestEqual("Unlock 2", State, 4);
		Semaphore.Unlock();
	}

	{
		int State = 0, Count = 0;
		FAwaitableSemaphore Semaphore(100, 50);
		World.Run(CORO
		{
			for (int i = 1; i < 20; ++i)
			{
				for (int j = 0; j < i; ++j)
				{
					co_await Semaphore;
					++Count;
				}
				State = i;
			}
		});
		Test.TestEqual("Initial state", State, 9);
		Test.TestEqual("Initial count", Count, 50);
		Semaphore.Unlock(4);

		Test.TestEqual("State 2", State, 9);
		Test.TestEqual("Count 2", Count, 54);
		Semaphore.Unlock();
		Test.TestEqual("State 3", State, 10);
		Test.TestEqual("Count 3", Count, 55);
		Semaphore.Unlock(100);
		Test.TestEqual("State 4", State, 17);
		Test.TestEqual("Count 4", Count, 155);
		Semaphore.Unlock(100);
		Test.TestEqual("State 5", State, 19);
	}
}
}

bool FSemaAsyncTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this);
	return true;
}

bool FSemaLatentTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this);
	return true;
}
