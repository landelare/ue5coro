# Awaiters

This page gives an overview of the various awaiters that come with the plugin.
This is not meant to be an exhaustive documentation, read the comments in the
header files for that.

## Aggregates

UE5Coro::WhenAny and WhenAll let you combine any type of co_awaitable objects
into one that resumes the coroutine when one or all of them have completed.
When multiple types of awaiters are mixed, it's unspecified whose system will
resume - for example:
```cpp
auto Async = UE5Coro::Async::MoveToThread(...);
auto Latent = UE5Coro::Latent::Seconds(...);
auto Task = UE5Coro::Tasks::MoveToTask();
co_await UE5Coro::WhenAll(Async, Latent, Task);
```
The code above might resume in an AsyncTask, game thread Tick, or the UE::Tasks
system.
WhenAll/WhenAny are thread safe.

## Async awaiters

The UE5Coro::Async namespace contains awaiters that let you conveniently move
execution between various named threads, notably between the game thread and
everything else.
See UE5Coro\:\:Tasks for support of the more modern UE\:\:Tasks system.

### TFuture

TFuture\<T\> is directly co_awaitable.
The co_await expression returns the result of the future and consumes the
TFuture object similarly to its Then and Next members.
As a result, if your future is not a temporary it will require being moved:
```c++
TPromise<int> Promise;
co_await Promise.GetFuture(); // OK, temporary future
TFuture<int> Future = Promise.GetFuture();
// co_await Future; // Won't compile
co_await MoveTemp(Future); // OK
```

Unlike TFuture::Next, the co_await expression will correctly match the expected
type, i.e., TFuture\<void\> will result in void instead of int and co_awaiting a
TFuture\<T&\> will result in T& instead of T*.

co_await resumes the coroutine on the same thread that TFuture::Then or Next
would use.

TSharedFuture\<T\> is not supported due to the underlying implementation of it
lacking completion callbacks.

## Latent awaiters

UE5Coro::Latent awaiters are locked to the game thread.
Their lifetime is tied to the world so it's possible, e.g., if PIE ends that
co_awaiting them will not resume your coroutine.
In this case your stack is still unwound normally (similarly to if an exception
was thrown) and your destructors are called so it's safe to use `FScopeLock`s,
smart pointers, etc. across a co_await, but something like this could cause
problems:

```cpp
T* Thing = new T();
co_await UE5Coro::Latent::Something(); // This may not resume
delete Thing;
```

It's undefined when exactly your coroutine stack is unwound, it might be, e.g.,
when the latent action is destroyed or when the co_await would normally resume
the coroutine.
In practice, for awaiters in this namespace it will usually happen within 2
ticks.

### Latent callbacks

To help with the situation above, the engine's own `ON_SCOPE_EXIT` can be used
to place code in a destructor, ensuring that it will always run even if the
latent action manager cancels the coroutine.

The types in UE5Coro/LatentCallbacks.h provide specialized versions of this that
only execute the provided function/lambda if the coroutine is canceled for a
certain reason.
Note that a coroutine canceling itself with `UE5Coro::Latent::Cancel()` counts
as neither of these but a normal completion.

### Chained latent actions

Most existing latent actions in the engine return void so there's nothing
that you could take or store to co_await them.
There are two wrappers provided in UE5Coro::Latent to make this work,
one of which is available even in C++17:

```cpp
using namespace std::placeholders; // for ChainEx
using namespace UE5Coro;

// Automatic parameter matching (C++20 only): skip WorldContextObject and LatentInfo
co_await Latent::Chain(&UKismetSystemLibrary::Delay, 1.0f);

// For members, provide the object as the first parameter (C++20 only):
co_await Latent::Chain(&UMediaPlayer::OpenSourceLatent, MediaPlayer /*this*/,
                       MediaSource, Options, bSuccess);

// Manual parameter matching, _1 is WorldContextObject and _2 is LatentInfo:
co_await Latent::ChainEx(&UKismetSystemLibrary::Delay, _1, 1.0f, _2);
co_await Latent::ChainEx(&UMediaPlayer::OpenSourceLatent, MediaPlayer, _1, _2,
                         MediaSource, Options, bSuccess);
```

As it is impossible to read UFUNCTION(Meta) information at C++ compile time
to figure out which parameter truly is the world context object, Chain uses
**compile-time** heuristics to handle most latent functions found in the engine:
* The first `UObject*` or `UWorld*` that's not `this` is the world context.
* The first `FLatentActionInfo` is the latent info.
* All other parameters of these types are treated as regular parameters and are
  expected to be passed in.

If this doesn't apply to your function or you're using C++17, use
`Latent::ChainEx` and explicitly provide `_1` for the world context (if needed)
and `_2` for the latent info (mandatory) where they belong.
They work exactly like they do in
[std::bind](https://en.cppreference.com/w/cpp/utility/functional/bind).

#### Debugging/implementation notes

In popular debuggers (Visual Studio 2022 and JetBrains Rider 2022.1 tested)
`Chain` tends to result in very long call stacks if the chained function is
getting debugged. These are marked `[Inline Frame]` or `[Inlined]` (if your
code is optimized, `Development` or above) and all of them tend to point at the
exact same assembly instruction; this entire segment of the call stack is for
display only and can be safely disregarded.

It cannot be guaranteed but it's been verified that the `Chain` wrappers do get
optimized and turn into a regular function call or get completely inlined in
`Shipping`.

#### Rvalue references

Although this doesn't apply to UFUNCTIONs, passing rvalue references to
Chain() is **not** equivalent to passing them straight to the chained function:
the reference that the function will receive will be to a move-constructed
object, not the original.
This matters in extremely unusual scenarios where the caller wants to still
access the rvalue object after the latent function has returned.
If for some reason you need exactly this, refer to the implementation of
ChainEx to see how to register yourself with UUE5CoroSubsystem and call the
function taking rvalue references directly as an unsupported last-resort option.

## UE::Tasks

UE\:\:Tasks\:\:TTask\<T\> is directly co_awaitable.
Doing so will resume the coroutine within the tasks system once the task has
completed.
The result of the co_await expression will be T& (not T!) or void, matching
TTask\<T\>::GetResult().

The UE5Coro::Tasks namespace provides a convenience function to move to a
TTask without having to use the lambda syntax.

Latent coroutines will need to `co_await Async::MoveToGameThread();` at some
later point to return to the game thread and correctly complete.

## HTTP

UE5Coro\:\:Http\:\:ProcessAsync wraps a FHttpRequestRef in an awaiter that
resumes your coroutine when the request is done (including errors).
Unlike OnProcessRequestComplete() this does **not** force you back on the game
thread, but you can start and finish there if you wish of course.
