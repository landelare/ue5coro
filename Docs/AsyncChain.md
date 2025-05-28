# Async chain

UE5Coro::Async::Chain calls coroutine-unaware functions that take a delegate
parameter, and resumes the coroutine when that delegate is triggered.

It's mainly useful for engine functions that make a copy of a pre-bound
delegate, and ignore any further changes to it.
Delegates can be [co_awaited directly](Implicit.md#delegates) if this is not a
problem, which should always be preferred over Async::Chain whenever possible.

For everything else that isn't supported by one of these features, see the
[generic workarounds](Implicit.md#generic-workarounds) that may be used instead.

> [!CAUTION]
> Async chaining does **NOT** support expedited cancellation, to make sure that
> the engine's copy of the delegate has something to call.
> 
> Only one delegate callback is supported.
> Do not use Chain for functions that might call their delegate parameter
> zero or multiple times.
> The former will lead to the coroutine getting stuck forever, the latter to
> undefined behavior.

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
