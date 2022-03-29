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
#include <coroutine>

namespace UE5Coro::Private
{
class FAsyncPromise;
class FLatentAwaiter;
class FLatentCancellation;
class FLatentPromise;
}

namespace UE5Coro::Latent
{
/** Stops the latent coroutine immediately WITHOUT firing the latent exec pin.<br>
 *  The coroutine WILL NOT be resumed. */
UE5CORO_API Private::FLatentCancellation Abort();

/** Resumes the coroutine in the next tick.<br>
 *  Useful in a generic are-we-there-yet loop since latent actions poll anyway. */
UE5CORO_API Private::FLatentAwaiter NextTick();

/** Resumes the coroutine on the first tick after the given number of frames. */
UE5CORO_API Private::FLatentAwaiter Frames(int32);

/** Resumes the coroutine the specified amount of seconds later.<br>
 *  This is affected by both pause and time dilation. */
UE5CORO_API Private::FLatentAwaiter Seconds(float);

/** Resumes the coroutine the specified amount of seconds later.<br>
 *  This is affected by time dilation only, NOT pause. */
UE5CORO_API Private::FLatentAwaiter UnpausedSeconds(float);

/** Resumes the coroutine the specified amount of seconds later.<br>
 *  This is not affected by pause or time dilation. */
UE5CORO_API Private::FLatentAwaiter RealSeconds(float);

/** Resumes the coroutine the specified amount of seconds later.<br>
 *  This is affected by pause only, NOT time dilation. */
UE5CORO_API Private::FLatentAwaiter AudioSeconds(float);

/** Resumes the coroutine once the chained static latent action has finished,
 *  with automatic parameter matching.<br>Example usage:<br>
 *  co_await Latent::Chain(&UKismetSystemLibrary::Delay, 1.0f); */
template<typename... FnParams>
Private::FLatentAwaiter Chain(auto (*Function)(FnParams...), auto&&... Args);

/** Resumes the coroutine once the chained member latent action has finished,
 *  with automatic parameter matching.<br>Example usage:<br>
 *  co_await Latent::Chain(&UMediaPlayer::OpenSourceLatent, MediaPlayer,
 *                        MediaSource, Options, bSuccess); */
template<std::derived_from<UObject> Class, typename... FnParams>
Private::FLatentAwaiter Chain(auto (Class::*Function)(FnParams...),
                              Class* Object, auto&&... Args);

/** Resumes the coroutine once the chained latent action has finished,
 *  with manual parameter matching.<br>
 *  Use std::placeholders::_1 and _2 for the world context and LatentInfo.<br>
 *  Example usage:<br>
 *  co_await Latent::ChainEx(&UKismetSystemLibrary::Delay, _1, 1.0f, _2); */
Private::FLatentAwaiter ChainEx(auto&& Function, auto&&... Args);
}

namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FLatentCancellation final
{
public:
	bool await_ready() { return false; }
	void await_resume() { }
	void await_suspend(std::coroutine_handle<FLatentPromise>);
};

class [[nodiscard]] UE5CORO_API FLatentAwaiter final
{
	void* State;
	bool (*Resume)(void*& State, bool bCleanup);

public:
	explicit FLatentAwaiter(void* State, bool (*Resume)(void*&, bool))
		: State(State), Resume(Resume) { }
	UE_NONCOPYABLE(FLatentAwaiter);
	~FLatentAwaiter();

	bool ShouldResume() { return (*Resume)(State, false); }

	bool await_ready() { return Resume ? ShouldResume() : true; }
	void await_resume() { }
	void await_suspend(std::coroutine_handle<FAsyncPromise>);
	void await_suspend(std::coroutine_handle<FLatentPromise>);
};
}

#include "LatentChain.inl"
