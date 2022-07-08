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

#include "UE5Coro/AggregateAwaiters.h"

namespace UE5Coro::Private
{
bool FAggregateAwaiter::await_ready()
{
	Data->Lock.Lock();
	checkf(std::holds_alternative<std::monostate>(Data->Handle),
	       TEXT("Attempting to reuse aggregate awaiter"));

	// Unlock if ready and resume immediately,
	// otherwise carry the lock to await_suspend
	bool bReady = Data->Count <= 0;
	if (bReady)
		Data->Lock.Unlock();
	return bReady;
}

void FAggregateAwaiter::await_suspend(FAsyncHandle Handle)
{
	checkf(!Data->Lock.TryLock(), TEXT("Internal error"));
	checkf(std::holds_alternative<std::monostate>(Data->Handle),
	       TEXT("Attempting to reuse aggregate awaiter"));

	Data->Handle = Handle;
	Data->Lock.Unlock();
}

void FAggregateAwaiter::await_suspend(FLatentHandle Handle)
{
	checkf(!Data->Lock.TryLock(), TEXT("Internal error"));
	checkf(std::holds_alternative<std::monostate>(Data->Handle),
	       TEXT("Attempting to reuse aggregate awaiter"));

	Handle.promise().DetachFromGameThread();
	Data->Handle = Handle;
	Data->Lock.Unlock();
}
}
