# Private implementation details

> [!CAUTION]
> This page describes some of the inner workings of UE5Coro, hoping that it
> might be useful for someone making a fork or a competing plugin.
>
> The reader is assumed to be deeply familiar with C++ coroutines and the
> language's use of coroutine_traits, coroutine_handle, promise_type, etc.
>
> **Everything in the private API is subject to change in any version, without**
> **prior deprecation.**

## Assertions

Because memory handling in C++ can be very complex, especially when it comes to
asynchronous execution, plugin code is aggressive with `check`s and other
functionality intended to catch bugs early.
These are always accompanied by a message:
* `ensureMsgf` means what you're doing is likely to be wrong, but it was handled.
* `checkf` means what you're doing is definitely wrong and will likely crash.
* `checkf(..., "Internal error: ...")` means you've found a bug in the plugin.

## Background, history

The design of this plugin is not perfect, and likely not optimal, either.
The focus has always been on convenience first, targeting gameplay and adjacent
code that isn't performance critical.
A few major features (mainly return values and universal cancellation) have
further increased the overhead.

UE5Coro 0.9 never had a public release; it initially existed as part of a
commercial project, without having a separate name yet.
It has been significantly redesigned for its 1.0 release as free software, which
unified the feature set available to async and latent coroutines.

Ever since 1.0, changes have been somewhat limited by keeping compatibility with
existing code, even if this is not an ironclad promise.
There have been plenty of breaking changes between 1.x and 1.y releases, not
unlike Unreal itself.

Having two types of coroutine promises, and the various awaiters **not** sharing
a common base class are perhaps the two design decisions with the most drawbacks.
It's not universally bad though, and it comes with benefits, which is why it's
still used in 2.0.

### FAsyncPromise/FLatentPromise

