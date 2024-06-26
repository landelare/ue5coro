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

// This file will be #included directly in .gen.cpp, so it's treated as external
#include "UE5Coro.h"
#include "UnrealTypes.generated.h"

/** USTRUCT wrapper for TCoroutine<>. */
USTRUCT(BlueprintInternalUseOnly)
struct FVoidCoroutine final
#if CPP
	: UE5Coro::TCoroutine<>
#endif
{
	GENERATED_BODY()

	/** This constructor is public to placate the reflection system and BP.
	 *  Do not use directly. Attempting to interact with the backing coroutine
	 *  of a default-constructed FVoidCoroutine is undefined behavior. */
	explicit FVoidCoroutine() noexcept : TCoroutine(nullptr) { }

	/** Implicit conversion from any TCoroutine. */
	template<typename T>
	FVoidCoroutine(const TCoroutine<T>& Coroutine) noexcept
		: TCoroutine(Coroutine) { }

	template<typename T>
	FVoidCoroutine(TCoroutine<T>&& Coroutine) noexcept
		: TCoroutine(std::move(Coroutine)) { }

	/** Returns true if this object represents a real coroutine call.
	 *  In this case, it is safe to treat this object as a TCoroutine<>. */
	[[nodiscard]] bool IsValid() const noexcept
	{
		return static_cast<bool>(Extras);
	}
};

static_assert(sizeof(FVoidCoroutine) == sizeof(UE5Coro::TCoroutine<>));

/** Taking this struct as a parameter in a coroutine will force latent execution
 *  mode, while retaining automatic coroutine target and world context detection.
 *
 *  This struct is compatible with UFUNCTIONs, and hidden on BP call nodes.
 *
 *  See UE5Coro::TLatentContext for a more advanced alternative that works with
 *  lambdas and other non-UObject-member functions that cannot rely on automatic
 *  world context detection. */
USTRUCT(BlueprintInternalUseOnly)
struct UE5CORO_API FForceLatentCoroutine final
{
	GENERATED_BODY()
};
