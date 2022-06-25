# Async coroutines

Returning `FAsyncCoroutine` (unfortunately not namespaced due to UHT limitations)
from a function makes it coroutine-enabled and lets you `co_await` various
awaiters provided by this library, found in `namespace UE5Coro::Async` and
`namespace UE5Coro::Latent`. Any async coroutine can use both async and latent
awaiters, but latent awaiters are limited to the game thread.

`FAsyncCoroutine` is a minimal (around `sizeof(void*)` depending on your
platform's implementation of `std::coroutine_handle`) struct that can access
the underlying coroutine, but it doesn't own it. These objects are safe to
discard or keep around for longer than needed, but interacting with a `delete`d
coroutine is undefined behavior as usual.
Coroutines get `delete`d when or shortly after they finish.

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
stop this by issuing `co_await Latent::Cancel();`.

If the `UFUNCTION` is called again with the same callback target/UUID while a
coroutine is already running, a second copy will **not** start similarly to most
built-in engine latent actions.

You may use `Async::MoveToThread` to switch threads, but the coroutine must finish
on the game thread. If the latent action manager decides to delete the latent task
and it's on another thread, it may continue until the next `co_await` after which
your stack will be unwound **on the game thread**.

## Awaiters

[Click here](Awaiters.md) for an overview of the various awaiters that come
with the plugin.

Although it's not directly forbidden to reuse awaiter objects, it's recommended
not to as the effects are rarely what you need and could change in future
versions. Treat them as expired once they've been `co_await`ed.

The awaiter types that are in the `UE5Coro::Private` namespace are subject to
change in any future version. Most of the time, you don't even need to know
about them, e.g. `co_await Something();`, but if you want to store them in
a variable (see below), use `auto`.

There are some additional situations that could cause unexpected behavior:
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

### Coroutines

`FAsyncCoroutine`s themselves are awaitable, `co_await`ing them will resume the
caller when the callee coroutine finishes for any reason, including
`Latent::Cancel()`. Async coroutines try to resume on a similar thread as they
were on when `co_await` was run (game thread to game thread, render thread to
render thread, etc.), latent coroutines resume on the next tick after the
callee ended.

## Coroutines and UObject lifetimes

While coroutines provide a synchronous-looking interface, they do not run
synchronously, and this can lead to problems that might be harder to spot due to
the friendly linear-looking syntax. Most coroutines will not need to worry about
these issues, but for advanced scenarios it's something you'll need to keep in
mind.

Your function immediately returns when you `co_await`, which means that the
garbage collector might run before you resume. Your function parameters and local
variables technically live in a "vanilla C++" struct with no UPROPERTY
declarations and therefore are eligible for garbage collection.

The usual solutions for multithreading and `UObject` access/GC keepalive such as
`AddToRoot`, `FGCObject`, `TStrongObjectPtr`, etc. still apply. If something would
work for `std::vector` it will probably work for coroutines, too.

Examples of dangerous code:

```cpp
using namespace UE5Coro;

FAsyncCoroutine AMyActor::Latent(UObject* Obj, FLatentActionInfo)
{
    // You're synchronously running before the first co_await, Obj is as your
    // caller passed it in. TWeakObjectPtr is safe to keep outside a UPROPERTY.
    TWeakObjectPtr<UObject> ObjPtr(Obj);

    co_await Latent::Seconds(1); // Obj might get garbage collected during this!

    if (Obj) // This is useless, Obj could be a dangling pointer
        Foo(Obj);
    if (auto* Obj2 = ObjPtr.Get()) // This is safe
        Foo(Obj2);

    // This is also safe! co_await will not resume if `this` is destroyed, so you
    // would not reach this line, but your local variables would run their
    // destructors and get freed as expected (see "Latent Mode" above).
    if (SomeUPropertyOnAMyActor)
        Foo(this);

    // Latent protection extends to awaiting Async awaiters and thread hopping:
    co_await Async::MoveToThread(ENamedThreads::AnyBackgroundThreadNormalTask);
    Foo(this); // Not safe, the GC might run on the game thread
    co_await Async::MoveToGameThread();
    Foo(this); // But this is OK! The co_await above resumed so `this` is valid.
}
```

Especially dangerous if you're running on another thread, `this` protection
and `co_await` _not_ resuming the coroutine does not apply if you're not latent:

```cpp
using namespace UE5Coro;

FAsyncCoroutine UMyExampleClass::DontDoThisAtHome(UObject* Dangerous)
{
    checkf(IsInGameThread(), TEXT("This example needs to start on the GT"));

    // You can be sure this remains valid until you co_await
    UObject* Obj = NewObject<UObject>();
    if (IsValid(Dangerous))
        Dangerous->Safe();

    // Latent protection applies when co_awaiting Latent awaiters even if you're
    // not latent, this might not resume if ActorObj gets destroyed:
    co_await Latent::Chain(&ASomeActor::SomethingLatent, ActorObj, 1.0f);

    // But not here:
    co_await Async::MoveToThread(ENamedThreads::AnyBackgroundThreadNormalTask);
    // You're no longer synchronously running on the game thread,
    // Obj *IS* eligible for garbage collection and Dangerous might be dangling!
    co_await Async::MoveToGameThread();

    // You're back on the game thread, but all of these could be destroyed by now:
    Dangerous->OhNo();
    Obj->Ouch();
    SomeMemberVariable++; // Even `this` could be GC'd by now!
}
```
