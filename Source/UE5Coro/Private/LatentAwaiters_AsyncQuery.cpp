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

#include "UE5Coro/LatentAwaiters.h"
#include <optional>

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
template<typename T>
using TQueryDelegate = std::conditional_t<std::is_same_v<T, FHitResult>,
                                          FTraceDelegate, FOverlapDelegate>;
template<typename T>
using TQueryDatum = std::conditional_t<std::is_same_v<T, FHitResult>,
                                       FTraceDatum, FOverlapDatum>;
}

namespace UE5Coro::Private
{
template<typename T>
class TAsyncQueryAwaiter<T>::TImpl
{
public:
	FPromise* Promise = nullptr;
	std::optional<TArray<T>> Result;

	void ReceiveResult(const FTraceHandle&, TQueryDatum<T>& Datum)
	{
		// Receive results
		if constexpr (std::is_same_v<T, FHitResult>)
			Result = std::move(Datum.OutHits);
		else
			Result = std::move(Datum.OutOverlaps);

		// If the coroutine is suspended (Promise is valid), resume it now
		if (Promise)
			Promise->Resume();
	}
};

template<typename T>
template<typename... P, typename... A>
TAsyncQueryAwaiter<T>::TAsyncQueryAwaiter(UWorld* World,
                                          FTraceHandle (UWorld::*Fn)(P...),
                                          A... Params)
	: Impl(new TImpl)
{
	checkf(IsInGameThread(),
	       TEXT("Async queries may only be started from the game thread."));
	auto Delegate = TQueryDelegate<T>::CreateSP(Impl.ToSharedRef(),
	                                            &TImpl::ReceiveResult);
	(World->*Fn)(Params..., &Delegate, 0);
}

template<typename T>
TAsyncQueryAwaiter<T>::~TAsyncQueryAwaiter() = default;

template<typename T>
TAsyncQueryAwaiter<T>& TAsyncQueryAwaiter<T>::operator co_await() &
{
	return *this;
}

template<typename T>
TAsyncQueryAwaiterRV<T>& TAsyncQueryAwaiter<T>::operator co_await() &&
{
	static_assert(sizeof(*this) == sizeof(TAsyncQueryAwaiterRV<T>));
	// Technically, this object is not a TAsyncQueryAwaiterRV
	return *std::launder(reinterpret_cast<TAsyncQueryAwaiterRV<T>*>(this));
}

template<typename T>
bool TAsyncQueryAwaiter<T>::await_ready()
{
	checkf(IsInGameThread(),
	       TEXT("Async queries may only be awaited on the game thread."));
	return Impl->Result.has_value();
}

template<typename T>
void TAsyncQueryAwaiter<T>::Suspend(FPromise& Promise)
{
	checkf(IsInGameThread(),
	       TEXT("Async queries may only be awaited on the game thread."));
	checkf(!Impl->Promise, TEXT("Attempted second concurrent co_await"));
	Impl->Promise = &Promise;
}

template<typename T>
const TArray<T>& TAsyncQueryAwaiter<T>::await_resume()
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: expected to resume on the game thread"));
	checkf(Impl->Result.has_value(),
	       TEXT("Internal error: resuming without a query result"));
	return *Impl->Result;
}

template<typename T>
TArray<T> TAsyncQueryAwaiterRV<T>::await_resume()
{
	return const_cast<TArray<T>&&>(TAsyncQueryAwaiter<T>::await_resume());
}

template class UE5CORO_API TAsyncQueryAwaiter<FHitResult>;
template class UE5CORO_API TAsyncQueryAwaiterRV<FHitResult>;
template class UE5CORO_API TAsyncQueryAwaiter<FOverlapResult>;
template class UE5CORO_API TAsyncQueryAwaiterRV<FOverlapResult>;
}

