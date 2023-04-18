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

#pragma once

#include "CoreMinimal.h"
#include "UE5Coro/Definitions.h"
#include "UE5Coro/AsyncCoroutine.h"

namespace UE5Coro
{
namespace Private
{
class FPromise;
class FCancellationAwaiter;
}

/**
 * Guards against user-requested cancellation. For advanced use.<br>
 * This does NOT affect a latent coroutine destroyed by the latent action
 * manager.<br><br>
 * If any number of these objects is in scope within a coroutine returning
 * TCoroutine, it will delay cancellations and not process them in co_awaits.<br>
 * The first co_await after the last one of these has gone out of scope will
 * process the cancellation that was deferred.<br>
 */
class [[nodiscard]] UE5CORO_API FCancellationGuard
{
#if UE5CORO_DEBUG
	Private::FPromise* Promise;
#endif

public:
	FCancellationGuard();
	UE_NONCOPYABLE(FCancellationGuard);
	~FCancellationGuard();

	// These objects only make sense as locals
	void* operator new(std::size_t) = delete;
	void* operator new[](std::size_t) = delete;
};

/**
 * Provided for advanced scenarios, prefer ON_SCOPE_EXIT or RAII for
 * unconditional cleanup.<br><br>
 * This will ONLY call the provided callback if this object is in scope within
 * a coroutine that's being cleaned up early: due to manual cancellation, the
 * latent action manager deleting its corresponding latent action, etc.<br>
 * <br>Example usage:<br>
 * FOnCoroutineCanceled Guard([this]{cleanup code});
 */
struct [[nodiscard]] UE5CORO_API FOnCoroutineCanceled
	: ScopeExitSupport::TScopeGuard<std::function<void()>>
{
	explicit FOnCoroutineCanceled(std::function<void()> Fn);
};

/** co_awaiting the return value of this function does nothing if the calling
 *  coroutine is not currently canceled.
 *  If it is canceled, the cancellation will be processed immediately.
 *  FCancellationGuards are respected. */
UE5CORO_API Private::FCancellationAwaiter FinishNowIfCanceled();

/** Checks if the current coroutine is canceled, without processing the
 *  cancellation.<br>
 *  Prefer co_await to invoke normal cancellation processing instead.<br>
 *  Only valid to call from within a coroutine returning TCoroutine.
 *  @return True if the current coroutine is canceled.<br>
 *  FCancellationGuards do not affect the return value of this function. */
[[nodiscard]] UE5CORO_API bool IsCurrentCoroutineCanceled();
}

namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FCancellationAwaiter
	: public TAwaiter<FCancellationAwaiter>
{
public:
	bool await_ready();
	void Suspend(FPromise&);
};
}
