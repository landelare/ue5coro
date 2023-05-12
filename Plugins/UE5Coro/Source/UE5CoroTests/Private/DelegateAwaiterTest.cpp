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
#include "UE5CoroTestObject.h"
#include "UE5Coro/AsyncAwaiters.h"
#include "UE5Coro/LatentAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;
using namespace UE5Coro::Private::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDelegateTestCore, "UE5Coro.Delegate.Core",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::CriticalPriority |
                                 EAutomationTestFlags::SmokeFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDelegateTestAsync, "UE5Coro.Delegate.Async",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDelegateTestLatent, "UE5Coro.Delegate.Latent",
                                 EAutomationTestFlags::ApplicationContextMask |
                                 EAutomationTestFlags::HighPriority |
                                 EAutomationTestFlags::ProductFilter)

namespace
{
template<bool bDynamic, bool bMulticast>
struct TSelect;

template<>
struct TSelect<false, false>
{
	using FVoid = TDelegate<void()>;
	using FParams = TDelegate<void(int, int&)>;
	using FRetVal = TDelegate<FUE5CoroTestConstructionChecker()>;
	using FAll = TDelegate<FUE5CoroTestConstructionChecker(int, int&)>;
};

template<>
struct TSelect<false, true>
{
	using FVoid = TMulticastDelegate<void()>;
	using FParams = TMulticastDelegate<void(int, int&)>;
	using FRetVal = TMulticastDelegate<FUE5CoroTestConstructionChecker()>;
	using FAll = TMulticastDelegate<FUE5CoroTestConstructionChecker(int, int&)>;
};

template<>
struct TSelect<true, false>
{
	using FVoid = FUE5CoroTestDynamicVoidDelegate;
	using FParams = FUE5CoroTestDynamicParamsDelegate;
	using FRetVal = FUE5CoroTestDynamicRetvalDelegate;
	using FAll = FUE5CoroTestDynamicAllDelegate;
};

template<>
struct TSelect<true, true>
{
	using FVoid = FUE5CoroTestDynamicMulticastVoidDelegate;
	using FParams = FUE5CoroTestDynamicMulticastParamsDelegate;
};

struct alignas(4096) FHighlyAlignedByte
{
	uint8 Value;
};

template<bool bDynamic, bool bMulticast, bool bLatentWrapper, typename... T>
void DoTest(FAutomationTestBase& Test)
{
	FTestWorld World;

	auto Invoke = [](auto& Delegate, auto&&... Params)
	{
		if constexpr (bMulticast)
			return Delegate.Broadcast(std::forward<decltype(Params)>(Params)...);
		else
			return Delegate.Execute(std::forward<decltype(Params)>(Params)...);
	};

	{
		bool bDone = false;
		typename TSelect<bDynamic, bMulticast>::FVoid Delegate;
		World.Run(CORO
		{
			if constexpr (bLatentWrapper)
				co_await Latent::UntilDelegate(Delegate);
			else
				co_await Delegate;
			bDone = true;
		});
		World.EndTick();
		Test.TestFalse(TEXT("Not done yet"), bDone);
		Invoke(Delegate);
		if constexpr (bLatentWrapper)
			World.Tick();
		Test.TestTrue(TEXT("Done"), bDone);
	}

	{
		bool bDone = false;
		typename TSelect<bDynamic, bMulticast>::FParams Delegate;
		World.Run(CORO
		{
			if constexpr (bLatentWrapper)
				co_await Latent::UntilDelegate(Delegate);
			else
			{
				auto&& [A, B] = co_await Delegate;
				static_assert(!std::is_reference_v<decltype(A)>);
				static_assert(std::is_lvalue_reference_v<decltype(B)>);
				Test.TestEqual(TEXT("Param 1"), A, 1);
				Test.TestEqual(TEXT("Param 2"), B, 2);
				B = 3;
			}
			bDone = true;
		});
		World.EndTick();
		Test.TestFalse(TEXT("Not done yet"), bDone);
		int Two = 2;
		Invoke(Delegate, 1, Two);
		if constexpr (bLatentWrapper)
			World.Tick();
		else
			Test.TestEqual(TEXT("Reference writes back"), Two, 3);
		Test.TestTrue(TEXT("Done"), bDone);
	}

	if constexpr (!bMulticast)
	{
		{
			bool bDone = false;
			typename TSelect<bDynamic, bMulticast>::FRetVal Delegate;
			World.Run(CORO
			{
				if constexpr (bLatentWrapper)
					co_await Latent::UntilDelegate(Delegate);
				else
					co_await Delegate;
				bDone = true;
				FUE5CoroTestConstructionChecker::bConstructed = false;
			});
			World.EndTick();
			Test.TestFalse(TEXT("Not done yet"), bDone);
			Invoke(Delegate);
			if constexpr (bLatentWrapper)
				World.Tick();
			else
				Test.TestTrue(TEXT("Return value"),
				              FUE5CoroTestConstructionChecker::bConstructed);
			Test.TestTrue(TEXT("Done"), bDone);
		}

		{
			bool bDone = false;
			typename TSelect<bDynamic, bMulticast>::FAll Delegate;
			World.Run(CORO
			{
				if constexpr (bLatentWrapper)
					co_await Latent::UntilDelegate(Delegate);
				else
				{
					auto&& [A, B] = co_await Delegate;
					static_assert(!std::is_reference_v<decltype(A)>);
					static_assert(std::is_lvalue_reference_v<decltype(B)>);
					Test.TestEqual(TEXT("Param 1"), A, 1);
					Test.TestEqual(TEXT("Param 2"), B, 2);
					B = 3;
				}
				bDone = true;
				FUE5CoroTestConstructionChecker::bConstructed = false;
			});
			World.EndTick();
			Test.TestFalse(TEXT("Not done yet"), bDone);
			int Two = 2;
			Invoke(Delegate, 1, Two);
			Test.TestTrue(TEXT("Return value"),
			              FUE5CoroTestConstructionChecker::bConstructed);
			if constexpr (bLatentWrapper)
				World.Tick();
			else
				Test.TestEqual(TEXT("Reference writes back"), Two, 3);
			Test.TestTrue(TEXT("Done"), bDone);
		}
	}

	// Sparse delegate tests
	if constexpr (bDynamic && bMulticast)
	{
		{
			bool bDone = false;
			auto* Object = NewObject<UUE5CoroTestObject>();
			World.Run(CORO
			{
				if constexpr (bLatentWrapper)
					co_await Latent::UntilDelegate(Object->SparseDelegate);
				else
					co_await Object->SparseDelegate;
				bDone = true;
			});
			World.EndTick();
			Test.TestFalse(TEXT("Not done yet"), bDone);
			Invoke(Object->SparseDelegate);
			Object->SparseDelegate.Broadcast();
			if constexpr (bLatentWrapper)
				World.Tick();
			Test.TestTrue(TEXT("Done"), bDone);
		}

		{
			bool bDone = false;
			auto* Object = NewObject<UUE5CoroTestObject>();
			World.Run(CORO
			{
				if constexpr (bLatentWrapper)
					co_await Latent::UntilDelegate(Object->SparseParamsDelegate);
				else
				{
					auto&& [A, B] = co_await Object->SparseParamsDelegate;
					static_assert(!std::is_reference_v<decltype(A)>);
					static_assert(std::is_lvalue_reference_v<decltype(B)>);
					Test.TestEqual(TEXT("Param 1"), A, 1);
					Test.TestEqual(TEXT("Param 2"), B, 2);
					B = 3;
				}
				bDone = true;
			});
			World.EndTick();
			Test.TestFalse(TEXT("Not done yet"), bDone);
			int Two = 2;
			Object->SparseParamsDelegate.Broadcast<int, int&>(1, Two);
			if constexpr (bLatentWrapper)
				World.Tick();
			else
				Test.TestEqual(TEXT("Reference writes back"), Two, 3);
			Test.TestTrue(TEXT("Done"), bDone);
		}
	}
}

template<int N, typename... T>
void DoTests(FAutomationTestBase& Test)
{
	if constexpr (N < 8)
	{
		DoTest<(N & 4) != 0, (N & 2) != 0, (N & 1) != 0, T...>(Test);
		DoTests<N + 1, T...>(Test);
	}
}
}

