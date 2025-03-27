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
#include "UE5Coro/Definition.h"
#include <concepts>
#include <functional>
#include "Engine/OverlapResult.h"
#include "Engine/StreamableManager.h"
#include "UE5Coro/Private.h"
#include "UE5Coro/Promise.h"
#include "WorldCollision.h"

namespace UE5Coro::Latent
{
#pragma region Tick

/** Resumes the coroutine in the next tick.
 *  See Latent::Until for an alternative to while-NextTick loops. */
UE5CORO_API auto NextTick() -> Private::FLatentAwaiter;

/** Resumes the coroutine the given number of ticks later. */
UE5CORO_API auto Ticks(int64 Ticks) -> Private::FLatentAwaiter;

/** Polls the provided function, resumes the coroutine when it returns true. */
UE5CORO_API auto Until(std::function<bool()> Function)
	-> Private::FLatentAwaiter;

#pragma endregion

/** Resumes the coroutine after the provided other coroutine completes, but the
 *  wait itself is forced to latent mode regardless of the awaiting coroutine's
 *  execution mode. */
[[deprecated("This wrapper is no longer needed.")]]
UE5CORO_API auto UntilCoroutine(TCoroutine<> Coroutine)
	-> Private::TLatentCoroutineAwaiter<void, false>;

/** Resumes the coroutine after the delegate executes.
 *  Delegate parameters are ignored, a return value is not provided. */
[[deprecated("This wrapper is no longer needed.")]]
auto UntilDelegate(Private::TIsDelegate auto& Delegate)
	-> Private::FLatentAwaiter;

#pragma region Time

/** Resumes the coroutine the specified amount of seconds later.
 *  This is affected by both pause and time dilation. */
UE5CORO_API auto Seconds(double Seconds) -> Private::FLatentAwaiter;

/** Resumes the coroutine the specified amount of seconds later.
 *  This is affected by time dilation only, NOT pause. */
UE5CORO_API auto UnpausedSeconds(double Seconds) -> Private::FLatentAwaiter;

/** Resumes the coroutine the specified amount of seconds later.
 *  This is not affected by pause or time dilation. */
UE5CORO_API auto RealSeconds(double Seconds) -> Private::FLatentAwaiter;

/** Resumes the coroutine the specified amount of seconds later.
 *  This is affected by pause only, NOT time dilation. */
UE5CORO_API auto AudioSeconds(double Seconds) -> Private::FLatentAwaiter;

/** Resumes the coroutine when world time reaches the provided value.
 *  This is affected by both pause and time dilation. */
UE5CORO_API auto UntilTime(double Seconds) -> Private::FLatentAwaiter;

/** Resumes the coroutine when unpaused world time reaches the provided value.
 *  This is affected by time dilation only, NOT pause. */
UE5CORO_API auto UntilUnpausedTime(double Seconds) -> Private::FLatentAwaiter;

/** Resumes the coroutine when real world time reaches the provided value.
 *  This is not affected by pause or time dilation. */
UE5CORO_API auto UntilRealTime(double Seconds) -> Private::FLatentAwaiter;

/** Resumes the coroutine when audio world time reaches the provided value.
 *  This is affected by pause only, NOT time dilation. */
UE5CORO_API auto UntilAudioTime(double Seconds) -> Private::FLatentAwaiter;

#pragma endregion

#pragma region Chain

/** Resumes the coroutine once the chained static latent action has finished,
 *  with automatic parameter matching.
 *
 *  The result of the await expression is true if the chained latent action
 *  finished normally, false if it didn't. */
template<typename... FnParams>
auto Chain(auto (*Function)(FnParams...), auto&&... Args)
	-> Private::FLatentChainAwaiter;

/** Resumes the coroutine once the chained member latent action has finished,
 *  with automatic parameter matching.
 *
 *  The result of the await expression is true if the chained latent action
 *  finished normally, false if it didn't. */
template<std::derived_from<UObject> Class, typename... FnParams>
auto Chain(Class* Object, auto (Class::*Function)(FnParams...), auto&&... Args)
	-> Private::FLatentChainAwaiter;

/** Resumes the coroutine once the chained latent action has finished,
 *  with manual parameter matching.
 *
 *  The result of the await expression is true if the chained latent action
 *  finished normally, false if it didn't.
 *
 *  Use std::placeholders::_1 and _2 for the world context and LatentInfo,
 *  respectively. The other parameters follow the rules of std::bind. */
auto ChainEx(auto&& Function, auto&&... Args) -> Private::FLatentChainAwaiter;

#pragma endregion

#pragma region Async loading, asset management

/** Asynchronously starts loading the object, resumes once it's loaded.
 *  The result of the await expression is a pointer to the loaded object. */
template<std::derived_from<UObject> T>
auto AsyncLoadObject(TSoftObjectPtr<T>,
	TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::TAsyncLoadAwaiter<T*, 0>;

/** Asynchronously starts loading the objects, resumes once they're loaded.
 *  The result of the await expression is TArray<T*>. */
template<std::derived_from<UObject> T>
auto AsyncLoadObjects(const TArray<TSoftObjectPtr<T>>&,
	TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::TAsyncLoadAwaiter<TArray<T*>, 0>;

/** Asynchronously starts loading the objects at the given paths,
 *  resumes once they're loaded. The loaded objects are not resolved. */
UE5CORO_API auto AsyncLoadObjects(TArray<FSoftObjectPath>,
	TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::FLatentAwaiter;

/** Asynchronously starts loading the primary asset with any bundles specified,
 *  resumes once they're loaded.
 *  The asset will stay in memory until explicitly unloaded. */
UE5CORO_API auto AsyncLoadPrimaryAsset(const FPrimaryAssetId& AssetToLoad,
	const TArray<FName>& LoadBundles = {},
	TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::FLatentAwaiter;

/** Asynchronously starts loading the primary asset of the given type with any
 *  bundles specified, resumes once they're loaded.
 *  The asset will stay in memory until explicitly unloaded.
 *  The result of the await expression is the loaded T* or nullptr. */
template<std::derived_from<UObject> T>
auto AsyncLoadPrimaryAsset(FPrimaryAssetId AssetToLoad,
	const TArray<FName>& LoadBundles = {},
	TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::TAsyncLoadAwaiter<T*, 1>;

/** Asynchronously starts loading the primary assets with any bundles specified,
 *  resumes once they're loaded.
 *  The assets will stay in memory until explicitly unloaded. */
UE5CORO_API auto AsyncLoadPrimaryAssets(TArray<FPrimaryAssetId> AssetsToLoad,
	const TArray<FName>& LoadBundles = {},
	TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::FLatentAwaiter;

/** Asynchronously starts loading the primary assets of the given type with any
 *  bundles specified, resumes once they're loaded.
 *  The assets will stay in memory until explicitly unloaded.
 *  The result of the await expression is the loaded and filtered TArray<T*>. */
template<std::derived_from<UObject> T>
auto AsyncLoadPrimaryAssets(TArray<FPrimaryAssetId> AssetsToLoad,
	const TArray<FName>& LoadBundles = {},
	TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::TAsyncLoadAwaiter<TArray<T*>, 1>;

/** Asynchronously starts loading the class, resumes once it's loaded.
 *  The result of the await expression is a pointer to the loaded UClass. */
UE5CORO_API auto AsyncLoadClass(TSoftClassPtr<>,
	TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::TAsyncLoadAwaiter<UClass*, 0>;

/** Asynchronously starts loading the classes, resumes once they're loaded.
 *  The result of the await expression is TArray<UClass*>. */
UE5CORO_API auto AsyncLoadClasses(const TArray<TSoftClassPtr<>>&,
	TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::TAsyncLoadAwaiter<TArray<UClass*>, 0>;

/** Asynchronously starts loading the package, resumes once it's loaded.
 *  The result of the await expression is a pointer to the UPackage.
 *  For parameters, see the engine function ::LoadPackageAsync(). */
UE5CORO_API auto AsyncLoadPackage(const FPackagePath& Path,
	FName PackageNameToCreate = NAME_None,
	EPackageFlags PackageFlags = PKG_None, int32 PIEInstanceID = INDEX_NONE,
	TAsyncLoadPriority PackagePriority = 0,
	const FLinkerInstancingContext* InstancingContext = nullptr)
	-> Private::FPackageLoadAwaiter;

/** Asynchronously begins the bundle state change on the provided assets,
 *  resumes once it's done.
 *  For parameters, see UAssetManager::ChangeBundleStateForPrimaryAssets. */
UE5CORO_API auto AsyncChangeBundleStateForPrimaryAssets(
	const TArray<FPrimaryAssetId>& AssetsToChange,
	const TArray<FName>& AddBundles, const TArray<FName>& RemoveBundles,
	bool bRemoveAllBundles = false,
	TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::FLatentAwaiter;

/** Asynchronously begins the bundle state change on every primary asset that
 *  matches OldBundles, resumes once it's done. */
UE5CORO_API auto AsyncChangeBundleStateForMatchingPrimaryAssets(
	const TArray<FName>& NewBundles, const TArray<FName>& OldBundles,
	TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
	-> Private::FLatentAwaiter;

#pragma endregion

#pragma region Async collision queries

// Async UWorld queries. For parameters, see their originals in World.h.
// It's slightly more efficient to co_await rvalues of these instead of lvalues.

UE5CORO_API auto AsyncLineTraceByChannel(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, ECollisionChannel TraceChannel,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam,
	const FCollisionResponseParams& ResponseParam =
		FCollisionResponseParams::DefaultResponseParam)
	-> Private::TAsyncQueryAwaiter<FHitResult>;

UE5CORO_API auto AsyncLineTraceByObjectType(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End,
	const FCollisionObjectQueryParams& ObjectQueryParams,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
	-> Private::TAsyncQueryAwaiter<FHitResult>;

UE5CORO_API auto AsyncLineTraceByProfile(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, FName ProfileName,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
	-> Private::TAsyncQueryAwaiter<FHitResult>;

UE5CORO_API auto AsyncSweepByChannel(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, const FQuat& Rot,
	ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam,
	const FCollisionResponseParams& ResponseParam =
		FCollisionResponseParams::DefaultResponseParam)
	-> Private::TAsyncQueryAwaiter<FHitResult>;

UE5CORO_API auto AsyncSweepByObjectType(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, const FQuat& Rot,
	const FCollisionObjectQueryParams& ObjectQueryParams,
	const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
	-> Private::TAsyncQueryAwaiter<FHitResult>;

UE5CORO_API auto AsyncSweepByProfile(
	const UObject* WorldContextObject, EAsyncTraceType InTraceType,
	const FVector& Start, const FVector& End, const FQuat& Rot,
	FName ProfileName, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
	-> Private::TAsyncQueryAwaiter<FHitResult>;

UE5CORO_API auto AsyncOverlapByChannel(
	const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot,
	ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam,
	const FCollisionResponseParams& ResponseParam =
		FCollisionResponseParams::DefaultResponseParam)
	-> Private::TAsyncQueryAwaiter<FOverlapResult>;

UE5CORO_API auto AsyncOverlapByObjectType(
	const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot,
	const FCollisionObjectQueryParams& ObjectQueryParams,
	const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
	-> Private::TAsyncQueryAwaiter<FOverlapResult>;

UE5CORO_API auto AsyncOverlapByProfile(
	const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot,
	FName ProfileName, const FCollisionShape& CollisionShape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
	-> Private::TAsyncQueryAwaiter<FOverlapResult>;

#pragma endregion
}

#pragma region Private
namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FLatentAwaiter // not TAwaiter
{
	friend class FPendingAsyncCoroutine;
	friend class FPendingLatentCoroutine;
	void Suspend(FAsyncPromise&);
	void Suspend(FLatentPromise&);

	void Clear() noexcept { State = nullptr; Resume = nullptr; }
	[[nodiscard]] bool IsValid() const noexcept { return Resume != nullptr; }
	[[nodiscard]] bool ShouldResume();

	// Copying is for internal use only
	FLatentAwaiter(const FLatentAwaiter&) = default;
	FLatentAwaiter& operator=(const FLatentAwaiter&) = default;

protected:
	void* State;
	bool (*Resume)(void* State, bool bCleanup);
#if UE5CORO_DEBUG
	UWorld* OriginalWorld;
#endif

public:
	explicit FLatentAwaiter(void* State, bool (*Resume)(void*, bool),
	                        auto WorldSensitive) noexcept(!UE5CORO_DEBUG);
	FLatentAwaiter(FLatentAwaiter&&) noexcept;
	~FLatentAwaiter();

	[[nodiscard]] bool await_ready() { return ShouldResume(); }

	template<std::derived_from<FPromise> P>
	void await_suspend(std::coroutine_handle<P> Handle)
	{
		Suspend(Handle.promise());
	}

	void await_resume() noexcept { }
};

static_assert(std::is_standard_layout_v<FLatentAwaiter>);

namespace AsyncLoad
{
template<int> // Switches between non-exported types
UE5CORO_API TArray<UObject*> InternalResume(void*);
}

template<typename T, int HiddenType>
struct [[nodiscard]] TAsyncLoadAwaiter final : FLatentAwaiter
{
	explicit TAsyncLoadAwaiter(FLatentAwaiter&& Other) noexcept
		: FLatentAwaiter(std::move(Other)) { }
	TAsyncLoadAwaiter(TAsyncLoadAwaiter&&) noexcept = default;

	T await_resume() requires TIsTArray_V<T>
	{
		using V = std::remove_pointer_t<typename T::ElementType>;
		static_assert(std::is_pointer_v<typename T::ElementType>);
		static_assert(std::derived_from<V, UObject>);
		static_assert(!std::is_const_v<V>);

		TArray<UObject*> Assets = AsyncLoad::InternalResume<HiddenType>(State);
		[[maybe_unused]] int OldNum = Assets.Num();
		Assets.RemoveAll([](UObject* Ptr) { return !Ptr->IsA<V>(); });
		if constexpr (HiddenType == 0) // These come from typed soft ptrs
			check(Assets.Num() == OldNum); // Strict check
		return std::move(*std::launder(reinterpret_cast<T*>(&Assets)));
	}

	T await_resume() requires (!TIsTArray_V<T>)
	{
		using V = std::remove_pointer_t<T>;
		static_assert(std::is_pointer_v<T>);
		static_assert(std::derived_from<V, UObject>);

		TArray<UObject*> Assets = AsyncLoad::InternalResume<HiddenType>(State);
		checkf(Assets.Num() <= 1,
		       TEXT("Unexpected multiple assets for single load"));
		return Assets.IsValidIndex(0) ? Cast<V>(Assets[0]) : nullptr;
	}
};

static_assert(sizeof(FLatentAwaiter) ==
              sizeof(TAsyncLoadAwaiter<UObject*, 0>));
static_assert(sizeof(FLatentAwaiter) ==
              sizeof(TAsyncLoadAwaiter<TArray<UObject*>, 0>));

struct [[nodiscard]] UE5CORO_API FPackageLoadAwaiter final : FLatentAwaiter
{
	explicit FPackageLoadAwaiter(
		const FPackagePath& Path, FName PackageNameToCreate,
		EPackageFlags PackageFlags, int32 PIEInstanceID,
		TAsyncLoadPriority PackagePriority,
		const FLinkerInstancingContext* InstancingContext);
	FPackageLoadAwaiter(FPackageLoadAwaiter&&) noexcept = default;

	UPackage* await_resume();
};
static_assert(sizeof(FPackageLoadAwaiter) == sizeof(FLatentAwaiter));

template<typename T>
struct [[nodiscard]] UE5CORO_API TAsyncQueryAwaiter : FLatentAwaiter
{
	template<typename... P>
	explicit TAsyncQueryAwaiter(UWorld*, FTraceHandle (UWorld::*)(P...),
	                            auto&&...);

	// Workaround for not being able to rvalue overload await_resume
	TAsyncQueryAwaiter& operator co_await() & { return *this; }
	TAsyncQueryAwaiterRV<T>& operator co_await() &&;

	const TArray<T>& await_resume();
};
static_assert(sizeof(TAsyncQueryAwaiter<FHitResult>) == sizeof(FLatentAwaiter));
static_assert(sizeof(TAsyncQueryAwaiter<FOverlapResult>) ==
              sizeof(FLatentAwaiter));

template<typename T>
struct [[nodiscard]] UE5CORO_API TAsyncQueryAwaiterRV : TAsyncQueryAwaiter<T>
{
	TAsyncQueryAwaiterRV() = delete; // Objects of this type are never created
	TArray<T> await_resume();
};
static_assert(sizeof(TAsyncQueryAwaiterRV<FHitResult>) == sizeof(FLatentAwaiter));
static_assert(sizeof(TAsyncQueryAwaiterRV<FOverlapResult>) ==
              sizeof(FLatentAwaiter));
}

auto UE5Coro::Latent::UntilDelegate(Private::TIsDelegate auto& Delegate)
	-> Private::FLatentAwaiter
{
	using namespace UE5Coro::Private;
	auto [Awaiter, Target] = UntilDelegateCore();

	using FDelegate = std::remove_reference_t<decltype(Delegate)>;
	if constexpr (TIsMulticastDelegate<FDelegate>)
	{
		if constexpr (TIsDynamicDelegate<FDelegate>)
		{
			FScriptDelegate D;
			D.BindUFunction(Target, NAME_Core);
			Delegate.Add(D);
		}
		else
			Delegate.AddUFunction(Target, NAME_Core);
	}
	else
		Delegate.BindUFunction(Target, NAME_Core);

	return std::move(Awaiter);
}

template<std::derived_from<UObject> T>
auto UE5Coro::Latent::AsyncLoadObject(TSoftObjectPtr<T> Ptr,
                                      TAsyncLoadPriority Priority)
	-> Private::TAsyncLoadAwaiter<T*, 0>
{
	return Private::TAsyncLoadAwaiter<T*, 0>(
		AsyncLoadObjects(TArray{Ptr.ToSoftObjectPath()}, Priority));
}

template<std::derived_from<UObject> T>
auto UE5Coro::Latent::AsyncLoadObjects(const TArray<TSoftObjectPtr<T>>& Ptrs,
                                       TAsyncLoadPriority Priority)
	-> Private::TAsyncLoadAwaiter<TArray<T*>, 0>
{
	TArray<FSoftObjectPath> Paths;
	Paths.Reserve(Ptrs.Num());
	for (const auto& Ptr : Ptrs)
		Paths.Add(Ptr.ToSoftObjectPath());

	return Private::TAsyncLoadAwaiter<TArray<T*>, 0>(
		AsyncLoadObjects(std::move(Paths), Priority));
}

template<std::derived_from<UObject> T>
auto UE5Coro::Latent::AsyncLoadPrimaryAsset(FPrimaryAssetId AssetToLoad,
                                            const TArray<FName>& LoadBundles,
                                            TAsyncLoadPriority Priority)
	-> Private::TAsyncLoadAwaiter<T*, 1>
{
	return Private::TAsyncLoadAwaiter<T*, 1>(
		AsyncLoadPrimaryAsset(std::move(AssetToLoad), LoadBundles, Priority));
}

template<std::derived_from<UObject> T>
auto UE5Coro::Latent::AsyncLoadPrimaryAssets(TArray<FPrimaryAssetId> AssetsToLoad,
                                             const TArray<FName>& LoadBundles,
                                             TAsyncLoadPriority Priority)
	-> Private::TAsyncLoadAwaiter<TArray<T*>, 1>
{
	return Private::TAsyncLoadAwaiter<TArray<T*>, 1>(
		AsyncLoadPrimaryAssets(std::move(AssetsToLoad), LoadBundles, Priority));
}

#include "LatentChain.inl"
#pragma endregion
