# UE5Coro

UE5Coro implements C++20
[coroutine](https://en.cppreference.com/w/cpp/language/coroutines) support for
Unreal Engine 5 with a focus on gameplay logic, convenience, and providing
seamless integration with the engine.

> [!NOTE]
> Support for C++17, older compilers, platforms, and engine versions is
> available in the legacy UE5Coro 1.x series.

There's built-in support for easy authoring of latent UFUNCTIONs.
Change the return type of a latent UFUNCTION to make it a coroutine, and you get
all the FPendingLatentAction boilerplate for free, with BP-safe multithreading
support out of the box:
```cpp
UFUNCTION(BlueprintCallable, meta = (Latent, LatentInfo = LatentInfo))
FVoidCoroutine Example(FLatentActionInfo LatentInfo)
{
    UE_LOGFMT(LogTemp, Display, "Started");
    co_await UE5Coro::Latent::Seconds(1); // Does not block the game thread!
    UE_LOGFMT(LogTemp, Display, "Done");
    co_await UE5Coro::Async::MoveToThread(ENamedThreads::AnyThread);
    auto Value = HeavyComputation();
    co_await UE5Coro::Async::MoveToGameThread();
    UseComputedValue(Value);
}
```

This coroutine will automatically track its target UObject across threads, so
even if its owning object is destroyed before it finishes, it won't crash due to
a dangling `this` on the game thread.

Even the coroutine return type is hidden from BP to not disturb designers:

![Latent Blueprint node for the Example function above](Docs/latent_node.png)

Not interested in latent UFUNCTIONs?
Not a problem.
Raw C++ coroutines are also supported, with the exact same feature set.
The backing implementation is selected at compile time; latent actions are
only created if you actually use them.

Change your return type to one of the coroutine types provided by this plugin,
and complex asynchronous tasks that would be cumbersome to implement yourself
become trivial one-liners that Just Work™, eliminating the need for callbacks
and other handlers.

* Enjoying the convenience of LoadSynchronous, but not the drawbacks?<br>
  `UObject* HardPtr = co_await AsyncLoadObject(SoftPtr);` lets you keep only the
  benefits (and your FPS).
* What about spreading a heavy computation across multiple ticks?<br>
  Add a `co_await NextTick();` inside a loop, and you're already done.
  There's a time budget class that lets you specify the desired processing time
  directly, and let the coroutine dynamically schedule itself.
* Speaking of dynamic scheduling, throttling can be as simple as this:<br>
  `co_await Ticks(bCloseToCamera ? 1 : 2);`
* Why time slice on the game thread when you have multiple CPU cores eager to
  work?<br>
  Add `co_await MoveToTask();` to your function, and everything after that line
  will run within the UE\:\:Tasks system on a worker thread.<br>
* Want to go back?
  It's `co_await MoveToGameThread();`.<br>
  You're free to arbitrarily move between threads, and latent UFUNCTION
  coroutines automatically move back to the game thread when they're done to
  resume BP.
* Still not convinced? Here's how to run an entire timeline:<br>
  `co_await Timeline(this, From, To, Duration, YourUpdateLambdaGoesHere);`
* Hard to please?
  Here's how you can asynchronously wait for a DYNAMIC delegate without writing
  a UFUNCTION just for the AddDynamic/BindDynamic, or being in a UCLASS, or any
  class at all:<br>
  `co_await YourDynamicDelegate;` (that's the entire code)
* Oh, you wanted parameters with that delegate?<br>
  `auto [Your, Parameters] = co_await YourDynamicDelegate;`

This should give a taste of the significant reduction in code and effort that's
possible with this plugin.
Less and simpler code to write generally translates to fewer bugs, and
asynchronous code being easy to write means there's no friction when it comes to
doing things the right way, right away.

Say goodbye to that good-enough-for-now™ LoadSynchronous that's still in
Shipping, two updates later.
With the task at hand reduced from "write all the StreamableManager boilerplate
and move a part of the calling function into a callback" to merely "stick
co_await in front of it", you'll finish quicker than it would've taken to come
up with a justification for why the synchronous blocking version is somehow
acceptable.

There are plenty of additional features in the plugin, such as generators that
let you avoid allocating an entire TArray just to return a variable number of
values.
Easier for you to write, easier for the compiler to
[optimize](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1365r0.pdf),
you only need O(1) storage instead of O(N) for N items, what's not to like?

# List of features

The following links will get you to the relevant pages of the documentation.
Bookmark this section if you prefer to read the latest documentation in your
browser, or read it straight from your IDE.
Every API function is documented in the C++ headers, and the releases contain
the Markdown source of the documentation that you're reading right now.

## Coroutine authoring

These features focus on exposing coroutines to the rest of the engine.

* [Coroutines](Docs/Coroutine.md) (obviously)
  * [Cancellation](Docs/Cancellation.md) support has its own page.
* [Generators](Docs/Generator.md)
* [Gameplay Ability System](Docs/GAS.md) integration works slightly differently.
<!-- There is an additional, unlisted documentation page: Private.md -->

## Unreal integration

These wrappers provide convenient ways to consume engine features from your
coroutines.

* [AI](Docs/AI.md) integration (MoveTo, pathfinding...)
* [Animation awaiters](Docs/Animation.md) (montages, notifies...)
* [Async awaiters](Docs/Async.md) (multithreading, synchronization...)
* [HTTP](Docs/Http.md) (asynchronous HTTP requests)
* [Implicit awaiters](Docs/Implicit.md) (certain engine types are directly
  co_awaitable, without wrappers)
* [Latent awaiters](Docs/Latent.md) (game thread interaction, Delay...)
  * [Asset loading](Docs/LatentLoad.md) (async loading soft pointers, bundles...)
  * [Async collision queries](Docs/LatentCollision.md) (line traces, overlap checks...)
  * [Latent chain](Docs/LatentChain.md) (universal latent action wrapper)
  * [Tick time budget](Docs/LatentTickTimeBudget.md) (run for x ms per frame)
* [Latent callbacks](Docs/LatentCallback.md) (interaction with the latent
  action manager)

> [!NOTE]
> Most of these functions return undocumented internal types from the
> `UE5Coro::Private` namespace.
> Client code should not refer to anything from this namespace directly, as
> everything within is subject to change in future versions, **without** prior
> deprecation.
>
> Most often, this is not an issue: for example, the unnamed temporary object in
> `co_await Something()` does not appear in source code.
> If a `Private` return value needs to be stored, use `auto` (or a constrained
> `TAwaitable auto`) to avoid writing the type's name.
>
> Directly calling the public-by-necessity C++ awaitable functions
> `await_ready`, `await_suspend`, and `await_resume` is not supported on any
> awaiter.

## Additional features

* [Aggregate awaiters](Docs/Aggregate.md) (WhenAny, WhenAll, Race...)
* [Latent timelines](Docs/LatentTimeline.md) (smooth interpolation on tick)
* [Threading primitives](Docs/Threading.md) (semaphores, events...)

# Installation

Only numbered releases are supported.
Do not use the Git branches directly.

Download the release that you've chosen, and extract it into your project's
Plugins folder.
Rename the folder to just UE5Coro, without a version number.
Done correctly, you should end up with
`YourProject\Plugins\UE5Coro\UE5Coro.uplugin`.

> [!NOTE]
> Please refer to the release's own README if you're using 1.x.
> It had a different method of installation involving multiple plugins.

## Project setup

Your project might use some legacy settings that need to be removed to unlock
C++20 support, which otherwise comes as standard in new projects made in
Unreal Engine 5.3 or later.

In your **Target.cs** files (all of them), make sure that you're using the
latest settings and include order version:

```c#
DefaultBuildSettings = BuildSettingsVersion.Latest;
IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
```

If you're using the legacy `bEnableCppCoroutinesForEvaluation` flag, that's not
needed anymore, and it should no longer be explicitly turned on; doing so may
cause issues.
It is recommended to remove all references to it from your build files.

If you're setting `CppStandard` to `CppStandardVersion.Cpp17` in a Build.cs
file... don't :)

## Usage

Reference the `"UE5Coro"` module from your Build.cs as you would any other
C++ module, and use `#include "UE5Coro.h"`.
The plugin itself does not need to be enabled.

Some functionality is in optional modules that need to be referenced separately.
For instance, Gameplay Ability System support needs `"UE5CoroGAS"` in Build.cs,
and `#include "UE5CoroGAS.h"`.
The core UE5Coro module only depends on engine modules that are enabled by
default.

> [!IMPORTANT]
> Do not directly #include any other header, only the one matching the module's
> name.
> Major IDEs used with Unreal Engine are known to get header suggestions wrong.
> If you add UE5Coro.h to your PCH, you can make it available everywhere.

# Updates

To update, delete UE5Coro from your project's Plugins folder, and install the
new version using the instructions above.

# Packaging

Packaging UE5Coro separately (from the Plugins window) is not needed, and not
supported.

# Removal

To remove the plugin from your project, reimplement all your coroutines without
its functionality, remove all references to the plugin and its modules, and add
a core redirect from `/Script/UE5CoroK2.K2Node_UE5CoroCallCoroutine` to
`/Script/BlueprintGraph.K2Node_CallFunction`.
