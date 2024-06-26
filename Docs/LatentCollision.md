# Async collision queries

These functions live in the `UE5Coro::Latent` namespace and return types that
satisfy the TLatentAwaiter concept, therefore [latent](Latent.md#latent-awaiters)
rules apply.

### auto AsyncLineTraceByChannel(const UObject* WorldContextObject, EAsyncTraceType InTraceType, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam, const FCollisionResponseParams& ResponseParam = FCollisionResponseParams::DefaultResponseParam)
### auto AsyncLineTraceByObjectType(const UObject* WorldContextObject, EAsyncTraceType InTraceType, const FVector& Start, const FVector& End, const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
### auto AsyncLineTraceByProfile(const UObject* WorldContextObject, EAsyncTraceType InTraceType, const FVector& Start, const FVector& End, FName ProfileName, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
### auto AsyncSweepByChannel(const UObject* WorldContextObject, EAsyncTraceType InTraceType, const FVector& Start, const FVector& End, const FQuat& Rot, ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam, const FCollisionResponseParams& ResponseParam = FCollisionResponseParams::DefaultResponseParam)
### auto AsyncSweepByObjectType(const UObject* WorldContextObject, EAsyncTraceType InTraceType, const FVector& Start, const FVector& End, const FQuat& Rot, const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
### auto AsyncSweepByProfile(const UObject* WorldContextObject, EAsyncTraceType InTraceType, const FVector& Start, const FVector& End, const FQuat& Rot, FName ProfileName, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
### auto AsyncOverlapByChannel(const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot, ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam, const FCollisionResponseParams& ResponseParam = FCollisionResponseParams::DefaultResponseParam)
### auto AsyncOverlapByObjectType(const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot, const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
### auto AsyncOverlapByProfile(const UObject* WorldContextObject, const FVector& Pos, const FQuat& Rot, FName ProfileName, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)

For more information on the parameters and the queries themselves, see the
identically-named functions on UWorld.

These functions start an async collision query, and return an object that can be
awaited to resume the coroutine when the query is done.
The return value may be copied, copies will refer to the same query.

It can be more efficient to await rvalues of the objects that these functions
return, as they will offer a TArray rvalue as a result instead of an immovable
const reference.
Awaiting an rvalue will invalidate all other copies of the awaiter (if copies
were made at all).

Multiple async queries may be overlapped before the first one is awaited, for
higher throughput.
Awaiting a query that has already finished will continue synchronously.

The await expression's result depends on the function's name, and whether its
return value was awaited as an lvalue or rvalue:

|      |`Trace` or `Sweep` in function name|`Overlap` in function name     |
|------|-----------------------------------|-------------------------------|
|lvalue|`const TArray<FHitResult>&`        |`const TArray<FOverlapResult>&`|
|rvalue|`TArray<FHitResult>`               |`TArray<FOverlapResult>`       |

Example:
```cpp
using namespace UE5Coro::Latent;

// The simplest usage effortlessly avoids all copies:
TArray<FHitResult> Result1 = co_await AsyncLineTraceByObjectType(this, /*...*/);

// Three overlapped queries
auto Query2 = AsyncLineTraceByChannel(this, /*...*/);
auto Query3A = AsyncSweepByProfile(this, /*...*/);
auto Query3B = Query3;
TArray<FOverlapResult> Result4 = co_await AsyncOverlapByChannel(this, /*...*/); // Move

TArray<FHitResult> Result2A = co_await Query2; // Copy
const TArray<FHitResult>& Result2B = co_await Query2; // Reference into Query2
TArray<FHitResult> Result3 = co_await std::move(Query3A); // Move from Query3A
// Query3A and Query3B are now invalid
```
