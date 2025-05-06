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

#include "UE5Coro/AggregateAwaiter.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

void FAggregateAwaiter::Cancel(void* This, FPromise& Promise)
{
	auto* Awaiter = static_cast<FAggregateAwaiter*>(This);
	if (Promise.UnregisterCancelableAwaiter<false>())
	{
		TArray<TCoroutine<>> Handles;
		{
			auto* Data = Awaiter->Data.get();
			UE::TUniqueLock Lock(Data->Lock);
			verifyf(!std::exchange(Data->bCanceled, true),
			        TEXT("Internal error: double cancellation"));
			verifyf(std::exchange(Data->Promise, nullptr) == &Promise,
			        TEXT("Internal error: expected active awaiter"));
			Handles = std::move(Data->Handles);
		}
		FAsyncYieldAwaiter::Suspend(Promise);
		for (auto& Coro : Handles) // Cancel all inner coroutines
			Coro.Cancel();
	}
}

int FAggregateAwaiter::GetResumerIndex() const
{
	checkf(Data->Count <= 0, TEXT("Internal error: resuming too early"));
	checkf(Data->Count == 0 || Data->Index != -1,
	       TEXT("Internal error: resuming with no result"));
	checkf(!Data->bCanceled, TEXT("Internal error: resuming after cancellation"));
	return Data->Index;
}

FAggregateAwaiter::FAggregateAwaiter(auto All,
                                     const TArray<TCoroutine<>>& Coroutines)
	: TCancelableAwaiter(&Cancel),
	  Data(std::make_shared<FData>(All.value ? Coroutines.Num()
	                                         : Coroutines.Num() ? 1 : 0))
{
	for (int i = 0; i < Coroutines.Num(); ++i)
		Data->Handles.Add(Consume(Data, i, Coroutines[i]));
}
template UE5CORO_API FAggregateAwaiter::FAggregateAwaiter(
	std::false_type, const TArray<TCoroutine<>>&);
template UE5CORO_API FAggregateAwaiter::FAggregateAwaiter(
	std::true_type, const TArray<TCoroutine<>>&);

bool FAggregateAwaiter::await_ready()
{
	checkf(Data, TEXT("Attempting to await moved-from aggregate awaiter"));
	Data->Lock.Lock();
	checkf(!Data->Promise, TEXT("Attempting to reuse aggregate awaiter"));
	checkf(!Data->bCanceled,
	       TEXT("Attempting to reuse canceled aggregate awaiter"));

	// Unlock if ready and resume immediately by returning true,
	// otherwise carry the lock to await_suspend/Suspend
	bool bReady = Data->Count <= 0;
	if (bReady)
		Data->Lock.Unlock();
	return bReady;
}

void FAggregateAwaiter::Suspend(FPromise& Promise)
{
	checkf(Data->Lock.IsLocked(), TEXT("Internal error: lock was not taken"));
	checkf(!Data->Promise, TEXT("Attempting to reuse aggregate awaiter"));
	checkf(!Data->bCanceled,
	       TEXT("Attempting to reuse canceled aggregate awaiter"));

	UE::TUniqueLock Lock(Promise.GetLock());

	if (Promise.RegisterCancelableAwaiter(this))
		Data->Promise = &Promise;
	else
		FAsyncYieldAwaiter::Suspend(Promise);
	Data->Lock.Unlock();
}

FAnyAwaiter UE5Coro::WhenAny(const TArray<TCoroutine<>>& Coroutines)
{
	return FAnyAwaiter(std::false_type(), Coroutines);
}

FRaceAwaiter UE5Coro::Race(TArray<TCoroutine<>> Array)
{
	return FRaceAwaiter(std::move(Array));
}

FAllAwaiter UE5Coro::WhenAll(const TArray<TCoroutine<>>& Coroutines)
{
	return FAllAwaiter(std::true_type(), Coroutines);
}

