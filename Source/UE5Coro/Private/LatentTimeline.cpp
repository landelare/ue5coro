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

#include "UE5Coro/LatentTimeline.h"
#include "UE5Coro/LatentAwaiters.h"
#include "UE5Coro/UE5CoroSubsystem.h"

using namespace UE5Coro;
using namespace UE5Coro::Latent;

namespace
{
// Dummy FLatentActionInfo parameter to force a FLatentPromise and get tied to
// a FLatentActionManager, otherwise this would keep running even after the
// world is gone.
template<auto GetTime>
FAsyncCoroutine CommonTimeline(double From, double To, double Length,
                               std::function<void(double)> Fn,
                               FLatentActionInfo = {})
{
	checkf(IsInGameThread(),
	       TEXT("Latent coroutines may only be started on the game thread"));
	double Start = (GWorld->*GetTime)();
	for (;;)
	{
		// Make sure the last call is exactly at Length
		double Alpha = FMath::Min((GWorld->*GetTime)() - Start, Length);
		Fn(FMath::Lerp(From, To, Alpha));
		if (Alpha == Length)
			co_return;
		co_await NextTick();
	}
}
}

FAsyncCoroutine Latent::Timeline(double From, double To, double Length,
                                 std::function<void(double)> Fn)
{
	auto Info = GWorld->GetSubsystem<UUE5CoroSubsystem>()->MakeLatentInfo();
	return CommonTimeline<&UWorld::GetTimeSeconds>(From, To, Length,
	                                               std::move(Fn), Info);
}

FAsyncCoroutine Latent::UnpausedTimeline(double From, double To, double Length,
                                         std::function<void(double)> Fn)
{
	auto Info = GWorld->GetSubsystem<UUE5CoroSubsystem>()->MakeLatentInfo();
	return CommonTimeline<&UWorld::GetUnpausedTimeSeconds>(From, To, Length,
		std::move(Fn), Info);
}

FAsyncCoroutine Latent::RealTimeline(double From, double To, double Length,
                                     std::function<void(double)> Fn)
{
	auto Info = GWorld->GetSubsystem<UUE5CoroSubsystem>()->MakeLatentInfo();
	return CommonTimeline<&UWorld::GetRealTimeSeconds>(From, To, Length,
		std::move(Fn), Info);
}

FAsyncCoroutine Latent::AudioTimeline(double From, double To, double Length,
                                      std::function<void(double)> Fn)
{
	auto Info = GWorld->GetSubsystem<UUE5CoroSubsystem>()->MakeLatentInfo();
	return CommonTimeline<&UWorld::GetAudioTimeSeconds>(From, To, Length,
		std::move(Fn), Info);
}
