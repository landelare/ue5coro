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

#include "Engine/AssetManager.h"
#include "UE5Coro/LatentAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
template<typename T, typename Item>
struct TLatentLoader
{
	T Manager;
	TArray<Item> Sources;
	TSharedPtr<FStreamableHandle> Handle;

	explicit TLatentLoader(TArray<Item> Paths,
	                       TAsyncLoadPriority Priority)
#if UE5CORO_CPP20
		requires std::is_same_v<Item, FSoftObjectPath>
#endif
		: Sources(std::move(Paths))
	{
		static_assert(std::is_same_v<T, FStreamableManager>);
		checkf(IsInGameThread(),
		       TEXT("Latent awaiters may only be used on the game thread"));
		Handle = Manager.RequestAsyncLoad(Sources, FStreamableDelegate(),
		                                  Priority);
	}

	explicit TLatentLoader(TArray<Item> AssetIds,
	                       const TArray<FName>& Bundles,
	                       TAsyncLoadPriority Priority)
#if UE5CORO_CPP20
		requires std::is_same_v<Item, FPrimaryAssetId>
#endif
		: Manager(UAssetManager::Get()), Sources(std::move(AssetIds))
	{
		static_assert(std::is_same_v<T, UAssetManager&>);
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
			if constexpr (std::is_same_v<Item, FSoftObjectPath>)
				Obj = i.ResolveObject();
			else if constexpr (std::is_same_v<Item, FPrimaryAssetId>)
				Obj = Manager.GetPrimaryAssetObject(i);
			else
				// This needs to depend on a template parameter
				static_assert(false && std::is_void_v<Item>, "Unknown type");

			// Null filtering matches how the array BP nodes behave
			if (IsValid(Obj))
				Items.Add(Obj);
		}
		return Items;
	}
};
using FLatentLoader = TLatentLoader<FStreamableManager, FSoftObjectPath>;
using FPrimaryLoader = TLatentLoader<UAssetManager&, FPrimaryAssetId>;

template<typename T>
bool ShouldResume(void*& Loader, bool bCleanup)
{
	auto* This = static_cast<T*>(Loader);

	if (UNLIKELY(bCleanup))
	{
		delete This;
		return false;
	}

	// This is the same logic that FLoadAssetActionBase::UpdateOperation() uses.
	// !Handle is how UAssetManager communicates an instant/synchronous finish.
	auto& Handle = This->Handle;
	return !Handle || Handle->HasLoadCompleted() || Handle->WasCanceled();
}
}

template<int HiddenType>
TArray<UObject*> AsyncLoad::InternalResume(void* State)
{
	using T = std::conditional_t<HiddenType == 0, FLatentLoader, FPrimaryLoader>;
	checkf(ShouldResume<T>(State, false), TEXT("Internal error"));

	return static_cast<T*>(State)->ResolveItems();
}
template UE5CORO_API TArray<UObject*> AsyncLoad::InternalResume<0>(void*);
template UE5CORO_API TArray<UObject*> AsyncLoad::InternalResume<1>(void*);

FLatentAwaiter Latent::AsyncLoadObjects(TArray<FSoftObjectPath> Paths,
                                        TAsyncLoadPriority Priority)
{
	return FLatentAwaiter(new FLatentLoader(std::move(Paths), Priority),
	                      &ShouldResume<FLatentLoader>);
}

FLatentAwaiter Latent::AsyncLoadPrimaryAsset(
	const FPrimaryAssetId& AssetToLoad,
	const TArray<FName>& LoadBundles,
	TAsyncLoadPriority Priority)
{
	return AsyncLoadPrimaryAssets(TArray{AssetToLoad}, LoadBundles, Priority);
}

FLatentAwaiter Latent::AsyncLoadPrimaryAssets(
	TArray<FPrimaryAssetId> AssetsToLoad,
	const TArray<FName>& LoadBundles,
	TAsyncLoadPriority Priority)
{
	return FLatentAwaiter(
		new FPrimaryLoader(std::move(AssetsToLoad), LoadBundles, Priority),
		&ShouldResume<FPrimaryLoader>);
}

TAsyncLoadAwaiter<UClass*, 0> Latent::AsyncLoadClass(TSoftClassPtr<UObject> Ptr,
                                                     TAsyncLoadPriority Priority)
{
	return TAsyncLoadAwaiter<UClass*, 0>(
		AsyncLoadObjects(TArray{Ptr.ToSoftObjectPath()}, Priority));
}

TAsyncLoadAwaiter<TArray<UClass*>, 0> Latent::AsyncLoadClasses(
	const TArray<TSoftClassPtr<UObject>>& Ptrs,
	TAsyncLoadPriority Priority)
{
	TArray<FSoftObjectPath> Paths;
	Paths.Reserve(Ptrs.Num());
	for (const auto& Ptr : Ptrs)
		Paths.Add(Ptr.ToSoftObjectPath());

	return TAsyncLoadAwaiter<TArray<UClass*>, 0>(
		AsyncLoadObjects(std::move(Paths), Priority));
}

FPackageLoadAwaiter Latent::AsyncLoadPackage(
	const FPackagePath& Path, FName PackageNameToCreate,
	EPackageFlags PackageFlags, int32 PIEInstanceID,
	TAsyncLoadPriority PackagePriority,
	const FLinkerInstancingContext* InstancingContext)
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
	: State(new FState)
{
	auto Delegate = FLoadPackageAsyncDelegate::CreateSP(
		State.ToSharedRef(), &FState::Loaded);
	LoadPackageAsync(Path, PackageNameToCreate, std::move(Delegate),
	                 PackageFlags, PIEInstanceID, PackagePriority,
	                 InstancingContext);
}

void FPackageLoadAwaiter::FState::Loaded(const FName&, UPackage* Package,
                                         EAsyncLoadingResult::Type)
{
	checkf(IsInGameThread(), TEXT("Internal error"));
	Result.Reset(Package);
	std::visit([](auto InHandle)
	{
		// monostate indicates that the load finished between AsyncLoadPackage()
		// and co_await
		if constexpr (!std::is_same_v<decltype(InHandle), std::monostate>)
			InHandle.promise().Resume();
	}, Handle);
}

bool FPackageLoadAwaiter::await_ready()
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	checkf(State, TEXT("Attempting to use invalid awaiter"));
	return State->Result.IsValid();
}

template<typename P>
void FPackageLoadAwaiter::await_suspend(stdcoro::coroutine_handle<P> InHandle)
{
	checkf(IsInGameThread(),
	       TEXT("Latent awaiters may only be used on the game thread"));
	checkf(std::holds_alternative<std::monostate>(State->Handle),
	       TEXT("Attempted second concurrent co_await"));

	if constexpr (std::is_same_v<P, FLatentPromise>)
		InHandle.promise().DetachFromGameThread();
	State->Handle = InHandle;
}
template UE5CORO_API void FPackageLoadAwaiter::await_suspend(FAsyncHandle);
template UE5CORO_API void FPackageLoadAwaiter::await_suspend(FLatentHandle);

UPackage* FPackageLoadAwaiter::await_resume()
{
	checkf(IsInGameThread() && State, TEXT("Internal error"));
	return State->Result.Get();
}
