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
#include <optional>
#include "Interfaces/IHttpRequest.h"
#include "Misc/SpinLock.h"
#include "UE5Coro/AsyncCoroutine.h"

namespace UE5Coro::Private
{
class FHttpAwaiter;
}

namespace UE5Coro::Http
{
/** Processes the request, resumes the coroutine after it's done.<br>
 *  The result of the co_await expression will be a TTuple of
 *  FHttpResponsePtr and bool bConnectedSuccessfully. */
UE5CORO_API Private::FHttpAwaiter ProcessAsync(FHttpRequestRef);
}

namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FHttpAwaiter : public TAwaiter<FHttpAwaiter>
{
	struct [[nodiscard]] UE5CORO_API FState
	{
		const ENamedThreads::Type Thread;
		const FHttpRequestRef Request;
		UE::FSpinLock Lock;
		FPromise* Promise = nullptr;
		bool bSuspended = false;
		// end Lock
		std::optional<TTuple<FHttpResponsePtr, bool>> Result;

		explicit FState(FHttpRequestRef&&);
		void RequestComplete(FHttpRequestPtr, FHttpResponsePtr, bool);
		void Resume();
	};
	TSharedPtr<FState> State;

public:
	explicit FHttpAwaiter(FHttpRequestRef&& Request);

	bool await_ready();
	void Suspend(FPromise&);
	TTuple<FHttpResponsePtr, bool> await_resume();
};
}
