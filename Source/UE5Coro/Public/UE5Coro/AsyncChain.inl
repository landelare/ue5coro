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

#include "UE5Coro/Private.h"

#pragma region Private
namespace UE5Coro::Private
{
template<typename...>
struct TFindDelegate;

template<TIsDelegateOrPointer T, typename... Rest>
struct TFindDelegate<T, Rest...>
{
	using type = std::remove_cvref_t<std::remove_pointer_t<T>>;
};

template<typename F, typename... T>
struct TFindDelegate<F, T...>
{
	using type = typename TFindDelegate<T...>::type;
};

template<typename...>
struct TAsyncChain;

// Terminator
template<>
struct TAsyncChain<>
{
	static void Call(TIsDelegate auto*, auto&& Fn)
	{
		std::forward<decltype(Fn)>(Fn)();
	}
};

// Delegate
template<TIsDelegateOrPointer T, typename... Types>
struct TAsyncChain<T, Types...>
{
	static void Call(TIsDelegate auto* Delegate, auto&& Fn, auto&&... Args)
	{
		if constexpr (std::is_pointer_v<T>)
			TAsyncChain<Types...>::Call(
				Delegate,
				std::bind_front(std::forward<decltype(Fn)>(Fn), Delegate),
				std::forward<decltype(Args)>(Args)...);
		else
			TAsyncChain<Types...>::Call(
				Delegate,
				std::bind_front(std::forward<decltype(Fn)>(Fn),
				                std::ref(*Delegate)),
				std::forward<decltype(Args)>(Args)...);
	}
};

// Everything else
template<typename T, typename... Types>
struct TAsyncChain<T, Types...>
{
	static void Call(TIsDelegate auto* Delegate, auto&& Fn, auto&& Arg1,
	                 auto&&... Args)
	{
		TAsyncChain<Types...>::Call(
			Delegate,
			std::bind_front(std::forward<decltype(Fn)>(Fn),
			                TForwardRef<decltype(Arg1)>(Arg1)),
			std::forward<decltype(Args)>(Args)...);
	}
};

template<typename>
struct TConvertTupleToAsyncChain;

template<typename... T>
struct TConvertTupleToAsyncChain<std::tuple<T...>>
{
	using type = TAsyncChain<T...>;
};

template<TIsDelegate TDelegate, typename TFn, typename TParamsTuple,
         typename... TArgs>
class TAsyncChainAwaiter final
	: public TAwaitTransform<FNonCancelable, TDelegate>::FAwaiter
{
	using Super = typename TAwaitTransform<FNonCancelable, TDelegate>::FAwaiter;
#if UE5CORO_DEBUG
	bool bUsed = false;
	bool bResumed = false;
#endif
	TDelegate* Delegate;
	TFn Fn;
	std::tuple<std::decay_t<TArgs>...> Args;

public:
	explicit TAsyncChainAwaiter(TFn Fn, TArgs&&... Args)
		: Super(*(Delegate = new TDelegate)), Fn(std::move(Fn)),
		  Args(std::forward<TArgs>(Args)...) { }
	UE_NONCOPYABLE(TAsyncChainAwaiter);

	~TAsyncChainAwaiter()
	{
		// Work around an engine bug in dynamic multicast delegates
		AsyncTask(ENamedThreads::GameThread,
		          [Delegate = Delegate] { delete Delegate; });
	}

	template<typename P>
	void await_suspend(std::coroutine_handle<P> Handle)
	{
#if UE5CORO_DEBUG
		verifyf(!std::exchange(bUsed, true),
		        TEXT("Async::Chain is not reusable"));
#endif
		// Bind the delegate and arrange for the coroutine to be resumed
		Super::await_suspend(Handle);
		// Call the function with the bound delegate; doing so might delete this
		Call(std::make_index_sequence<sizeof...(TArgs)>());
	}

#if UE5CORO_DEBUG
	decltype(auto) await_resume()
	{
		verifyf(!std::exchange(bResumed, true),
		        TEXT("Chained delegate was called multiple times"));
		return Super::await_resume();
	}
#endif

private:
	template<size_t... I>
	void Call(std::index_sequence<I...>)
	{
		TConvertTupleToAsyncChain<TParamsTuple>::type::Call(
			Delegate, std::move(Fn), std::get<I>(std::move(Args))...);
	}
};
}

namespace UE5Coro::Async
{
template<typename... FnParams>
auto Chain(auto (*Fn)(FnParams...), auto&&... Args)
{
	static_assert((0 + ... + Private::TIsDelegateOrPointer<FnParams>) == 1,
	              "Chained function must take exactly 1 delegate parameter");
	static_assert(sizeof...(FnParams) == sizeof...(Args) + 1,
	              "Incorrect number of arguments provided (skip the delegate!)");
	using FDelegate = typename Private::TFindDelegate<FnParams...>::type;
	return Private::TAsyncChainAwaiter<FDelegate, decltype(Fn),
	                                   std::tuple<FnParams...>, decltype(Args)...>(
		Fn, std::forward<decltype(Args)>(Args)...);
}

template<typename Class, typename... FnParams>
auto Chain(Class* Object, auto (Class::*Fn)(FnParams...), auto&&... Args)
{
	static_assert((0 + ... + Private::TIsDelegateOrPointer<FnParams>) == 1,
	              "Chained function must take exactly 1 delegate parameter");
	static_assert(sizeof...(FnParams) == sizeof...(Args) + 1,
	              "Incorrect number of arguments provided (skip the delegate!)");
	using FDelegate = typename Private::TFindDelegate<FnParams...>::type;
	return Private::TAsyncChainAwaiter<FDelegate,
	                                   decltype(std::bind_front(Fn, Object)),
	                                   std::tuple<FnParams...>, decltype(Args)...>(
		std::bind_front(Fn, Object), std::forward<decltype(Args)>(Args)...);
}
}
#pragma endregion
