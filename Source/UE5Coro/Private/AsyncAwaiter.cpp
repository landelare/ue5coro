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

#include "UE5Coro/AsyncAwaiter.h"
#include "TimerThread.h"

using namespace UE5Coro::Private;

namespace
{
class FResumeTask final
{
	ENamedThreads::Type Thread;
	FPromise& Promise;

public:
	explicit FResumeTask(ENamedThreads::Type Thread, FPromise& Promise)
		: Thread(Thread), Promise(Promise) { }

	void DoTask(ENamedThreads::Type, FGraphEvent*) { Promise.Resume(); }

	ENamedThreads::Type GetDesiredThread() const { return Thread; }

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
	// Don't move threads if we're already on the target thread
	auto ThisThread = FTaskGraphInterface::Get().GetCurrentThreadIfKnown();
	return (ThisThread & ThreadTypeMask) == (Thread & ThreadTypeMask);
}

void FAsyncAwaiter::Suspend(FPromise& Promise)
{
	TGraphTask<FResumeTask>::CreateTask().ConstructAndDispatchWhenReady(Thread,
	                                                                    Promise);
}

FAsyncTimeAwaiter::FAsyncTimeAwaiter(const FAsyncTimeAwaiter& Other)
	: TCancelableAwaiter(&Cancel), TargetTime(Other.TargetTime),
	  Thread(Other.Thread) // bAnyThread included
{
	// Such a copy would need to happen after the coroutine was canceled
	ensureMsgf(TargetTime != std::numeric_limits<double>::lowest(),
	           TEXT("Copying a canceled awaiter copies the cancellation, too"));
}

FAsyncTimeAwaiter::~FAsyncTimeAwaiter()
{
	if (Promise) [[unlikely]]
		FTimerThread::Get().TryUnregister(this);
}

bool FAsyncTimeAwaiter::await_ready() noexcept
{
	return FPlatformTime::Seconds() >= TargetTime;
}

void FAsyncTimeAwaiter::Suspend(FPromise& InPromise)
{
	checkf(!Promise, TEXT("Internal error: double suspend after await_ready"));
	if (bAnyThread)
		Thread = ENamedThreads::AnyThread;
	else
		Thread = FTaskGraphInterface::Get().GetCurrentThreadIfKnown();
	UE::TUniqueLock Lock(InPromise.GetLock());
	Promise = &InPromise;
	if (!InPromise.RegisterCancelableAwaiter(this))
		TargetTime = std::numeric_limits<double>::lowest(); // Expire ASAP
	FTimerThread::Get().Register(this);
}

void FAsyncTimeAwaiter::Cancel(void* This, FPromise& Promise)
{
	// Synchronize with the timer thread first, promise second
	if (auto* Awaiter = static_cast<FAsyncTimeAwaiter*>(This);
	    FTimerThread::Get().TryUnregister(Awaiter))
	{
		if (Promise.UnregisterCancelableAwaiter<false>())
		{
			verifyf(Awaiter->Promise.exchange(nullptr) == &Promise,
			        TEXT("Internal error: mismatched promise at cancellation"));
			AsyncTask(Awaiter->Thread, [&Promise] { Promise.Resume(); });
		}
		else
			check(!"Internal error: unexpected race condition");
	}
}

void FAsyncTimeAwaiter::Resume()
{
	// This is called from the timer thread, coroutine resumption must be async
	checkf(Promise, TEXT("Internal error: spurious resume without suspension"));
	if (auto* P = Promise.exchange(nullptr);
	    P->UnregisterCancelableAwaiter<true>())
		AsyncTask(Thread, [P] { P->Resume(); });
	else
		check(!"Internal error: unexpected race condition");
}

void FAsyncYieldAwaiter::Suspend(FPromise& Promise)
{
	TGraphTask<FResumeTask>::CreateTask().ConstructAndDispatchWhenReady(
		FTaskGraphInterface::Get().GetCurrentThreadIfKnown(), Promise);
}
