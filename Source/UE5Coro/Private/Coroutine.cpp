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

#include "UE5Coro/Coroutine.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

TMulticastDelegate<void()>& TCoroutine<>::OnCompletion()
{
	UE::TScopeLock _(Extras->Lock);
	checkf(!Extras->IsComplete(),
	       TEXT("Attempting to use a complete/invalid TCoroutine"));
	return Extras->Continuations_DEPRECATED;
	// It's unsafe to return the delegate with the lock unlocked,
	// but this is the old, deprecated behavior
}

bool TCoroutine<>::Wait(uint32 WaitTimeMilliseconds,
                        bool bIgnoreThreadIdleStats) const
{
	return Extras->Completed->Wait(WaitTimeMilliseconds, bIgnoreThreadIdleStats);
}

bool TCoroutine<>::IsDone() const
{
	return Wait(0, true);
}

void TCoroutine<>::SetDebugName(const TCHAR* Name)
{
#if UE5CORO_DEBUG
	if (ensureMsgf(FPromise::ResumeStack.Num() > 0,
	               TEXT("Attempting to set a debug name outside a coroutine")))
		FPromise::ResumeStack.Last()->Extras->DebugName = Name;
#endif
}
