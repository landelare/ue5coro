# Threading primitives

These classes provide replacements for their built-in Unreal versions, with
coroutine support.

## FAwaitableEvent

This class behaves similarly to Unreal Engine's FEvent/FEventRef, but it does
not have Wait() functions; instead, it is directly awaitable.
Every operation is thread safe.

FAwaitableEvent objects are immovable.
Use smart pointers if moves/copies are required.

FAwaitableEvent supports expedited cancellation.
Cancellations are processed on the same kind of named thread as the one that
called Cancel().

### FAwaitableEvent::FAwaitableEvent(EEventMode Mode = EEventMode::AutoReset, bool bInitialState = false)

Initializes a new event in the specified mode and initial state.
Defaults to a dormant auto-reset event, matching FEvent and FEventRef.

### void FAwaitableEvent::Trigger()

Triggers the event.
If there are eligible coroutine(s) awaiting this event, they will be resumed
directly from this call, on the caller's thread.

EEventMode::AutoReset events will clear themselves after this, and it is
guaranteed that only one coroutine will be resumed per Trigger.

EEventMode::ManualReset events let everything through that's currently awaiting,
even if the event gets reset after Trigger is called, but before it returns.
In this case, coroutines that await this event after the Reset() call will
suspend.

### void FAwaitableEvent::Reset()

Resets the event, which will cause subsequent awaits to suspend their coroutines
until the event is next triggered.

### bool FAwaitableEvent::IsManualReset() const noexcept

Returns true if this event was created as EEventMode::ManualReset.

## FAwaitableSemaphore

This class behaves similarly to std::counting_semaphore, but it does not have an
acquire() method; instead, it is directly awaitable, which will acquire 1 count.
Every operation is thread safe.

Unreal Engine has multiple, significantly different FSemaphore types, the most
common being a rather heavy, named interprocess semaphore on Windows.
Interprocess usage is not supported.

FAwaitableSemaphore objects are immovable.
Use smart pointers if moves/copies are required.

FAwaitableSemaphore supports expedited cancellation.
Cancellations are processed on the same kind of named thread as the one that
called Cancel().

### FAwaitableSemaphore::FAwaitableSemaphore(int Capacity = 1, int InitialCount = 1)

Initializes a new semaphore with the specified capacity and initial count.
Defaults to an unlocked binary semaphore.

Capacity must be positive, and InitialCount cannot be negative or greater than
Capacity.

### void FAwaitableSemaphore::Unlock(int InCount = 1)

Unlocks (releases) this semaphore the specified number of times, defaulting to
one.
This will resume at most InCount coroutines currently awaiting this semaphore.

Unlocking the semaphore above its capacity results in undefined behavior.
