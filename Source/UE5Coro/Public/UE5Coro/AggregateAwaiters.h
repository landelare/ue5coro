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

#include <memory>
#include "Misc/SpinLock.h"
#include "UE5Coro/AsyncCoroutine.h"

namespace UE5Coro::Private
{
class FAggregateAwaiter;
}

namespace UE5Coro
{
/** co_awaits all parameters, resumes its own awaiting coroutine when any
 *  of them finishes. */
Private::FAggregateAwaiter WhenAny(TAwaitable auto&&...);

/** co_awaits all parameters, resumes its own awaiting coroutine when all
 *  of them finish. */
Private::FAggregateAwaiter WhenAll(TAwaitable auto&&...);
}

namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FAggregateAwaiter
{
	struct FData
	{
		UE::FSpinLock Lock;
		int Count;
		FOptionalHandleVariant Handle;

		explicit FData(int Count) : Count(Count) { }
	};
	std::shared_ptr<FData> Data;

	static FAsyncCoroutine Consume(auto, auto&&);

public:
	FAggregateAwaiter(int Count, auto&&... Awaiters)
		: Data(std::make_shared<FData>(Count))
	{
		(Consume(Data, std::forward<decltype(Awaiters)>(Awaiters)), ...);
	}
	UE_NONCOPYABLE(FAggregateAwaiter);

	bool await_ready();
	void await_suspend(FAsyncHandle);
	void await_suspend(FLatentHandle);
	void await_resume() { }
};
}

UE5Coro::Private::FAggregateAwaiter UE5Coro::WhenAny(TAwaitable auto&&... Args)
{
	return {sizeof...(Args) ? 1 : 0, std::forward<decltype(Args)>(Args)...};
}

UE5Coro::Private::FAggregateAwaiter UE5Coro::WhenAll(TAwaitable auto&&... Args)
{
	return {sizeof...(Args), std::forward<decltype(Args)>(Args)...};
}

FAsyncCoroutine UE5Coro::Private::FAggregateAwaiter::Consume(auto Data,
                                                             auto&& Awaiter)
{
	co_await Awaiter;

	UE::TScopeLock _(Data->Lock);
	if (--Data->Count != 0)
		co_return;
	auto Handle = Data->Handle;
	_.Unlock();

	if (std::holds_alternative<FLatentHandle>(Handle))
		std::get<FLatentHandle>(Handle).promise().ThreadSafeResume();
	else if (std::holds_alternative<FAsyncHandle>(Handle))
		std::get<FAsyncHandle>(Handle).resume();
	else
		; // we haven't been co_awaited yet, await_ready deals with this
}

