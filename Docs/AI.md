# UE5CoroAI

This optional extra module integrates with the engine's "classic" AI
functionality, such as the AIModule, NavigationSystem, but not MASS.
It provides awaiters for various tasks performed with these systems, such as
"AI Move To".

Projects that don't use these will not benefit from this module.

> [!IMPORTANT]
> This module is currently in **beta**.
>
> Breaking behavioral changes may occur in future versions if needed for fixes,
> although the API is intended to remain stable.

To use UE5CoroAI, reference "UE5CoroAI" in your Build.cs, and use
`#include "UE5CoroAI.h"`.

Everything detailed on this page is in the `UE5Coro::AI` namespace.
For more details, refer to the comments above these functions' declarations and
the engine functions that they wrap.
All functions and awaiters in this namespace may only be used on the game thread.

### auto FindPath(UObject* WorldContextObject, const FPathFindingQuery& Query, EPathFindingMode::Type Mode)

Starts an async pathfinding operation with `UNavigationSystemV1::FindPathAsync`.

The result of awaiting this function's returned object is a
`TTuple<ENavigationQueryResult::Type, FNavPathSharedPtr>` containing the
pathfinding operation's results.

The returned awaiter is world sensitive, see [this page](Latent.md) for details.

Example:
```cpp
using namespace UE5Coro::AI;

if (auto [Result, Path] = co_await FindPath(this, Query, Mode);
    Result == ENavigationQueryResult::Success)
    DoSomethingWith(Path);
```

### auto AIMoveTo(AAIController* Controller, ? Target, float AcceptanceRadius = -1, EAIOptionFlag::Type StopOnOverlap = EAIOptionFlag::Default, EAIOptionFlag::Type AcceptPartialPath = EAIOptionFlag::Default, bool bUsePathfinding = true, bool bLockAILogic = true, bool bUseContinuousGoalTracking = false, EAIOptionFlag::Type ProjectGoalOnNavigation = EAIOptionFlag::Default)

This function has two overloads: `Target` may be AActor* or FVector.

It invokes `UAITask_MoveTo::AIMoveTo` with the provided parameters.

Awaiting the return value will resume the coroutine when the move finishes for
any reason, including failure.
The await expression will provide the EPathFollowingResult.

The returned awaiter is world sensitive, see [this page](Latent.md) for details.

Example:
```cpp
using namespace UE5Coro::AI;

AActor* Target = GetTarget();
if (co_await AIMoveTo(Controller, Target) == EPathFollowingResult::Success)
    HandOverItem(Target, Item);
```

### auto SimpleMoveTo(AController* Controller, AActor* Target)
### auto SimpleMoveTo(AController* Controller, FVector Target)

These functions behave identically to
`UAIBlueprintHelperLibrary::SimpleMoveToActor` (the AActor* overload) or
`UAIBlueprintHelperLibrary::SimpleMoveToLocation` (the FVector overload),
including their internal hardcoded constants and other quirks, such as injecting
a component into the controller, and return an object that may be awaited to
asynchronously receive the move's result.

This function does not require the controller to be an AI controller.

Awaiting the return value will resume the coroutine when the move finishes for
any reason, including failure.
The await expression will result in the operation's FPathFollowingResult.

The returned awaiter is world sensitive, see [this page](Latent.md) for details.

Example:
```cpp
using namespace UE5Coro::AI;

if (FPathFollowingResult Result = co_await SimpleMoveTo(Controller, Target);
    Result.IsSuccess())
    ArrivedAtTarget.Broadcast();
```
