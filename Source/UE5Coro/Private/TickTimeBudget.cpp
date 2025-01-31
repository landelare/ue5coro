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

#include "UE5Coro/TickTimeBudget.h"

using namespace UE5Coro::Latent;

namespace
{
bool WaitForNextFrame(void* State, bool)
{
	// Return false on the suspending frame itself
	return GFrameCounter > reinterpret_cast<uint64>(State);
}
}

FTickTimeBudget::FTickTimeBudget(double SecondsPerTick)
	: FLatentAwaiter(nullptr, &WaitForNextFrame, std::false_type())
{
	// Check undefined conversion behavior before it occurs
	checkf(SecondsPerTick / FPlatformTime::GetSecondsPerCycle() <
	       std::numeric_limits<int>::max(),
	       TEXT("On this platform, the largest supported time budget is %f ms"),
	       FPlatformTime::GetSecondsPerCycle() *
	       std::numeric_limits<int>::max() * 1000.0);
	// This division is not ideal, but Unreal doesn't report cycles/sec.
	// With double/double, there should be enough precision left over though.
	CyclesPerTick = static_cast<int>(SecondsPerTick /
	                                 FPlatformTime::GetSecondsPerCycle());
	Start = FPlatformTime::Cycles(); // Start the clock immediately
}

FTickTimeBudget FTickTimeBudget::Seconds(double SecondsPerTick)
{
	return FTickTimeBudget(SecondsPerTick);
}

FTickTimeBudget FTickTimeBudget::Milliseconds(double MillisecondsPerTick)
{
	return FTickTimeBudget(MillisecondsPerTick / 1'000.0);
}

FTickTimeBudget FTickTimeBudget::Microseconds(double MicrosecondsPerTick)
{
	return FTickTimeBudget(MicrosecondsPerTick / 1'000'000.0);
}

bool FTickTimeBudget::await_ready()
{
	if (int Elapsed = FPlatformTime::Cycles() - Start; // integer wraparound here
	    Elapsed < CyclesPerTick) [[likely]]
		return true;
	else
	{
		State = reinterpret_cast<void*>(GFrameCounter); // Resume on next tick
		return false;
	}
}

void FTickTimeBudget::await_resume()
{
	// Reset the clock if and only if the coroutine was actually suspended
	if (State) [[unlikely]]
	{
		State = nullptr;
		Start = FPlatformTime::Cycles();
	}
}
