# Latent timelines

These four built-in coroutines run in latent mode, and as such, they can only
be called from the game thread, and they'll self-cancel if their world context
object is destroyed.

TCoroutine\<\> itself does not directly satisfy the TLatentAwaiter concept,
however, when awaited from a latent coroutine, it will provide latent behavior.
(The temporary awaiter that gets created behind the scenes does satisfy the
concept.)

### TCoroutine<> Timeline(const UObject* WorldContextObject, double From, double To, double Duration, std::function<void(double)> Update, bool bRunWhenPaused = false)
### TCoroutine<> UnpausedTimeline(const UObject* WorldContextObject, double From, double To, double Duration, std::function<void(double)> Update, bool bRunWhenPaused = true)
### TCoroutine<> RealTimeline(const UObject* WorldContextObject, double From, double To, double Duration, std::function<void(double)> Update, bool bRunWhenPaused = true)
### TCoroutine<> AudioTimeline(const UObject* WorldContextObject, double From, double To, double Duration, std::function<void(double)> Update, bool bRunWhenPaused = false)

These four functions will repeatedly call the provided callback on tick on the
game thread, with a value linearly interpolated between `From` and `To`, both
inclusive.
`From` may be larger than `To` for backwards interpolation.

NaNs, infinities, etc. are not handled, but the `ENABLE_NAN_DIAGNOSTIC` engine
macro is respected, and when #defined, these coroutines will collaborate with
the engine, and detect these special values.
This incurs some additional overhead.

The functions' implementations are nearly identical, the differences between
them are which UWorld time they use, and the default value of `bRunWhenPaused`:

|                      |Timeline|UnpausedTimeline|RealTimeline|AudioTimeline|
|----------------------|:------:|:--------------:|:----------:|:-----------:|
|Time dilation         |✅       |✅               |❌           |❌            |
|Pause                 |✅       |❌               |❌           |✅            |
|bRunWhenPaused default|`false` |`true`          |`true`      |`false`      |

✅=respected, ❌=ignored

Running while paused on a timeline that respects pause will repeat the same
value until the world is unpaused.

Example of how this can fit into a larger coroutine (although functionality like
cooldowns should ideally be handled by the [Gameplay Ability System](GAS.md)):
```cpp
using namespace UE5Coro::Latent;

// A half-second linear dash prototype
TCoroutine<> AActress::Dash(FVector From, FVector To, FForceLatentCoroutine = {})
{
    bCanDash = false; // Lock out overlapping dashes while this one is running
    auto Cooldown = Seconds(DashCooldownDuration);
    co_await Timeline(this, 0, 1, 0.5, [&](double Alpha)
    {
        FVector Location = FMath::Lerp(From, To, Alpha);
        SetActorLocation(Location);
    });
    DashComplete.Broadcast();
    co_await Cooldown;
    bCanDash = true;
}
```

> [!TIP]
> If a coroutine's only `co_await` is one of the Timelines, it's often more
> efficient to `return` it instead.
>
> This simplified example does not implement a separate cooldown or lockouts:
> ```cpp
> using namespace UE5Coro::Latent;
>
> TCoroutine<> AActress::Dash(FVector From, FVector To)
> {
>     TCoroutine Coro = Timeline(this, 0, 1, 0.5, [=, this](double Alpha)
>     {
>         FVector Location = FMath::Lerp(From, To, Alpha);
>         SetActorLocation(Location);
>     });
>     Coro.ContinueWithWeak(this, [](AActress* This)
>     {
>         This->DashComplete.Broadcast();
>     });
>     return Coro;
> }
> ```
