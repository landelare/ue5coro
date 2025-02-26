# Async awaiters

These awaiters from the `UE5Coro::Async` namespace mostly deal with
multithreading.

The returned values from the following functions are generally copyable and
reusable.
Awaiting them multiple times will perform the same action again.

### auto MoveToThread(ENamedThreads::Type) noexcept

Awaiting the return value of this function moves the coroutine's execution to
the provided named thread.

Nothing happens if the coroutine is already there: it will continue running
synchronously.

Example:
```cpp
using namespace UE5Coro;
using namespace UE5Coro::Async;

TCoroutine<> ThreadHopperExample()
{
    OnCallerThread();
    co_await MoveToThread(ENamedThreads::RHIThread);
    OnRHIThread();
    co_await MoveToThread(ENamedThreads::AnyBackgroundThreadNormalTask);
    OnBackgroundThread();
    co_await MoveToThread(ENamedThreads::GameThread); // See MoveToGameThread()
    OnGameThread();
}
```

### auto MoveToGameThread() noexcept

This is a convenience shortcut for `MoveToThread(ENamedThreads::GameThread)`
with identical usage and behavior.

### auto MoveToSimilarThread()

The return value of this function remembers which kind of named thread it was
called from, and it can be used to later return to that thread, or an equivalent
one (game thread to game thread, render thread to render thread, background
thread to any other background thread, etc.).

It is useful if the calling thread is not known in advance.

The return value can be awaited multiple times to keep returning to the thread
that was originally remembered.

Nothing happens if the coroutine is already running on that thread.
As such, `co_await MoveToSimilarThread();` is pointless.
The return value should be stored and used later from another thread.

For example:
```cpp
using namespace UE5Coro::Async;

auto GoBack = MoveToSimilarThread(); // Current thread recorded here
co_await MoveToThread(ENamedThreads::AnyBackgroundThreadNormalTask);
DoBackgroundProcessing();
co_await GoBack; // Return to the recorded thread
```

### auto MoveToTask(const TCHAR* DebugName = nullptr)

Awaiting the return value of this function moves its calling coroutine into a
task from the UE::Tasks system.
The provided debug name will be passed to the task.

Multiple awaits will start one task each, with the same debug name.

Example:
```cpp
using namespace UE5Coro::Async;

co_await MoveToTask();
auto Value = DoHeavyProcessing();
co_await MoveToGameThread();
UseValueOnGameThread(std::move(Value));
```

### auto MoveToThreadPool(FQueuedThreadPool& ThreadPool = *GThreadPool, EQueuedWorkPriority Priority = EQueuedWorkPriority::Normal)

The return value of this function lets coroutines move execution into the
specified thread pool, with the specified priority.

The return value is reusable, it remembers the originally-provided thread pool
and priority, and awaiting it will keep queueing back on the same thread pool.

Behavior is undefined if the thread pool is not valid at the time of the
co_await.

Example:
```cpp
using namespace UE5Coro::Async;

TCoroutine<> ProcessOnThreadPool(AActress* Target, FQueuedThreadPool& ThreadPool,
                                 FForceLatentCoroutine = {})
{
    if (!ensure(IsValid(Target)))
        co_return;
    co_await MoveToThreadPool(ThreadPool);
    auto Value = SomeExpensiveFunction();
    co_await MoveToGameThread();
    Target->SetValue(std::move(Value));
}
```

### auto Yield() noexcept

The return value of this function moves the coroutine back into the same kind
of named thread that it was on (like MoveToSimilarThread), but it is
guaranteed to suspend the coroutine (unlike the MoveTo[...]Thread functions).

The return value, if reused, always yields back to a thread similar to the
current thread at the time of the co_await, i.e., the original thread is not
recorded.
As a result, storing the return value is relatively pointless.
It is essentially free to create and contains no data.

Example:
```cpp
using namespace UE5Coro::Async;

TCoroutine<> Example()
{
    OnCallerThread();
    auto Yielder = Yield(); // Not recommended usage, only for demonstration
    co_await Yield(); // The coroutine will return to its caller here
    OnCallerThread();
    co_await MoveToGameThread();
    OnGameThread();
    co_await Yielder; // Yield does not record the original thread
    OnGameThread();
}
```

### auto MoveToNewThread(EThreadPriority Priority = TPri_Normal, uint64 Affinity = FPlatformAffinity::GetNoAffinityMask(), EThreadCreateFlags Flags = EThreadCreateFlags::None) noexcept

Awaiting the return value of this function will move the coroutine's execution
into a freshly-created, "full-fat" thread.
Each co_await will start a new thread.
The thread will end when the coroutine co_returns or moves to another thread.

This is convenient to use for a long-running, blocking operation that would
negatively impact the engine's thread pool.

For the parameters, see the engine function `FRunnableThread::Create()`.

Example:
```cpp
using namespace UE5Coro;
using namespace UE5Coro::Async;

TCoroutine<> LongWait(FEvent* Event)
{
    co_await MoveToNewThread();
    Event->Wait(/*infinite*/);
    co_await MoveToGameThread();
    DoThingsAfterEventTrigger();
}
```

### auto PlatformSeconds(double Seconds) noexcept
### auto PlatformSecondsAnyThread(double Seconds) noexcept
### auto UntilPlatformTime(double Time) noexcept
### auto UntilPlatformTimeAnyThread(double Time) noexcept

These functions behave somewhat similarly to UE5Coro::Latent::RealSeconds and
UE5Coro::Latent::UntilRealTime, but they're free-threaded, and do not require a
world or a backing latent action.

As such, they're suitable to use in early engine code before a world is
available for latent actions, or situations where dealing with a world is not
desired or convenient.

The source of time is the `FPlatformTime::Seconds()` engine function.

* PlatformSeconds(AnyThread) will suspend the coroutine for the specified amount
  of seconds.
* UntilPlatformTime(AnyThread) will suspend until FPlatformTime::Seconds()
  reaches the specified point in time.

The awaiter returned by these functions supports expedited cancellation.

The AnyThread functions will resume the coroutine on a background thread, the
shorter-named ones will try and resume on the original named thread (game thread
to game thread, render thread to render thread, etc.).<br>
The AnyThread variants are marginally more efficient if this functionality is
not needed.

The return values are copyable, reusable, and awaiting them does nothing if
their original target time has already passed.
Copying a return value copies the target time and the desired thread from the
original call.

Time is measured from the function call. Example:

```cpp
using namespace UE5Coro::Async;

auto Awaiter = PlatformSecondsAnyThread(2);
co_await PlatformSeconds(0.3);
co_await Awaiter; // This will resume after approximately 1.7 seconds
co_await MoveToGameThread();
co_await Awaiter; // No-op
check(IsInGameThread());
```
