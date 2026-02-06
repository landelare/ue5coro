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

#pragma region Private
namespace UE5Coro
{
// Argument deduction on TCoroutine<>
template<typename V>
TCoroutine<std::decay_t<V>> TCoroutine<>::FromResult(V&& Value)
{
	co_return std::forward<V>(Value);
}

// TCoroutine<T> matches T exactly
template<typename T>
TCoroutine<T> TCoroutine<T>::FromResult(T Value)
{
	co_return std::move(Value);
}

template<typename T>
const T& TCoroutine<T>::GetResult() const
{
	Wait();
	auto* ExtrasT = static_cast<Private::TPromiseExtras<T>*>(Extras.get());
#if UE5CORO_DEBUG
	ensureMsgf(!ExtrasT->bMoveUsed, TEXT("GetResult called after MoveResult"));
#endif
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

void TCoroutine<>::ContinueWith(std::invocable auto Fn)
{
	Extras->ContinueWith<void>(std::move(Fn));
}

void TCoroutine<>::ContinueWithWeak(Private::TStrongPtr auto Ptr,
                                    std::invocable auto Fn)
{
	using FWeak = Private::TWeak<decltype(Ptr)>;
	ContinueWith([Weak = typename FWeak::weak(std::move(Ptr)),
	              Fn = std::move(Fn)]
	{
		auto Strong = FWeak::Strengthen(Weak);
		if (FWeak::Get(Strong))
			Fn();
	});
}

void TCoroutine<>::ContinueWithWeak(Private::TStrongPtr auto Ptr,
	Private::TInvocableWithPtr<decltype(Ptr)> auto Fn)
{
	using FWeak = Private::TWeak<decltype(Ptr)>;
	ContinueWith([Weak = typename FWeak::weak(std::move(Ptr)),
	              Fn = std::move(Fn)]
	{
		auto Strong = FWeak::Strengthen(Weak);
		if (auto* Raw = FWeak::Get(Strong))
			std::invoke(Fn, Raw);
	});
}

template<typename T>
void TCoroutine<T>::ContinueWith(std::invocable<T> auto Fn)
{
	Extras->ContinueWith<T>(std::move(Fn));
}

template<typename T>
void TCoroutine<T>::ContinueWithWeak(Private::TStrongPtr auto Ptr,
                                     std::invocable<T> auto Fn)
{
	using FWeak = Private::TWeak<decltype(Ptr)>;
	ContinueWith([Weak = typename FWeak::weak(std::move(Ptr)),
	              Fn = std::move(Fn)](const T& Result)
	{
		auto Strong = FWeak::Strengthen(Weak);
		if (FWeak::Get(Strong))
			std::invoke(Fn, Result);
	});
}

template<typename T>
void TCoroutine<T>::ContinueWithWeak(
	Private::TStrongPtr auto Ptr,
	Private::TInvocableWithPtr<decltype(Ptr), T> auto Fn)
{
	using FWeak = Private::TWeak<decltype(Ptr)>;
	ContinueWith([Weak = typename FWeak::weak(std::move(Ptr)),
	              Fn = std::move(Fn)](const T& Result)
	{
		auto Strong = FWeak::Strengthen(Weak);
		if (auto* Raw = FWeak::Get(Strong))
			std::invoke(Fn, Raw, Result);
	});
}
}

// Declare these here, so that co_awaiting TCoroutines always picks them up.
// They're implemented elsewhere.
namespace UE5Coro::Private
{
template<typename, bool> struct TAsyncCoroutineAwaiter;
template<typename, bool> struct TLatentCoroutineAwaiter;

template<typename T>
struct TAwaitTransform<FAsyncPromise, TCoroutine<T>>
{
	TAsyncCoroutineAwaiter<T, false> operator()(const TCoroutine<T>&);
	TAsyncCoroutineAwaiter<T, true> operator()(TCoroutine<T>&&);
};

template<typename T>
struct TAwaitTransform<FLatentPromise, TCoroutine<T>>
{
	TLatentCoroutineAwaiter<T, false> operator()(const TCoroutine<T>&);
	TLatentCoroutineAwaiter<T, true> operator()(TCoroutine<T>&&);
};

template<typename P>
struct TAwaitTransform<P, FVoidCoroutine>
	: TAwaitTransform<P, TCoroutine<>> { };
}
#pragma endregion
