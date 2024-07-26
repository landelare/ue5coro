# Coroutine cancellation

Coroutines returning [UE5Coro::TCoroutine](Async.md) come with integrated
cancellation support.
A canceled coroutine will **NOT** resume at the next co_await, but instead
complete unsuccessfully: destructors of locals will be called (including
`ON_SCOPE_EXIT`), and the return value of the coroutine is `T()`.

* Async mode coroutines clean up on the thread that would've resumed them.
* Latent mode coroutines always clean up on the game thread.

Cancellations are usually processed at the point of a coroutine resuming, not
suspending.
A latent mode coroutine awaiting a latent awaiter (in the `UE5Coro::Latent`
namespace) is an exception and will react to the cancellation at the next tick.

Nothing on this page applies to `UE5Coro::TGenerator<T>`, which is controlled by
its caller, and can be canceled by simply destroying it.

## Coroutine-initiated

For latent coroutines **only**, `co_await UE5Coro::Latent::Cancel()` never
resumes the coroutine but instead self-cancels and proceeds to cleanup.
Its output latent exec pin in BP will **NOT** trigger.

If you're running a coroutine in latent mode that is BlueprintCallable but not
latent (which is supported), this makes no difference in BP.
The exec pin triggers synchronously at the first co_await or co_return.

Coroutines running in async mode do not need this at all and can simply
co_return.
They're never latent in BP.

## User-requested

TCoroutine::Cancel() requests the underlying coroutine to stop running, which
will be served at the next co_await as usual.

Canceling a coroutine that has completed is safe to do and has no effect.

## Engine-initiated

Latent mode coroutines are owned by their UWorld's latent action manager,
that may decide to `delete` them while they're running.
This is translated to a forced cancellation of the coroutine.

If this happens while an async mode coroutine is co_awaiting a latent action,
that causes a normal cancellation that may be guarded against (see below).

# Auxiliary features

There are some additional features to support cancellation:

## Cancellation guard

For advanced use, `UE5Coro::FCancellationGuard` acts as a RAII guard against
cancellation.
Before using it, consider if your coroutine is still valid if its `this` is
destroyed.
FCancellationGuard objects are only valid to have as local variables in a
coroutine returning TCoroutine; using them anywhere else is undefined behavior.

* If one or more of these objects are alive within a coroutine body,
TCoroutine::Cancel() requests are deferred until the first co_await that resumes
after the last one has gone out of scope.
* Latent::Cancel() will `check()` if used with an active FCancellationGuard.
* The latent action manager's `delete` ignores cancellation guards.

## Manual cancellation check

If you're running in a tight loop without a natural co_await but want to poll
for incoming cancellation requests, `co_await UE5Coro::FinishNowIfCanceled()`
lets you process them manually.

* If the coroutine was not canceled, this will continue running **synchronously**
and instantly.
* If the coroutine was canceled, co_await will instead divert to cleanup,
as usual.

The return value of `FinishNowIfCanceled()` is copyable, reusable, but
meaningless.
co_awaiting it will behave the same regardless of which object is used, you
cannot use it to listen to the cancellation of another coroutine.

Cancellation will be processed normally: FCancellationGuard is respected, except
for incoming `delete`s, etc. It's usually pointless to co_await this if
there's an active FCancellationGuard.

Async mode cancellations are processed on the thread that co_awaited.
Latent mode cancellations are always processed on the game thread.

`UE5Coro::IsCurrentCoroutineCanceled()` is also available that simply returns a
bool but does not process the cancellation.
This function "sees through" FCancellationGuards and will return `true` if an
incoming cancellation request is currently deferred.

Prefer `co_await FinishNowIfCanceled();` to
`if (IsCurrentCoroutineCanceled()) co_return;`.
