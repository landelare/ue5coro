# UE5CoroGAS

This optional extra plugin integrates with the Gameplay Ability System.

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
