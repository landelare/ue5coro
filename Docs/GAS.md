# UE5CoroGAS

UE5CoroGAS is a separate, optional module that may be used to gain extra
functionality centered around the engine's Gameplay Ability system.
Projects that don't use GAS for whatever reason will not benefit from this.

To use it, enable the engine's GameplayAbilities plugin, reference "UE5CoroGAS"
in your Build.cs, and use `#include "UE5CoroGAS.h"`.

This module uses the special `UE5Coro::GAS::FAbilityCoroutine` return type for
its coroutines.
Coroutines returning this run in a special
[latent mode](Coroutine.md#latent-mode) that handles ability-related events
depending on what class they're in.

The **ONLY** use of this type is being the return type of UE5CoroGAS-provided
pure virtual methods and their overrides.
Returning it from any other coroutine is undefined behavior.

Although it implicitly converts to TCoroutine\<\> (and may be passed around as
that type), obtaining the return value is not straightforward.
It is also unnecessary.
Interaction with these coroutines is mainly done through their GAS base class,
and the additional integrations that this module provides.

## UUE5CoroGameplayAbility

This class provides a convenient base class to implement asynchronous gameplay
abilities using a C++ coroutine.
Instead of overriding ActivateAbility, override the new ExecuteAbility method
with a coroutine.

> [!CAUTION]
> It is undefined behavior to override ExecuteAbility with a subroutine.

Every instancing policy is supported on Unreal Engine 5.3 and 5.4, including it
dynamically changing at runtime.

Starting with 5.5, NonInstanced is not supported.
Using the `AbilitySystem.Fix.AllowNonInstancedAbilities` CVar to get it back is
possible, but not recommended due to engine issues.

The following events are turned into interactions with the ExecuteAbility
coroutine:

* The coroutine completing calls EndAbility.
* Self-canceling acts as calling EndAbility(..., true).
  It's recommended to self-cancel instead of calling CancelAbility, as the
  latter won't be processed until the next co_await.
* Incoming CancelAbility calls cancel the coroutine, which will lead to an
  EndAbility(..., true) call, once processed.
* Incoming EndAbility calls (including from CancelAbility) cancel the coroutine.
* EndAbility replication is controlled by a property on UUE5CoroGameplayAbility
  that may be freely changed at any time.
  It defaults to being replicated.
* FCancellationGuard does **NOT** affect CanBeCanceled (but it may be freely
  overridden with your own custom logic).
  Cancellations will be received, and (unforced ones) deferred until the last
  guard goes out of scope.
* See [below](#garbage-collection-considerations) for notes on unusual garbage
  collection behavior driven by GAS itself that might lead to forced
  cancellations.

You'll need to call CommitAbility from the coroutine, as usual.
You're free to override any other method not marked `final`.
It is assumed that your overrides will call their Super counterparts for correct
operation (except ExecuteAbility, which is PURE_VIRTUAL).

UUE5CoroGameplayAbility is marked NotBlueprintable, but you may reverse this in
your subclasses.
You're responsible for interacting correctly with ExecuteAbility from BP.
The BlueprintImplementableEvents for ActivateAbility/ActivateAbilityFromEvent
will not be called in subclasses, use normal BP gameplay abilities for those.

### auto UUE5CoroGameplayAbility::Task(UObject*, bool bAutoActivate = true)

This protected function takes a UObject* with a single BlueprintAssignable
delegate, and returns an awaiter that will wait for that delegate to be
Broadcast().

`UGameplayTask`s and `UBlueprintAsyncActionBase`s are automatically activated
if `bAutoActivate` is true (the default):

```cpp
using namespace UE5Coro;

GAS::FAbilityCoroutine UExampleGameplayAbility::ExecuteAbility(...)
{
    // This will automatically find and use the EventReceived delegate
    co_await Task(UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(...));
}
```

This wrapper is locked to the game thread, responds to cancellations
immediately, but it also discards delegate parameters.
It is the preferred way to await single-delegate tasks from a gameplay ability
coroutine.
It is also world sensitive, see [this page](Latent.md) for details.

In cases where this is not suitable, activate the task manually, and
[co_await its delegate(s)](Implicit.md#delegates) directly.
This method supports delegate return values, but it will not track worlds.
You'll need to ensure that the coroutine is canceled or otherwise resumed if the
task gets destroyed before it's complete.

## UUE5CoroAbilityTask

This class lets you implement an ability task with a coroutine.
Instead of overriding Activate, override Execute with a coroutine to perform the
task and override Succeeded/Failed to broadcast the delegates the task needs.

> [!CAUTION]
> It is undefined behavior to override Execute with a subroutine.

Due to UnrealHeaderTool limitations, it's not possible to provide a ready-to-go
generic ability task.
Subclasses will need to provide the static UFUNCTION to create the task and
UPROPERTY delegates for completion events.

Unreal expects gameplay tasks to call their delegates _after_ EndTask, so make
sure that they are broadcast from Succeeded or Failed, not at the end of Execute.
GAS itself will mark the task as garbage in EndTask, so `this` will be invalid
(but not garbage collected yet) when Succeeded or Failed runs.

Execute will run in latent mode with the following additional integrations:

* The coroutine completing will call EndTask and one of Succeeded or Failed,
  the latter two being virtuals on `UUE5CoroAbilityTask` with no-op default
  implementations.
  Self-cancellation triggers Failed instead of Succeeded.
* OnDestroy (e.g., from EndTask) will cancel the coroutine.

## UUE5CoroSimpleAbilityTask

This class is a convenience subclass of UUE5CoroAbilityTask that provides a
generic pair of delegates broadcast from Succeeded and Failed, for coroutine
tasks that don't need additional delegates or completion logic.

Subclasses of this class only need to provide the static UFUNCTION for BP on top
of overriding Execute.

# Garbage collection considerations

GAS itself sometimes marks abilities and tasks as garbage.
This is usually in response to EndAbility, EndTask, or other forms of
engine-initiated cancellation, such as PIE ending.
Notably, non-instanced gameplay abilities are not subject to this, since they
run on the CDO.

If GAS marks an object as garbage, that will prompt the latent action manager
to remove its latent actions when it next ticks, including the one that drives
the coroutine behind the scenes.
Since the coroutine can no longer run in this case, it is force canceled,
ignoring FCancellationGuards.
`!IsValid(this)` will be observable, e.g., in destructors called on local
variables inside the coroutine, or other guards such as ON_SCOPE_EXIT.
`UE5Coro::FOnCoroutineCanceled` and `UE5Coro::Latent::FOnObjectDestroyed`
respond to this kind of forced cancellation.

If the coroutine is not on the game thread when it's force canceled, the
cancellation's processing is delayed until the next co_await (as usual), but
it is possible that `this` will have been `delete`d by the time that happens.

To better align with the engine's expectations and match how BP abilities/tasks
would behave, UE5CoroGAS classes have additional behavior changes compared to
"regular" latent mode for forced cancellations:

* UUE5CoroGameplayAbility will not call EndAbility on itself.
* UUE5CoroAbilityTask will not call EndTask, Succeeded, or Failed.
* UUE5CoroSimpleAbilityTask won't Broadcast any of its delegates.
