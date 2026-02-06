# Latent callbacks

These types in the `UE5Coro::Latent` namespace have similar uses to the engine's
`ON_SCOPE_EXIT`, and `UE5Coro::FOnCoroutineCanceled`, but they specifically
target latent coroutines' backing latent actions.
They're not awaiters, and as such, TLatentAwaiter does not apply.

These only make sense as local variables within a **latent** coroutine.
All other usage is undefined behavior.
In practice, they will most often do nothing in the invalid case, but this is
not guaranteed.

With coroutines, these types are even less often needed than manually overriding
the appropriate functions in FPendingLatentAction.
Prefer unconditional cleanup with RAII and/or `ON_SCOPE_EXIT` in most
situations.

## FOnAbnormalExit

This type acts as a combination of the other two, FOnActionAborted and
FOnObjectDestroyed.

The provided callback will be called only if the coroutine is destroyed in
response to NotifyActionAborted or NotifyObjectDestroyed callbacks from the
engine.

The callback will run on the game thread, regardless of where the coroutine was
running before its cancellation.

Example:
```cpp
using namespace UE5Coro;
using namespace UE5Coro::Latent;

UFUNCTION(BlueprintCallable, meta = (Latent, LatentInfo = LatentInfo))
FVoidCoroutine Example(FLatentActionInfo LatentInfo)
{
    ON_SCOPE_EXIT { UE_LOGFMT(LogTemp, Display, "Finally"); };
    FOnCoroutineCanceled Cancel([] { UE_LOGFMT(LogTemp, Display,
                                               "Any cancellation"); });
    FOnAbnormalExit Exit([] { UE_LOGFMT(LogTemp, Display,
        "Only called if the latent action manager initiates cancellation"); });
    co_await Seconds(1);
    UE_LOGFMT(LogTemp, Display, "Success");
}
```

## FOnActionAborted

Usage and threading behavior is identical to FOnAbnormalExit, but the callback
will only run if the coroutine is destroyed because the latent action manager
has aborted its latent action.

See `FPendingLatentAction::NotifyActionAborted` in the engine, that this exposes.

Example:
```cpp
using namespace UE5Coro::Latent;

UFUNCTION(BlueprintCallable, meta = (Latent, LatentInfo = LatentInfo))
FVoidCoroutine Example(FLatentActionInfo LatentInfo)
{
    FOnActionAborted _([] { UE_LOGFMT(LogTemp, Display, "Action aborted"); });
    co_await Seconds(1);
    UE_LOGFMT(LogTemp, Display, "Success");
}
```

## FOnObjectDestroyed

Usage and threading behavior is identical to FOnAbnormalExit, but the callback
will only run if the coroutine is destroyed because its target UObject has been
garbage collected.

See `FPendingLatentAction::NotifyObjectDestroyed` in the engine, that this exposes.

Example:
```cpp
using namespace UE5Coro::Latent;

UFUNCTION(BlueprintCallable, meta = (Latent, LatentInfo = LatentInfo))
FVoidCoroutine Example(FLatentActionInfo LatentInfo)
{
    FOnObjectDestroyed _([] { UE_LOGFMT(LogTemp, Display, "Object destroyed"); });
    co_await Seconds(1);
    UE_LOGFMT(LogTemp, Display, "Success");
}
```
