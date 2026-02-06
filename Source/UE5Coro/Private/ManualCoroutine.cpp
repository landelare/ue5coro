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

#include "UE5Coro/ManualCoroutine.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

TManualCoroutine<void>::TManualCoroutine(FString DebugName)
	: TCoroutine<>(TManualPromiseExtras<void>::Run(std::move(DebugName)))
{
	// Initial refcount is already set to 1
}

TManualCoroutine<void>::TManualCoroutine(const TManualCoroutine& Other)
	: TCoroutine<>(Other)
{
	TManualPromiseExtras<void>::RawCast(Extras)->AddRef();
}

TManualCoroutine<void>::~TManualCoroutine()
{
	if (TManualPromiseExtras<void>::RawCast(Extras)->Release())
		Cancel();
}

TManualCoroutine<void>& TManualCoroutine<void>::operator=(
	const TManualCoroutine& Other)
{
	if (this == &Other)
		return *this;
	this->~TManualCoroutine();
	return *new (this) TManualCoroutine(Other);
}

void TManualCoroutine<void>::SetResult()
{
	bool bSuccessful = TrySetResult();
	ensureMsgf(bSuccessful, TEXT("The coroutine was already complete"));
}

bool TManualCoroutine<void>::TrySetResult()
{
	auto ExtrasT = TManualPromiseExtras<void>::SharedCast(Extras);
	auto& Lock = ExtrasT->Lock;
	Lock.lock(); // Block incoming cancellations
	if (!ExtrasT->IsComplete())
	{
		Lock.unlock();
		ExtrasT->Event.Trigger(); // Might delete this if the coroutine cleans up
		// Success is synchronous, cancellation isn't
		return ExtrasT->bWasSuccessful;
	}
	else
	{
		Lock.unlock();
		return false; // Already complete
	}
}