The promises will be described in more detail [later on](#promises).
The main motivation for having two backing implementations for coroutines
hearkens back to UE5Coro 0.9, where these were independent systems with separate
awaiters, and very straightforward, minimalistic implementations.

Async coroutines returned FAsyncCoroutine, latent coroutines returned
FLatentCoroutine, and they could only await things from their own matching
namespace.

Async awaiters called `coroutine_handle::resume()` from a callback, latent
awaiters were locked to the game thread, and used the FLatentAwaiter system that
is still present in 2.0.

The single biggest complaint from users was this separation, and it quickly
became clear that a unified feature set was inevitable.
The primary roadblock was multithreading from a latent coroutine, while its
ownership remained on the game thread.
The latent action manager is engine code, and it doesn't cooperate.

The ownership problem was solved by DetachFromGameThread, which will be
[described later](#flatentpromise).
Crossing over in the opposite direction is much more straightforward; it's
handled by FPendingAsyncCoroutine (as opposed to FPendingLatentCoroutine).

### No awaiter base class

This has some pretty obvious limitations, e.g., WhenAny/WhenAll cannot take
TArray\<IAwaiter*\>: their types and count must be known at compile time.

The two main reasons why this tradeoff was chosen were performance (which was
more significant back then, as described in the next section), and the fact that
some types behave differently based on what kind of coroutine awaits them.

### Memory management

All of these limitations would be directly solved by an alternative design,
which was explored and decided against in the early days of the plugin, but it
is thought to be perfectly viable.
Maybe one day, a competing plugin will appear that takes this path or something
similar.

In this design, the coroutine promises and awaiters would be shared pointers,
deferring all memory management concerns to runtime.
A latent action getting `delete`d would be no big deal, since it would only take
away one reference while the coroutine promise would presumably still be alive.
Engine callbacks could receive weak pointers to the awaiter, reducing memory
leaks from never-ending awaits to the control block.

This is more or less how coroutines are done, e.g., in .NET, but it comes with a
surprisingly large performance penalty in C++.
When `FPromiseExtras` was added in 1.7 (which is held in a shared pointer),
coroutine creation time jumped by 30%!
Using the thread-unsafe TSharedPtr is not an option for UE5Coro, due to the
now-pervasive multithreading support.

.NET is in a better position, since its memory allocation is faster than C++,
and passing objects around does not involve atomic reference counting.

The additional overhead was considered too high back when unifying
FAsyncPromise/FLatentPromise was initially done, but it was a necessity to
support coroutine return values, which had a much higher impact, and was deemed
worthy of the overhead.

Of course, this means that now, with `FPromiseExtras` being present, the
additional overhead from more shared pointer usage would be proportionally lower
than it was back then.

## Debug tools

Debug.h contains a few unused debug utilities that might be useful.

Defining `UE5CORO_PRIVATE_USE_DEBUG_ALLOCATOR` to 1 applies a lighter version of
stompmalloc to just coroutine states (Windows only).
It deliberately leaks address space (memory is decommitted, but not released) to
force new promises to be allocated at different addresses.

ClearEvents/GEventLog can be used as a low-overhead event logger to debug
multithreading issues.
Use ClearEvents() before the problem section, and
`UE5CORO_PRIVATE_DEBUG_EVENT(Pretty much anything)` to trace execution.
`bLogThread` can be set to true to capture the source thread of each message,
but this has higher overhead.

`GLastDebugID` and `GActiveCoroutines` can help to track down coroutine leaks.
`FPromiseExtras::DebugID` uses the same counter.

`Use()` is useful on some compilers to extend the lifetimes of certain primitive
local variables that are optimized out even in debug builds.

## Style

Although UE5Coro aims to provide a native, Unreal-looking public API, its
implementation deviates from the traditional, "pure" Unreal style, where doing
so was considered beneficial.

### Use of STL

STL types were chosen where they performed equally or better than Unreal's
reinventions, and in a few instances, they were used to avoid bugs in the Unreal
version.
Some of the usages in the latter category unfortunately also appear on the
public API, in order to avoid unnecessary wrappers or conversion.

### Move semantics

There are a few functions that take "expensive" parameters by value, instead of
the more commonly used const reference, e.g., TArray instead of const TArray&.

This is done on a case-by-case basis, where the parameter needs longer life, or
it's otherwise consumed.
This makes it possible for callers to avoid a copy by passing an rvalue to the
function.
Using a const reference would enforce a copy, even if callers could opt to move
data instead.
This is a mistake that Unreal's own code makes rather often.

std::move and std::forward are preferred over their Unreal counterparts, because
they are compiler intrinsics under MSVC and perform better in debug builds.

## Cancellation

Cancellation was a highly-requested feature for a long time, and initially
thought practically impossible.
Major systems with a lot more engineering behind them have essentially given up
on this, and instead expect coroutines to implement cooperative cancellation
themselves (e.g., CancellationToken in .NET).

The key realization for UE5Coro was that instead of solving the generic,
nigh-(or truly?) impossible cancellation problem, it would be enough to offer a
vastly-simplified version that could only happen if the coroutine is **not**
running.

This is why cancellation processing happens only on `co_await`: coroutines are
in a known state that is safe to divert.

The reason why coroutines process cancellations on resume is that many engine
functions copy any delegates that are passed in and offer no cancellation
support themselves.
Handling these would need something akin to the aforementioned
everything-is-a-shared-pointer setup, which would come at higher overhead.

Despite this, where it was deemed important, some awaiters indeed use shared
pointers.
UE5Coro::Private::FTwoLives implements a simplified version of this, for
situations where exactly two things are sharing data: the awaiter, and whatever
engine function was wrapped.
It is only a pointer large, which already includes 32 spare bits for custom data.

Awaiters that can handle expedited cancellation can declare support by calling
FPromise::RegisterCancelableAwaiter.

## Promises

The built-in specializations of std::coroutine_traits inspect the arguments,
looking for FLatentActionInfo, FForceLatentCoroutine, and TLatentContext
parameters to determine the execution mode and parameterize
TCoroutinePromise accordingly.

The classes involved are (all within the `UE5Coro::Private` namespace):

* `FPromise` implements cancellation, ContinueWith, and pass-through exception
  handling.
  A coroutine promise is forced to react to unhandled exceptions in C++, even if
  exceptions are otherwise disabled.
* `FPromiseExtras` contains fields whose lifetime does not match the FPromise's,
  such as storage for the coroutine's result.
  TCoroutine and FPromise hold shared_ptrs to this.
* `FAsyncPromise` provides trivial implementations for async mode, which is
  overall the simpler execution mode of the two.
* `FLatentPromise` contains the logic of transferring ownership from/to the
  latent action manager.
* `TCoroutinePromise<T, Base>` inherits from one of the above two promises and
  adds return type support, with a specialization for `void`.<br>
  `TCoroutine`s normally get this as their `promise_type`.
* UE5CoroGAS has a special `TAbilityPromise` that's parameterized with the
  owning class instead of the return type.

### FPromise

`FPromise::Current()` is callable from coroutine bodies to get access to the
current promise from any thread.
Cancellation-related types use this internally to talk to the promise directly.

Calling `get_return_object()` on the FPromise lets a coroutine obtain its return
value before it would be returned, which can be useful sometimes.

There's some additional debug data stored in debug builds, with natvis support.

### FAsyncPromise

This class is mostly unremarkable, and it's only mentioned for completeness.
The execution of coroutines with this promise is mostly similar to how a "pure"
C++ coroutine would work, mainly dealing with callbacks instead of
Unreal-style polls/ticks.

### FLatentPromise

This class bridges latent TCoroutines and their backing latent actions.

In initial_suspend, it mimics the usual BP behavior of **NOT** starting
execution if another similar latent action is already running (such as hitting a
Delay node while it's active), otherwise it creates a
`new FPendingLatentCoroutine` and registers it with the world.
The world owns this object and indirectly the coroutine execution, but there's a
notable and complex exception to this rule.

One of the defining features of UE5Coro is that the BP/latent and full async
multithreading features seamlessly work across both modes, and the available
functionality is identical (with a few latent exclusives related to interacting
with the backing latent action, which doesn't make sense for async).

This is directly at odds with the latent action manager **owning** the coroutine
execution on the game thread.
It is possible that while a coroutine is currently executing on a background
thread, the latent action manager `delete`s its pending latent action on the
game thread.

FLatentPromise has the ability to "detach" from the game thread.
In this state, ownership of the coroutine execution is temporarily assumed to
make sure the coroutine can reach the next co_await safely, where any incoming
`delete`s are inspected, and processed if needed.
Most detaching is done automatically by TAwaiter::await_suspend, which should
not be overloaded in derived classes (it would be `final` if C++ allowed it),
but `Suspend` used instead.

Any awaiter detaching a FLatentPromise must guarantee that FPromise::Resume is
called at some point, which will determine if ownership should return to the
game thread (and the latent action manager), or if it should remain detached.

A latent coroutine reaching final_suspend will always attach the promise to
the game thread.

## Awaiters

Although all awaiters will please C++, many of them will not work.
For instance, `co_await std::suspend_always();` will compile, but leak memory
when used, because there's no exposed way to directly resume the
coroutine_handle (the closest feature to this is UE5Coro::FAwaitableEvent).

It's paramount that the promises' Resume() function is called instead of
coroutine_handle::resume().
This lets promises handle cancellation, and game thread ownership in the case of
FLatentPromise.

Most awaiters in this plugin inherit from one of two base types: FLatentAwaiter
for game thread polls, and TAwaiter/TCancelableAwaiter for everything else that
needs to detach a FLatentPromise.
These are designed to use no `virtual`s, although FLatentAwaiter and
TCancelableAwaiter have a regular function pointer in them (which is one fewer
indirection than a vtable).

Using these lets you define your own awaiter types and have them work similarly
to the built-in ones, instead of relying on the generic awaiters on the public
API (TCoroutine\<\>, delegates, FAwaitableEvent, Latent::Until...).

### TAwaiter

The main functionality of this tiny helper is to detach FLatentPromises from the
game thread, and transform await_suspend(std::coroutine_handle\<T\>) calls
to Suspend(T&), enabling overloads and polymorphism on the promise type.
It also comes with some reasonable defaults for await_ready (not ready) and
await_resume (void no-op).

It uses CRTP and static dispatch, to make sure as much code is eligible for
inlining as possible.
Awaiters may implement either `Suspend(FPromise&)`, or
`Suspend(FAsyncPromise&)`+`Suspend(FLatentPromise&)`, which will cover every
TCoroutinePromise type.
Be very careful with further levels of inheritance.
It's usually done to provide a better implementation of await_resume for static
dispatch.
You might want to consider another level of CRTP for other scenarios, so that T
is set to the most derived subclass.

Numerous awaiters assume that await_suspend will be called right after
await_ready returns false, and leave locks locked for additional safety and
performance, instead of having a const await_ready and locking again in
await_suspend, then rechecking.

Some of these could be made const, but some await_readys are truly mutating.

### TCancelableAwaiter

This TAwaiter subclass is for awaiters that support direct, expedited
cancellation.
This involves handling a complicated multithreading scenario, involving up to
three threads concurrently racing (a latent coroutine detached from the game
thread gets destroyed on the game thread, resumed on thread A, and canceled on
thread B).

Do not shadow `fn_` in derived types.
It is named unusually to make this less likely to happen.

#### How to author a TCancelableAwaiter

In Suspend():
* Lock the promise.
* Call `Promise.RegisterCancelableAwaiter(this)`.
* If it returns true, Cancel() might be called at any time, including right now,
  but it will block until the promise is unlocked.
* If it returns false, clean up if needed, and unconditionally `Resume()` the
  coroutine **asynchronously**.
* Only resume a promise that returns true from UnregisterCancelableAwaiter().
  Normal resumptions are allowed to be synchronous.

In `fn_` (Cancel()):
* You already hold Extras->Lock, which blocks a potential ~FPromise(), and
  UnregisterCancelableAwaiter\<true\>().
* Call `Promise.UnregisterCancelableAwaiter<false>()`.
  Do nothing if it returns false.
  Something else received true, and it has arranged to resume the coroutine.
* If it returns true, clean up if needed, and `Resume()` the coroutine
  **asynchronously**.

Awaiters must guarantee thread-safety between cancellations and Suspend/Resume,
and that they resume the coroutine exactly once.

Usually, this is the natural outcome of UnregisterCancelableAwaiter() returning
false for every call but the first, but it might require additional
synchronization.
Destroying the coroutine will destroy the awaiter object, if it was a local
variable or temporary.
Resuming a canceled coroutine might result in its (and the awaiter's)
destruction before Resume() returns.

### FLatentAwaiter

This base class is used for awaiters that are polling/tick-based on the game
thread.
It is **NOT** derived from TAwaiter, and its implementations of Suspend() differ
greatly based on the awaiting promise.

Although every latent awaiter type is considered private, them being latent
awaiters is exposed via the `UE5Coro::TLatentAwaiter` concept, mainly for
documentation purposes, but some contrived template code could draw conclusions
on the expected behavior of an arbitrary awaiter based on this.

State is a generic pointer, but many latent awaiters use it as 64 bits of
storage space instead, to avoid indirection and/or an additional heap allocation.
For instance, a `double` fits right in.
This assumption is guarded with a static_assert, but Unreal Engine 5 itself does
not support 32-bit platforms anyway.

#### Async/Latent

Awaiting a FLatentAwaiter with FAsyncPromise creates a latent action in the
world in order to tick the awaiter.
Unlike latent coroutines, async coroutines create one latent action for each
individual co_await.

This means the latent action manager gets temporary ownership of a coroutine
that normally owns and manages itself.

The latent action getting `delete`d is translated to a regular cancellation.
Since async coroutines own themselves, this cancellation is not forced.
A `delete`d Private::FPendingAsyncCoroutine immediately returns ownership
instead of taking the coroutine with it.

#### Latent/Latent

FLatentPromise has a fast path for co_awaiting a FLatentPromise: it is passed
through to the underlying FPendingLatentCoroutine and polled directly from there,
bypassing the promise and the coroutine while it's active, and talking directly
to the latent action manager.

This behavior also enables these awaiters (and _only_ these awaiters) to react
to incoming cancellations at the next tick, instead of having to wait for some
callback to happen in order to properly clean up and process the cancellation at
a potentially much later point.

Subclassing FLatentAwaiter must be done with care: the derived object will be
object sliced when it's copied, so its `sizeof` must remain the same.
This is usually only done to reimplement `await_resume` and return a meaningful
value from the await expression based on the State field.

The FLatentAwaiter portion is copied into the promise, to eliminate as many
indirections as possible.
UE5Coro 1.x used a pointer directly into the co_await expression, which allowed
for polymorphism, but this was never needed in practice.
This is also why `Resume` is a function pointer instead of a virtual method: a
single-entry vtable would need one extra indirection.

If no return value is needed, it's fairly simple to define new latent awaiters:
return FLatentAwaiter itself initialized with an arbitrary state and a suitable
function pointer that will be called once on co_await and then once per tick.
The third constructor argument should be std::true_type() if the function is
sensitive to world changes, or std::false_type() otherwise.
This only affects an ensure() in debug.

Return true if the coroutine should be resumed, and clean up the state if the
bool parameter was true.
Refer to the many existing examples in the plugin code.
This internal type has been fairly stable and consistent across the plugin's
development.

The return value of Resume is ignored when called with bCleanup set to true.

## TAwaitTransform

This trait allows the promises' `await_transform` to be extended from anywhere
with specializations, including client code outside the plugin modules.
The default implementation forwards its argument unchanged, or calls
`operator co_await` on it, if it has one.

This is the primary method used to make Unreal types awaitable without wrapper
functions or reinterpret_cast hacks, although the latter is still used to
provide more efficient rvalue implementations.

TAwaitTransform will receive the kind of promise that it's transforming for.
This is how TCoroutine creates a ContinueWith-based TAsyncCoroutineAwaiter, or a
FLatentAwaiter-based TLatentCoroutineAwaiter depending on what kind of coroutine
it was awaited in.

## Blueprint support

Coroutine UFUNCTION calls are handled by a custom K2Node, intended to clean
things up a little in BP by removing useless pins.
The changes are purely visual.

It works by runtime patching the UFunctions themselves, so that you don't have
to mark every single one of them BlueprintInternalUseOnly yourself.
This has no performance penalty in Shipping builds, since K2Nodes are editor
only.

Its tooltip has also been changed to "Call Coroutine" so that it's easy to tell
apart in the editor if you need to for any reason.
It's a LOCTEXT, in case your team uses a localized editor.

If you don't like this for whatever reason, it's trivial to erase this class
when UE5Coro is introduced to a project, and everything will work the same.
Erasing it later will require a core redirect back to K2Node_CallFunction.
