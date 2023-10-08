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

#include "TimerThread.h"
#include "UE5Coro/AsyncAwaiters.h"
#include <mutex>

using namespace UE5Coro::Private;

std::once_flag FTimerThread::Once;
FTimerThread* FTimerThread::Instance;

namespace
{
// TArray auto-dereferencing requires an explicit predicate
bool Less(const FAsyncTimeAwaiter& A, const FAsyncTimeAwaiter& B)
{
	return A < B;
}
}

FTimerThread& FTimerThread::Get()
{
	std::call_once(Once, [] { Instance = new FTimerThread; });
	return *Instance;
}

void FTimerThread::Register(FAsyncTimeAwaiter* Awaiter)
{
	std::scoped_lock _(Lock);
	Queue.HeapPush(Awaiter, &Less);
	Event->Trigger();
}

void FTimerThread::TryUnregister(FAsyncTimeAwaiter* Awaiter)
{
	std::scoped_lock _(Lock);
	// Slow, but this function is called extremely rarely
	auto Idx = Queue.Find(Awaiter);
	if (Idx == INDEX_NONE)
		return;
	Queue.HeapRemoveAt(Idx, &Less);
}

FTimerThread::FTimerThread()
	: Event(FPlatformProcess::GetSynchEventFromPool())
	, Thread(TEXT("UE5Coro Timer Thread"), [this] { Run(); })
{
}

void FTimerThread::Run()
{
	for (;;)
		RunOnce();
}

void FTimerThread::RunOnce()
{
	auto Wait = FTimespan::MaxValue();
	{
		std::scoped_lock _(Lock);
		if (Queue.Num() > 0)
			Wait = FMath::Max(FTimespan::Zero(),
			                  FTimespan::FromSeconds(Queue.HeapTop()->TargetTime -
			                                         FPlatformTime::Seconds()));
	}
	Event->Wait(Wait);
	std::scoped_lock _(Lock);
	auto Now = FPlatformTime::Seconds();
	while (Queue.Num() > 0)
	{
		if (auto& Next = Queue.HeapTop(); Next->TargetTime <= Now)
		{
			Resume(Next);
			Queue.HeapPopDiscard(&Less);
		}
		else
			break;
	}
}

void FTimerThread::Resume(FAsyncTimeAwaiter* Awaiter)
{
	Awaiter->Resume();
}
