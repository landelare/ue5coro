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

#include "UE5Coro/LatentAwaiter.h"
#include "Engine/AssetManager.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
template<typename T, typename Item>
struct TLatentLoader final
{
	T Manager;
	TArray<Item> Sources;
	TSharedPtr<FStreamableHandle> Handle;

	explicit TLatentLoader(TArray<Item> Paths, TAsyncLoadPriority Priority)
		requires std::same_as<Item, FSoftObjectPath>
		: Sources(std::move(Paths))
	{
		static_assert(std::same_as<T, FStreamableManager>);
		checkf(IsInGameThread(),
		       TEXT("Latent awaiters may only be used on the game thread"));
		Handle = Manager.RequestAsyncLoad(Sources, FStreamableDelegate(),
		                                  Priority);
	}

	explicit TLatentLoader(TArray<Item> AssetIds, const TArray<FName>& Bundles,
	                       TAsyncLoadPriority Priority)
		requires std::same_as<Item, FPrimaryAssetId>
		: Manager(UAssetManager::Get()), Sources(std::move(AssetIds))
	{
		static_assert(std::same_as<T, UAssetManager&>);
		checkf(IsInGameThread(),
		       TEXT("Latent awaiters may only be used on the game thread"));
		Handle = Manager.LoadPrimaryAssets(Sources, Bundles,
		                                   FStreamableDelegate(), Priority);
	}

	~TLatentLoader()
	{
		checkf(IsInGameThread(), TEXT("Unexpected cleanup off the game thread"));
		if (Handle)
			Handle->ReleaseHandle();
	}

	TArray<UObject*> ResolveItems()
	{
		checkf(IsInGameThread(),
		       TEXT("Unexpected object resolve request off the game thread"));
		// Handle->GetLoadedAssets() is unreliable, the async loading BP nodes
		// re-resolve the sources instead once loading is done. Let's do that.
		TArray<UObject*> Items;
		for (auto& i : Sources)
		{
			UObject* Obj = nullptr;
			if constexpr (std::same_as<Item, FSoftObjectPath>)
				Obj = i.ResolveObject();
			else if constexpr (std::same_as<Item, FPrimaryAssetId>)
				Obj = Manager.GetPrimaryAssetObject(i);
			else
				static_assert(bFalse<T>, "Unknown type");

			// Null filtering matches how the array BP nodes behave
			if (IsValid(Obj))
				Items.Add(Obj);
		}
		return Items;
	}

	static bool ShouldResume(void* State, bool bCleanup)
	{
		auto* This = static_cast<TLatentLoader*>(State);
		if (bCleanup) [[unlikely]]
		{
			delete This;
			return false;
		}

		// This condition matches FLoadAssetActionBase::UpdateOperation().
		// !Handle is how UAssetManager reports an instant/synchronous finish.
		auto& Handle = This->Handle;
		return !Handle || Handle->HasLoadCompleted() || Handle->WasCanceled();
	}
};
using FLatentLoader = TLatentLoader<FStreamableManager, FSoftObjectPath>;
using FPrimaryLoader = TLatentLoader<UAssetManager&, FPrimaryAssetId>;

struct FPackageLoadState final
{
	using FPtr = TSharedRef<FPackageLoadState, ESPMode::NotThreadSafe>;
	TStrongObjectPtr<UPackage> Result; // This might be carried across co_awaits

#if UE5CORO_DEBUG
	~FPackageLoadState()
	{
		checkf(IsInGameThread(), TEXT("Expected cleanup on the game thread"));
	}
#endif

	void ReceiveResult(const FName&, UPackage* Package, EAsyncLoadingResult::Type)
	{
		checkf(IsInGameThread(),
		       TEXT("Internal error: expected callback on the game thread"));
		checkf(IsValid(Package),
		       TEXT("Internal error: unhandled invalid freshly-loaded package"));
		checkf(!Result.IsValid(),
		       TEXT("Internal error: unexpected double result"));
		Result.Reset(Package); // Store the result
	}

	static bool ShouldResume(void* State, bool bCleanup)
	{
		if (bCleanup) [[unlikely]]
		{
			delete static_cast<FPtr*>(State);
			return false;
		}
		return static_cast<FPtr*>(State)->Get().Result.IsValid();
	}
};
}

