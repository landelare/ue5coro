# UE5Coro

This plugin implements C\+\+
[coroutines](https://en.cppreference.com/w/cpp/language/coroutines) for
Unreal Engine 5 with a focus on gameplay logic and BP integration.

## Setup

Download the release that you wish to use from the
[Releases](https://github.com/landelare/ue5coro/releases) page, extract it to
your project's Plugins folder and in case you downloaded a source code zip,
rename the folder that it contains to just `UE5Coro` so that you end up with
`YourProject\Plugins\UE5Coro\UE5Coro.uplugin`.

### C++20

In modules where you wish to use coroutines, add or change this line in the
corresponding **Build.cs** file:
```c#
CppStandard = CppStandardVersion.Cpp20;
```

Add `"UE5Coro"` to your dependency module names in the same Build.cs file – up
to you if it's private or public – enable the plugin in the Unreal editor,
and you're ready to go!

### C++17

In your **Target.cs** files for projects where you wish to use coroutines,
add this line:
```c#
bEnableCppCoroutinesForEvaluation = true;
```

No additional per-module setup is needed in this case, add `"UE5Coro"` to your
dependency module names and enable the plugin like any other.

_Potential UE5.1 bug:_ if you're building the engine from source and it seems to
be rebuilding itself for no reason once you've done the Target.cs change above,
edit TargetRules.cs in the engine instead so that this flag is true by default.

## Features

`#include "UE5Coro.h"` gives you every functionality that this plugin provides.
You may opt to IWYU the various smaller headers, but no guidance is given as to
which feature requires which header.

Click these links for the detailed description of the main features provided
by this plugin, or keep reading for a quick overview.

* [Async coroutines](Docs/Async.md) control their own resumption by awaiting
various awaiter objects. They can be used to implement BP latent actions such as
Delay, or as a generic fork in code execution like AsyncTask, but not
necessarily involving multithreading.
  * [Cancellation](Docs/Cancellation.md) support has its own page.
* [Generators](Docs/Generator.md) are caller-controlled and return a variable
number of results without having to allocate and go through a temporary TArray.
* [Overview of built-in awaiters](Docs/Awaiters.md) that you can use with async
coroutines.

### Async coroutines

Return `UE5Coro::TCoroutine<>` from a function to make it coroutine enabled and
support co_await inside.
UFUNCTIONs need to use the `FAsyncCoroutine` wrapper.

Having a `FLatentActionInfo` parameter makes the coroutine implement a BP latent
action.
You do not need to do anything with this parameter, just have it and the plugin
will register it with the latent action manager.
World context objects are also supported and automatically processed.
It's recommended to have them as the first parameter.
Don't forget the necessary UFUNCTION metadata to make this a latent node in BP!

```cpp
UFUNCTION(BlueprintCallable, Meta = (Latent, LatentInfo = "LatentInfo"))
FAsyncCoroutine AExampleActor::Latent(FLatentActionInfo LatentInfo)
{
    // This will *not* block the game thread for a second!
    co_await UE5Coro::Latent::Seconds(1.0);
    OneSecondLater();
}
```

The returned struct has no use in BP and is automatically hidden:
![AExampleActor::Latent as a BP node](Docs/latent_node.png)

You're not limited to BP latent actions, or UCLASS members:

```cpp
UE5Coro::TCoroutine<> MyGlobalHelperFunction()
{
    co_await UE5Coro::Latent::Seconds(1.0);
    OneSecondLater();
}
```

Or even regular functions:

```cpp
void Example(int Value)
{
    auto Lambda = [Value]() -> UE5Coro::TCoroutine<int>
    {
        co_await UE5Coro::Tasks::MoveToTask();
        co_return PerformExpensiveTask(Value);
    };
    int ExpensiveResult = Lambda().GetResult();
}
```

Both BP latent actions and free-running asynchronous coroutines have a unified
feature set: you can seamlessly co_await the same things from both and if
needed, your BP latent action becomes a threading placeholder or additional
behind-the-scenes latent actions are started as needed.

BP Latent actions are considered complete for BP when control leaves the scope
of the coroutine body completely, either implicitly (running to the final `}`)
or explicitly via `co_return;`.

Asynchronous coroutines (in both modes) synchronously return to their callers at
the first co_await or co_return that they encounter and the rest of the function
body runs either independently (in async mode) or through the latent action
manager (in latent mode).

Everything co_awaitable works in every asynchronous coroutine, regardless of its
BP integration:

```cpp
using namespace UE5Coro;

UFUNCTION(BlueprintCallable, Meta = (Latent, LatentInfo = "LatentInfo"))
FAsyncCoroutine UExampleFunctionLibrary::K2_Foo(FLatentActionInfo LatentInfo)
{
    // You can freely hop between threads even though this is BP:
    co_await Async::MoveToThread(ENamedThreads::AnyBackgroundThreadNormalTask);
    DoExpensiveThingOnBackgroundThread();

    // However, awaiting latent actions has to be started from the game thread:
    co_await Async::MoveToGameThread();
    co_await Latent::Seconds(1.0f);
}
```

There are various other engine features with coroutine support including some
engine types that are made directly co_awaitable by the plugin.
Check out the [Awaiters](Docs/Awaiters.md) page for an overview.

### Generators

Generators can be used to return an arbitrary number of items from a function
without having to pass them through temp arrays, etc.
In C# they're known as iterators.

Returning `UE5Coro::TGenerator<T>` makes a function coroutine enabled,
supporting `co_yield`:

```cpp
using namespace UE5Coro;

TGenerator<FString> MakeParkingSpaces(int Num)
{
    for (int i = 1; i <= Num; ++i)
        co_yield FString::Printf(TEXT("🅿️ %d"), i);
}

// Elsewhere
for (const FString& Str : MakeParkingSpaces(123))
    Process(Str);
```

co_yield and co_await cannot be mixed.
Asynchronous coroutines control their own execution and wait for certain events,
while generators are caller-controlled and yield values on demand.

In particular, it's not guaranteed that your generator function body will even
run to completion if your caller decides to stop early.
This enables scenarios where generators may co_yield an infinite number of
elements and callers only taking a finite few:

```cpp
using namespace UE5Coro;

TGenerator<int> NotTrulyInfinite()
{
    FString WillBeDestroyed = TEXT("Read on");
    int* Dangerous = new int;
    for (;;)
        co_yield 1;
    delete Dangerous;
}

// Elsewhere:
TGenerator<int> Generator = NotTrulyInfinite();
for (int i = 0; i < 5; ++i)
    Generator.Resume();
```

In this case, your coroutine stack will be unwound when the TGenerator object
is destroyed, and the destructors of locals within the coroutine run like usual,
as if the last `co_yield` was a `throw` (but no exceptions are involved).

In the example above, the FString will be freed but the `delete` line will never
run.
Use RAII or helpers such as `ON_SCOPE_EXIT` if you expect to not run to
completion.