bool FDelegateTestCore::RunTest(const FString& Parameters)
{
	// The implementation relies heavily on .gen.cpp structs' in-memory layouts.
	// See what changed in TBaseUFunctionDelegateInstance::Execute if this fails.

	// This is what needs to be matched: same offsets, same alignment
	struct FReference
	{
		TCHAR X;
		FHighlyAlignedByte Y;
		double Z;
	};
	using FTestType = TDecayedPayload<TCHAR, FHighlyAlignedByte, double>;
	alignas(FTestType) uint8 Storage[sizeof(FTestType)];
	auto& TestData = *std::launder(reinterpret_cast<FTestType*>(&Storage));
	static_assert(alignof(FTestType) == alignof(FReference));
	static_assert(sizeof(FTestType) == sizeof(FReference));

	auto PayloadOffset = [&TestData](auto& Field) -> size_t
	{
		return reinterpret_cast<uint8*>(&Field) -
		       reinterpret_cast<uint8*>(&TestData);
	};
	bool bOk = true;
	bOk &= TestEqual(TEXT("X"), PayloadOffset(TestData.get<0>()),
	                 STRUCT_OFFSET(FReference, X));
	bOk &= TestEqual(TEXT("Y"), PayloadOffset(TestData.get<1>()),
	                 STRUCT_OFFSET(FReference, Y));
	bOk &= TestEqual(TEXT("Z"), PayloadOffset(TestData.get<2>()),
	                 STRUCT_OFFSET(FReference, Z));

	FMemory::Memzero(TestData);
	TestData.get<0>() = TEXT('♩');
	TestData.get<1>().Value = static_cast<uint8>(TEXT('♪'));
	TestData.get<2>() = TEXT('♫') + 0.5;

	auto* AsRef = std::launder(reinterpret_cast<FReference*>(&TestData));
	bOk &= TestEqual(TEXT("X′"), AsRef->X, TEXT('♩'));
	bOk &= TestEqual(TEXT("Y′"), AsRef->Y.Value, static_cast<uint8>(TEXT('♪')));
	bOk &= TestEqual(TEXT("Z′"), AsRef->Z, TEXT('♫') + 0.5);

	// Please open an issue with your platform/build type if this fails!
	checkf(bOk, TEXT("Internal error: Unexpected delegate memory layout"));
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
