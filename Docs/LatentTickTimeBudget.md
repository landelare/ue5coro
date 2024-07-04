# Tick time budget

`UE5Coro::Latent::FTickTimeBudget` provides a convenient way to limit processing
on the game thread to the specified amount of time, instead of having to
manually tune the number of items processed per tick[^timeslice].

[^timeslice]: This can be trivially implemented with
              `if (i % LoopsPerTick == 0) co_await NextTick();` in a loop.

This class is not considered a latent awaiter (it's not an undocumented type
returned from a function), but it may only be used on the game thread.

Awaiting it will do nothing until the budget has been exhausted, after which, it
will behave like `UE5Coro::Latent::NextTick()` once, then return to doing
nothing for a while.

The intended use of this type is to be created outside a loop, then repeatedly
awaited inside.
This pattern ensures that at least one iteration will run, no matter how long it
took.

> [!TIP]
> Although this feature is available to both async and latent coroutines, it's
> optimized for use in latent coroutines.
> The additional async overhead is a fixed amount per tick, regardless of how
> many co_awaits fit into the tick.

### static FTickTimeBudget Seconds(double SecondsPerTick)

Returns an object that lets code through for the specified amount of seconds per
tick.

### static FTickTimeBudget Milliseconds(double MillisecondsPerTick)

Returns an object that lets code through for the specified amount of
milliseconds per tick.

### static FTickTimeBudget Microseconds(double MicrosecondsPerTick)

Returns an object that lets code through for the specified amount of
microseconds per tick.

## Examples

Processing a fixed number of items on a 1 ms budget:
```cpp
using namespace UE5Coro;
using namespace UE5Coro::Latent;

TCoroutine<> ProcessItems(TArray<FExampleItem> Items, FForceLatentCoroutine = {})
{
    auto Budget = FTickTimeBudget::Milliseconds(1);
    for (auto& Item : Items)
    {
        ProcessItem(Item);
        co_await Budget;
    }
}
```

Multi-stage processing works just as well, increasing the granularity at which
work can be deferred to the next tick.
The coroutine will resume where it left off:
```cpp
using namespace UE5Coro;
using namespace UE5Coro::Latent;

TCoroutine<> ProcessItems(TArray<FExampleItem> Items, FForceLatentCoroutine = {})
{
    auto Budget = FTickTimeBudget::Milliseconds(1);
    for (auto& Item : Items)
    {
        PreProcess(Item);
        co_await Budget;
        ProcessCore(Item);
        co_await Budget;
        PostProcess(Item);
        co_await Budget;
    }
}
```

Processing items sent to the game thread from other threads (such as actor
spawning instructions), allowing 2 ms per tick:
```cpp
using namespace UE5Coro;
using namespace UE5Coro::Latent;

TCoroutine<> ProcessItems(TMpscQueue<FExample>& Queue, FForceLatentCoroutine = {})
{
    for (;;)
    {
        auto Budget = FTickTimeBudget::Milliseconds(2);
        for (FExample Item; Queue.Dequeue(Item); co_await Budget)
            ProcessItem(Item);
        // If the queue is empty, delay to the next tick, otherwise the outer
        // loop would lock up the game thread
        co_await NextTick();
    }
}
```

Repeatedly calling a worker function for 0.5 ms (500 µs) per tick:
```cpp
using namespace UE5Coro::Latent;

for (auto Budget = FFrameTimeBudget::Microseconds(500); !bDone; co_await Budget)
    PerformOneStepOfWork();
```
