# UE5CoroGAS

This optional extra plugin integrates with the Gameplay Ability System.
It provides coroutine-based replacements of various GAS-related classes with
tighter integration than what would be possible using the public UE5Coro API.

This plugin also introduces the `UE5Coro::GAS::FAbilityCoroutine` return type.
Coroutines returning this run in a special [latent mode](Async.md#latent-mode)
that handles ability-related events depending on what class they're in.

The **ONLY** use of this type is to mark UE5CoroGAS-provided pure virtuals for
C++.
Only use it as the return type when you're overriding these methods, it does
nothing useful anywhere else.

## Gameplay abilities

`UUE5CoroGameplayAbility` provides a convenient base class to implement
abilities using a C++ coroutine.
Instead of overriding ActivateAbility, override the new ExecuteAbility instead.
**Overriding ExecuteAbility with a subroutine is undefined behavior.**

Every instancing policy is supported, including it dynamically changing at
runtime.

The following events are turned into interactions with the ExecuteAbility
coroutine:

* The coroutine completing calls EndAbility.
* Self-canceling with Latent::Cancel() acts as calling EndAbility(..., true).
It's recommended to self-cancel this way.
Calling CancelAbility won't be processed until the next co_await.
* Incoming CancelAbility calls cancel the coroutine, which will lead to an
EndAbility(..., true) call once processed.
* Incoming EndAbility calls (including from CancelAbility) cancel the coroutine.
* EndAbility replication is controlled by a property on UUE5CoroGameplayAbility
that may be freely changed at any time and defaults to replicated.
* FCancellationGuard does **NOT** affect CanBeCanceled (but it may be overridden).
Cancellations will be received and (unforced ones) deferred until the last guard
goes out of scope.
* See [below](#garbage-collection-considerations) for notes on unusual garbage
collection behavior driven by GAS itself that might lead to forced cancellations.

You'll need to call CommitAbility normally from the coroutine as appropriate.
You're free to override any other method not marked `final`.
It is assumed that your overrides will call their Super counterparts for correct
operation.

UUE5CoroGameplayAbility is marked NotBlueprintable, but you may reverse this in
your subclasses.
You're responsible for interacting correctly with ExecuteAbility from BP.
The BlueprintImplementableEvents for ActivateAbility(FromEvent) will not be
called.

### Task awaiter

`UUE5CoroGameplayAbility::Task` takes a UObject* with a single
BlueprintAssignable delegate and wraps it in an awaitable object.
`UGameplayTask`s and `UBlueprintAsyncActionBase`s are automatically activated.

This wrapper is locked to the game thread, responds to cancellations
immediately, but also discards parameters.
It is the preferred way to await single-delegate tasks from a gameplay ability
coroutine.
Consider using something like `Latent::UntilDelegate` instead of co_awaiting the
delegate directly if you need to deal with multiple UPROPERTYs.
co_awaiting the delegate is fully supported, but it can lead to memory leaks if
it never activates.

## Ability tasks

`UUE5CoroAbilityTask` lets you implement an ability task with a coroutine.
Instead of overriding Activate, override Execute with a coroutine to perform the
task and Succeded/Failed to broadcast the delegates your task needs.<br>
**It is undefined behavior to override Execute with a subroutine.**

Due to UnrealHeaderTool limitations, it's not possible to provide a ready-to-go
generic ability task.
Your subclass will need to provide the static UFUNCTION to create the task and
UPROPERTY delegates for completion events.

`UUE5CoroSimpleAbilityTask` provides a generic pair of delegates corresponding
to Succeeded and Failed, and is recommended as the base class for coroutine
tasks that don't need additional delegates.
You only need to provide the static UFUNCTION.

Unreal expects a gameplay task to call its delegates _after_ EndTask, so make
sure that they are broadcast from Succeeded or Failed, not Execute.
GAS itself will mark the task as garbage in EndTask, so `this` will not be valid
(but also not garbage collected yet) when Succeeded or Failed runs.

Execute will run in latent mode with the following additional integrations:

* The coroutine completing will call EndTask and one of Succeeded or Failed,
the latter two being virtuals on `UUE5CoroAbilityTask` with no-op default
implementations.
`UUE5CoroSimpleAbilityTask` broadcasts the delegate corresponding to the method.
Self-cancel with `Latent::Cancel` to trigger Failed instead of Succeeded.
* OnDestroy (e.g., from EndTask) will cancel the coroutine.
* See [below](#garbage-collection-considerations) for notes on unusual garbage
collection behavior driven by GAS itself that might lead to forced cancellations.

# Garbage collection considerations

GAS itself sometimes marks abilities and tasks as garbage.
This is usually in response to EndAbility, EndTask, or other forms of
engine-initiated cancellation, such as PIE ending.
Notably, non-instanced gameplay abilities are not subject to this, since they
run on the CDO.

If GAS marks your object as garbage, that will prompt the latent action manager
to remove its latent actions when it next ticks, including the one that drives
the coroutine behind the scenes.
Since the coroutine can no longer run in this case, it is force canceled,
ignoring FCancellationGuards.
`!IsValid(this)` will be observable, e.g., in destructors called on local
variables inside the coroutine, or other guards such as ON_SCOPE_EXIT.
`Latent::FOnObjectDestroyed` responds to this kind of forced cancellation.

If the coroutine is not on the game thread when it's force canceled, the
cancellation's processing is delayed until the next `co_await` as usual, but it
is possible that `this` is deleted by the time that happens.

To better align with the engine's expectations and match how BP abilites/tasks
would behave, FAbilityCoroutine's cancellation processing slightly differs from
a regular latent TCoroutine's.

In case of a forced cancellation:
* UUE5CoroGameplayAbility does not call EndAbility on itself.
* UUE5CoroAbilityTask does not call EndTask, Succeeded, or Failed.
* UUE5CoroSimpleAbilityTask doesn't Broadcast any of its delegates.
