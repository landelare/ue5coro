# Async chain

UE5Coro::Async::Chain calls coroutine-unaware functions that take a delegate
parameter, and resumes the coroutine when that delegate is triggered.

The parameters of the chained function and the delegate must be
_MoveConstructible_.
The delegate's return type must be _DefaultConstructible_ or void.

Async::Chain is mainly useful for engine functions that make a copy of a
pre-bound delegate, and ignore any further changes to it.
Delegates can be [co_awaited directly](Implicit.md#delegates) if the function
only references the delegate, and allows callers to bind or unbind later.

> [!CAUTION]
> Do not chain functions that might call the delegate zero or multiple times.
>
> Incoming callbacks after the first invoke undefined behavior, unless the
> delegate is DYNAMIC or the chained function is guaranteed to honor
> Unbind/Remove on the original delegate that is passed to it.

A direct `co_await` should always be preferred over Async::Chain whenever
possible.
Async::Chain will cleanly unsubscribe from its temporary delegate before
resuming the coroutine, but it assumes that the chained function ignores this.
Expedited cancellation is **NOT** supported, to make sure that the engine's copy
of the bound delegate has something to call, even if the coroutine is already
canceled.
The cancellation will be processed right after the delegate is triggered, unless
cancellation is blocked.

For everything not supported by Async::Chain, or when its suitability or safety
is in doubt, there are safe
[generic workarounds](Implicit.md#generic-workarounds) that can be used instead.

### auto Chain(? (*Function)(?...), A&&... Args)
### auto Chain(T* Object, ? (T::*Function)(?...), A&&... Args)

The full, <!--even more--> cryptic declarations are omitted from the
documentation.
The first overload is for pointers to static functions, the second is for
non-static member functions on classes.

The chained function must take exactly one delegate parameter.
For the list of supported delegate types, see [this page](Implicit.md#delegates).
The delegate parameter may be a value, a reference, or a pointer.

A suitable delegate will be created, bound, and passed to the function, and it
will be called with `Args` forwarded as the rest of its arguments.
The delegate will directly and synchronously call into the coroutine.

The await expression results in an unspecified type that may be used with
structured bindings to optionally receive the delegate's parameters.
Reference parameters are passed through as references, and may be written to.
These writes will go through the delegate call into the original referenced
variables.

The await expression's value may be safely discarded if the coroutine does not
want to receive the parameters.
Using the return value of the await expression in any way that's not part of a
structured binding declaration (including storing the entirety of it in an
`auto` local variable) is not supported.

The validity of references and pointers depend on the delegate's caller, but
even the shortest-lived references will be valid until the next co_await or
co_return.

The chained function's synchronous return value is discarded.
For delegates with non-void return types, a default-constructed value is
returned to the caller at the coroutine's next co_await or co_return.
Even if the coroutine has a different co_return result, the delegate will
receive the default-constructed value.
The delegate's return type and the coroutine's result type are independent.

Example:
```c++
using namespace UE5Coro;

// DECLARE_DYNAMIC_MULTICAST_DELEGATE(FExampleDelegate);
// void ChainMe(FExampleDelegate*);
// void AActress::ChainThis(int, const TDelegate<void(FString)>&, float);

TCoroutine<> AActress::Example()
{
    // Skip the delegate parameter, provide arguments for the rest:
    co_await Async::Chain(&ChainMe);
    auto&& [String] = co_await Async::Chain(this, &AActress::ChainThis, 1, 2.0f);
}
```
