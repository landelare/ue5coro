# Generators

Returning TGenerator\<T\> allows you to co_yield an arbitrary number of values
through it (even infinite!) with the caller having control over when and how
many to fetch.
This can be more straightforward and efficient than creating a TArray, as the
values are generated and returned one at a time on demand.

There are also significant compiler optimizations available if, e.g., the
generator is immediately used in a for loop, iterated over, and discarded
(mostly inlining and HALO).
The underlying C++ language feature was designed to scale down to embedded
devices.

Given this example generator...

```cpp
using namespace UE5Coro;

TGenerator<int> CountToThree()
{
    co_yield 1;
    co_yield 2;
    co_yield 3;
}
```

## Manual API

...you can run it manually using the full API, giving you complete control over
retrieving the value and when the coroutine resumes.
Make sure you check for generator validity if it's not guaranteed that it will
co_yield something, reading the current value when there isn't any will crash!

```cpp
TGenerator<int> G1 = CountToThree(); // Runs to co_yield 1; then returns
do
{
    if (G1) // Check!
        int Value = G1.Current(); // will receive 1, 2, 3
// Resume() continues the function. For convenience it returns validity.
} while (G1.Resume());
```

Attempting to Resume() a generator that has completely co_returned is valid, a
no-op, and returns false.

Current() returns a reference to the expression that's evaluated for the current
co_yield.
Unusually for C++ references, this **can** be an lvalue reference to a temporary
value (e.g. `co_yield 1 + 2;` would give you an int& to 3, not an int&&).
Even though the value is temporary from the generator coroutine's perspective,
"time is frozen" until it's resumed so the value is guaranteed to be alive and
valid for the caller of Current.
You can of course decide to MoveTemp/std::move it out of the expression.

## Iterators

...you can also use generators with UE-style iterators:

```cpp
TGenerator<int> G2 = CountToThree();
for (auto It = G2.CreateIterator(); It; ++It)
    DoSomethingWith(*It);
```

...or STL-style, including range-based for:

```cpp
TGenerator<int> G3 = CountToThree();
for (int Value : G3)
    DoSomethingWith(Value);
```

Using these common patterns to write a for loop naturally includes the necessary
checks before reading the generator's current value.
Note that the postfix `++` returns void to avoid copying the entire function
state.
It behaves exactly like the prefix `++` for generators.

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
until some event happens but see also latent actions and awaiters that
encapsulate this kind of logic at a higher level in case you're tick-based on
the game thread.

## Remarks

If you're used to Unity coroutines or just regular .NET IEnumerable\<T\> and
yield, note that unlike C# iterators TGenerators start immediately when
called, not at the first Resume.

This behavior fits the semantics of STL and UE iterators better that expect
begin()/CreateIterator() to already be on the first element as opposed to
IEnumerator\<T\>.MoveNext() moving onto the first element.

This also means that you don't have to create an "inner" generator coroutine in
order to do parameter validation, like it's often suggested in C#.
