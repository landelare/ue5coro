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

#include "UObject/GCObjectScopeGuard.h"

namespace UE5Coro::Private
{
class FPromiseExtras;
template<typename, typename> class TCoroutinePromise;

// Transforms T to its weak pointer version
template<typename>
struct TWeak : std::false_type { };

template<typename T>
struct TWeak<T*> : std::bool_constant<std::is_convertible_v<T*, const UObject*>>
{
	using strong = TGCObjectScopeGuard<T>;
	using weak = TWeakObjectPtr<T>;
	using ptr = std::enable_if_t<TWeak::value, T*>;
	static strong Strengthen(const weak& Weak)
	{
		// Doing this correctly would require locking the private critical
		// section of UGCObjectReferencer. For most usages, this won't matter.
		// If you're hitting this ensure, either make the coroutine finish on
		// the GT, or use ContinueWith (non-Weak) with your own threading code.
		ensureMsgf(IsInGameThread(),
		           TEXT("Warning: UObjects are inherently not thread safe"));
		// There's no API to convert a weak ptr to a strong one...
		return strong(Weak.Get()); // relying on C++17 mandatory RVO
	}
	static ptr Get(const strong& Strong) { return Strong.Get(); }
};

template<typename T> // ContinueWith isn't guaranteed GT only
struct TWeak<TSharedPtr<T, ESPMode::ThreadSafe>> : std::true_type
{
	using strong = TSharedPtr<T, ESPMode::ThreadSafe>;
	using weak = TWeakPtr<T, ESPMode::ThreadSafe>;
	using ptr = T*;
	static strong Strengthen(const weak& Weak) { return Weak.Pin(); }
	static ptr Get(const strong& Strong) { return Strong.Get(); }
};

template<typename T>
struct TWeak<std::shared_ptr<T>> : std::true_type
{
	using strong = std::shared_ptr<T>;
	using weak = std::weak_ptr<T>;
	using ptr = T*;
	static strong Strengthen(const weak& Weak) { return Weak.lock(); }
	static ptr Get(const strong& Strong) { return Strong.get(); }
};
}
