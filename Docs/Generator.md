# Generators

Returning TGenerator\<T\> from a function allows it to yield an arbitrary number
of values through it (including infinite), with the caller having control over
when, and how many to fetch.
Each time, the function will resume and run until the next `co_yield`, or until
completion.

This can be more straightforward and memory efficient than allocating memory for
a TArray, as the values are generated and returned one at a time, on demand.
Generators can also benefit from compiler
[HALO](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1365r0.pdf).

There are multiple ways to consume a TGenerator.
It has methods for direct manipulation, and it provides a highly-efficient
iterator that is only 1 pointer in size.
The iterator can be used for STL-style, LLVM-style, and Unreal-style loops:
begin() and CreateIterator() are identical.

The canonical name of the iterator's type is `TGenerator<T>::iterator`.
Its internal name from the `UE5Coro::Private` namespace should not appear in
code, as it is subject to change at any time without prior deprecation.
`iterator` will be updated to refer to the new type if/when this happens.

## Manual API

TGenerator itself has numerous methods to directly interact with the function's
execution that it represents.

The following functions may be used together like so:
```cpp
for (TGenerator<int> Gen = Example(); Gen; Gen.Resume())
    UE_LOGFMT(LogTemp, Display, "Current value = {0}", Gen.Current());
```

### explicit TGenerator\<T\>::operator bool() const noexcept

Generators will convert to true as long as they have something in Current, and
become false when the underlying function call has completed.

> [!NOTE]
> There is no "before first yield" state that can be observed through
> TGenerator, unlike C# coroutines based on IEnumerator.
>
> C++ works with an "after last" state, such as the one represented by end().

### bool TGenerator\<T\>::Resume()

Runs the generator for one step.
Returns true if the generator yielded something and false if it has completed.
The last yielded value can be read from Current().
Calling this method invalidates every active iterator.

Resuming a generator that has already completed is safe: it will do nothing and
keep returning false.

### T& TGenerator\<T\>::Current() const

Returns a reference to the value the generator is currently co_yielding.
Calling Current() on a generator that has completed is undefined behavior.

The generator is inside the co_yield expression as this happens, so even
temporaries are accessible as lvalue references:

```cpp
using namespace UE5Coro;

TGenerator<FString> Example()
{
    co_yield TEXT("Hello!"); // The FString is a temporary here...
}

// ...but the FString will be alive for as long as the function is frozen
// in the middle of co_yield, making it suitably long-lived here:
TGenerator<FString> Generator = Example();
if (Generator)
{
    FString& String = Generator.Current();
    SomethingThatTakesFStringRef(String);
    // Still alive!
}
// This call will invalidate the reference
Generator.Resume();
```

These values can be moved out of the generator if desired, assuming only one
thread does it at most once.
The value will be moved directly out of the co_yield expression.
There are no copies made, or extra storage provided by TGenerator for the value,
for maximum efficiency.

## STL-style iterator API

The usual pair of begin() and end() are provided, std::begin and std::end will
pick these up.
They may be used standalone, or through range-based for:

```cpp
for (auto& Value : SomeGenerator())
    DoSomethingWith(Value);
```

This construct (the generator living entirely in a range-based for loop) is the
best-case scenario for compiler optimizations, and is recommended as the main
way of consuming TGenerators.

### iterator TGenerator\<T\>::begin() noexcept

Returns an iterator representing the generator's **current(!)** state.
There is no rewind functionality.
This function behaves identically to CreateIterator().

An iterator that moves to the end of the generator with ++ becomes equal to
end().
Using prefix * or -> on an iterator equal to end() is undefined behavior.

Copying iterators is allowed, but attempting to manipulate a generator from
multiple iterators is undefined behavior.
Once a copy, or another call to begin() or CreateIterator() is made, all other
iterators should be considered invalid.
Likewise, calling ++ on an iterator manipulates the generator directly and
invalidates all other active iterators.

A postfix ++ operator is provided for tradition, but it returns void, and acts
as a preincrement.
Returning a copy of the generator state before it was resumed is impossible.

### iterator TGenerator\<T\>::end() const noexcept

Returns a sentinel iterator that compares equal to iterators that have moved off
the end of a generator.
These iterators are not invalidated by the generator changing state, and may be
freely copied.

This makes LLVM-style for loops supported (but unnecessary):
```cpp
auto Generator = SomeGenerator();
for (auto i = Generator.begin(), e = Generator.end(); i != e; ++i)
    DoSomethingWith(*i);
```

## Unreal-style iterator API

It would be remiss of an Unreal plugin to not support Unreal-style iteration.

### iterator TGenerator\<T\>::CreateIterator() noexcept

Returns an iterator representing the generator's **current(!)** state.
There is no rewind functionality.
This function behaves identically to begin().

The iterator can convert to bool, and it has ++ operators to support the Unreal
style of iteration:

```cpp
for (auto It = SomeGenerator.CreateIterator(); It; ++It)
    DoSomethingWith(*It);
```

The same remarks apply as to the STL-style iterator API regarding usage.
A UE-style iterator that converts to false is equivalent to one equal to end(),
representing that the backing TGenerator has completed, and that it no longer
has a value in Current().
