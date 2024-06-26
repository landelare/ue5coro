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
#include "UE5Coro/Coroutine.h"
#include "UE5Coro/LatentAwaiter.h"
#include "UE5Coro/UnrealTypes.h"

using namespace UE5Coro;
using namespace UE5Coro::Latent;

namespace
{
// Force to latent, otherwise it would keep running even after the world is gone.
template<auto GetTime>
TCoroutine<> CommonTimeline(const UObject* WorldContextObject,
                            double From, double To, double Duration,
                            std::function<void(double)> Update,
                            bool bRunWhenPaused, FForceLatentCoroutine = {})
{
#if ENABLE_NAN_DIAGNOSTIC
	if (FMath::IsNaN(From) || FMath::IsNaN(To) || FMath::IsNaN(Duration))
	{
		logOrEnsureNanError(TEXT("Latent timeline started with NaN parameter"));
	}
	// Not a NaN right now but it could lead to one after division
	if (Duration < SMALL_NUMBER)
	{
		logOrEnsureNanError(
			TEXT("Latent timeline started with very short duration"));
	}
#endif
	// Clamp negative and small lengths to something that can be divided by
	Duration = FMath::Max(Duration, UE_SMALL_NUMBER);

	// These checks are mostly redundant, FLatentPromise checks similar things
	checkf(IsInGameThread(),
	       TEXT("Latent coroutines may only be started on the game thread"));
	checkf(IsValid(WorldContextObject) && IsValid(WorldContextObject->GetWorld()),
	       TEXT("Latent timeline started without valid world"));
	auto* World = WorldContextObject->GetWorld();

	double Start = (World->*GetTime)();
	for (;;)
	{
		// Make sure the last call is exactly at Duration
		double Time = FMath::Min((World->*GetTime)() - Start, Duration);
		// If the world is paused, only evaluate the function if asked.
		if (bRunWhenPaused || !World->IsPaused())
		{
			double Value = FMath::Lerp(From, To, Time / Duration);
#if ENABLE_NAN_DIAGNOSTIC
			// Incredibly high Time values could cause this to go wrong
			if (!FMath::IsFinite(Value)) [[unlikely]]
			{
				logOrEnsureNanError(TEXT("Latent timeline derailed"));
			}
#endif
			Update(Value);
			if (Time == Duration) // This hard equality will work due to Min()
				co_return;
		}
		co_await NextTick();

		checkf(WorldContextObject->GetWorld() == World,
		       TEXT("Coroutine travel/rename between worlds is not supported"));
		// How and why did this coroutine fail to self-cancel in this case?
		checkf(IsValid(WorldContextObject) && IsValid(World),
		       TEXT("Internal error: timeline still running on invalid world"));
	}
}
}

TCoroutine<> Latent::Timeline(const UObject* WorldContextObject,
                              double From, double To, double Duration,
                              std::function<void(double)> Update,
                              bool bRunWhenPaused)
{
	return CommonTimeline<&UWorld::GetTimeSeconds>(
		WorldContextObject, From, To, Duration, std::move(Update),
		bRunWhenPaused);
}

TCoroutine<> Latent::UnpausedTimeline(const UObject* WorldContextObject,
                                      double From, double To, double Duration,
                                      std::function<void(double)> Update,
                                      bool bRunWhenPaused)
{
	return CommonTimeline<&UWorld::GetUnpausedTimeSeconds>(
		WorldContextObject, From, To, Duration, std::move(Update),
		bRunWhenPaused);
}

TCoroutine<> Latent::RealTimeline(const UObject* WorldContextObject,
                                  double From, double To, double Duration,
                                  std::function<void(double)> Update,
                                  bool bRunWhenPaused)
{
	return CommonTimeline<&UWorld::GetRealTimeSeconds>(
		WorldContextObject, From, To, Duration, std::move(Update),
		bRunWhenPaused);
}

TCoroutine<> Latent::AudioTimeline(const UObject* WorldContextObject,
                                   double From, double To, double Duration,
                                   std::function<void(double)> Update,
                                   bool bRunWhenPaused)
{
	return CommonTimeline<&UWorld::GetAudioTimeSeconds>(
		WorldContextObject, From, To, Duration, std::move(Update),
		bRunWhenPaused);
}
