# Awaiters

This page gives an overview of the various awaiters that come with the plugin.
This is not meant to be an exhaustive documentation, read the comments in the
header files for that.

## Aggregates

UE5Coro::WhenAny and WhenAll let you combine any type of co_awaitable objects
into one that resumes the coroutine when one or all of them have completed.

UE5Coro::Race behaves like WhenAny, but it can only take TCoroutines (including
implicitly-converted FAsyncCoroutines), and the first coroutine to complete will
cancel the others.

When multiple types of awaiters are mixed, it's unspecified whose system will
resume - for example:
```cpp
auto Async = UE5Coro::Async::MoveToThread(...);
auto Latent = UE5Coro::Latent::Seconds(...);
auto Task = UE5Coro::Tasks::MoveToTask();
co_await UE5Coro::WhenAll(Async, MoveTemp(Latent), Task);
```
The code above might resume in an AsyncTask, game thread Tick, or the UE::Tasks
system.
WhenAny, Race, and WhenAll are all thread safe.

Some awaiters (mostly Latent ones) require being moved into the call like in the
example above.
C\+\+20 will let you know that the call's constraints were not satisfied on the
calling line.
C\+\+17 will hit a static_assert inside the function, prompting you to fix it.
The calling line will often be found in the error's notes somewhere.

Every parameter is consumed and counts as co_awaited by these calls, even if
WhenAny or Race finish early.

The return values of these functions are copyable and allow one concurrent
co_await across all copies.
Once the initial co_await has finished, further ones continue synchronously.

## Async awaiters

The UE5Coro::Async namespace contains awaiters that let you conveniently move
execution between various named threads, notably between the game thread and
everything else.
See UE5Coro\:\:Tasks for support of the more modern UE\:\:Tasks system.

The return values of these functions are copyable, thread-safe, and allow any
number of concurrent co_awaits.

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

TFuture\<T\> itself is movable and can only be used (including co_await) once.

## Latent awaiters

UE5Coro::Latent awaiters are locked to the game thread.
Their lifetime is tied to the world and the latent action manager can decide to
cancel them, so it's possible, e.g., if PIE ends or its owning AActor is
destroyed, that co_awaiting them will not resume your coroutine.
In this case the coroutine's state and locals are still destroyed normally
(similarly to if an exception was thrown) and their destructors called, so it's
safe to use `FScopeLock`s, smart pointers, etc. across a co_await, but something
like this could cause problems:

```cpp
T* Thing = new T();
co_await UE5Coro::Latent::Something(); // This may not resume
delete Thing;
```

It's undefined when exactly the coroutine's state is cleared, it might be, e.g.,
when the latent action is destroyed or when the co_await would normally resume
the coroutine.
In practice, for awaiters in this namespace it will usually happen within 2
ticks.

Note that while async mode coroutines normally drive and own themselves, if
they're currently co_awaiting a latent awaiter, the world **can** decide to
destroy the coroutine, in which case the same cancellation/cleanup happens.

The return values of these functions are movable and some of them support
multiple concurrent co_awaits, but relying on the latter is not recommended.

### Latent callbacks

To help with the example code from the previous section above, the engine's own
`ON_SCOPE_EXIT` can be used to place code in a destructor, ensuring that it will
always run even if the coroutine is canceled.

The types in UE5Coro/LatentCallbacks.h provide specialized versions of this that
only execute the provided function/lambda if the coroutine is canceled by the
latent action manager for a certain reason.
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

The return values of these functions are movable, game thread only, and support
multiple concurrent co_awaits.

<sup>
There are known issues with Latent::Chain on older versions of MSVC (VS2019)
that result in incorrectly-compiled code.
Calling Chain will issue compile-time warnings if this is detected.
VS2022 and Clang seem to be unaffected and are recommended for C++20 overall.
ChainEx may be used as a workaround if you cannot update.
</sup>

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

The UE5Coro::Tasks namespace provides a convenience function (MoveToTask) to
move to a TTask without having to use the lambda syntax.
The return value of MoveToTask is copyable, thread-safe, and reusable.

Latent coroutines will need to `co_await Async::MoveToGameThread();` at some
later point to return to the game thread and correctly complete.

## Animation

UE5Coro\:\:Anim contains numerous functions to interact with animation montages,
notifies, etc.
All of these functions "snapshot" the currently-playing instance of the montage
when called and ignore every other instance.

If the calling coroutine is suspended while the animation notify happens, its
notify payload is retrieved and returned as the value of the co_await expression.
Otherwise if, e.g., you call one of these functions but only co_await the return
value later, it will immediately continue if the notify has happened in between
with no payload.

This limitation is due to `FBranchingPointNotifyPayload` in the engine
containing pointers to UObjects without an accompanying UPROPERTY().
If you need information from the payload, make sure to read it before the next
co_await and store values appropriately, e.g., in TStrongObjectPtr local
variables.

FNames or bInterrupted flags from other functions in this namespace are always
valid, none of this is a concern if you don't actually use the payload.

```cpp
using namespace UE5Coro::Anim;
using namespace UE5Coro::Latent;

// Example 1:
// Guaranteed-valid payload. No time passes on the game thread between the call
// and its co_await. If a notify happened before calling the function, this will
// wait until the next one.
auto [Name, Payload] = co_await PlayMontageNotifyBegin(MyInstance, MyMontage);
auto Awaiter = PlayMontageNotifyBegin(MyInstance, MyMontage);
LengthySubroutine(); // The game thread is not released, notifies cannot happen.
Tie(Name, Payload) = co_await Awaiter; // Still a guaranteed-valid payload
co_await NextTick(); // The game thread is released, invalidating Payload
// Payload is a dangling pointer now!

// Example 2:
auto Awaiter = PlayMontageNotifyBegin(MyInstance, MyMontage);
co_await Seconds(1); // Game time passes, a notify may or may not happen here
auto [Name, Payload] = co_await Awaiter;
if (Payload)
{
    // The notify happened after co_await Awaiter. Payload is valid on this line.
    co_await NextTick(); // The game thread is released, invalidating Payload
    // Game time has passed, Payload is a dangling pointer now!
}
else
    ; // The notify happened after PlayMontageNotifyBegin, before co_await Awaiter
```

The return values of these functions are copyable, game thread only, support one
concurrent co_await, any number of sequential ones, and it's guaranteed that the
second and further co_awaits will NOT have a valid payload pointer.

## HTTP

UE5Coro\:\:Http\:\:ProcessAsync wraps a FHttpRequestRef in an awaiter that
resumes your coroutine when the request is done (including errors).
Unlike OnProcessRequestComplete() this does **not** force you back on the game
thread, but you can start and finish there if you wish of course.

The return type of this function is copyable, thread-safe, supports one
concurrent co_await across all copies, and any number of sequential ones after
that.
