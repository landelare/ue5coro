# Aggregate awaiters

These awaiters allow you to combine multiple awaitables or TCoroutines into one
operation.

They support expedited cancellation.

The return values of the functions are copyable and allow one concurrent
co_await.
Once the first await completes, further awaits return the same value (except for
WhenAll, which is `void`) synchronously on the calling thread.

A coroutine awaiting WhenAny, WhenAll, and Race will resume on a thread
corresponding to one of the parameters passed in, or on the thread that canceled
the coroutine.
For instance, if all parameters would resume on the game thread when awaited
directly, and the coroutine is never canceled, or only canceled from the game
thread (a common case), their aggregate is guaranteed to resume on the game
thread, but if one of them would resume on another thread, or the coroutine
might be canceled from another thread, then it will resume on either the game
thread, or that other thread.

The Latent versions of WhenAny and WhenAll always resume on the game thread.

### auto WhenAny(TAwaitable auto&&... Awaitables)
### auto WhenAny(const TArray\<TCoroutine\<\>\>& Coroutines)

These functions `co_await` everything passed in, and resume the calling
coroutine when the first one completes.
The rest are ignored, but they're still in a co_awaited state, which matters for
types that cannot be reused.

Completion includes **un**successful completions.

The result of awaiting the return value of these functions is the index of the
parameter that first completed.
The two overloads behave identically.
In case of zero parameters, a negative value will be returned instantly on the
calling thread (0 would mean the first parameter).

Simple example:
```cpp
using namespace UE5Coro;

int FirstTask = co_await WhenAny(TaskA /*0*/, TaskB /*1*/, TaskC /*2*/);
```

Complex example:
```cpp
using namespace UE5Coro;

auto Awaiter1 = SomeAsyncTask();
auto Awaiter2 = AnotherAsyncTask();
DoSomethingUsefulBeforeAwaiting();
int FirstAwaiter = co_await WhenAny(std::move(Awaiter1), std::move(Awaiter2));
```

### auto Latent::WhenAny(TLatentContext\<const UObject\> LatentContext, TAwaitable auto&&... Awaitables)

For advanced use on the game thread only.
This function works like the regular WhenAny, but the parameters are awaited in
latent mode, with the provided context.

Example:
```cpp
using namespace UE5Coro::Latent;

bool bTimedOut = !!co_await WhenAny(this, Task, Latent::Seconds(1));
```

### auto Race(TArray\<TCoroutine\<\>\> Coroutines)
### auto Race(TCoroutine\<T\>... Coroutines)

These functions behave similarly to WhenAny, but the first coroutine to complete
will cancel all the others.
Completion includes **un**successful completions.

Parameters may only be TCoroutines, not anything awaitable, since awaiters are
not directly cancelable.
Both overloads behave identically.

If zero coroutines are racing, Race immediately succeeds and a negative value is
returned from the co_await expression (0 would mean the first coroutine).

There is no RaceLatent.
The input coroutines have already determined their execution modes.

Canceling a coroutine that's currently awaiting Race() will process
cancellations immediately.
If the return value of Race is destroyed for any reason, the race and all
coroutines participating in it will be canceled, even if it wasn't awaited.

Example:
```cpp
using namespace UE5Coro;

int Action = co_await Race(TryAttacking(), TryHiding(), TryJumping());
bool bJumped = (Action == 2);
```

### auto WhenAll(TAwaitable auto&&... Awaitables)
### auto WhenAll(const TArray\<TCoroutine\<\>\>& Coroutines)

These functions co_await everything passed in (consuming awaiters that are not
reusable), and resume the calling coroutine once the last of them completes.

Completion includes **un**successful completions.

In case of zero parameters, the operation succeeds immediately, and the
coroutine continues synchronously on the caller thread.

Example:
```cpp
using namespace UE5Coro;

TArray<TCoroutine<>> Tasks;
for (int i = 0; i < 100; ++i)
    Tasks.Add(ExpensiveAsyncCoroutine(i));
DoSomethingUsefulBeforeAwaiting();
co_await WhenAll(Tasks);
```

### auto Latent::WhenAll(TLatentContext\<const UObject\> LatentContext, TAwaitable auto&&... Awaitables)

For advanced use on the game thread only.
This function works like the regular WhenAll, but the parameters are awaited in
latent mode, with the provided context.

Example:
```cpp
using namespace UE5Coro::Latent;

co_await WhenAll(this, TaskA, TaskB);
```
