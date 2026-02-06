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

#include "UE5Coro/Promise.h"
#include "Misc/ScopeExit.h"
#include "UE5Coro/Threading.h"

#pragma region Private
namespace UE5Coro::Private
{
template<typename T>
class [[nodiscard]] TManualPromiseExtras : public std::conditional_t<
	std::is_void_v<T>, FPromiseExtras, TPromiseExtras<T>>
{
	using Super = std::conditional_t<
		std::is_void_v<T>, FPromiseExtras, TPromiseExtras<T>>;
	using ThisClass = TManualPromiseExtras;
	std::atomic<int> RefCnt = 1; // TManualCoroutines, but not TCoroutines

public:
	FAwaitableEvent Event;

	explicit TManualPromiseExtras(FPromise& Promise) : Super(Promise) { }
	void AddRef() { verify(++RefCnt > 0); }
	[[nodiscard]] bool Release() { return !--RefCnt; }

	// Must be static, the object is constructed as part of the call
	static TCoroutine<T> Run(FString DebugName, FManualCoroutineOverride = {})
	{
		TCoroutine<>::SetDebugName(std::move(DebugName));
		auto* This = static_cast<ThisClass*>(FPromise::Current().Extras.get());
#if UE5CORO_DEBUG || UE5CORO_ENABLE_COROUTINE_TRACKING
		checkf(!FCString::Strcmp(This->DebugPromiseType, TEXT("Async")),
		       TEXT("Internal error: expected async promise"));
		This->DebugPromiseType = TEXT("Manual");
#endif
		if constexpr (std::is_void_v<T>)
			co_await This->Event;
		else
		{
			bool bReturnValue = false;
			ON_SCOPE_EXIT
			{
				// Clear out a spurious return value that might have arrived
				// during the acceptance of a cancellation
				if (!bReturnValue) [[unlikely]]
					This->ReturnValue = {};
			};
			co_await This->Event;
			bReturnValue = true; // No cancellation is processed from now on
			co_return FManualCoroutineOverride();
		}
	}

	static ThisClass* RawCast(const std::shared_ptr<FPromiseExtras>& Extras)
	{
		return static_cast<ThisClass*>(Extras.get());
	}

	static std::shared_ptr<ThisClass> SharedCast(
		const std::shared_ptr<FPromiseExtras>& Extras)
	{
		return std::static_pointer_cast<ThisClass>(Extras);
	}
};
}

template<typename T>
struct std::coroutine_traits<UE5Coro::TCoroutine<T>, FString,
                             UE5Coro::Private::FManualCoroutineOverride>
{
	using promise_type = UE5Coro::Private::TCoroutinePromise<
		T, UE5Coro::Private::FAsyncPromise,
		UE5Coro::Private::TManualPromiseExtras<T>>;

	// Extra sanity checks
	template<typename> struct TPromiseTypeCheck : std::false_type { };
	template<std::derived_from<UE5Coro::Private::FPromiseExtras> E>
	struct TPromiseTypeCheck<UE5Coro::Private::TCoroutinePromise<T,
		UE5Coro::Private::FAsyncPromise, E>> : std::true_type
	{ static_assert(std::derived_from<
		UE5Coro::Private::TManualPromiseExtras<T>, E>); };
	static_assert(TPromiseTypeCheck<typename std::coroutine_traits<
		UE5Coro::TCoroutine<T>>::promise_type>::value);
};

namespace UE5Coro
{
template<typename T>
TManualCoroutine<T>::TManualCoroutine(FString DebugName)
	: TCoroutine<T>(Private::TManualPromiseExtras<T>::Run(std::move(DebugName)))
{
	// Initial refcount is already set to 1
}

template<typename T>
TManualCoroutine<T>::TManualCoroutine(const TManualCoroutine& Other)
	: TCoroutine<T>(Other)
{
	Private::TManualPromiseExtras<T>::RawCast(this->Extras)->AddRef();
}

template<typename T>
TManualCoroutine<T>::~TManualCoroutine()
{
	if (Private::TManualPromiseExtras<T>::RawCast(this->Extras)->Release())
		this->Cancel();
}

template<typename T>
TManualCoroutine<T>& TManualCoroutine<T>::operator=(const TManualCoroutine& Other)
{
	if (this == &Other)
		return *this;
	this->~TManualCoroutine();
	return *new (this) TManualCoroutine(Other);
}

template<typename T>
void TManualCoroutine<T>::SetResult(T Result)
{
	bool bSuccessful = TrySetResult(std::move(Result));
	ensureMsgf(bSuccessful, TEXT("The coroutine was already complete"));
}

template<typename T>
bool TManualCoroutine<T>::TrySetResult(T Result)
{
	static_assert(!std::is_void_v<T>); // This should go to the specialization
	auto ExtrasT = Private::TManualPromiseExtras<T>::SharedCast(this->Extras);
	auto& Lock = ExtrasT->Lock;
	Lock.lock(); // Block incoming cancellations
	if (!ExtrasT->IsComplete())
	{
		ExtrasT->ReturnValue = std::move(Result);
		Lock.unlock();
		ExtrasT->Event.Trigger(); // Might delete this if the coroutine cleans up
		// Success is synchronous, cancellation isn't
		return ExtrasT->bWasSuccessful;
	}
	else
	{
		Lock.unlock();
		return false; // Already complete, discard the result
	}
}

}

namespace UE5Coro::Private
{
template<typename T>
struct TAwaitTransform<FAsyncPromise, TManualCoroutine<T>>
	: TAwaitTransform<FAsyncPromise, TCoroutine<T>>
{
};

template<typename T>
struct TAwaitTransform<FLatentPromise, TManualCoroutine<T>>
	: TAwaitTransform<FLatentPromise, TCoroutine<T>>
{
};
}
#pragma endregion
