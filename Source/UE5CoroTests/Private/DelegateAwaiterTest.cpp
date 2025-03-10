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

#include "Misc/AutomationTest.h"
#include "TestDelegates.h"
#include "TestWorld.h"
#include "UE5Coro.h"
#include "UE5CoroTestObject.h"

using namespace UE5Coro;
using namespace UE5Coro::Latent;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDelegateTestCore, "UE5Coro.Delegate.Core",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::CriticalPriority |
                                 EAutomationTestFlags::SmokeFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDelegateTestAsync, "UE5Coro.Delegate.Async",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDelegateTestLatent, "UE5Coro.Delegate.Latent",
                                 EAutomationTestFlags_ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

// For testing UntilDelegate
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning (disable:4996)
#endif

namespace
{
struct alignas(4096) FHighlyAlignedByte
{
	uint8 Value;
	operator uint8() const { return Value; }
};

template<bool bDynamic, bool bMulticast, bool bLatentWrapper, typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	auto Invoke = []<typename... A>(auto& Delegate, A&&... Args)
	{
		if constexpr (bMulticast)
			return Delegate.Broadcast(std::forward<A>(Args)...);
		else
			return Delegate.Execute(std::forward<A>(Args)...);
	};

	{
		bool bDone = false;
		typename TDelegateForTest<bDynamic, bMulticast>::FVoid Delegate;
		World.Run(CORO
		{
			if constexpr (bLatentWrapper)
				co_await UntilDelegate(Delegate);
			else
				co_await Delegate;
			bDone = true;
		});
		World.EndTick();
		Test.TestFalse("Not done yet", bDone);
		Invoke(Delegate);
		if constexpr (bLatentWrapper)
			World.Tick();
		Test.TestTrue("Done", bDone);
	}

	{
		bool bDone = false;
		typename TDelegateForTest<bDynamic, bMulticast>::FParams Delegate;
		World.Run(CORO
		{
			if constexpr (bLatentWrapper)
				co_await UntilDelegate(Delegate);
			else
			{
				auto&& [A, B] = co_await Delegate;
				static_assert(!std::is_reference_v<decltype(A)>);
				static_assert(std::is_lvalue_reference_v<decltype(B)>);
				Test.TestEqual("Param 1", A, 1);
				Test.TestEqual("Param 2", B, 2);
				B = 3;
			}
			bDone = true;
		});
		World.EndTick();
		Test.TestFalse("Not done yet", bDone);
		int Value = 2;
		Invoke(Delegate, 1, Value);
		if constexpr (bLatentWrapper)
			World.Tick();
		else
			Test.TestEqual("Reference writes back", Value, 3);
		Test.TestTrue("Done", bDone);
	}

	if constexpr (!bMulticast)
	{
		bool bDone = false;
		typename TDelegateForTest<bDynamic, bMulticast>::FRetVal Delegate;
		World.Run(CORO
		{
			if constexpr (bLatentWrapper)
				co_await UntilDelegate(Delegate);
			else
				co_await Delegate;
			bDone = true;
			FUE5CoroTestConstructionChecker::bConstructed = false;
		});
		World.EndTick();
		Test.TestFalse("Not done yet", bDone);
		Invoke(Delegate);
		if constexpr (bLatentWrapper)
			World.Tick();
		else
			Test.TestTrue("Return value",
			              FUE5CoroTestConstructionChecker::bConstructed);
		Test.TestTrue("Done", bDone);
	}

	if constexpr (!bMulticast)
	{
		bool bDone = false;
		typename TDelegateForTest<bDynamic, bMulticast>::FAll Delegate;
		World.Run(CORO
		{
			if constexpr (bLatentWrapper)
				co_await UntilDelegate(Delegate);
			else
			{
				auto&& [A, B] = co_await Delegate;
				static_assert(!std::is_reference_v<decltype(A)>);
				static_assert(std::is_lvalue_reference_v<decltype(B)>);
				Test.TestEqual("Param 1", A, 1);
				Test.TestEqual("Param 2", B, 2);
				B = 3;
			}
			bDone = true;
			FUE5CoroTestConstructionChecker::bConstructed = false;
		});
		World.EndTick();
		Test.TestFalse("Not done yet", bDone);
		int Value = 2;
		Invoke(Delegate, 1, Value);
		Test.TestTrue("Return value",
		              FUE5CoroTestConstructionChecker::bConstructed);
		if constexpr (bLatentWrapper)
			World.Tick();
		else
			Test.TestEqual("Reference writes back", Value, 3);
		Test.TestTrue("Done", bDone);
	}

