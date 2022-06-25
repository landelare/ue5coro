# Awaiters

This page gives an overview of the various awaiters that come with the plugin.
This is not meant to be an exhaustive documentation (read the comments in the
header files for that).

## Async tasks

`UE5Coro::Async` contains awaiters that let you conveniently move execution
between various threads (notably between the game thread and everything else).

## Latent awaiters

`UE5Coro::Latent` awaiters are implemented as latent actions, even if your
coroutine is otherwise not one. This makes their lifetime tied to the world so
it's possible, e.g., if PIE ends that `co_await`ing them will not resume your
coroutine. In this case your stack is still unwound normally (similarly to if
an exception was thrown) and your destructors are called so it's safe to use
`FScopeLock`s, smart pointers, etc. across a `co_await`, but something like
this could cause problems:

```cpp
T* Thing = new T();
co_await UE5Coro::Latent::Something(); // This may not resume
delete Thing;
```

### Latent callbacks

To help with the situation above, the engine's own `ON_SCOPE_EXIT` can be used
to place code in a destructor, ensuring that it will always run even if the
latent action manager cancels the coroutine.

The types in `LatentCallbacks.h` provide specialized versions of this that only
execute the provided function/lambda if the coroutine is canceled for a certain
reason. Note that a coroutine canceling itself with `Latent::Cancel()` counts
as neither of these.

### Chained latent actions

Most existing latent actions in the engine return `void` so there's nothing
that you could take or store to `co_await` them. There are two wrappers
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

As it is impossible to read `UFUNCTION(Meta)` information at C++ compile time
to figure out which parameter truly is the world context object, `Chain` uses
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
getting debugged. These are marked `[Inline Frame]` or `[Inlined]` (if your
code is optimized, `Development` or above) and all of them tend to point at the
exact same assembly instruction; this entire segment of the call stack is for
display only and can be safely disregarded.

It cannot be guaranteed but it's been verified that the `Chain` wrappers do get
optimized and turn into a regular function call or get completely inlined in
`Shipping`.

#### Rvalue references

Although this doesn't apply to `UFUNCTION`s, passing rvalue references to
`Latent::Chain` is **not** equivalent to passing them straight to the chained
function: the reference that the function will receive will be to a
move-constructed object, not the original. This matters in extremely unusual
scenarios where the caller wants to still access the rvalue object after the
latent function has returned. If for some reason you need exactly this, refer
to the implementation of `Latent::ChainEx` to see how to register yourself with
`UUE5CoroSubsystem` and call the function taking rvalue references directly.

## HTTP

`UE5Coro::Http::ProcessAsync` wraps a FHttpRequestRef in an awaiter that
resumes your coroutine when the request is done (including errors).
Unlike OnProcessRequestComplete() this does **not** force you back on the game
thread (you can start and finish there if you wish of course).
