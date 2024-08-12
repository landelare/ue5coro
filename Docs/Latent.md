# Latent awaiters

These awaiters in the `UE5Coro::Latent` namespace are strongly tied to the game
thread and are not thread safe.
They must be created, used, and destroyed on the game thread.
A coroutine holding such a local variable is allowed to move to another thread,
as long as it guarantees that the latent awaiter will not be touched from that
other thread.

The awaiters returned by every function in this namespace satisfy the
UE5Coro::TLatentAwaiter concept, which comes with unique behavior: when used
from a latent coroutine, the implementation takes a fast path with reduced
indirections, and cancellations are processed as early as possible during the
await, before the coroutine would normally resume.

When such a type is awaited from an async coroutine, a latent action is created
and registered behind the scenes to handle the awaiter, which incurs additional
overhead.
When this happens, the GWorld global variable is read, and it must be valid at
the time of the co_await (which is usually the case on the game thread).
Latent coroutines do not have this limitation and do not access GWorld.
The functions from this namespace may still do.

### auto NextTick()
### auto Ticks(int64 Ticks)

The return value of these resume the coroutine after the specified number of
ticks have elapsed.
Negative values are not supported.

NextTick() is a convenience shortcut for Ticks(1).
Ticks(0) is already complete, and awaiting it does nothing.

The counters start when these functions are called, and they can be awaited
later:
```cpp
// The next 4 lines run within the same tick:
auto AwaiterA = Ticks(1);
auto AwaiterB = Ticks(4);
auto AwaiterC = Ticks(3);
auto AwaiterD = Ticks(2);

co_await AwaiterA; // Awaits 1 tick, as instructed
co_await AwaiterB; // 1 tick later, awaits the remaining 3 ticks for Ticks(4)
co_await AwaiterC; // Already done
co_await AwaiterD; // Already done
```

Usage example:
```cpp
using namespace UE5Coro::Latent;

UFUNCTION(BlueprintCallable, meta = (Latent, LatentInfo = LatentInfo))
FVoidCoroutine ProcessItems(TArray<FItem> Items, FLatentActionInfo LatentInfo)
{
    // Process items, 128 per tick
    for (int i = 0; i < Items.Num(); ++i)
    {
        if (i % 128 == 0) // This will return to the caller ASAP when i==0
            co_await NextTick();
        ProcessItem(Items[i]);
    }
}
```

### auto Until(std::function<bool()> Function)

The return value of this function, when co_awaited, polls the provided function
each tick, and resumes the coroutine when it first returns true.

It's roughly equivalent to `while (!Function()) co_await NextTick();`, but the
provided function is internally called on a fast path, without repeatedly
resuming and suspending the coroutine.

Example:
```cpp
using namespace UE5Coro::Latent;

UFUNCTION(BlueprintCallable, meta = (Latent, LatentInfo = LatentInfo))
FVoidCoroutine Example(FLatentActionInfo LatentInfo)
{
    co_await Until([&] { return bProceedWithExample; });
    Done();
}
```

### auto UntilCoroutine(TCoroutine<> Coroutine)

This wrapper forces awaits of TCoroutines to be performed in latent mode,
even if the caller coroutine is async.
Normally, the implementation is chosen based on the _awaiting_ coroutine's
execution mode, with async coroutines using a callback-based approach that is
optimized for no CPU overhead until the awaited coroutine completes.
The _awaited_ coroutine's execution mode doesn't matter.

Forcing latent mode can improve responsiveness to cancellations of the awaiting
coroutine, at the price of the added runtime overhead of creating and ticking a
backing latent action for the await.
It also requires the await to happen on the game thread.

Using this wrapper has no effect in a latent coroutine.
It already has a latent action, which gets reused.

Example:
```cpp
using namespace UE5Coro;
using namespace UE5Coro::Latent;

TCoroutine<> Example();

TCoroutine<> Async()
{
    co_await UntilCoroutine(Example()); // The await is latent
    co_await Example(); // The await is async
}

TCoroutine<> Latent(UWorld*, FLatentActionInfo)
{
    co_await UntilCoroutine(Example()); // Pointless force to latent
    co_await Example(); // The await is naturally latent already
}
```

### auto UntilDelegate(T& Delegate)

This function provides a safer alternative to
[awaiting a delegate directly](Implicit.md#delegates).
It is recommended to be used from latent coroutines if the delegate might not
get invoked.
Cancellation is handled promptly, however, parameters are not provided to the
coroutine.
The delegate itself may still have parameters and/or a return value (which will
be default constructed).

Example:
```cpp
using namespace UE5Coro::Latent;

// Instead of co_await ExampleDelegate;
co_await UntilDelegate(ExampleDelegate);
```

### auto Seconds(double Seconds)
### auto UnpausedSeconds(double Seconds)
### auto RealSeconds(double Seconds)
### auto AudioSeconds(double Seconds)

The awaiters returned by these functions wait for a specified amount of time
before resuming the coroutine, as measured by the current world (GWorld) at the
time of calling these functions.
As such, dilation and/or pause might affect them, and everything during a tick
is considered to happen at the same time.

|             |Seconds|UnpausedSeconds|RealSeconds|AudioSeconds|
|-------------|:-----:|:-------------:|:---------:|:----------:|
|Time dilation|✅      |✅              |❌          |❌           |
|Pause        |✅      |❌              |❌          |✅           |

✅=respected, ❌=ignored

Example:
```cpp
using namespace UE5Coro::Latent;

UFUNCTION(BlueprintCallable, meta = (Latent, LatentInfo = LatentInfo))
FVoidCoroutine CountDown(int Value, FLatentActionInfo LatentInfo)
{
    for (int i = Value; i > 0; --i)
    {
        UE_LOGFMT(LogTemp, Display, "{0}...", i);
        co_await Seconds(1.0);
    }
    UE_LOGFMT(LogTemp, Display, "Time's up!");
}
```

Waiting for a negative amount of time will `ensure` and finish immediately.

See UE5Coro::Async::PlatformSeconds for a thread-safe alternative to RealSeconds
that does not require a world.

### auto UntilTime(double Seconds)
### auto UntilUnpausedTime(double Seconds)
### auto UntilRealTime(double Seconds)
### auto UntilAudioTime(double Seconds)

These behave identically to their Seconds counterparts, but the returned values
will await until the current world reaches the specified point in time, instead
of an amount of time.
Waiting for a time in the past will similarly `ensure` and finish immediately.

For example, `UntilTime(GWorld->GetTimeSeconds() + 10)` is equivalent to
`Seconds(10)`.

For more details, see the Seconds family of functions right above this section.

The async counterpart of UntilRealTime is UE5Coro::Async::UntilPlatformTime.
