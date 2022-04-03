# Async coroutines

Returning `FAsyncCoroutine` (unfortunately not namespaced due to UHT limitations)
from a function makes it coroutine-enabled and lets you `co_await` various
awaiters provided by this library, found in `namespace UE5Coro::Async` and
`namespace UE5Coro::Latent`. Any async coroutine can use both async and latent
awaiters, but latent awaiters are limited to the game thread.

## Execution modes

There are two major execution modes of async coroutines–they can either run
autonomously or implement a latent `UFUNCTION`, tied to the latent action manager.

### Async mode

If your function **does not** have a `FLatentActionInfo` parameter, the coroutine
is running in "async mode".
You still have access to awaiters in `namespace UE5Coro::Latent` (locked to the
game thread) but as far as your callers are concerned, the function returns at the
first `co_await` and drives itself after that point.

This mode is mainly a replacement for "fire and forget" `AsyncTask`s and timers.

### Latent mode

If your function (probably a `UFUNCTION` in this case but this is **not** checked)
takes `FLatentActionInfo`, the coroutine is running in "latent mode".
The world will be fetched from the first `UObject*` parameter that returns a valid
pointer from `GetWorld()` with `GWorld` used as a last resort fallback, and the
latent info will be registered with that world's latent action manager, there's no
need to call `AddNewAction()`.

The output exec pin will fire in BP when the coroutine `co_return`s (most often
this happens naturally as control leaves the scope of the function), but you can
stop this by issuing `co_await Latent::Abort();`.

If the `UFUNCTION` is called again with the same callback target/UUID while a
coroutine is already running, a second copy will **not** start similarly to most
built-in engine latent actions.

You may use `Async::MoveToThread` to switch threads, but the coroutine must finish
on the game thread. If the latent action manager decides to delete the latent task
and it's on another thread, it may continue until the next `co_await` after which
your stack will be unwound **on the game thread**.

## Awaiters

There are two main categories of awaiters that are similar to coroutine execution
modes but orthogonal to them–both modes can use awaiters from both namespaces.
The ones in `namespace UE5Coro::Async` work with `AsyncTask`s under the hood,
the ones in `namespace UE5Coro::Latent` are implemented as latent actions (even if
you are not running in latent mode) and tied to the world so it's possible, e.g.,
if PIE ends that `co_await` will not resume your coroutine.
Destructors are still called, similarly to if an exception was thrown so it's safe
to use `FScopeLock`s, smart pointers... but doing this could cause memory leaks:

```cpp
T* Thing = new T();
co_await UE5Coro::Latent::Something(); // This may not resume if, e.g., PIE ends
delete Thing;
```

Most awaiters in the `Latent` namespace can only be used once and any further
`co_await`s instantly continue. Awaiters in the `Async` namespace are reusable and
always move you to their designated thread but they are as cheap to make as an
`int` so only do this if it makes your code clearer.

There are some other situations that could cause unexpected behavior:
* `co_await`ing `namespace UE5Coro::Latent` awaiters off the game thread.
* Moving to a named thread that's not enabled, e.g., RHI.
* Expecting to resume a latent awaiter while paused or otherwise not ticking.

Generally speaking, the same rules and limitations apply as the underlying system
that drives the current awaiter.

### Overlapping awaiters

It is possible to run multiple awaiters overlapped, which makes sense for some of
them that perform useful actions and not just wait (but not limited to them):

```cpp
using namespace UE5Coro;

FAsyncCoroutine AMyActor::GuaranteedSlowLoad(int, FLatentActionInfo)
{
    // Awaiter types are in the Private namespace and subject to change, use auto
    auto Wait1 = Latent::Seconds(1); // The clock starts now!
    auto Wait2 = Latent::Seconds(0.5);
    auto Load1 = Latent::AsyncLoadObject(MySoftPtr1);
    auto Load2 = Latent::AsyncLoadObject(MySoftPtr2);
    co_await Load1;
    co_await Load2;
    co_await Wait1; // Waste the remainder of that 1 second
    co_await Wait2; // This will resume immediately, not half a second later
}
```

### Chained latent actions

