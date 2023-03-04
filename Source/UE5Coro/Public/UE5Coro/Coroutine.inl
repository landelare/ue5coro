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

namespace UE5Coro
{
template<typename T>
const T& TCoroutine<T>::GetResult() const
{
	Wait();
	auto* ExtrasT = static_cast<Private::TPromiseExtras<T>*>(Extras.get());
	return ExtrasT->ReturnValue;
}

template<typename T>
T&& TCoroutine<T>::MoveResult()
{
	Wait();
	auto* ExtrasT = static_cast<Private::TPromiseExtras<T>*>(Extras.get());
#if UE5CORO_DEBUG
	[[maybe_unused]] bool bOld = false;
	ensureMsgf(ExtrasT->bMoveUsed.compare_exchange_strong(bOld, true),
	           TEXT("MoveResult called multiple times on the same value"));
#endif
	return std::move(ExtrasT->ReturnValue);
}

template<typename F>
std::enable_if_t<std::is_invocable_v<F>> TCoroutine<>::ContinueWith(F Continuation)
{
	UE::TScopeLock _(Extras->Lock);
	if (Extras->IsComplete())
	{
		_.Unlock();
		Continuation();
	}
	else
		Extras->OnCompleted = [Parent = std::move(Extras->OnCompleted),
		                       Fn = std::move(Continuation)]
		{
			Parent();
			Fn();
		};
}

template<typename U, typename F>
std::enable_if_t<Private::TWeak<U>::value && std::is_invocable_v<F>>
TCoroutine<>::ContinueWithWeak(U Ptr, F Continuation)
{
	ContinueWith([Weak = typename Private::TWeak<U>::weak(std::move(Ptr)),
	              Fn = std::move(Continuation)]
	{
		auto Strong = Private::TWeak<U>::Strengthen(Weak);
		if (Private::TWeak<U>::Get(Strong))
			Fn();
	});
}

template<typename U, typename F>
std::enable_if_t<std::is_invocable_v<F, typename Private::TWeak<U>::ptr>>
TCoroutine<>::ContinueWithWeak(U Ptr, F Continuation)
{
	ContinueWith([Weak = typename Private::TWeak<U>::weak(std::move(Ptr)),
	              Fn = std::move(Continuation)]
	{
		auto Strong = Private::TWeak<U>::Strengthen(Weak);
		if (auto* Raw = Private::TWeak<U>::Get(Strong))
			std::invoke(Fn, Raw);
	});
}

template<typename T>
template<typename F>
std::enable_if_t<std::is_invocable_v<F> || std::is_invocable_v<F, T>>
TCoroutine<T>::ContinueWith(F Continuation)
{
	// Handle functors that can't take (T) with the void specialization.
	// This can't be an overload, it would be considered ambiguous.
	if constexpr (!std::is_invocable_v<F, T>)
		TCoroutine<>::ContinueWith(std::move(Continuation));
	else
	{
		auto* ExtrasT = static_cast<Private::TPromiseExtras<T>*>(Extras.get());
		UE::TScopeLock _(ExtrasT->Lock);
		if (ExtrasT->IsComplete())
		{
			_.Unlock();
			std::invoke(Continuation, ExtrasT->ReturnValue);
		}
		else
			ExtrasT->OnCompletedT = [Parent = std::move(ExtrasT->OnCompletedT),
			                         Fn = std::move(Continuation)](const T& Result)
			{
				Parent(Result);
				std::invoke(Fn, Result);
			};
	}
}

template<typename T>
template<typename U, typename F>
std::enable_if_t<Private::TWeak<U>::value &&
                 (std::is_invocable_v<F> || std::is_invocable_v<F, T>)>
TCoroutine<T>::ContinueWithWeak(U Ptr, F Continuation)
{
	if constexpr (!std::is_invocable_v<F, T>)
		TCoroutine<>::ContinueWithWeak(std::move(Ptr), std::move(Continuation));
	else
		ContinueWith([Weak = typename Private::TWeak<U>::weak(std::move(Ptr)),
		              Fn = std::move(Continuation)](const T& Result)
		{
			auto Strong = Private::TWeak<U>::Strengthen(Weak);
			if (Private::TWeak<U>::Get(Strong))
			    std::invoke(Fn, Result);
		});
}

template<typename T>
template<typename U, typename F>
std::enable_if_t<std::is_invocable_v<F, typename Private::TWeak<U>::ptr> ||
                 std::is_invocable_v<F, typename Private::TWeak<U>::ptr, T>>
TCoroutine<T>::ContinueWithWeak(U Ptr, F Continuation)
{
	if constexpr (!std::is_invocable_v<F, typename Private::TWeak<U>::ptr, T>)
		TCoroutine<>::ContinueWithWeak(std::move(Ptr), std::move(Continuation));
	else
		ContinueWith([Weak = typename Private::TWeak<U>::weak(std::move(Ptr)),
		              Fn = std::move(Continuation)](const T& Result)
		{
			auto Strong = Private::TWeak<U>::Strengthen(Weak);
			if (auto* Raw = Private::TWeak<U>::Get(Strong))
				std::invoke(Fn, Raw, Result);
		});
}
}

// Declare these here, so that co_awaiting TCoroutines always picks them up.
// They're implemented in another header.
namespace UE5Coro::Private
{
template<typename T> class TAsyncCoroutineAwaiter;
template<typename T> class TLatentCoroutineAwaiter;

template<typename T>
struct Private::TAwaitTransform<FAsyncPromise, TCoroutine<T>>
{
	TAsyncCoroutineAwaiter<T> operator()(TCoroutine<T>);
};

template<typename T>
struct Private::TAwaitTransform<FLatentPromise, TCoroutine<T>>
{
	TLatentCoroutineAwaiter<T> operator()(TCoroutine<T>);
};

template<typename P>
struct Private::TAwaitTransform<P, FAsyncCoroutine>
{
	auto operator()(FAsyncCoroutine Coro)
	{
		return TAwaitTransform<P, TCoroutine<>>()(std::move(Coro));
	}
};
}
