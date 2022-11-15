# Async coroutines

Returning `FAsyncCoroutine` (unfortunately not namespaced due to UHT limitations)
from a function makes it coroutine-enabled and lets you co_await various
awaiters provided by this library, found in various namespaces within UE5Coro
such as UE5Coro\:\:Async or UE5Coro\:\:Latent.
Any async coroutine can use any awaiter, but some awaiters are limited to the
game thread.

FAsyncCoroutine is a minimal (usually `sizeof(void*)` depending on your
platform's implementation of std::coroutine_handle) struct that can access the
underlying coroutine, but it doesn't own it.
These objects are cheap to pass by value and safe to discard or keep around for
longer than needed, but interacting with a freed coroutine through them is
undefined behavior.
Coroutines get deleted when or shortly after they finish.

Accessing or manipulating the underlying std::coroutine_handle directly is not
supported and is extremely likely to break.

## Debugging

FAsyncCoroutine::SetDebugName() applies a debug name to the currently-running
coroutine's promise object, which is otherwise an implementation detail.
This has no effect at runtime (and does nothing in Shipping), but it's useful
for debug viewing these objects.

You might want to macro `FAsyncCoroutine::SetDebugName(TEXT(__FUNCTION__))`.

Looking at these or promise objects in general as part of `__coro_frame_ptr`
seems to be unreliable in practice, moving one level up in the call stack to
Resume() tends to work better when tested with Visual Studio 2022 17.4 and
JetBrains Rider 2022.3.
A .natvis file is provided to automatically display this debug info.

In debug builds (controlled by `UE5CORO_DEBUG`) a synchronous resume stack is
also kept to aid in debugging complex cases of coroutine resumption, mostly
having to do with WhenAny or WhenAll.

## Execution modes

There are two major execution modes of async coroutines: they can either run
autonomously or implement a latent UFUNCTION, tied to the latent action manager.

### Async mode

If your function **does not** have a `FLatentActionInfo` parameter, the
coroutine is running in "async mode".
You still have access to awaiters in the UE5Coro::Latent namespace (locked to
the game thread) but as far as your callers are concerned, the function returns
at the first co_await and drives itself after that point.

This mode is mainly a replacement for "fire and forget" AsyncTasks and timers.

### Latent mode

If your function (probably a UFUNCTION in this case but this is **not** checked
or required) takes `FLatentActionInfo`, the coroutine is running in "latent mode".
The world will be fetched from the first UObject* parameter that returns a valid
pointer from GetWorld() with GWorld used as a last resort fallback.

The latent info will be registered with that world's latent action manager,
there's no need to call FLatentActionManager::AddNewAction().

The output exec pin will fire in BP when the coroutine co_returns (most often
this happens naturally as control leaves the scope of the function), but you can
stop this by issuing `co_await UE5Coro::Latent::Cancel();`.
As an exception, this one will not resume the coroutine but complete it without
execution resuming in BP.
The destructors of local variables, etc. will run as usual.

If the UFUNCTION is called again with the same callback target/UUID while a
coroutine is already running, a second copy will **not** start, matching the
behavior of most of the engine's built-in latent actions.

You may use awaiters such as UE5Coro\:\:Async\:\:MoveToThread or
UE5Coro\:\:Tasks\:\:MoveToTask to switch threads, but the coroutine must finish
on the game thread.
If the latent action manager decides to delete the latent task and it's on
another thread, it may continue until the next co_await after which your stack
will be unwound **on the game thread**.

## Awaiters

[Click here](Awaiters.md) for an overview of the various awaiters that come
with the plugin.

Most awaiters from this plugin can only be used once and will `check()` if
reused.
There are a few (notably in the UE5Coro::Async namespace) that may be reused,
but these are so cheap to create – around the cost of an int – that you should
be recreating them for consistency.

It's recommended to treat every awaiter as "moved-from" or invalid after they've
been co_awaited. This includes being co_awaited through wrappers such as WhenAll.

The awaiter types that are in the `UE5Coro::Private` namespace are subject to
change in any future version with no prior deprecation.
Most of the time, you don't even need to know about them, e.g.,
`co_await Something();`.
If you want to store them in a variable (see below), use `auto` for source
compatibility.

There are some additional situations that could cause unexpected behavior, such
as crashes or coroutines "deadlocking":
* co_awaiting UE5Coro::Latent awaiters off the game thread.
* Moving to a named thread that's not enabled, e.g., RHI.
* Expecting to resume a latent awaiter while paused or otherwise not ticking.

Generally speaking, the same rules and limitations apply as the underlying
engine systems that drive the current awaiter and its awaiting coroutine.

### Overlapping awaiters

It is possible to run multiple awaiters overlapped, which makes sense for
(but isn't limited to) some of them that perform useful actions, not just wait:

```cpp
FAsyncCoroutine AMyActor::GuaranteedSlowLoad(FLatentActionInfo)
{
    auto Wait1 = UE5Coro::Latent::Seconds(1); // The clock starts now!
    auto Wait2 = UE5Coro::Latent::Seconds(0.5); // This starts at the same time!
    auto Load1 = UE5Coro::Latent::AsyncLoadObject(MySoftPtr1);
    auto Load2 = UE5Coro::Latent::AsyncLoadObject(MySoftPtr2);
    co_await UE5Coro::WhenAll(Load1, Load2); // Wait for both to be loaded
    co_await Wait1; // Waste the remainder of that 1 second
    co_await Wait2; // This is already over, it won't wait half a second
}
```

### Other coroutines

`FAsyncCoroutine`s themselves are awaitable, co_awaiting them will resume the
caller when the callee coroutine finishes for any reason, **including**
`UE5Coro::Latent::Cancel()`.
Async coroutines try to resume on a similar thread as they were on when co_await
was issued (game thread to game thread, render thread to render thread, etc.),
latent coroutines resume on the next tick after the callee ended.

## Coroutines and UObject lifetimes

While coroutines provide a synchronous-looking interface, they do not run
synchronously (that's kind of the point🙂) and this can lead to problems that
might be harder to spot due to the friendly linear-looking syntax.
Most coroutines will not need to worry about these issues, but for advanced
scenarios it's something you'll need to keep in mind.

Your function immediately returns when you co_await, which means that the
garbage collector might run before you resume.
Your function parameters and local variables technically live in a "raw C++"
struct with no UPROPERTY declarations (generated by the compiler) and therefore
are eligible for garbage collection.

The usual solutions for multithreading and UObject access/GC keepalive such as
AddToRoot, FGCObject, TStrongObjectPtr, etc. still apply.
If something would work for std::vector it will probably work for coroutines, too.

Examples of dangerous code:

```cpp
using namespace UE5Coro;

FAsyncCoroutine AMyActor::Latent(UObject* Obj, FLatentActionInfo)
{
    // You're synchronously running before the first co_await, Obj is as your
    // caller passed it in. TWeakObjectPtr is safe to keep outside a UPROPERTY.
    TWeakObjectPtr<UObject> ObjPtr(Obj);

    co_await Latent::Seconds(1); // Obj might get garbage collected during this!

    if (IsValid(Obj)) // Dangerous, Obj could be a dangling pointer by now!
        Foo(Obj);
    if (auto* Obj2 = ObjPtr.Get()) // This is safe, might be nullptr
        Foo(Obj2);

    // This is also safe, but only because of the FLatentActionInfo parameter!
    // Destroying an actor cancels all of its latent actions at the engine level,
    // so you would never reach this point.
    if (SomeUPropertyOnAMyActor)
        Foo(this);

    // Latent protection extends to other awaiters and thread hopping:
    co_await Tasks::MoveToTask();
    Foo(this); // Not safe, the GC might run on the game thread
    co_await Async::MoveToGameThread();
    Foo(this); // But this is OK! The co_await above resumed so `this` is valid.
}
```

Especially dangerous if you're running on another thread, `this` protection
and co_await _not_ resuming the coroutine does not apply if you're not latent:

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
