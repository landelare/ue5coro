# Latent chain

These functions call coroutine-unaware latent functions from C++ and let callers
await their execution, enabling usage similar to chaining 🕒 nodes in Blueprint.

Their return types satisfy the TLatentAwaiter concept, therefore
[latent](Latent.md#latent-awaiters) rules apply.
Additionally, these functions require GWorld to be valid when called, as it will
be used to register the latent action that the called functions create.
GWorld is usually valid during the execution of game-thread-bound gameplay code.

Only latent actions are supported.
🕒 nodes created through other methods, e.g., UBlueprintAsyncActionBase are not
supported.
See [delegate support](Implicit.md#delegates) for awaiting the delegates of
these.

The returned awaiter assumes that the chained latent action is world sensitive,
see [this page](Latent.md) for details.

### auto Chain(? (*Function)(?...), T&&... Args)
### auto Chain(U* Object, ? (U::*Function)(?...), T&&... Args)

The full, <!--even more--> cryptic declarations are omitted from the
documentation.
The first overload is for static UFUNCTIONs, the second is for non-static member
UFUNCTIONs.

Calling these functions is similar to delegate bindings, but the world context
and FLatentActionInfo parameters **must be skipped** when providing arguments.
These will be automatically inserted.

The functions return an object that can be co_awaited from the game thread to
resume the calling coroutine when the latent action has finished.

The await expression will result in `true` if the latent action finished
successfully, i.e., it would have continued its caller BP on its output exec pin;
and `false` if it failed, i.e., it completed, but it would not have resumed BP.

For latent actions with customized K2Nodes, the K2Node behavior is **not**
respected, only the core latent action that's created when the function is
called from C++.

Example calls:
```cpp
using namespace UE5Coro::Latent;

// For actual delays, UE5Coro::Latent::Seconds should be used instead, which
// is designed for C++ and more efficient to await.
// This engine function is static:
// static void Delay(const UObject* WorldContextObject, float Duration,
//                   FLatentActionInfo LatentInfo)
bool bSuccess = co_await Chain(&UKismetSystemLibrary::Delay, 1.0f);

// This engine function is not static:
// void OpenSourceLatent(const UObject* WorldContextObject,
//     FLatentActionInfo LatentInfo, UMediaSource* MediaSource,
//     const FMediaPlayerOptions& Options, bool& bSuccess)
co_await Chain(MyMediaPlayer, &UMediaPlayer::OpenSourceLatent, MyMediaSource,
               MyOptions, bSuccess);
```

WorldContextObject and LatentInfo detection is based on compile-time heuristics:
* The first UObject\* or UWorld\* parameter (including pointers to const) is
  assumed to be the world context object, and will receive GWorld.
* `this` is not considered for automatic matching, and it's explicitly passed
  into the member function pointer overload of Chain.
* The first FLatentActionInfo parameter (including const FLatentActionInfo&) is
  assumed to be _the_ latent info parameter, and will receive a suitable
  generated value.

The remaining parameters are taken from the Chain call, and forwarded through.
Lvalue "out" references are respected: the chained call will write back to the
original variable, which means that the reference must be valid throughout the
entire execution of the chained latent action.
This is often the case if it's local to the coroutine, or a member of its object.

Writes to rvalue references (not relevant for UFUNCTIONs) are **not** propagated
out of Chain, and it is recommended to not chain functions taking rvalue
references due to the subtle lifetime bugs that this introduces.
Most engine latent UFUNCTIONs take only values and/or lvalue references.

These heuristics cover the vast majority of latent UFUNCTIONs in the engine, but
they're not perfect.
If a function looks different from what is expected, use the following function:

### auto ChainEx(? Function, T&&... Args)

This function behaves identically to Chain, but without automatic parameter
matching, as a last resort for unusual functions that Chain does not cover.

Provide the world context explicitly, or use std::placeholders::_1 for it, and
pass std::placeholders::_2 instead of the latent info parameter.
Only _2 is mandatory.

The parameters are passed the same way as to std::bind, which notably means that
the object for member function pointers comes after the function pointer, and
references need special treatment.
Passing lvalue references requires the values to be wrapped in std::ref() or
std::cref() calls, otherwise the function will reference a (potentially object
sliced) copy.
Passing rvalue references is more or less unsupported.
For more details, see std::bind() itself.

The return value is identical to Chain's, and co_awaiting it will result in the
same bool indicating success.

The Chain examples above, rewritten with ChainEx:
```cpp
using namespace std::placeholders;
using namespace UE5Coro::Latent;

// Instead of Chain(&UKismetSystemLibrary::Delay, 1.0f)
bool bSuccess = co_await ChainEx(&UKismetSystemLibrary::Delay, _1, 1.0f, _2);
// or ChainEx(&UKismetSystemLibrary::Delay, MyWorldContext, 1.0f, _2)

// Instead of Chain(MyMediaPlayer, &UMediaPlayer::OpenSourceLatent,
//                  MyMediaSource, MyOptions, bSuccess)
co_await ChainEx(&UMediaPlayer::OpenSourceLatent, MyMediaPlayer, _1, _2,
                 MyMediaSource, std::cref(MyOptions), std::ref(bSuccess));
// or ChainEx(&UMediaPlayer::OpenSourceLatent, MyMediaPlayer, this, _2,
//            MyMediaSource, std::cref(MyOptions), std::ref(bSuccess))
```
