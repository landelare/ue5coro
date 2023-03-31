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
#include "Engine/StreamableManager.h"
#include "UE5Coro/AsyncCoroutine.h"

namespace UE5Coro::Private
{
class FAsyncPromise;
class FLatentAwaiter;
class FLatentCancellation;
class FLatentChainAwaiter;
class FLatentPromise;
class FPackageLoadAwaiter;
template<typename, int> class TAsyncLoadAwaiter;
template<typename> class TAsyncQueryAwaiter;
template<typename> class TAsyncQueryAwaiterRV;
}

namespace UE5Coro::Latent
{
/** Stops the latent coroutine immediately WITHOUT firing the latent exec pin.<br>
 *  The coroutine WILL NOT be resumed.
 *  This does not count as the coroutine being aborted.
 *  @see TCoroutine<>::Cancel to cancel a coroutine from outside. */
Private::FLatentCancellation Cancel() noexcept;

#pragma region Tick

/** Resumes the coroutine in the next tick.<br>
 *  @see Latent::Until for an alternative to while-NextTick loops. */
UE5CORO_API Private::FLatentAwaiter NextTick();

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

#if UE5CORO_CPP20
/** Resumes the coroutine once the chained static latent action has finished,
 *  with automatic parameter matching.<br>
 *  The result of the co_await expression is true if the chained latent action
 *  finished normally, false if it didn't.<br>
 *  Example usage:<br>
 *  co_await Latent::Chain(&UKismetSystemLibrary::Delay, 1.0f); */
template<typename... FnParams>
Private::FLatentChainAwaiter Chain(auto (*Function)(FnParams...), auto&&... Args);

/** Resumes the coroutine once the chained member latent action has finished,
 *  with automatic parameter matching.
 *  The result of the co_await expression is true if the chained latent action
 *  finished normally, false if it didn't.<br>
 *  Example usage:<br>
 *  co_await Latent::Chain(&UMediaPlayer::OpenSourceLatent, MediaPlayer,
 *                        MediaSource, Options, bSuccess); */
template<std::derived_from<UObject> Class, typename... FnParams>
Private::FLatentChainAwaiter Chain(auto (Class::*Function)(FnParams...),
                                   Class* Object, auto&&... Args);
#endif

/** Resumes the coroutine once the chained latent action has finished,
 *  with manual parameter matching.<br>
 *  The result of the co_await expression is true if the chained latent action
 *  finished normally, false if it didn't.<br>
 *  Use std::placeholders::_1 and _2 for the world context and LatentInfo.<br>
 *  Example usage:<br>
 *  co_await Latent::ChainEx(&UKismetSystemLibrary::Delay, _1, 1.0f, _2); */
template<typename F, typename... A>
Private::FLatentChainAwaiter ChainEx(F&& Function, A&&... Args);

#pragma endregion

#pragma region Async loading

/** Asynchronously starts loading the object, resumes once it's loaded.<br>
 *  The result of the co_await expression is the loaded T*. */
template<typename T>
auto AsyncLoadObject(TSoftObjectPtr<T>,
	TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
	-> std::enable_if_t<std::is_base_of_v<UObject, T>,
	                    Private::TAsyncLoadAwaiter<T*, 0>>;

/** Asynchronously starts loading the objects, resumes once they're loaded.<br>
 *  The result of the co_await expression is TArray<T*>. */
template<typename T>
auto AsyncLoadObjects(const TArray<TSoftObjectPtr<T>>&,
	TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
	-> std::enable_if_t<std::is_base_of_v<UObject, T>,
	                    Private::TAsyncLoadAwaiter<TArray<T*>, 0>>;

/** Asynchronously starts loading the objects at the given paths,
 *  resumes once they're loaded. The loaded objects are not resolved. */
UE5CORO_API auto AsyncLoadObjects(TArray<FSoftObjectPath>,
	TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::FLatentAwaiter;

/** Asynchronously starts loading the primary asset with any bundles specified,
 *  resumes once they're loaded.<br>
 *  The asset will stay in memory until explicitly unloaded. */
UE5CORO_API auto AsyncLoadPrimaryAsset(const FPrimaryAssetId& AssetToLoad,
	const TArray<FName>& LoadBundles = {},
	TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::FLatentAwaiter;

/** Asynchronously starts loading the primary asset of the given type with any
 *  bundles specified, resumes once they're loaded.<br>
 *  The asset will stay in memory until explicitly unloaded.<br>
 *  The result of the co_await expression is the loaded T* or nullptr. */
template<typename T>
auto AsyncLoadPrimaryAsset(FPrimaryAssetId AssetToLoad,
	const TArray<FName>& LoadBundles = {},
	TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
	-> std::enable_if_t<std::is_base_of_v<UObject, T>,
	                    Private::TAsyncLoadAwaiter<T*, 1>>;

/** Asynchronously starts loading the primary assets with any bundles specified,
 *  resumes once they're loaded.<br>
 *  The assets will stay in memory until explicitly unloaded. */
UE5CORO_API auto AsyncLoadPrimaryAssets(TArray<FPrimaryAssetId> AssetsToLoad,
	const TArray<FName>& LoadBundles = {},
	TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::FLatentAwaiter;

/** Asynchronously starts loading the primary assets of the given type with any
 *  bundles specified, resumes once they're loaded.<br>
 *  The assets will stay in memory until explicitly unloaded.<br>
 *  The result of the co_await expression is the loaded and filtered TArray<T*>. */
template<typename T>
auto AsyncLoadPrimaryAssets(TArray<FPrimaryAssetId> AssetsToLoad,
	const TArray<FName>& LoadBundles = {},
	TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
	-> std::enable_if_t<std::is_base_of_v<UObject, T>,
	                    Private::TAsyncLoadAwaiter<TArray<T*>, 1>>;

/** Asynchronously starts loading the class, resumes once it's loaded.<br>
 *  The result of the co_await expression is the loaded UClass*. */
UE5CORO_API auto AsyncLoadClass(TSoftClassPtr<UObject>,
	TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::TAsyncLoadAwaiter<UClass*, 0>;

/** Asynchronously starts loading the classes, resumes once they're loaded.<br>
 *  The result of the co_await expression is TArray<UClass*>. */
UE5CORO_API auto AsyncLoadClasses(const TArray<TSoftClassPtr<UObject>>&,
	TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::TAsyncLoadAwaiter<TArray<UClass*>, 0>;

/** Asynchronously starts loading the package, resumes once it's loaded.<br>
 *  The result of the co_await expression is the UPackage*.<br>
 *  For parameters see the engine function ::LoadPackageAsync(). */
UE5CORO_API auto AsyncLoadPackage(const FPackagePath& Path,
	FName PackageNameToCreate = NAME_None,
	EPackageFlags PackageFlags = PKG_None, int32 PIEInstanceID = INDEX_NONE,
	TAsyncLoadPriority PackagePriority = 0,
	const FLinkerInstancingContext* InstancingContext = nullptr)
	-> Private::FPackageLoadAwaiter;

#pragma endregion

#pragma region Async collision queries

// Async UWorld queries. For parameters, see their originals in World.h.
// It's slightly more efficient to co_await rvalues of these instead of lvalues.

UE5CORO_API Private::TAsyncQueryAwaiter<FHitResult> AsyncLineTraceByChannel(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, ECollisionChannel TraceChannel,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam,
	const FCollisionResponseParams& ResponseParam =
		FCollisionResponseParams::DefaultResponseParam);

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
	const FCollisionResponseParams& ResponseParam =
		FCollisionResponseParams::DefaultResponseParam);

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
	const FCollisionResponseParams& ResponseParam =
		FCollisionResponseParams::DefaultResponseParam);

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
class [[nodiscard]] UE5CORO_API FLatentCancellation final // not TAwaiter
{
public:
	bool await_ready() noexcept { return false; }

	template<typename P>
	auto await_suspend(stdcoro::coroutine_handle<P> Handle)
		// co_awaiting this in async mode is meaningless, use co_return instead.
		-> std::enable_if_t<std::is_base_of_v<FLatentPromise, P>>
	{
		Handle.promise().CancelFromWithin();
	}

	void await_resume() noexcept { }
};

class [[nodiscard]] UE5CORO_API FLatentAwaiter // not TAwaiter
{
	void Suspend(FAsyncPromise&);
	void Suspend(FLatentPromise&);

protected:
	void* State;
	bool (*Resume)(void*& State, bool bCleanup);

public:
	explicit FLatentAwaiter(void* State, bool (*Resume)(void*&, bool)) noexcept
		: State(State), Resume(Resume) { }
	FLatentAwaiter(const FLatentAwaiter&) = delete;
	FLatentAwaiter(FLatentAwaiter&&) noexcept;
	~FLatentAwaiter();

	bool ShouldResume();

	bool await_ready() { return ShouldResume(); }

	template<typename P>
	auto await_suspend(stdcoro::coroutine_handle<P> Handle)
		-> std::enable_if_t<std::is_base_of_v<FPromise, P>>
	{
		Suspend(Handle.promise());
	}

	void await_resume() noexcept { }
};

namespace AsyncLoad
{
template<int> // Switches between non-exported types
UE5CORO_API TArray<UObject*> InternalResume(void*);
}

template<typename T, int HiddenType>
class [[nodiscard]] TAsyncLoadAwaiter : public FLatentAwaiter
{
public:
	explicit TAsyncLoadAwaiter(FLatentAwaiter&& Other) noexcept
		: FLatentAwaiter(std::move(Other)) { }
	TAsyncLoadAwaiter(TAsyncLoadAwaiter&&) noexcept = default;

	T await_resume()
	{
		TArray<UObject*> Assets = AsyncLoad::InternalResume<HiddenType>(State);
		if constexpr (TIsTArray<T>::Value)
		{
			static_assert(std::is_pointer_v<typename T::ElementType>);
			using V = std::remove_pointer_t<typename T::ElementType>;
			static_assert(std::is_base_of_v<UObject, V>);
			static_assert(!std::is_const_v<V>);
			[[maybe_unused]] int OldNum = Assets.Num();
			Assets.RemoveAll([](UObject* Ptr) { return !Ptr->IsA<V>(); });
			if constexpr (HiddenType == 0) // These come from typed soft ptrs
				check(Assets.Num() == OldNum); // Strict check
			return std::move(*std::launder(reinterpret_cast<T*>(&Assets)));
		}
		else
		{
			static_assert(std::is_pointer_v<T>);
			using V = std::remove_pointer_t<T>;
			static_assert(std::is_base_of_v<UObject, V>);
			checkf(Assets.Num() <= 1,
			       TEXT("Unexpected multiple assets for single load"));
			return Assets.IsValidIndex(0) ? Cast<V>(Assets[0]) : nullptr;
		}
	}
};

static_assert(sizeof(FLatentAwaiter) ==
              sizeof(TAsyncLoadAwaiter<UObject*, 0>));
static_assert(sizeof(FLatentAwaiter) ==
              sizeof(TAsyncLoadAwaiter<TArray<UObject*>, 0>));

class [[nodiscard]] UE5CORO_API FPackageLoadAwaiter
	: public TAwaiter<FPackageLoadAwaiter>
{
	struct FState
	{
		FPromise* Promise = nullptr;
		TStrongObjectPtr<UPackage> Result; // This might be carried across co_awaits
		void Loaded(const FName&, UPackage*, EAsyncLoadingResult::Type);
	};
	TSharedPtr<FState, ESPMode::NotThreadSafe> State;

public:
	explicit FPackageLoadAwaiter(
		const FPackagePath& Path, FName PackageNameToCreate,
		EPackageFlags PackageFlags, int32 PIEInstanceID,
		TAsyncLoadPriority PackagePriority,
		const FLinkerInstancingContext* InstancingContext);

	bool await_ready();
	void Suspend(FPromise&);
	UPackage* await_resume();
};

template<typename T>
class [[nodiscard]] UE5CORO_API TAsyncQueryAwaiter
	: public TAwaiter<TAsyncQueryAwaiter<T>>
{
	class TImpl;
	TSharedPtr<TImpl, ESPMode::NotThreadSafe> Impl;

public:
	template<typename... P, typename... A>
	explicit TAsyncQueryAwaiter(UWorld*, FTraceHandle (UWorld::*)(P...), A...);
	~TAsyncQueryAwaiter();

	// Workaround for not being able to rvalue overload await_resume
	TAsyncQueryAwaiter& operator co_await() &;
	TAsyncQueryAwaiterRV<T>& operator co_await() &&;

	bool await_ready();
	void Suspend(FPromise&);
	const TArray<T>& await_resume();
};

template<typename T>
class [[nodiscard]] UE5CORO_API TAsyncQueryAwaiterRV
	: public TAsyncQueryAwaiter<T>
{
public:
	TAsyncQueryAwaiterRV() = delete; // Objects of this type are never created
	TArray<T> await_resume();
};
}

inline UE5Coro::Private::FLatentCancellation UE5Coro::Latent::Cancel() noexcept
{
	return {};
}

template<typename T>
auto UE5Coro::Latent::AsyncLoadObject(TSoftObjectPtr<T> Ptr,
                                      TAsyncLoadPriority Priority)
	-> std::enable_if_t<std::is_base_of_v<UObject, T>,
	                    Private::TAsyncLoadAwaiter<T*, 0>>
{
	return Private::TAsyncLoadAwaiter<T*, 0>(
		AsyncLoadObjects(TArray{Ptr.ToSoftObjectPath()}, Priority));
}

template<typename T>
auto UE5Coro::Latent::AsyncLoadObjects(const TArray<TSoftObjectPtr<T>>& Ptrs,
                                       TAsyncLoadPriority Priority)
	-> std::enable_if_t<std::is_base_of_v<UObject, T>,
	                    Private::TAsyncLoadAwaiter<TArray<T*>, 0>>
{
	TArray<FSoftObjectPath> Paths;
	Paths.Reserve(Ptrs.Num());
	for (const auto& Ptr : Ptrs)
		Paths.Add(Ptr.ToSoftObjectPath());

	return Private::TAsyncLoadAwaiter<TArray<T*>, 0>(
		AsyncLoadObjects(std::move(Paths), Priority));
}

template<typename T>
auto UE5Coro::Latent::AsyncLoadPrimaryAsset(FPrimaryAssetId AssetToLoad,
                                            const TArray<FName>& LoadBundles,
                                            TAsyncLoadPriority Priority)
	-> std::enable_if_t<std::is_base_of_v<UObject, T>,
	                    Private::TAsyncLoadAwaiter<T*, 1>>
{
	return Private::TAsyncLoadAwaiter<T*, 1>(
		AsyncLoadPrimaryAsset(std::move(AssetToLoad), LoadBundles, Priority));
}

template<typename T>
auto UE5Coro::Latent::AsyncLoadPrimaryAssets(TArray<FPrimaryAssetId> AssetsToLoad,
                                             const TArray<FName>& LoadBundles,
                                             TAsyncLoadPriority Priority)
	-> std::enable_if_t<std::is_base_of_v<UObject, T>,
	                    Private::TAsyncLoadAwaiter<TArray<T*>, 1>>
{
	return Private::TAsyncLoadAwaiter<TArray<T*>, 1>(
		AsyncLoadPrimaryAssets(std::move(AssetsToLoad), LoadBundles, Priority));
}

#include "LatentChain.inl"
