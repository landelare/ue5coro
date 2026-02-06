# Animation awaiters

These functions, found in the `UE5Coro::Anim` namespace, let you interact with
montages and their notifies from the game thread.
They're convenient for gameplay tasks that need to react to what happens during
an animation, or to animations themselves changing.

The return values of these functions may be copied, but only one of the copies
may be awaited at one time (as if they were shared pointers to something).

Do not reuse the returned values.
Subsequent awaits are not guaranteed to return a valid value.

### auto MontageBlendingOut(UAnimInstance* Instance, UAnimMontage* Montage)
### auto MontageEnded(UAnimInstance* Instance, UAnimMontage* Montage)

These functions wait for the montage to blend out or end on the provided
instance.
Awaiting them returns a bool indicating if this was caused by an interruption.

The anim instance getting destroyed counts as an interruption.
IsValid(AnimInstance) may be used after the co_await to handle this separately,
if desired.

Example:
```cpp
using namespace UE5Coro::Anim;

TCoroutine<> AActress::Dash()
{
    UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
    AnimInstance->Montage_Play(DashMontage);
    bool bInterrupted = co_await MontageBlendingOut(AnimInstance, DashMontage);
    // Do something after the animation finished
}
```

### auto NextNotify(UAnimInstance* Instance, FName NotifyName)

Waits for the named anim notify to happen on the specified instance, i.e., when
AnimNotify_MyNotifyName() would be called, but without having to implement a
UFUNCTION with a magic name.

Example:
```cpp
using namespace UE5Coro::Anim;

TCoroutine<> AActress::Attack()
{
    UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
    AnimInstance->Montage_Play(AttackMontage);
    co_await NextNotify(AnimInstance, "AttackHit");
    // Do something when the hit landed
}
```

### auto PlayMontageNotifyBegin(UAnimInstance* Instance, UAnimMontage* Montage)
### auto PlayMontageNotifyBegin(UAnimInstance* Instance, UAnimMontage* Montage, FName NotifyName)
### auto PlayMontageNotifyEnd(UAnimInstance* Instance, UAnimMontage* Montage)
### auto PlayMontageNotifyEnd(UAnimInstance* Instance, UAnimMontage* Montage, FName NotifyName)

These functions wait for any (2-parameter overloads) or the specified
(3-parameter overloads) play montage notify of a given montage to begin or end
on the specified instance.
Play montage notifies are different from animation montage notifies.
They are related to branching points in a montage.

Awaiting the return values of these functions will result in a
`const FBranchingPointNotifyPayload*` for the 3-parameter overloads, and
`TTuple<FName, const FBranchingPointNotifyPayload*>` for the 2-parameter
overloads, identifying which play montage notify just began or ended.

The pointer points to engine-managed memory, and as such, it has a limited
lifetime only until the next co_await or co_return.
It might also be nullptr.

Example:
```cpp
using namespace UE5Coro::Anim;

auto [Name, Payload] = co_await PlayMontageNotifyBegin(AnimInstance, Montage);
if (Payload)
{
    ExampleBeginHandler(Payload);
    // The old Payload value becomes dangling, but it's immediately replaced by
    // a new pointer:
    Payload = co_await PlayMontageNotifyEnd(AnimInstance, Montage, Name);
    if (Payload)
    {
        ExampleEndHandler(Payload);
        co_await NextTick();
        // Payload is a non-null, dangling pointer here. Do not use!
    }
}
```
