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
#include "UE5Coro/Definitions.h"
#include <mutex>
#include <shared_mutex>
#include "Delegates/DelegateBase.h"

/******************************************************************************
 *          This file only contains private implementation details.           *
 ******************************************************************************/

namespace UE5Coro::Private
{
// On Windows, both std::mutex and std::shared_mutex are SRWLOCKs, but mutex
// has extra padding for ABI compatibility. Prefer shared_mutex for now.
#ifdef _MSVC_STL_VERSION
using FMutex = std::conditional_t<sizeof(std::shared_mutex) < sizeof(std::mutex),
                                  std::shared_mutex, std::mutex>;
#else
using FMutex = std::mutex;
#endif

template<typename T>
constexpr bool TIsSparseDelegate = std::is_base_of_v<FSparseDelegate, T>;

template<typename T>
constexpr bool TIsDynamicDelegate =
	std::is_base_of_v<FScriptDelegate, T> ||
	std::is_base_of_v<FMulticastScriptDelegate, T> ||
	// Sparse delegates are always dynamic multicast
	TIsSparseDelegate<T>;

template<typename T>
constexpr bool TIsMulticastDelegate =
	std::is_base_of_v<FDefaultDelegateUserPolicy::FMulticastDelegateExtras, T> ||
#if ENGINE_MINOR_VERSION >= 1
	std::is_base_of_v<FDefaultTSDelegateUserPolicy::FMulticastDelegateExtras, T> ||
#endif
	std::is_base_of_v<TMulticastScriptDelegate<>, T> ||
	// Sparse delegates are always dynamic multicast
	TIsSparseDelegate<T>;

template<typename T>
constexpr bool TIsDelegate =
	std::is_base_of_v<FDefaultDelegateUserPolicy::FDelegateExtras, T> ||
#if ENGINE_MINOR_VERSION >= 1
	std::is_base_of_v<FDefaultTSDelegateUserPolicy::FDelegateExtras, T> ||
#endif
	TIsDynamicDelegate<T> || TIsMulticastDelegate<T>;
}