FRaceAwaiter::FRaceAwaiter(TArray<TCoroutine<>>&& Array)
	: TCancelableAwaiter(&Cancel),
	  Data(std::make_shared<FData>(std::move(Array)))
{
	// Add a continuation to every coroutine, but any one of them might
	// invalidate the array
	for (int i = 0; i < Data->Handles.Num(); ++i)
	{
		TCoroutine<>* Coro;
		{
			// Must be limited in scope because ContinueWith may be synchronous
			UE::TUniqueLock Lock(Data->Lock);
			if (Data->Index != -1) // Did a coroutine finish during this loop?
				return; // Don't bother asking the others, they've all canceled
			Coro = &Data->Handles[i];
		}

		Coro->ContinueWith([Data = Data, i]
		{
			UE::TDynamicUniqueLock Lock(Data->Lock);

			// Nothing to do if this wasn't the first one, or the race is canceled
			if (Data->Index != -1 || Data->bCanceled)
				return;
			Data->Index = i;

			for (int j = 0; j < Data->Handles.Num(); ++j)
				if (j != i) // Cancel the others
					Data->Handles[j].Cancel();

			if (auto* Promise = Data->Promise)
			{
				Lock.Unlock();
				if (Promise->UnregisterCancelableAwaiter<true>())
					Promise->Resume();
			}
		});
	}
}

FRaceAwaiter::~FRaceAwaiter()
{
	UE::TUniqueLock Lock(Data->Lock);
	Data->bCanceled = true;
	if (Data->Index == -1)
		for (auto& Handle : Data->Handles)
			Handle.Cancel();
}

void FRaceAwaiter::Cancel(void* This, FPromise& Promise)
{
	auto* Awaiter = static_cast<FRaceAwaiter*>(This);
	if (Promise.UnregisterCancelableAwaiter<false>())
	{
		UE::TUniqueLock Lock(Awaiter->Data->Lock);
		checkf(Awaiter->Data->Promise,
		       TEXT("Internal error: expected active awaiter"));
		verifyf(!std::exchange(Awaiter->Data->bCanceled, true),
		        TEXT("Internal error: unexpected double cancellation"));
		for (auto& Handle : Awaiter->Data->Handles)
			Handle.Cancel();
		FAsyncYieldAwaiter::Suspend(Promise);
	}
}

bool FRaceAwaiter::await_ready()
{
	Data->Lock.Lock();
	checkf(!Data->bCanceled, TEXT("Attempting to reuse canceled awaiter"));
	if (Data->Handles.Num() == 0 || Data->Index != -1)
	{
		Data->Lock.Unlock();
		return true;
	}
	else
		return false; // Passing the lock to Suspend
}

void FRaceAwaiter::Suspend(FPromise& Promise)
{
	// Expecting a lock from await_ready
	checkf(Data->Lock.IsLocked(), TEXT("Internal error: lock not held"));
	checkf(!Data->bCanceled, TEXT("Attempting to reuse canceled awaiter"));
	checkf(!Data->Promise, TEXT("Unexpected double race await"));

	UE::TUniqueLock Lock(Promise.GetLock());
	if (Promise.RegisterCancelableAwaiter(this))
		Data->Promise = &Promise;
	else
		FAsyncYieldAwaiter::Suspend(Promise);
	Data->Lock.Unlock();
}

int FRaceAwaiter::await_resume() noexcept
{
	// This will be read on the same thread that wrote Index, or after
	// await_ready determined its value; no lock needed
	checkf(Data->Handles.Num() == 0 || Data->Index != -1,
	       TEXT("Internal error: resuming with unknown result"));
	return Data->Index;
}

bool FLatentAggregate::ShouldResume(void* State, bool bCleanup)
{
	auto* This = static_cast<FLatentAggregate*>(State);
	if (bCleanup)
	{
		for (auto& Handle : This->Handles)
			Handle.Cancel();
		This->Release();
		return false;
	}
	return This->Remaining <= 0;
}

void FLatentAggregate::Release()
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: expected to be released on the game thread"));
	checkf(RefCount > 0, TEXT("Internal error: RefCount underflow"));
	if (--RefCount == 0)
		delete this;
}

int FLatentAnyAwaiter::await_resume()
{
	return static_cast<FLatentAggregate*>(State)->First;
}
