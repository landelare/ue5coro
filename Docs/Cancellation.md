# Coroutine cancellation

> [!NOTE]
> Nothing on this page applies to generators.
> TGenerator owns the coroutine execution, destroying it serves the same purpose.

Coroutines returning TCoroutine come with integrated cancellation support.
A canceled coroutine's `co_await` will divert execution as if an uncatchable
exception was thrown[^noexcept]:
destructors of local variables will run, their associated memory is freed, etc.

[^noexcept]: Exceptions are not involved, and this feature is fully available if
             they're turned off.

Once the cancellation has been fully processed, TCoroutine::IsDone() will return
true, and TCoroutine::WasSuccessful() will return false.
TCoroutine\<T\>'s result will be set to a default-constructed T() (if T≠`void`).

Cancellation is thread safe.
Async coroutines clean up either on the thread that canceled them or the one on
which they would've continued running.
Latent coroutines always clean up on the game thread **when canceled**.

> [!NOTE]
> If a latent coroutine runs to completion while not on the game thread, cleanup
> will happen on that thread before the coroutine is considered complete,
> instead of the game thread.
> This is due to C++ language rules, and it cannot be changed.
>
> Make sure to use `co_await MoveToGameThread()` before the coroutine ends if
> this is not desired, e.g., because there are latent awaiters in scope.

If a coroutine is canceled while it's currently running, nothing happens until
it co_awaits something, or co_returns.
A co_return (explicitly written, or implicit by running off the final `}` for
T=`void`) will complete successfully, and ignore the cancellation.

An awaiting coroutine that's canceled (either before, or during the await) will
process the cancellation at an unspecified time between the cancellation being
issued, and when the coroutine would normally **resume** execution.
Depending on what's being awaited, this can take a significant amount of time,
possibly infinite if the awaiter never completes.

In case of a latent coroutine awaiting something satisfying the TLatentAwaiter
concept, cancellations are processed within one tick regardless of the awaiter's
completion.

Awaiters satisfying the TCancelableAwaiter concept will similarly process
incoming cancellations quickly (not measured in ticks), even if they would
otherwise never resume the coroutine.

## Coroutine initiated

`co_await UE5Coro::FSelfCancellation()` will self-cancel and proceed to cleanup
instead of resuming the coroutine.

> [!CAUTION]
> A coroutine canceling itself during its cleanup (e.g., in ON_SCOPE_EXIT) will
> deadlock.

### Async coroutines

Self-cancellation is instant and synchronous on the awaiting thread.

### Latent coroutines

If self-cancellation happens on the game thread, cancellation is processed
immediately and synchronously; if done on another thread, the game thread will
see to it at the latent action manager's next tick.

If the coroutine implements a latent UFUNCTION, its latent output exec pin will
**not** trigger in BP.
Execution stops with the node that called the coroutine.

If the latent coroutine is `UFUNCTION(BlueprintCallable)`, but not
`meta = (Latent)` (which is a supported combination), cancellation makes no
difference in BP: the exec pin triggers synchronously at the first co_await
or co_return, regardless of cancellations.

## Externally requested

TCoroutine::Cancel() requests the underlying coroutine to stop running, which
will be served at an unspecified time during the current (if the coroutine is
suspended) or next (if it isn't) co_await, as explained in the first section.

Canceling a coroutine that has already completed or about to complete is safe to
do, thread safe, and has no effect.
Multiple cancellations have the same effect as one.

> [!CAUTION]
> A coroutine canceling itself during its cleanup (e.g., in ON_SCOPE_EXIT) will
> deadlock.

There is no functionality to withdraw a cancellation.

## Engine initiated

Latent coroutines are owned by their UWorld's latent action manager, which may
decide to `delete` their latent action while they're running.
This is translated to a forced cancellation of the coroutine.

If this happens while an async coroutine is awaiting a TLatentAwaiter (which
causes a temporary latent action to get created behind the scenes that may be
`delete`d), that causes a normal cancellation that may be guarded against.
See below for cancellation guards.

# Auxiliary features

There are some additional features for a coroutine to explicitly interact with
its own cancellation:

## FCancellationGuard

For advanced use, `UE5Coro::FCancellationGuard` can defer incoming cancellation
requests in case there's a section of code where it's important that co_awaits
do resume the coroutine.
Before using it, consider if your coroutine is still valid if its `this` is
destroyed.

FCancellationGuard objects are only valid to have as local variables in a
coroutine returning TCoroutine; using them anywhere else is undefined behavior.

* If one or more of these objects are alive within a coroutine body,
  TCoroutine::Cancel() requests are deferred until the last one has gone out of
  scope, and co_awaits will resume the coroutine even if canceled.
* Attempting to self-cancel is illegal with an active cancellation guard.
* Latent coroutines ignore cancellation guards if their backing latent action is
  `delete`d by the engine.

Example:
```cpp
using namespace UE5Coro;

TCoroutine<FThing> Example()
{
    {
        FCancellationGuard Guard;
        co_await ImportantFunction1();
        co_await ImportantFunction2();
    } // Normal cancellations can only occur on the next line:
    co_return co_await RegularThing();
}
```

## FOnCoroutineCanceled

This scope guard behaves similarly to `ON_SCOPE_EXIT` in a coroutine, but it
only runs the provided callback if the coroutine is currently being destroyed in
response to cancellation (forced latent or regular).
`ON_SCOPE_EXIT` should be preferred in most scenarios for unconditional cleanup.

It is undefined whether the callback runs or not if this object is not local to
a coroutine returning TCoroutine or a compatible type.

Example:
```cpp
using namespace UE5Coro;

TCoroutine<> Example()
{
    ON_SCOPE_EXIT { UE_LOGFMT(LogTemp, Display, "Finally"); };
    FOnCoroutineCanceled _([] { UE_LOGFMT(LogTemp, Display, "Canceled"); });
    co_await RegularThing();
    UE_LOGFMT(LogTemp, Display, "Successful");
}
```

## Manual cancellation check

There are a couple of functions in the `UE5Coro` namespace that directly
interact with cancellation.

### auto FinishNowIfCanceled() noexcept

If you're running in a tight loop without a natural co_await, but you want to
handle incoming cancellation requests, `co_await UE5Coro::FinishNowIfCanceled()`
lets you process them manually.

* If the coroutine was not canceled, it will continue running **synchronously**
  and instantly, making this check relatively cheap.
* If the coroutine was canceled, co_await will instead divert to cleanup,
  as usual.

The return value of `FinishNowIfCanceled()` is copyable, reusable, but
meaningless.
Awaiting it will behave the same regardless of which object is used: it cannot
be used to observe the cancellation of another coroutine.

Cancellation will be processed normally: FCancellationGuard is respected, except
for incoming `delete`s, etc.
It's allowed to use this function if there's an active FCancellationGuard, in
which case only forced cancellations are let through.

Async coroutines will process the cancellation on the current thread.
Latent coroutines' cancellations are always processed on the game thread.

Example:
```cpp
using namespace UE5Coro;

for (int i = 0; i < Items.Num(); ++i)
{
    ProcessItem(Items[i]);
    // Process cancellation after every 128 items
    if (i % 128 == 0)
        co_await FinishNowIfCanceled();
}
```

### bool IsCurrentCoroutineCanceled()

This function simply returns a bool, without processing the cancellation.
This function "sees through" FCancellationGuards, and it will return `true` if
an incoming cancellation request is currently deferred.

Calling this from outside a TCoroutine-compatible coroutine invokes undefined
behavior.

Prefer `co_await FinishNowIfCanceled();` instead of
`if (IsCurrentCoroutineCanceled()) co_return;`.
