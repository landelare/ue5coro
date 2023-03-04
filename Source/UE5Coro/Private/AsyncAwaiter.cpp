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

using namespace UE5Coro::Private;

namespace
{
class FResumeTask
{
	ENamedThreads::Type Thread;
	FPromise& Promise;

public:
	explicit FResumeTask(ENamedThreads::Type Thread, FPromise& Promise)
		: Thread(Thread), Promise(Promise) { }

	void DoTask(ENamedThreads::Type, FGraphEvent*)
	{
		Promise.Resume();
	}

	ENamedThreads::Type GetDesiredThread() const
	{
		return Thread;
	}

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FResumeTask,
		                                STATGROUP_ThreadPoolAsyncTasks);
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::FireAndForget;
	}
};
}

bool FAsyncAwaiter::await_ready()
{
	if (ResumeAfter.has_value())
		return ResumeAfter.value().IsDone();
	return false;
}

void FAsyncAwaiter::Suspend(FPromise& Promise)
{
	auto* Task = TGraphTask<FResumeTask>::CreateTask()
	                                     .ConstructAndHold(Thread, Promise);
	if (ResumeAfter.has_value())
		ResumeAfter.value().ContinueWith([Task] { Task->Unlock(); });
	else
		Task->Unlock();
}

#if UE5CORO_DEBUG
void FAsyncAwaiter::Suspend(FLatentPromise& Promise)
{
	// Extra checks on a latent promise
	auto CurrentState = Promise.GetLatentState();
	checkf(CurrentState < FLatentPromise::Canceled, // not done yet
	       TEXT("Unexpected latent coroutine state %d"), CurrentState);
	Suspend(static_cast<FPromise&>(Promise));
}
#endif
