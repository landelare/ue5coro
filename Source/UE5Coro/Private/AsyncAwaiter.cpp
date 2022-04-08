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

#include "UE5Coro/AsyncAwaiters.h"
#include "UE5Coro/LatentCallbacks.h"

using namespace UE5Coro::Private;

void FAsyncAwaiter::await_suspend(std::coroutine_handle<FAsyncPromise> Handle)
{
	// Easy mode, nothing else can decide to delete the coroutine
	AsyncTask(Thread, [Handle]
	{
		Handle.resume();
	});
}

void FAsyncAwaiter::await_suspend(std::coroutine_handle<FLatentPromise> Handle)
{
	auto& Promise = Handle.promise();
	auto& State = Promise.GetMutableLatentState();
	checkCode(
		auto CurrentState = State.load();
		checkf(CurrentState < FLatentPromise::Canceled,
		       TEXT("Unexpected latent coroutine state %d"), CurrentState);
	);

	// Is this a latent->async transition or async->async chain?
	if (auto Old = FLatentPromise::LatentRunning;
		State.compare_exchange_strong(Old, FLatentPromise::AsyncRunning) ||
		Old == FLatentPromise::AsyncRunning ||
		Old == FLatentPromise::DeferredDestroy)
		AsyncTask(Thread, [Handle]
		{
			auto& Promise = Handle.promise();
			auto& State = Promise.GetMutableLatentState();

			// If this AsyncTask is running on the game thread, attempt the
			// async->latent transition (even though this is an AsyncTask)
			if (IsInGameThread())
			{
				auto Old = FLatentPromise::AsyncRunning;
				State.compare_exchange_strong(Old, FLatentPromise::LatentRunning);
				// This might fail if State == DeferredDestroy which is OK
				checkf(State == FLatentPromise::LatentRunning ||
				       State == FLatentPromise::DeferredDestroy,
				       TEXT("Unexpected state when returning to game thread"));
			}

			// Did a deferred deletion request arrive before the task started?
			if (State == FLatentPromise::DeferredDestroy) [[unlikely]]
				AsyncTask(ENamedThreads::GameThread, [Promise]() mutable
				{
					// Finish the coroutine on the game thread: destructors, etc.
					Promise.Destroy();
				});
			else
				// We're committed to a resumption now. Deletion requests will
				// end up in another async co_await since the coroutine must
				// return to the game thread. If this is the game thread already,
				// ~FPendingLatentCoroutine cannot run and the coroutine will
				// either co_await or return_void.
				Promise.Resume();
		});
	else
		checkf(false, TEXT("Unexpected latent coroutine state %d"), Old);
}