	// Sparse delegate tests
	if constexpr (bDynamic && bMulticast)
	{
		bool bDone = false;
		auto* Object = NewObject<UUE5CoroTestObject>(World);
		World.Run(CORO
		{
			if constexpr (bLatentWrapper)
				co_await UntilDelegate(Object->SparseDelegate);
			else
				co_await Object->SparseDelegate;
			bDone = true;
		});
		World.EndTick();
		Test.TestFalse("Not done yet", bDone);
		Invoke(Object->SparseDelegate);
		Object->SparseDelegate.Broadcast();
		if constexpr (bLatentWrapper)
			World.Tick();
		Test.TestTrue("Done", bDone);
	}

	if constexpr (bDynamic && bMulticast)
	{
		bool bDone = false;
		auto* Object = NewObject<UUE5CoroTestObject>(World);
		World.Run(CORO
		{
			if constexpr (bLatentWrapper)
				co_await UntilDelegate(Object->SparseParamsDelegate);
			else
			{
				auto&& [A, B] = co_await Object->SparseParamsDelegate;
				static_assert(!std::is_reference_v<decltype(A)>);
				static_assert(std::is_lvalue_reference_v<decltype(B)>);
				Test.TestEqual("Param 1", A, 1);
				Test.TestEqual("Param 2", B, 2);
				B = 3;
			}
			bDone = true;
		});
		World.EndTick();
		Test.TestFalse("Not done yet", bDone);
		int Value = 2;
		Object->SparseParamsDelegate.Broadcast<int, int&>(1, Value);
		if constexpr (bLatentWrapper)
			World.Tick();
		else
			Test.TestEqual("Reference writes back", Value, 3);
		Test.TestTrue("Done", bDone);
	}
}

template<int N, typename... T>
void DoTests(FAutomationTestBase& Test)
{
	if constexpr (N <= 7)
	{
		DoTest<(N & 1) != 0, (N & 2) != 0, (N & 4) != 0, T...>(Test);
		DoTests<N + 1, T...>(Test);
	}
}
}

bool FDelegateTestCore::RunTest(const FString& Parameters)
{
	// This test is deliberately invoking undefined behavior.
	// DYNAMIC delegate support relies on UB behaving in a certain way, even in
	// the engine's own implementation.
	// See what changed in TBaseUFunctionDelegateInstance::Execute if this test
	// regresses when updating to a newer version of Unreal.
	bool bOk = true;

	// Expecting FPayload and FStruct to have identical size and memory layout
	using FPayload = TDecayedPayload<TCHAR, FHighlyAlignedByte, double>;
	struct FStruct
	{
		TCHAR X;
		FHighlyAlignedByte Y;
		double Z;
	};
	static_assert(alignof(FPayload) == alignof(FStruct));
	static_assert(sizeof(FPayload) == sizeof(FStruct));

	alignas(FStruct) uint8 Storage[sizeof(FStruct)]{};
	auto& AsPayload = *std::launder(reinterpret_cast<FPayload*>(&Storage));
	auto& AsStruct = *std::launder(reinterpret_cast<FStruct*>(&Storage));

	auto PayloadOffset = [&](auto& Field)
	{
		return reinterpret_cast<uintptr_t>(&Field) -
		       reinterpret_cast<uintptr_t>(&AsPayload);
	};
	// This could be done at compile time with stronger assumptions around
	// FPayload inheriting from TTupleBaseElement
	bOk &= TestEqual("X", PayloadOffset(AsPayload.get<0>()),
	                      STRUCT_OFFSET(FStruct, X));
	bOk &= TestEqual("Y", PayloadOffset(AsPayload.get<1>()),
	                      STRUCT_OFFSET(FStruct, Y));
	bOk &= TestEqual("Z", PayloadOffset(AsPayload.get<2>()),
	                      STRUCT_OFFSET(FStruct, Z));

	// These mainly sanity check codegen, the struct offsets were tested above
	bOk &= TestEqual("Initial X", AsPayload.get<0>(), TEXT('\0'));
	bOk &= TestEqual("Initial Y", AsPayload.get<1>(), 0);
	bOk &= TestEqual("Initial Z", AsPayload.get<2>(), 0.0);
	AsPayload.get<0>() = TEXT('♩');
	AsPayload.get<1>().Value = static_cast<uint8>(TEXT('♪'));
	AsPayload.get<2>() = TEXT('♫') + 0.5;
	bOk &= TestEqual(TEXT("X′"), AsStruct.X, TEXT('♩'));
	bOk &= TestEqual(TEXT("Y′"), AsStruct.Y, static_cast<uint8>(TEXT('♪')));
	bOk &= TestEqual(TEXT("Z′"), AsStruct.Z, TEXT('♫') + 0.5);

	// Please report a bug noting your platform/build type if this test fails!
	checkf(bOk, TEXT("Internal error: unexpected delegate memory layout"));
	return bOk;
}

bool FDelegateTestAsync::RunTest(const FString& Parameters)
{
	DoTests<0>(*this);
	return true;
}

bool FDelegateTestLatent::RunTest(const FString& Parameters)
{
	DoTests<0, FLatentActionInfo>(*this);
	return true;
}
