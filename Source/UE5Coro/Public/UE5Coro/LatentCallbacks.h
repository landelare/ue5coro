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
#include <functional>
#include "Misc/ScopeExit.h"

namespace UE5Coro::Latent
{
/**
 * Provided for advanced scenarios, prefer ON_SCOPE_EXIT or RAII for
 * unconditional cleanup, or FOnCoroutineCanceled for a generic handler.<br><br>
 * This is a combination of FOnActionAborted and FOnObjectDestroyed and will
 * ONLY run the provided callback in either of those two situations.<br>
 * It has no effect within an async mode coroutine. Note that because
 * NotifyObjectDestroyed is included, `this` might not be valid.<br>
 * <br>Example usage:<br>
 * Latent::FOnAbnormalExit Guard([]{cleanup code});
 */
struct [[nodiscard]] UE5CORO_API FOnAbnormalExit
	: ScopeExitSupport::TScopeGuard<std::function<void()>>
{
	explicit FOnAbnormalExit(std::function<void()> Fn);
};

/**
 * Provided for advanced scenarios, prefer ON_SCOPE_EXIT or RAII for
 * unconditional cleanup.<br><br>
 * This will ONLY call the provided callback if this object is in scope within
 * a latent mode coroutine that's aborted by the latent action manager.
 * It has no effect within an async mode coroutine.<br>
 * <br>Example usage:<br>
 * Latent::FOnActionAborted Guard([this]{cleanup code});
 * @see FPendingLatentAction::NotifyActionAborted()
 */
struct [[nodiscard]] UE5CORO_API FOnActionAborted
	: ScopeExitSupport::TScopeGuard<std::function<void()>>
{
	explicit FOnActionAborted(std::function<void()> Fn);
};

/**
 * Provided for advanced scenarios, prefer ON_SCOPE_EXIT or RAII for
 * unconditional cleanup.<br><br>
 * This will ONLY call the provided callback if this object is in scope within
 * a latent mode coroutine whose object has been garbage collected.
 * It has no effect within an async mode coroutine.<br>
 * <br>Example usage:<br>
 * Latent::FOnObjectDestroyed Guard([]{cleanup code});
 * @see FPendingLatentAction::NotifyObjectDestroyed()
 */
struct [[nodiscard]] UE5CORO_API FOnObjectDestroyed
	: ScopeExitSupport::TScopeGuard<std::function<void()>>
{
	explicit FOnObjectDestroyed(std::function<void()> Fn);
};
}
