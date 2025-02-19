// Copyright © Laura Andelare
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted (subject to the limitations in the disclaimer
// below) provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
// THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
// CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
// NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "CoreMinimal.h"
#include "UE5Coro/Definition.h"

#define UE5CORO_PRIVATE_USE_DEBUG_ALLOCATOR 0

#if UE5CORO_DEBUG
/** The utilities in this namespace are sometimes used to debug UE5Coro itself. */
namespace UE5Coro::Private::Debug
{
constexpr int GMaxEvents = 100;
constexpr bool bLogThread = false;

struct FThreadedEventLogEntry
{
	const char* Message;
	uint32 Thread;
	FThreadedEventLogEntry(const char* Message = nullptr)
		: Message(Message), Thread(FPlatformTLS::GetCurrentThreadId()) { }
};
using FEventLogEntry = std::conditional_t<bLogThread,
                                          FThreadedEventLogEntry, const char*>;
extern UE5CORO_API FEventLogEntry GEventLog[GMaxEvents];
extern UE5CORO_API std::atomic<int> GNextEvent;

extern UE5CORO_API std::atomic<int> GLastDebugID;
extern UE5CORO_API std::atomic<int> GActiveCoroutines;

inline void ClearEvents()
{
	std::ranges::fill(GEventLog, FEventLogEntry());
	GNextEvent = 0;
}

void Use(auto&&)
{
}

#define UE5CORO_PRIVATE_DEBUG_EVENT(...) do { \
	namespace D = ::UE5Coro::Private::Debug; \
	D::GEventLog[D::GNextEvent++] = #__VA_ARGS__; \
	FPlatformMisc::MemoryBarrier(); } while (false)
}
#endif
