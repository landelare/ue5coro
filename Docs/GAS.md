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
