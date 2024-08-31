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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratorTest, "UE5Coro.Generator",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::CriticalPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
TGenerator<int> CountUp(int Max)
{
	for (int i = 0; i <= Max; ++i)
		co_yield i;
}
}

bool FGeneratorTest::RunTest(const FString& Parameters)
{
	{
		auto Generator = []() -> TGenerator<int>
		{
			co_yield 1.0;
		}();
		TestEqual("Temporary type conversion", Generator.Current(), 1);
	}

	{
		TGenerator<int> Generator = CountUp(2);
		for (int i = 0; i <= 2; ++i)
		{
			TestEqual("Current", Generator.Current(), i);
			TestEqual("Resume", Generator.Resume(), i != 2);
		}
	}

	{
		TArray<int> Values;

		TGenerator<int> Count2 = CountUp(2);
		for (int i : Count2)
			Values.Add(i);
		// Not really classic iterator semantics, but we can't rewind
		TestEqual("begin()==end() at end", std::begin(Count2), std::end(Count2));
		TestEqual("Values.Num()", Values.Num(), 3);
		for (int i = 0; i <= 2; ++i)
			TestEqual("Values[i]", Values[i], i);
	}

	{
		TGenerator<int> Count2 = CountUp(2);
		// This makes an iterator, and discards it
		TestNotEqual("begin()!=end() at start", Count2.begin(), Count2.end());
		auto i = Count2.CreateIterator();
		int j = 0;
		for (; i; ++i)
			TestEqual("*i==j", *i, j++);
		TestEqual("iterator length", j, 3);
		TestFalse("!i at end", static_cast<bool>(i));
	}

	{
		bool bDestroyed = false;
		auto Gen = [&]() -> TGenerator<int>
		{
			ON_SCOPE_EXIT { bDestroyed = true; };
			co_yield 1;
			co_yield 5;
		};
		TGenerator<int> Lambda = Gen();
		TGenerator<int> Count = CountUp(3);
		Count.Resume();
		Count.Resume();
		TestFalse("Active", bDestroyed);
		TestEqual("Value 1", Lambda.Current(), 1);
		TestEqual("Value 2", Count.Current(), 2);
		Lambda = std::move(Count);
		TestTrue("Destroyed", bDestroyed);
		TestEqual("Moved to", Lambda.Current(), 2);
		TestFalse("Moved from", static_cast<bool>(Count));
	}

	return true;
}
