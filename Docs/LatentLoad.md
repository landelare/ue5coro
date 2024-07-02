# Asset loading

These functions live in the `UE5Coro::Latent` namespace, and as such, they can
only be used from the game thread.

Every function on this page returns a type satisfying the TLatentAwaiter concept.

### auto AsyncLoadObject\<T\>(TSoftObjectPtr\<T\>, TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
### auto AsyncLoadObjects\<T\>(const TArray<TSoftObjectPtr\<T\>>&, TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
### auto AsyncLoadObjects(TArray\<FSoftObjectPath\>, TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
### auto AsyncLoadClass(TSoftClassPtr\<\>, TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)
### auto AsyncLoadClasses(const TArray\<TSoftClassPtr\<\>\>&, TAsyncLoadPriority = FStreamableManager::DefaultAsyncLoadPriority)

These functions start loading the provided soft pointers, and return an awaiter
that resumes the calling coroutine when the loading is complete.

If everything is already loaded, the calling coroutine will continue immediately.
Multiple loads may be started before the first one is awaited, for higher
overall throughput.
The awaiters will strongly reference the loaded objects until awaited.

The result of the await expression is based on the first parameter of the
function:

|`co_await` result|What's being loaded        |
|-----------------|---------------------------|
|`T*`             |`TSoftObjectPtr<T>`        |
|`UClass*`        |`TSoftClassPtr<>`          |
|`TArray<T*>`     |`TArray<TSoftObjectPtr<T>>`|
|`TArray<UClass*>`|`TArray<TSoftClassPtr<>>`  |
|`void`           |`TArray<FSoftObjectPath>`  |

The FSoftObjectPath overload is intended to have lower overhead than the
templated TSoftObjectPtr one by not resolving the loaded objects (as well as not
inflating the compiled binary with additional template instantiations).
TSoftObjectPtr\<UObject\> can be used instead of FSoftObjectPath to load "any
object".

For typed TSoftClassPtrs, TSubclassOf\<T\> may be used to receive the returned
pointers, since it implicitly converts from UClass*.

Example:
```cpp
using namespace UE5Coro::Latent;

UClass* Class = co_await AsyncLoadClass(SoftClass);
if (AActor* SpawnedActor = GetWorld()->SpawnActor<AActor>(Class))
    SpawnedActor->Act();
```

### auto AsyncLoadPrimaryAsset(const FPrimaryAssetId& AssetToLoad, const TArray\<FName\>& LoadBundles = \{\}, TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
### auto AsyncLoadPrimaryAsset\<T\>(FPrimaryAssetId AssetToLoad, const TArray\<FName\>& LoadBundles = \{\}, TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
### auto AsyncLoadPrimaryAssets(TArray\<FPrimaryAssetId\> AssetsToLoad, const TArray\<FName\>& LoadBundles = \{\}, TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
### auto AsyncLoadPrimaryAssets\<T\>(TArray\<FPrimaryAssetId\> AssetsToLoad, const TArray\<FName\>& LoadBundles = \{\}, TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)

These functions work with primary asset IDs, and support specifying bundles.
Loading starts when the functions are called, and the return value may be used
to resume the calling coroutine when loading is complete.
Multiple operations may be overlapped before the first one is awaited for higher
throughput.

If the assets are already loaded, the calling coroutine will immediately
continue.

The loaded objects will stay in memory until explicitly unloaded.

The result of the await expression will be:
* `void` for the non-templated variants
* `T*` for AsyncLoadPrimaryAsset\<T\>
* `TArray<T*>` for AsyncLoadPrimaryAssets\<T\>

Example:
```cpp
using namespace UE5Coro::Latent;

for (auto* Asset : co_await AsyncLoadPrimaryAssets<UExampleAsset>(...))
    DoSomethingWith(Asset);
```

### auto AsyncLoadPackage(const FPackagePath& Path, FName PackageNameToCreate = NAME_None, EPackageFlags PackageFlags = PKG_None, int32 PIEInstanceID = INDEX_NONE, TAsyncLoadPriority PackagePriority = 0, const FLinkerInstancingContext* InstancingContext = nullptr)

Starts loading the UPackage at the specified path, returns an object that can be
awaited to resume the calling coroutine when loading is complete.
If the package is already loaded at the time of the co_await, nothing happens,
and the calling coroutine continues immediately.

The await expression will result in the loaded UPackage*.

For more details on the parameters, see the engine function `LoadPackageAsync()`
in the global namespace.

Example:
```cpp
using namespace UE5Coro::Latent;

UPackage* Package = co_await AsyncLoadPackage(Path);
```

### auto AsyncChangeBundleStateForPrimaryAssets(const TArray<FPrimaryAssetId>& AssetsToChange, const TArray<FName>& AddBundles, const TArray<FName>& RemoveBundles, bool bRemoveAllBundles = false, TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)
### auto AsyncChangeBundleStateForMatchingPrimaryAssets(const TArray<FName>& NewBundles, const TArray<FName>& OldBundles, TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority)

These functions begin the requested bundle state changes on the provided (first
function) or all matching (second function) primary assets, and return an object
that can be awaited to resume the calling coroutine when loading is complete or
if it was canceled.

If the asset manager determines there's nothing to do, the calling coroutine
will continue immediately.

For more details, see the corresponding functions on `UAssetManager`.

Example:
```cpp
using namespace UE5Coro::Latent;

co_await AsyncChangeBundleStateForMatchingPrimaryAssets({"LoadMe"}, {"RemoveMe"});
```