Most existing latent actions in the engine return `void` so there's nothing that
you could take or store to `co_await` them. There are two special wrappers
provided in `namespace UE5Coro::Latent` to make this work:

```cpp
using namespace std::placeholders; // for ChainEx
using namespace UE5Coro;
...

// Automatic parameter matching: simply skip WorldContextObject and LatentInfo
co_await Latent::Chain(&UKismetSystemLibrary::Delay, 1.0f);

// For members, provide the object as the first parameter like interface Execute_:
co_await Latent::Chain(&UMediaPlayer::OpenSourceLatent, MediaPlayer /*this*/,
                       MediaSource, Options, bSuccess);

// Manual parameter matching, _1 is WorldContextObject and _2 is LatentInfo:
co_await Latent::ChainEx(&UKismetSystemLibrary::Delay, _1, 1.0f, _2);
co_await Latent::ChainEx(&UMediaPlayer::OpenSourceLatent, MediaPlayer, _1, _2,
                         MediaSource, Options, bSuccess);
```

As it is impossible to read `UFUNCTION(Meta)` information at C++ compile time to
figure out which parameter truly is the world context object, `Chain` uses
type-based heuristics that can handle most latent functions found in the engine:
* The first `UObject*` or `UWorld*` that's not `this` is the world context.
* The first `FLatentActionInfo` is the latent info.
* All other parameters of these types are treated as regular parameters and are
  expected to be passed in.

If this doesn't apply to your function, use `Latent::ChainEx` and explicitly
provide `_1` for the world context (if needed) and `_2` for the latent info
(mandatory) where they belong. They work exactly like they do in
[`std::bind`](https://en.cppreference.com/w/cpp/utility/functional/bind).

#### Debugging/implementation notes

In popular debuggers (Visual Studio 2022 and JetBrains Rider 2022.1 tested)
`Chain` tends to result in very long call stacks if the chained function is
getting debugged. These are marked `[Inline Frame]` or `[Inlined]` (if your code
is optimized, `Development` or above) and all of them tend to point at the exact
same assembly instruction; this entire segment of the call stack is for display
only and can be safely disregarded.

It cannot be guaranteed but it's been verified that the `Chain` wrappers do get
optimized and turn into a regular function call or get completely inlined in
`Shipping`.

#### Rvalue references

Although this doesn't apply to `UFUNCTION`s, passing rvalue references to
`Latent::Chain` is **not** equivalent to passing them straight to the chained
function: the reference that the function will receive will be to a
move-constructed object, not the original. This matters in extremely unusual
scenarios where the caller wants to still access the rvalue object after the
latent function has returned. If for some reason you need exactly this, refer to
the implementation of `Latent::ChainEx` to see how to register yourself with
`UUE5CoroSubsystem` and call the function taking rvalue references directly.

## Async coroutines and garbage collection

The caveats are mostly the same as with regular `AsyncTask`s, but due to the
easy-to-write, synchronous-looking nature of coroutines it can be harder to notice
if you're dealing with `UObject`s incorrectly.

Your function immediately returns when you `co_await`, which is important if,
e.g., it's called from the game thread and you're dealing with `UObjects` through
parameters or `this`. These values are technically living in a "vanilla C++"
struct without any `UPROPERTY` declarations and therefore can be eligible for
garbage collection while you're on another thread:

```cpp
FAsyncCoroutine UMyExampleClass::DontDoThisAtHome(UObject* Dangerous)
{
    UObject* Obj = NewObject<UObject>();

    co_await Async::MoveToThread(ENamedThreads::AnyBackgroundThreadNormalTask);
    // You're no longer synchronously running on the game thread,
    // Obj *IS* eligible for garbage collection!

    co_await Async::MoveToGameThread();
    // You're back on the game thread, all of these could be dangling pointers:
    Dangerous->OhNo();
    Obj->Ouch();
    SomeMemberVariable++; // Even `this` could be GC'd by now!
}
```

The usual solutions for multithreading and `UObject` access/GC keepalive such as
`AddToRoot`, `FGCObject`, `TStrongObjectPtr`, etc. still apply.
