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
#include "UE5Coro/TaskAwaiter.h"
#include "UE5CoroDelegateCallbackTarget.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
struct FAutoStartResumeRunnable final : FRunnable
{
	FPromise& Promise;
	std::atomic<FRunnableThread*> Thread;

	explicit FAutoStartResumeRunnable(FPromise& Promise,
	                                  EThreadPriority Priority, uint64 Affinity,
	                                  EThreadCreateFlags Flags)
		: Promise(Promise), Thread(nullptr)
	{
		// Thread has to start as nullptr and get overwritten later
		Thread = FRunnableThread::Create(this,
		                                 TEXT("UE5Coro::Async::MoveToNewThread"),
		                                 0, Priority, Affinity, Flags);
		checkf(Thread, TEXT("Internal error: could not create thread"));
	}

	virtual uint32 Run() override
	{
		Promise.Resume();
		return 0;
	}

	virtual void Exit() override
	{
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]
		{
			FRunnableThread* ThreadPtr;
			// Rare case of exiting so quickly, Thread is still nullptr
			while ((ThreadPtr = Thread.load()) == nullptr) [[unlikely]]
				FPlatformProcess::Yield();
			ThreadPtr->WaitForCompletion();
			delete ThreadPtr;
			delete this;
		});
	}
};
}

FAsyncAwaiter Async::MoveToThread(ENamedThreads::Type Thread) noexcept
{
	return FAsyncAwaiter(Thread);
}

FAsyncAwaiter Async::MoveToGameThread() noexcept
{
	return FAsyncAwaiter(ENamedThreads::GameThread);
}

FAsyncAwaiter Async::MoveToSimilarThread()
{
	return FAsyncAwaiter(FTaskGraphInterface::Get().GetCurrentThreadIfKnown());
}

FTaskAwaiter Async::MoveToTask(const TCHAR* DebugName)
{
	return FTaskAwaiter(DebugName);
}

FThreadPoolAwaiter Async::MoveToThreadPool(FQueuedThreadPool& ThreadPool,
                                           EQueuedWorkPriority Priority)
{
	return FThreadPoolAwaiter(ThreadPool, Priority);
}

FAsyncYieldAwaiter Async::Yield() noexcept
{
	return {};
}

FNewThreadAwaiter Async::MoveToNewThread(EThreadPriority Priority,
                                         uint64 Affinity,
                                         EThreadCreateFlags Flags) noexcept
{
	return FNewThreadAwaiter(Priority, Affinity, Flags);
}

FAsyncTimeAwaiter Async::PlatformSeconds(double Seconds) noexcept
{
	return FAsyncTimeAwaiter(FPlatformTime::Seconds() + Seconds, false);
}

FAsyncTimeAwaiter Async::PlatformSecondsAnyThread(double Seconds) noexcept
{
	return FAsyncTimeAwaiter(FPlatformTime::Seconds() + Seconds, true);
}

FAsyncTimeAwaiter Async::UntilPlatformTime(double Time) noexcept
{
	return FAsyncTimeAwaiter(Time, false);
}

FAsyncTimeAwaiter Async::UntilPlatformTimeAnyThread(double Time) noexcept
{
	return FAsyncTimeAwaiter(Time, true);
}

void FThreadPoolAwaiter::DoThreadedWork()
{
	checkf(Promise.load(), TEXT("Internal error: scheduled without a promise"));
	bAbandoned = false;
	Promise.exchange(nullptr)->Resume();
}

void FThreadPoolAwaiter::Abandon()
{
	checkf(Promise.load(), TEXT("Internal error: scheduled without a promise"));
	bAbandoned = true;
	Promise.exchange(nullptr)->Resume();
}

FThreadPoolAwaiter::FThreadPoolAwaiter(const FThreadPoolAwaiter& Other)
	: Pool(Other.Pool), Priority(Other.Priority)
{
}

void FThreadPoolAwaiter::Suspend(FPromise& InPromise)
{
	checkf(!Promise, TEXT("Internal error: recursive suspension"));
	Promise = &InPromise;
	// Since the coroutine is suspended (and detached, if latent), this awaiter
	// will remain alive until Resume(). Pass ownership to the thread pool,
	// DoThreadedWork/Abandon will take it back.
	Pool.AddQueuedWork(this, Priority);
}

void FNewThreadAwaiter::Suspend(FPromise& Promise)
{
	new FAutoStartResumeRunnable(Promise, Priority, Affinity, Flags);
}

FDelegateAwaiter::FDelegateAwaiter()
	: TCancelableAwaiter(&Cancel)
{
}

#if UE5CORO_DEBUG
FDelegateAwaiter::~FDelegateAwaiter()
{
	checkf(!Promise, TEXT("Internal error: destroying active awaiter"));
}
#endif

void FDelegateAwaiter::Suspend(FPromise& InPromise)
{
	checkf(!Promise, TEXT("Internal error: unexpected double suspend"));
	checkf(Cleanup, TEXT("Internal error: awaiter not set up"));
	UE::TUniqueLock Lock(InPromise.GetLock());
	if (InPromise.RegisterCancelableAwaiter(this))
		Promise = &InPromise;
	else
	{
		Cleanup();
		FAsyncYieldAwaiter::Suspend(InPromise);
	}
}

void FDelegateAwaiter::Cancel(void* This, FPromise& Promise)
{
	if (Promise.UnregisterCancelableAwaiter<false>())
	{
		auto* Awaiter = static_cast<FDelegateAwaiter*>(This);
		verifyf(Awaiter->Promise.exchange(nullptr) == &Promise,
		        TEXT("Internal error: expected active awaiter"));
		checkf(Awaiter->Cleanup, TEXT("Internal error: awaiter not set up"));
		Awaiter->Cleanup();
		FAsyncYieldAwaiter::Suspend(Promise);
	}
}

void FDelegateAwaiter::Resume()
{
	checkf(Cleanup, TEXT("Internal error: awaiter not set up"));
	if (auto* P = Promise.exchange(nullptr);
	    P->UnregisterCancelableAwaiter<true>())
	{
		Cleanup();
		P->Resume();
	}
}

UObject* FDelegateAwaiter::SetupCallbackTarget(std::function<void(void*)> Fn)
{
	FGCScopeGuard _;
	auto* Target = NewObject<UUE5CoroDelegateCallbackTarget>();
	Target->SetInternalFlags(EInternalObjectFlags::Async);
	Target->Init(std::move(Fn));
	checkf(!Cleanup, TEXT("Internal error: double setup"));
	Cleanup = [Target]
	{
		FGCScopeGuard _;
		Target->ClearInternalFlags(EInternalObjectFlags::Async);
		Target->MarkAsGarbage();
	};
	return Target;
}
