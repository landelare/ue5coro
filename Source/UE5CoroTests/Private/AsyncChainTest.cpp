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

#include "TestDelegates.h"
#include "TestWorld.h"
#include "Misc/AutomationTest.h"
#include "UE5Coro.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncChainTestAsync, "UE5Coro.AsyncChain.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncChainTestLatent, "UE5Coro.AsyncChain.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

using namespace UE5Coro;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

namespace
{
void Invoke(const TIsDelegate auto& Delegate)
{
	FUE5CoroTestConstructionChecker::bConstructed = false;
	if constexpr (requires { Delegate.Broadcast(); })
		Delegate.Broadcast();
	else
		Delegate.Execute();
}

void InvokeParams(const TIsDelegate auto* Delegate)
{
	FUE5CoroTestConstructionChecker::bConstructed = false;
	int Two = 2;
	if constexpr (requires { Delegate->Broadcast(1, Two); })
		Delegate->Broadcast(1, Two);
	else
		Delegate->Execute(1, Two);
}

template<bool bDynamic, bool bMulticast, typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	bool bDone = false;
	World.Run(CORO
	{
		using FVoid = typename TDelegateForTest<bDynamic, bMulticast>::FVoid;
		co_await Async::Chain(&Invoke<FVoid>);

		using FParams = typename TDelegateForTest<bDynamic, bMulticast>::FParams;
		auto&& [A, B] = co_await Async::Chain(&InvokeParams<FParams>);
		static_assert(!std::is_reference_v<decltype(A)>);
		static_assert(std::is_lvalue_reference_v<decltype(B)>);
		Test.TestEqual("FParams A", A, 1);
		Test.TestEqual("FParams B", B, 2);

		using FRetVal = typename TDelegateForTest<bDynamic, bMulticast>::FRetVal;
		if constexpr (!std::is_void_v<FRetVal>)
		{
			co_await Async::Chain(&Invoke<FRetVal>);
			// Dynamic delegates preconstruct their return values
			Test.TestEqual("Before return FRetVal",
			               FUE5CoroTestConstructionChecker::bConstructed,
			               bDynamic);
			co_await Latent::NextTick(); // Clear stack
			Test.TestTrue("RetVal",
			              FUE5CoroTestConstructionChecker::bConstructed);
		}

		using FAll = typename TDelegateForTest<bDynamic, bMulticast>::FAll;
		if constexpr (!std::is_void_v<FAll>)
		{
			auto&& [C, D] = co_await Async::Chain(&InvokeParams<FAll>);
			static_assert(!std::is_reference_v<decltype(A)>);
			static_assert(std::is_lvalue_reference_v<decltype(B)>);
			Test.TestEqual("FAll C", C, 1);
			Test.TestEqual("FAll D", D, 2);
			// Dynamic delegates preconstruct their return values
			Test.TestEqual("Before return FAll",
			               FUE5CoroTestConstructionChecker::bConstructed,
			               bDynamic);
			co_await Latent::NextTick(); // Clear stack
			Test.TestTrue("FAll RetVal",
			              FUE5CoroTestConstructionChecker::bConstructed);
		}
		bDone = true;
	});
	FTestHelper::PumpGameThread(World, [&] { return bDone; });
}

template<typename... T>
void DoTests(FAutomationTestBase& Test)
{
	DoTest<false, false, T...>(Test);
	DoTest<false, true, T...>(Test);
	DoTest<true, false, T...>(Test);
	DoTest<true, true, T...>(Test);
}
}

bool FAsyncChainTestAsync::RunTest(const FString& Parameters)
{
	DoTests<>(*this);
	return true;
}

bool FAsyncChainTestLatent::RunTest(const FString& Parameters)
{
	DoTests<FLatentActionInfo>(*this);
	return true;
}
