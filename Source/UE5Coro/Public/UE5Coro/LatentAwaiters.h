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
#include "UE5Coro/AsyncCoroutine.h"

namespace UE5Coro::Private
{
class FAsyncPromise;
class FLatentAwaiter;
class FLatentCancellation;
class FLatentPromise;
class FPackageLoadAwaiter;
template<std::derived_from<UObject>> class TAsyncLoadAwaiter;
template<typename> class TAsyncQueryAwaiter;
}

namespace UE5Coro::Latent
{
/** Stops the latent coroutine immediately WITHOUT firing the latent exec pin.<br>
 *  The coroutine WILL NOT be resumed. This does not count as the coroutine being
 *  aborted. */
Private::FLatentCancellation Cancel();

#pragma region Tick

/** Resumes the coroutine in the next tick.<br>
 *  @see Latent::Until for an alternative to while-NextTick loops. */
UE5CORO_API Private::FLatentAwaiter NextTick();

[[deprecated("Use UE5Coro::Latent::Ticks instead")]]
UE5CORO_API Private::FLatentAwaiter Frames(int32);

/** Resumes the coroutine the given number of ticks later. */
UE5CORO_API Private::FLatentAwaiter Ticks(int64);

/** Polls the provided function, resumes the coroutine when it returns true. */
UE5CORO_API Private::FLatentAwaiter Until(std::function<bool()> Function);

#pragma endregion

#pragma region Time

/** Resumes the coroutine the specified amount of seconds later.<br>
 *  This is affected by both pause and time dilation. */
UE5CORO_API Private::FLatentAwaiter Seconds(double);

/** Resumes the coroutine the specified amount of seconds later.<br>
 *  This is affected by time dilation only, NOT pause. */
UE5CORO_API Private::FLatentAwaiter UnpausedSeconds(double);

/** Resumes the coroutine the specified amount of seconds later.<br>
 *  This is not affected by pause or time dilation. */
UE5CORO_API Private::FLatentAwaiter RealSeconds(double);

/** Resumes the coroutine the specified amount of seconds later.<br>
 *  This is affected by pause only, NOT time dilation. */
UE5CORO_API Private::FLatentAwaiter AudioSeconds(double);

#pragma endregion

#pragma region Chain

/** Resumes the coroutine once the chained static latent action has finished,
 *  with automatic parameter matching.<br>Example usage:<br>
 *  co_await Latent::Chain(&UKismetSystemLibrary::Delay, 1.0f); */
template<typename... FnParams>
Private::FLatentAwaiter Chain(auto (*Function)(FnParams...), auto&&... Args);

/** Resumes the coroutine once the chained member latent action has finished,
 *  with automatic parameter matching.<br>Example usage:<br>
 *  co_await Latent::Chain(&UMediaPlayer::OpenSourceLatent, MediaPlayer,
 *                        MediaSource, Options, bSuccess); */
template<std::derived_from<UObject> Class, typename... FnParams>
Private::FLatentAwaiter Chain(auto (Class::*Function)(FnParams...),
                              Class* Object, auto&&... Args);

/** Resumes the coroutine once the chained latent action has finished,
 *  with manual parameter matching.<br>
 *  Use std::placeholders::_1 and _2 for the world context and LatentInfo.<br>
 *  Example usage:<br>
 *  co_await Latent::ChainEx(&UKismetSystemLibrary::Delay, _1, 1.0f, _2); */
Private::FLatentAwaiter ChainEx(auto&& Function, auto&&... Args);

#pragma endregion

#pragma region Async loading

/** Asynchronously starts loading the object, resumes once it's loaded.<br>
 *  The result of the co_await expression is the T*. */
template<std::derived_from<UObject> T>
Private::TAsyncLoadAwaiter<T> AsyncLoadObject(TSoftObjectPtr<T>);

/** Asynchronously starts loading the class, resumes once it's loaded.<br>
 *  The result of the co_await expression is the UClass*. */
UE5CORO_API Private::TAsyncLoadAwaiter<UClass> AsyncLoadClass(
	TSoftClassPtr<UObject>);

/** Asynchronously starts loading the package, resumes once it's loaded.<br>
 *  The result of the co_await expression is the UPackage*.<br>
 *  For parameters see the engine function ::LoadPackageAsync(). */
UE5CORO_API Private::FPackageLoadAwaiter AsyncLoadPackage(
	const FPackagePath& Path, FName PackageNameToCreate = NAME_None,
	EPackageFlags PackageFlags = PKG_None, int32 PIEInstanceID = INDEX_NONE,
	TAsyncLoadPriority PackagePriority = 0,
	const FLinkerInstancingContext* InstancingContext = nullptr);

#pragma endregion

#pragma region Async collision queries

// Async UWorld queries. For parameters, see their originals in World.h.

UE5CORO_API Private::TAsyncQueryAwaiter<FHitResult> AsyncLineTraceByChannel(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, ECollisionChannel TraceChannel,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam,
	const FCollisionResponseParams& ResponseParam = FCollisionResponseParams::DefaultResponseParam);

UE5CORO_API Private::TAsyncQueryAwaiter<FHitResult> AsyncLineTraceByObjectType(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End,
	const FCollisionObjectQueryParams& ObjectQueryParams,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam);

UE5CORO_API Private::TAsyncQueryAwaiter<FHitResult> AsyncLineTraceByProfile(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, FName ProfileName,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam);

UE5CORO_API Private::TAsyncQueryAwaiter<FHitResult> AsyncSweepByChannel(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, const FQuat& Rot,
	ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam,
	const FCollisionResponseParams& ResponseParam = FCollisionResponseParams::DefaultResponseParam);

UE5CORO_API Private::TAsyncQueryAwaiter<FHitResult> AsyncSweepByObjectType(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, const FQuat& Rot,
	const FCollisionObjectQueryParams& ObjectQueryParams,
	const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam);

UE5CORO_API Private::TAsyncQueryAwaiter<FHitResult> AsyncSweepByProfile(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, const FQuat& Rot,
	FName ProfileName, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam);

UE5CORO_API Private::TAsyncQueryAwaiter<FOverlapResult> AsyncOverlapByChannel(
	const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot,
	ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam,
	const FCollisionResponseParams& ResponseParam = FCollisionResponseParams::DefaultResponseParam);

UE5CORO_API Private::TAsyncQueryAwaiter<FOverlapResult> AsyncOverlapByObjectType(
	const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot,
	const FCollisionObjectQueryParams& ObjectQueryParams,
	const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam);

UE5CORO_API Private::TAsyncQueryAwaiter<FOverlapResult> AsyncOverlapByProfile(
	const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot,
	FName ProfileName, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam);

#pragma endregion
}

namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FLatentCancellation final
{
public:
	bool await_ready() { return false; }
	void await_resume() { }
	void await_suspend(FLatentHandle);
};

class [[nodiscard]] UE5CORO_API FLatentAwaiter
{
protected:
	void* State;
	bool (*Resume)(void*& State, bool bCleanup);

	FLatentAwaiter(FLatentAwaiter&&);

public:
	explicit FLatentAwaiter(void* State, bool (*Resume)(void*&, bool))
		: State(State), Resume(Resume) { }
	~FLatentAwaiter();

	bool ShouldResume() { return (*Resume)(State, false); }

	bool await_ready() { return ShouldResume(); }
	void await_resume() { }
	void await_suspend(FAsyncHandle);
	void await_suspend(FLatentHandle);
};

namespace AsyncLoad
{
UE5CORO_API FLatentAwaiter InternalAsyncLoadObject(TSoftObjectPtr<UObject>);
UE5CORO_API UObject* InternalResume(void*);
}

template<std::derived_from<UObject> T>
class [[nodiscard]] TAsyncLoadAwaiter : public FLatentAwaiter
{
public:
	explicit TAsyncLoadAwaiter(FLatentAwaiter&& Other)
		: FLatentAwaiter(std::move(Other)) { }

	T* await_resume() { return Cast<T>(AsyncLoad::InternalResume(State)); }
};

static_assert(sizeof(FLatentAwaiter) == sizeof(TAsyncLoadAwaiter<UObject>));

class [[nodiscard]] UE5CORO_API FPackageLoadAwaiter
{
	FOptionalHandleVariant Handle;
	TStrongObjectPtr<UPackage> Result; // This might be carried across co_awaits

	void Loaded(const FName&, UPackage*, EAsyncLoadingResult::Type);

public:
	explicit FPackageLoadAwaiter(
		const FPackagePath& Path, FName PackageNameToCreate,
		EPackageFlags PackageFlags, int32 PIEInstanceID,
		TAsyncLoadPriority PackagePriority,
		const FLinkerInstancingContext* InstancingContext);
	UE_NONCOPYABLE(FPackageLoadAwaiter);

	bool await_ready() { return Result.IsValid(); }
	void await_suspend(FAsyncHandle);
	void await_suspend(FLatentHandle);
	UPackage* await_resume();
};

template<typename T>
class [[nodiscard]] TAsyncQueryAwaiter
{
	class TImpl;
	TImpl* Impl;

public:
	template<typename... P>
	explicit TAsyncQueryAwaiter(UWorld*, FTraceHandle (UWorld::*)(P...), auto...);
	UE5CORO_API ~TAsyncQueryAwaiter();
	UE_NONCOPYABLE(TAsyncQueryAwaiter);

	UE5CORO_API bool await_ready();
	UE5CORO_API void await_suspend(FAsyncHandle);
	UE5CORO_API void await_suspend(FLatentHandle);
	UE5CORO_API TArray<T> await_resume();
};
}

inline UE5Coro::Private::FLatentCancellation UE5Coro::Latent::Cancel()
{
	return {};
}

template<std::derived_from<UObject> T>
UE5Coro::Private::TAsyncLoadAwaiter<T> UE5Coro::Latent::AsyncLoadObject(
	TSoftObjectPtr<T> Ptr)
{
	return Private::TAsyncLoadAwaiter<T>(
		Private::AsyncLoad::InternalAsyncLoadObject(Ptr));
}

#include "LatentChain.inl"
