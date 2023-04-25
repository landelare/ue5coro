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

#include "UE5Coro/LatentAwaiters.h"
#include "Engine/World.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
bool WaitUntilFrame(void*& State, bool)
{
	return GFrameCounter >= reinterpret_cast<uint64>(State);
}

template<auto GetTime>
bool WaitUntilTime(void*& State, bool bCleanup)
{
	// Don't attempt to access GWorld in this case, it could be nullptr
	if (UNLIKELY(bCleanup))
		return false;

	static_assert(sizeof(void*) >= sizeof(double),
	              "32-bit platforms are not supported");
	auto& TargetTime = reinterpret_cast<double&>(State);
	return (GWorld->*GetTime)() >= TargetTime;
}

bool WaitUntilPredicate(void*& State, bool bCleanup)
{
	auto* Function = static_cast<std::function<bool()>*>(State);
	if (UNLIKELY(bCleanup))
	{
		delete Function;
		return false;
	}

	return (*Function)();
}

template<auto GetTime>
FLatentAwaiter GenericSeconds(double Seconds)
{
#if ENABLE_NAN_DIAGNOSTIC
	if (FMath::IsNaN(Seconds))
		logOrEnsureNanError(TEXT("Latent wait started with NaN time"));
#endif

	void* State = nullptr;
	reinterpret_cast<double&>(State) = (GWorld->*GetTime)() + Seconds;
	return FLatentAwaiter(State, &WaitUntilTime<GetTime>);
}
}

FLatentAwaiter Latent::NextTick()
{
	return Ticks(1);
}

FLatentAwaiter Latent::Ticks(int64 Ticks)
{
	ensureMsgf(Ticks >= 0, TEXT("Invalid number of ticks %lld"), Ticks);
	static_assert(sizeof(void*) == sizeof(uint64),
	              "32-bit platforms are not supported");
	uint64 Target = GFrameCounter + Ticks;
	return FLatentAwaiter(reinterpret_cast<void*>(Target), &WaitUntilFrame);
}

FLatentAwaiter Latent::Until(std::function<bool()> Function)
{
	checkf(Function, TEXT("Provided function is empty"));
	return FLatentAwaiter(new std::function(std::move(Function)),
	                      &WaitUntilPredicate);
}

FLatentAwaiter Latent::Seconds(double Seconds)
{
	return GenericSeconds<&UWorld::GetTimeSeconds>(Seconds);
}

FLatentAwaiter Latent::UnpausedSeconds(double Seconds)
{
	return GenericSeconds<&UWorld::GetUnpausedTimeSeconds>(Seconds);
}

FLatentAwaiter Latent::RealSeconds(double Seconds)
{
	return GenericSeconds<&UWorld::GetRealTimeSeconds>(Seconds);
}

FLatentAwaiter Latent::AudioSeconds(double Seconds)
{
	return GenericSeconds<&UWorld::GetAudioTimeSeconds>(Seconds);
}
