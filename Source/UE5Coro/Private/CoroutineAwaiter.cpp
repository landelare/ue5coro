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

#include "UE5Coro/CoroutineAwaiter.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
bool ShouldResumeLatentCoroutine(void* State, bool bCleanup)
{
	auto* This = static_cast<TCoroutine<>*>(State);
	if (bCleanup) [[unlikely]]
	{
		delete This;
		return false;
	}
	return This->IsDone();
}
}

FAsyncCoroutineAwaiter::FAsyncCoroutineAwaiter(TCoroutine<>&& Antecedent)
	: TCancelableAwaiter(&Cancel), Antecedent(std::move(Antecedent))
{
}

bool FAsyncCoroutineAwaiter::await_ready()
{
	return Antecedent.IsDone();
}

void FAsyncCoroutineAwaiter::Suspend(FPromise& Promise)
{
	checkf(!State, TEXT("Internal error: unexpected awaiter reuse"));
	UE::TUniqueLock L(Promise.GetLock());
	if (Promise.RegisterCancelableAwaiter(this))
	{
		State = new FTwoLives; // This must be created while the lock is held
		Antecedent.ContinueWith([&Promise, State = State]
		{
			// Call Release() first, it might indicate that the promise is gone.
			// If cancellation arrives right after Release() returns, it will
			// win UnregisterCancelableAwaiter(), and call the second Release().
			if (State->Release() && Promise.UnregisterCancelableAwaiter<true>())
			{
				State->Release();
				Promise.Resume();
			}
		});
	}
	else
		FAsyncYieldAwaiter::Suspend(Promise);
}

void FAsyncCoroutineAwaiter::Cancel(void* This, FPromise& Promise)
{
	if (Promise.UnregisterCancelableAwaiter<false>())
	{
		auto* Awaiter = static_cast<FAsyncCoroutineAwaiter*>(This);
		Awaiter->State->Release(); // Disarm the continuation
		FAsyncYieldAwaiter::Suspend(Promise);
	}
}

FLatentCoroutineAwaiter::FLatentCoroutineAwaiter(TCoroutine<>&& Antecedent)
	: FLatentAwaiter(new TCoroutine<>(std::move(Antecedent)),
	                 &ShouldResumeLatentCoroutine, std::false_type())
{
}
