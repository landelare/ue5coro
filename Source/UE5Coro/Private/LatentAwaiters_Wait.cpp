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
bool WaitOnce(void*& State, bool)
{
	if (State)
	{
		State = nullptr;
		return false;
	}
	return true;
}

bool WaitUntilFrame(void*& State, bool)
{
	return GFrameNumber >= reinterpret_cast<intptr_t>(State);
}

template<auto GetTime>
bool WaitUntil(void*& State, bool bCleanup)
{
	// Don't attempt to access GWorld in this case, it could be nullptr
	if (bCleanup) [[unlikely]]
		return false;

	float& TargetTime = reinterpret_cast<float&>(State);
	return (GWorld->*GetTime)() >= TargetTime;
}

template<auto GetTime>
FLatentAwaiter GenericSeconds(float Seconds)
{
	void* State = nullptr;
	reinterpret_cast<float&>(State) = (GWorld->*GetTime)() + Seconds;
	return FLatentAwaiter(State, &WaitUntil<GetTime>);
}
}

FLatentAwaiter Latent::NextTick()
{
	return FLatentAwaiter(reinterpret_cast<void*>(1), &WaitOnce);
}

FLatentAwaiter Latent::Frames(int32 Frames)
{
	ensureMsgf(Frames >= 0, TEXT("Invalid number of frames %d"), Frames);
	intptr_t TicksPtr = GFrameNumber + Frames;
	return FLatentAwaiter(reinterpret_cast<void*>(TicksPtr), &WaitUntilFrame);
}

FLatentAwaiter Latent::Seconds(float Seconds)
{
	return GenericSeconds<&UWorld::GetTimeSeconds>(Seconds);
}

FLatentAwaiter Latent::UnpausedSeconds(float Seconds)
{
	return GenericSeconds<&UWorld::GetUnpausedTimeSeconds>(Seconds);
}

FLatentAwaiter Latent::RealSeconds(float Seconds)
{
	return GenericSeconds<&UWorld::GetRealTimeSeconds>(Seconds);
}

FLatentAwaiter Latent::AudioSeconds(float Seconds)
{
	return GenericSeconds<&UWorld::GetAudioTimeSeconds>(Seconds);
}
