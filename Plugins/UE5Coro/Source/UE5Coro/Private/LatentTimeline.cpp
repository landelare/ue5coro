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
#include "UE5Coro/LatentAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Latent;

namespace
{
// Force to latent, otherwise it would keep running even after the world is gone.
template<auto GetTime>
TCoroutine<> CommonTimeline(double From, double To, double Length,
                            std::function<void(double)> Fn, bool bRunWhenPaused,
                            FForceLatentCoroutine = {})
{
#if ENABLE_NAN_DIAGNOSTIC
	if (FMath::IsNaN(From) || FMath::IsNaN(To) || FMath::IsNaN(Length))
		logOrEnsureNanError(TEXT("Latent timeline started with NaN parameter"));
	// Not a NaN right now but it could lead to one after division
	if (Length < SMALL_NUMBER)
		logOrEnsureNanError(
			TEXT("Latent timeline started with very short length"));
#endif
	// Clamp negative and small lengths to something that can be divided by
	Length = FMath::Max(Length, SMALL_NUMBER); // UE_SMALL_NUMBER is not in 5.0

	checkf(IsInGameThread(),
	       TEXT("Latent coroutines may only be started on the game thread"));
	double Start = (GWorld->*GetTime)();
	for (;;)
	{
		// Make sure the last call is exactly at Length
		double Time = FMath::Min((GWorld->*GetTime)() - Start, Length);
		// If the world is paused, only evaluate the function if asked.
		if (bRunWhenPaused || !GWorld->IsPaused())
		{
			double Value = FMath::Lerp(From, To, Time / Length);
#if ENABLE_NAN_DIAGNOSTIC
			// Incredibly high Time values could cause this to go wrong
			if (UNLIKELY(!FMath::IsFinite(Value)))
				logOrEnsureNanError(TEXT("Latent timeline derailed"));
#endif
			Fn(Value);
			if (Time == Length) // This hard == will work due to Min()
				co_return;
		}
		co_await NextTick();
	}
}
}

TCoroutine<> Latent::Timeline(double From, double To, double Length,
                              std::function<void(double)> Fn,
                              bool bRunWhenPaused)
{
	return CommonTimeline<&UWorld::GetTimeSeconds>(
		From, To, Length, std::move(Fn), bRunWhenPaused);
}

TCoroutine<> Latent::UnpausedTimeline(double From, double To, double Length,
                                      std::function<void(double)> Fn,
                                      bool bRunWhenPaused)
{
	return CommonTimeline<&UWorld::GetUnpausedTimeSeconds>(
		From, To, Length, std::move(Fn), bRunWhenPaused);
}

TCoroutine<> Latent::RealTimeline(double From, double To, double Length,
                                  std::function<void(double)> Fn,
                                  bool bRunWhenPaused)
{
	return CommonTimeline<&UWorld::GetRealTimeSeconds>(
		From, To, Length, std::move(Fn), bRunWhenPaused);
}

TCoroutine<> Latent::AudioTimeline(double From, double To, double Length,
                                   std::function<void(double)> Fn,
                                   bool bRunWhenPaused)
{
	return CommonTimeline<&UWorld::GetAudioTimeSeconds>(
		From, To, Length, std::move(Fn), bRunWhenPaused);
}
