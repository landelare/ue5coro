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
#include "UE5Coro.h"
#include "Misc/EngineVersionComparison.h"

#if UE_VERSION_OLDER_THAN(5, 5, 0)
constexpr EAutomationTestFlags::Type EAutomationTestFlags_ApplicationContextMask =
	EAutomationTestFlags::ApplicationContextMask;
#endif

#define CORO [&](T...) -> FVoidCoroutine
#define CORO_R(Type) [&](T...) -> TCoroutine<Type>
#define IF_CORO_ASYNC if constexpr (sizeof...(T) != 1)
#define IF_CORO_ASYNC_OR(Condition) if constexpr (sizeof...(T) != 1 || (Condition))
#define IF_CORO_LATENT if constexpr (sizeof...(T) == 1)

namespace UE5Coro::Private::Test
{
class UE5COROTESTS_API FTestWorld
{
	UWorld* PrevWorld;
	decltype(GFrameCounter) OldFrameCounter;

protected:
	UWorld* World;

public:
	FTestWorld();
	UE_NONCOPYABLE(FTestWorld);
	~FTestWorld();

	UWorld* operator->() const noexcept { return World; }
	operator UWorld*() const noexcept { return World; }

	void Tick(float DeltaSeconds = 0.125);
	void EndTick();

	auto Run(std::invocable auto Fn)
	{
		// Extend the lifetime of Fn's lambda captures until it is complete
		auto* ExtendedLifeFn = new auto(std::move(Fn));
		auto Coro = (*ExtendedLifeFn)();
		Coro.ContinueWith([=] { delete ExtendedLifeFn; });
		return Coro;
	}

	auto Run(std::invocable<FLatentActionInfo> auto Fn)
	{
		auto* Sys = World->GetSubsystem<UUE5CoroSubsystem>();
		auto LatentInfo = Sys->MakeLatentInfo();

		auto* ExtendedLifeFn = new auto(std::move(Fn));
		auto Coro = (*ExtendedLifeFn)(LatentInfo);
		Coro.ContinueWith([=] { delete ExtendedLifeFn; });
		return Coro;
	}
};

class UE5COROTESTS_API FTestHelper
{
public:
	static void PumpGameThread(FTestWorld& World,
	                           std::function<bool()> ExitCondition);
	static void CheckWorld(FAutomationTestBase& Test, UWorld* World);
	static bool ReadEvent(FAwaitableEvent&);
	static int ReadSemaphore(FAwaitableSemaphore&);
};
}
