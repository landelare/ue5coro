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
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEventAsyncTest, "UE5Coro.Threading.Event.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::CriticalPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEventLatentTest, "UE5Coro.Threading.Event.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::CriticalPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<typename... T>
void DoTest(FAutomationTestBase& Test, EEventMode Mode)
{
	FTestWorld World;

	{
		bool bResult = false;
		FAwaitableEvent Event(Mode, false);
		World.Run(CORO
		{
			co_await Event;
			bResult = true;
		});
		Test.TestFalse("Not triggered yet", bResult);
		Event.Trigger();
		Test.TestTrue("Triggered", bResult);
	}

	{
		int State = 0;
		FAwaitableEvent Event(Mode, false);
		World.Run(CORO
		{
			State += 1;
			co_await Event;
			State += 10;
		});
		World.Run(CORO
		{
			State += 2;
			co_await Event;
			State += 20;
		});
		Test.TestEqual("Start", State, 3);
		Event.Trigger();
		if (Mode == EEventMode::AutoReset)
		{
			Test.TestTrue("Done", State == 13 || State == 23);
			Event.Trigger();
		}
		Test.TestEqual("Done", State, 33);
	}

	{
		int State = 0;
		FAwaitableEvent Event(Mode, true);
		World.Run(CORO
		{
			co_await Event;
			State += 1;
			co_await Event;
			State += 10;
		});
		World.Run(CORO
		{
			co_await Event;
			State += 2;
			co_await Event;
			State += 20;
		});
		if (Mode == EEventMode::ManualReset)
			Test.TestEqual("Start", State, 33);
		else
		{
			Test.TestTrue("Start", State == 1 || State == 2);
			Event.Trigger();
			int OldState = State;
			Test.TestTrue("Trigger 1", State < 32);
			Event.Trigger();
			Test.TestTrue("Trigger 2A", State > OldState);
			Test.TestTrue("Trigger 2B", State < 32);
			Event.Trigger();
			Test.TestEqual("Trigger 3", State, 33);
		}
	}

	{
		int State = 0;
		FAwaitableEvent Event(Mode, true);
		World.Run(CORO
		{
			Event.Reset();
			++State;
			co_await Event;
			++State;
			co_await Event;
			Event.Reset();
			++State;
			co_await Event;
			++State;
		});
		Test.TestEqual("Start", State, 1);
		Event.Trigger();
		if (Mode == EEventMode::AutoReset)
		{
			Test.TestEqual("Trigger 1", State, 2);
			Event.Trigger();
		}
		Test.TestEqual("Trigger 2", State, 3);
		Event.Trigger();
		Test.TestEqual("Trigger 3", State, 4);
	}

	{
		int Result = -1;
		FAwaitableEvent Event(Mode, false);
		FAwaitableSemaphore Semaphore(1, 0);
		World.Run(CORO
		{
			Result = co_await WhenAny(Event, Semaphore);
			co_await NextTick(); // Won't resume
		});
		Test.TestEqual("No immediate resume", Result, -1);
		Event.Trigger();
		Test.TestEqual("Resumed", Result, 0);
		Semaphore.Unlock(); // Remove the active awaiter before destruction
	}

	{
		bool bDone = false;
		FAwaitableEvent Event(Mode, false);
		FAwaitableSemaphore Semaphore(1, 0);
		World.Run(CORO
		{
			co_await WhenAll(Event, Semaphore);
			bDone = true;
		});
		Test.TestFalse("No immediate resume", bDone);
		Event.Trigger();
		Test.TestFalse("Not done yet", bDone);
		Semaphore.Unlock();
		Test.TestTrue("Done", bDone);
	}
}
}

bool FEventAsyncTest::RunTest(const FString& Parameters)
{
	DoTest<>(*this, EEventMode::AutoReset);
	DoTest<>(*this, EEventMode::ManualReset);
	return true;
}

bool FEventLatentTest::RunTest(const FString& Parameters)
{
	DoTest<FLatentActionInfo>(*this, EEventMode::AutoReset);
	DoTest<FLatentActionInfo>(*this, EEventMode::ManualReset);
	return true;
}
