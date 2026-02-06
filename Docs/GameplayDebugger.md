# Gameplay Debugger integration

UE5Coro comes with built-in support for Unreal Engine's own Gameplay Debugger,
displaying the currently-running coroutines globally, or on the selected actor.
Coroutines can
[cooperate](Coroutine.md#static-void-tcoroutinesetdebugnamefstring-name) to
provide up-to-date debug descriptions of themselves for easier inspection.

A bonus localization feature is included, which can be used independently of the
debugger.

## Setup

The debugger requires UE5Coro to be built with coroutine tracking, which is off
by default, due to its global performance impact.
To enable it, add the following line to your active Target.cs:

```cs
UE5CoroModuleRules.bEnableGameplayDebuggerIntegration = true;
```

This will activate the UE5Coro Gameplay Debugger category, no further steps are
required.

<sup>
Active coroutine tracking is indicated by the UE5CORO_ENABLE_COROUTINE_TRACKING
macro, which may be used to guard code that's only supposed to run in this
specific case.
UE5CORO_DEBUG, or Unreal's built-in UE_BUILD_* macros should be preferred if the
goal is to simply exclude code from Shipping builds.
</sup>

### Optional: enable `conditional` globally

If you'd like to use the `conditional` text formatting modifier for your
project's localization, add this line to **every** Target.cs:

```cs
UE5CoroDebug.bRegisterConditionalTextFormatArgumentModifier = true;
```

Otherwise, UE5Coro will keep it for itself, and stay out of text formatting's
way in Shipping builds.

## Usage

After enabling the Gameplay Debugger and the UE5Coro category (please refer to
Unreal Engine's own Gameplay Debugger documentation on how to do this), the list
of running UE5Coro-managed coroutines across the entire engine will be shown:

![UE5Coro Gameplay Debugger screenshot](GameplayDebugger.avif)

> [!TIP]
> Localization is fully supported for non-English editors.
> The texts are in the UE5Coro namespace, to be gathered from text files in the
> Localization Dashboard. These use the internal `ue5coro_conditional` modifier,
> which is only available together with the Gameplay Debugger menu.

If an actor is selected for debugging, its coroutines are shown separately.
**Only latent coroutines can be associated with an actor.**

The amount of coroutines shown on screen is controlled by the CVars
`UE5Coro.MaxDisplayedCoroutines` and `UE5Coro.MaxDisplayedCoroutinesOnTarget`.

For each entry, the number next to the coroutine mode is its DebugID[^noid],
which is also shown by debuggers with natvis support.

[^noid]: It is unusual, but possible to enable Gameplay Debugger support with a
         non-DEBUG UE5Coro.
         In this case, debug IDs are not generated, and all coroutines will
         appear as #-1.

The quoted description (function names on the screenshot) is an arbitrary
FString controlled by the coroutine, see
[TCoroutine<>::SetDebugName()](Coroutine.md#static-void-tcoroutinesetdebugnamefstring-name)
for examples.

A `[Ticking]` async coroutine is on the game thread, using a temporary latent
action in order to be able to `co_await` a latent awaiter.
For more details, see [Async mode](Coroutine.md#async-mode).

A `[Detached]` latent coroutine is temporarily pinned and detached from the game
thread to prevent an early destruction.
This happens if it awaits a non-latent awaiter, usually to protect the coroutine
from an unexpected garbage collection while it's running on another thread, or
ensuring that a callback has an object to return to.
For more details, see [Latent mode](Coroutine.md#latent-mode).

## The `conditional` modifier

Once [enabled](#optional-enable-conditional-globally), `conditional` excludes
or inserts a formatted substring into the output, based on its argument.
It can be used instead of `gender` or `plural` if the condition is unaffected
by the local culture or grammatical rules.
Nested format strings are supported, but nested parentheses are incorrectly
parsed by Unreal Engine itself.

The following values will result in no output: `false`, `0`, `0U`, `±0.0f`,
`±0.0`,`""`, all genders, and anything else that (culture-awarely) formats to an
empty string.
Every other value will cause the formatted conditional text to be inserted into
the output.

Examples:
```c++
FText Format = LOCTEXT("TitleExample",
    "{Name}'s Adventures{Location}|conditional( in {Location})");

// Alice's Adventures
FText::FormatNamed(Format,
    TEXT("Name"), LOCTEXT("Protagonist", "Alice"),
    TEXT("Location"), INVTEXT(""));

// Alice's Adventures in Wonderland
FText::FormatNamed(Format,
    TEXT("Name"), LOCTEXT("Protagonist", "Alice"),
    TEXT("Location"), LOCTEXT("Setting", "Wonderland"));
```

```c++
FText Format = LOCTEXT("QuoteExample", "{0}|conditional(\"{0}\")");

// "Text", including the quotes
FText::FormatOrdered(Format, INVTEXT("Text"));

// An empty text, not ""
FText::FormatOrdered(Format, INVTEXT(""));
```

> [!IMPORTANT]
> Unreal does not evaluate modifiers without an argument.
> ```c++
> // "123{1}|conditional(hidden?)"
> FText::FormatOrdered(INVTEXT("{0}{1}|conditional(hidden?)"), 123);
>
> // "123"
> FText::FormatOrdered(INVTEXT("{0}{1}|conditional(hidden?)"), 123, false);
> ```