template<int HiddenType>
TArray<UObject*> AsyncLoad::InternalResume(void* State)
{
	using T = std::conditional_t<HiddenType, FPrimaryLoader, FLatentLoader>;
	checkf(T::ShouldResume(State, false),
	       TEXT("Internal error: resuming with !ShouldResume"));

	return static_cast<T*>(State)->ResolveItems();
}
template UE5CORO_API TArray<UObject*> AsyncLoad::InternalResume<0>(void*);
template UE5CORO_API TArray<UObject*> AsyncLoad::InternalResume<1>(void*);

FLatentAwaiter Latent::AsyncLoadObjects(TArray<FSoftObjectPath> Paths,
                                        TAsyncLoadPriority Priority)
{
	return FLatentAwaiter(new FLatentLoader(std::move(Paths), Priority),
	                      &FLatentLoader::ShouldResume, std::false_type());
}

FLatentAwaiter Latent::AsyncLoadPrimaryAsset(const FPrimaryAssetId& AssetToLoad,
                                             const TArray<FName>& LoadBundles,
                                             TAsyncLoadPriority Priority)
{
	return AsyncLoadPrimaryAssets(TArray{AssetToLoad}, LoadBundles, Priority);
}

FLatentAwaiter Latent::AsyncLoadPrimaryAssets(TArray<FPrimaryAssetId> AssetsToLoad,
                                              const TArray<FName>& LoadBundles,
                                              TAsyncLoadPriority Priority)
{
	return FLatentAwaiter(
		new FPrimaryLoader(std::move(AssetsToLoad), LoadBundles, Priority),
		&FPrimaryLoader::ShouldResume, std::false_type());
}

auto Latent::AsyncLoadClass(TSoftClassPtr<> Ptr, TAsyncLoadPriority Priority)
	-> TAsyncLoadAwaiter<UClass*, 0>
{
	return TAsyncLoadAwaiter<UClass*, 0>(
		AsyncLoadObjects(TArray{Ptr.ToSoftObjectPath()}, Priority));
}

auto Latent::AsyncLoadClasses(const TArray<TSoftClassPtr<>>& Ptrs,
                              TAsyncLoadPriority Priority)
	-> TAsyncLoadAwaiter<TArray<UClass*>, 0>
{
	TArray<FSoftObjectPath> Paths;
	Paths.Reserve(Ptrs.Num());
	for (const auto& Ptr : Ptrs)
		Paths.Add(Ptr.ToSoftObjectPath());

	return TAsyncLoadAwaiter<TArray<UClass*>, 0>(
		AsyncLoadObjects(std::move(Paths), Priority));
}

auto Latent::AsyncLoadPackage(const FPackagePath& Path,
                              FName PackageNameToCreate,
                              EPackageFlags PackageFlags, int32 PIEInstanceID,
                              TAsyncLoadPriority PackagePriority,
                              const FLinkerInstancingContext* InstancingContext)
	-> FPackageLoadAwaiter
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	return FPackageLoadAwaiter(Path, PackageNameToCreate, PackageFlags,
	                           PIEInstanceID, PackagePriority,
	                           InstancingContext);
}

FPackageLoadAwaiter::FPackageLoadAwaiter(
	const FPackagePath& Path, FName PackageNameToCreate,
	EPackageFlags PackageFlags, int32 PIEInstanceID,
	TAsyncLoadPriority PackagePriority,
	const FLinkerInstancingContext* InstancingContext)
	: FLatentAwaiter(new FPackageLoadState::FPtr(new FPackageLoadState),
	                 &FPackageLoadState::ShouldResume, std::false_type())
{
	auto& Ptr = *static_cast<FPackageLoadState::FPtr*>(State);
	auto Delegate = FLoadPackageAsyncDelegate::CreateSP(
		Ptr, &FPackageLoadState::ReceiveResult);
	LoadPackageAsync(Path, PackageNameToCreate, std::move(Delegate),
	                 PackageFlags, PIEInstanceID, PackagePriority,
	                 InstancingContext);
}

UPackage* FPackageLoadAwaiter::await_resume()
{
	checkf(IsInGameThread(),
	       TEXT("Internal error: expected to resume on the game thread"));
	auto& Ptr = *static_cast<FPackageLoadState::FPtr*>(State);
	checkf(Ptr->Result.IsValid(),
	       TEXT("Internal error: resuming without a valid result"));
	return Ptr->Result.Get();
}
