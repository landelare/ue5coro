# Generators

Returning `TGenerator<T>` allows you to `co_yield` an arbitrary number of values
through it (even infinite!) with the caller having control over when and how many
to fetch. This can be more straightforward and efficient than creating a `TArray`,
the values are generated and returned one at a time.

Given this example generator:
```cpp
using namespace UE5Coro;

TGenerator<int> CountToThree()
{
    co_yield 1;
    co_yield 2;
    co_yield 3;
}
```

You can run it manually using the full API, giving you complete control over
retrieving the value and when the coroutine resumes:
```cpp
TGenerator<int> G1 = CountToThree(); // Runs to co_yield 1; then returns
do
{
    // This check can be omitted if you're guaranteed at least one
    // co_yield from the generator. A generator that instantly co_returns will
    // check()/crash if you attempt to read a non-existent value.
    if (G1)
        int Value = G1.Current();

    // Resume() continues the function until the next co_yield or until
    // control leaves scope. As a convenience it returns operator bool().
} while (G1.Resume());
```

`Current()` returns a `T&` reference to the expression that's evaluated for the
current `co_yield`. Unusually for C++ references, this can be an lvalue reference
to a temporary value (e.g. `co_yield 1+2;` would give you an `int&` to 3, not an
`int&&`). Even though the value is temporary from the coroutine's perspective,
"time is frozen" until it's resumed so the value is guaranteed to be alive and
valid for the caller of `Current()`. You can of course decide to `MoveTemp()` it.

## Wrappers

Attempting to `Resume()` a generator that has completely `co_return`ed is valid, a
no-op, and returns `false`.

You can use generators with UE-style iterators:
```cpp
TGenerator<int> G2 = CountToThree();
for (auto It = G2.CreateIterator(); It; ++It)
    DoSomethingWith(*It);
```

They also work with range-based `for` (or STL algorithms):
```cpp
TGenerator<int> G3 = CountToThree();
for (int Value : G3)
    DoSomethingWith(Value);
```

## Advanced usage

Your caller can stop you at any point so feel free to go wild:
```cpp
TGenerator<uint64> Every64BitPrime()
{
    // You probably don't want to run this on tick...
    for (uint64 i = 2; i <= UINT64_MAX; ++i)
        if (IsPrime(i))
            co_yield i;
}

TGenerator<uint64> Primes = Every64BitPrime();
uint64 Two = Primes.Current();
Primes.Resume();
uint64 Three = Primes.Current();
Primes.Resume();
uint64 Five = Primes.Current();
// Done! Primes going out of scope will destroy the coroutine, i never becomes 6.
```

This can be used for `bKeepGoing`-style generators returning `true`, `true`, ...
until some event happens but see also latent actions that encapsulate this kind of
logic at a higher level in case you're tick-based on the game thread.

## Remarks

If you're used to Unity coroutines or just regular .NET `IEnumerable<T>` and
`yield`, note that unlike C# iterators `TGenerator<T>`s start immediately when
called, not at first `Resume()`.

This behavior fits the semantics of STL and UE iterators better that expect
`begin()`/`CreateIterator()` to already be on the first element as opposed to
`IEnumerator<T>.MoveNext()` moving onto the first element.

It also means that you don't have to create an "inner" generator coroutine in
order to do parameter validation.