TAsyncQueryAwaiter<FHitResult> Latent::AsyncLineTraceByChannel(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, ECollisionChannel TraceChannel,
	const FCollisionQueryParams& Params,
	const FCollisionResponseParams& ResponseParam)
{
	return TAsyncQueryAwaiter<FHitResult>(
		GEngine->GetWorldFromContextObjectChecked(WorldContextObject),
		&UWorld::AsyncLineTraceByChannel, InTraceType, Start, End, TraceChannel,
		Params, ResponseParam);
}

TAsyncQueryAwaiter<FHitResult> Latent::AsyncLineTraceByObjectType(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End,
	const FCollisionObjectQueryParams& ObjectQueryParams,
	const FCollisionQueryParams& Params)
{
	return TAsyncQueryAwaiter<FHitResult>(
		GEngine->GetWorldFromContextObjectChecked(WorldContextObject),
		&UWorld::AsyncLineTraceByObjectType, InTraceType, Start, End,
		ObjectQueryParams, Params);
}

TAsyncQueryAwaiter<FHitResult> Latent::AsyncLineTraceByProfile(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, FName ProfileName,
	const FCollisionQueryParams& Params)
{
	return TAsyncQueryAwaiter<FHitResult>(
		GEngine->GetWorldFromContextObjectChecked(WorldContextObject),
		&UWorld::AsyncLineTraceByProfile, InTraceType, Start, End, ProfileName,
		Params);
}

TAsyncQueryAwaiter<FHitResult> Latent::AsyncSweepByChannel(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, const FQuat& Rot,
	ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params,
	const FCollisionResponseParams& ResponseParam)
{
	return TAsyncQueryAwaiter<FHitResult>(
		GEngine->GetWorldFromContextObjectChecked(WorldContextObject),
		&UWorld::AsyncSweepByChannel, InTraceType, Start, End, Rot,
		TraceChannel, CollisionShape, Params, ResponseParam);
}

TAsyncQueryAwaiter<FHitResult> Latent::AsyncSweepByObjectType(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, const FQuat& Rot,
	const FCollisionObjectQueryParams& ObjectQueryParams,
	const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params)
{
	return TAsyncQueryAwaiter<FHitResult>(
		GEngine->GetWorldFromContextObjectChecked(WorldContextObject),
		&UWorld::AsyncSweepByObjectType, InTraceType, Start, End, Rot,
		ObjectQueryParams, CollisionShape, Params);
}

TAsyncQueryAwaiter<FHitResult> Latent::AsyncSweepByProfile(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, const FQuat& Rot,
	FName ProfileName, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params)
{
	return TAsyncQueryAwaiter<FHitResult>(
		GEngine->GetWorldFromContextObjectChecked(WorldContextObject),
		&UWorld::AsyncSweepByProfile, InTraceType, Start, End, Rot, ProfileName,
		CollisionShape, Params);
}

TAsyncQueryAwaiter<FOverlapResult> Latent::AsyncOverlapByChannel(
	const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot,
	ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params,
	const FCollisionResponseParams& ResponseParam)
{
	return TAsyncQueryAwaiter<FOverlapResult>(
		GEngine->GetWorldFromContextObjectChecked(WorldContextObject),
		&UWorld::AsyncOverlapByChannel, Pos, Rot, TraceChannel, CollisionShape,
		Params, ResponseParam);
}

TAsyncQueryAwaiter<FOverlapResult> Latent::AsyncOverlapByObjectType(
	const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot,
	const FCollisionObjectQueryParams& ObjectQueryParams,
	const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params)
{
	return TAsyncQueryAwaiter<FOverlapResult>(
		GEngine->GetWorldFromContextObjectChecked(WorldContextObject),
		&UWorld::AsyncOverlapByObjectType, Pos, Rot, ObjectQueryParams,
		CollisionShape, Params);
}

TAsyncQueryAwaiter<FOverlapResult> Latent::AsyncOverlapByProfile(
	const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot,
	FName ProfileName, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params)
{
	return TAsyncQueryAwaiter<FOverlapResult>(
		GEngine->GetWorldFromContextObjectChecked(WorldContextObject),
		&UWorld::AsyncOverlapByProfile, Pos, Rot, ProfileName, CollisionShape,
		Params);
}
