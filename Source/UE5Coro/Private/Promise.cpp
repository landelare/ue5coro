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

#include "UE5Coro/AsyncCoroutine.h"
#include "Misc/ScopeExit.h"

using namespace UE5Coro::Private;

#if UE5CORO_DEBUG
std::atomic<int> FPromise::LastDebugID = -1; // -1 = no coroutines yet
// This is a synchronous call stack that doesn't follow or track co_await!
thread_local TArray<FPromise*> FPromise::ResumeStack;
#endif

bool FPromiseExtras::IsComplete() const
{
	return Completed->Wait(0, true);
}

void FPromiseExtras::Complete()
{
	// This should be called with the mutex already held
	checkf(!Lock.TryLock(), TEXT("Internal error"));
	checkf(!IsComplete(), TEXT("Internal error"));
	Completed->Trigger();
	OnCompleted();
	OnCompleted = nullptr;
}

FPromise::FPromise(std::shared_ptr<FPromiseExtras> Extras,
                   const TCHAR* PromiseType)
	: Extras(std::move(Extras))
{
#if UE5CORO_DEBUG
	this->Extras->DebugID = ++LastDebugID;
	this->Extras->DebugPromiseType = PromiseType;
#endif
}

FPromise::~FPromise()
{
	// If something else is accessing the delegate, block until it's done
	UE::TScopeLock _(Extras->Lock);
	checkf(!Extras->IsComplete(),
	       TEXT("Unexpected late/double coroutine destruction"));
	auto Continuations = std::move(Extras->Continuations_DEPRECATED);
	Extras->Complete();
	_.Unlock();
	Continuations.Broadcast();
}

void FPromise::Resume()
{
#if UE5CORO_DEBUG
	checkf(!Extras->IsComplete(),
	       TEXT("Attempting to resume completed coroutine"));
	ResumeStack.Push(this);
	ON_SCOPE_EXIT
	{
		// Coroutine resumption might result in `this` having been freed already
		// and not being considered `Alive`.
		// This is technically undefined behavior.
		checkf(ResumeStack.Last() == this, TEXT("Internal error"));
		ResumeStack.Pop();
	};
#endif

	stdcoro::coroutine_handle<FPromise>::from_promise(*this).resume();
}

void FPromise::unhandled_exception()
{
#if PLATFORM_EXCEPTIONS_DISABLED
	check(!"Exceptions are not supported");
#else
	throw;
#endif
}
