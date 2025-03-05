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

#pragma once

#include "CoreMinimal.h"
#include "TestDelegates.generated.h"

USTRUCT()
struct FUE5CoroTestConstructionChecker
{
	GENERATED_BODY()
	static inline bool bConstructed = false;
	FUE5CoroTestConstructionChecker() { bConstructed = true; }
};

DECLARE_DYNAMIC_DELEGATE(FUE5CoroTestDynamicVoidDelegate);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FUE5CoroTestDynamicParamsDelegate,
                                   int, A, int&, B);
DECLARE_DYNAMIC_DELEGATE_RetVal(FUE5CoroTestConstructionChecker,
                                FUE5CoroTestDynamicRetvalDelegate);
DECLARE_DYNAMIC_DELEGATE_RetVal_TwoParams(FUE5CoroTestConstructionChecker,
                                          FUE5CoroTestDynamicAllDelegate,
                                          int, A, int&, B);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUE5CoroTestDynamicMulticastVoidDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FUE5CoroTestDynamicMulticastParamsDelegate, int, A, int&, B);

template<bool bDynamic, bool bMulticast>
struct TDelegateForTest;

template<>
struct TDelegateForTest<false, false>
{
	using FVoid = TDelegate<void()>;
	using FParams = TDelegate<void(int, int&)>;
	using FRetVal = TDelegate<FUE5CoroTestConstructionChecker()>;
	using FAll = TDelegate<FUE5CoroTestConstructionChecker(int, int&)>;
};

template<>
struct TDelegateForTest<false, true>
{
	using FVoid = TMulticastDelegate<void()>;
	using FParams = TMulticastDelegate<void(int, int&)>;
	using FRetVal = void;
	using FAll = void;
};

template<>
struct TDelegateForTest<true, false>
{
	using FVoid = FUE5CoroTestDynamicVoidDelegate;
	using FParams = FUE5CoroTestDynamicParamsDelegate;
	using FRetVal = FUE5CoroTestDynamicRetvalDelegate;
	using FAll = FUE5CoroTestDynamicAllDelegate;
};

template<>
struct TDelegateForTest<true, true>
{
	using FVoid = FUE5CoroTestDynamicMulticastVoidDelegate;
	using FParams = FUE5CoroTestDynamicMulticastParamsDelegate;
	using FRetVal = void;
	using FAll = void;
};

template<int N>
struct TDelegateSelector
{
	static constexpr bool bDynamic = (N & 1) != 0;
	static constexpr bool bMulticast = (N & 2) != 0;
	static constexpr bool bParams = (N & 4) != 0;
	static constexpr bool bRetval = (N & 8) != 0;

	using FSelector = TDelegateForTest<bDynamic, bMulticast>;
	using type = std::conditional_t<bParams,
		std::conditional_t<bRetval, typename FSelector::FAll,
		                            typename FSelector::FParams>,
		std::conditional_t<bRetval, typename FSelector::FRetVal,
		                            typename FSelector::FVoid>>;
};
