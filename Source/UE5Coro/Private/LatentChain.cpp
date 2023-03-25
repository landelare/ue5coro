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

#include "UE5Coro/LatentAwaiters.h"
#include "UE5Coro/UE5CoroSubsystem.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

std::tuple<FLatentActionInfo, FTwoLives*> Private::MakeLatentInfo()
{
	auto* Sys = GWorld->GetSubsystem<UUE5CoroSubsystem>();
	// Will be Released by the FLatentAwaiter from the caller
	// and UUE5CoroSubsystem on the latent action's completion.
	auto* Done = new FTwoLives;
	return {Sys->MakeLatentInfo(Done), Done};
}

FLatentChainAwaiter::FLatentChainAwaiter(FTwoLives* Done) noexcept
	: FLatentAwaiter(Done, &FTwoLives::ShouldResume)
{
}

bool FLatentChainAwaiter::await_resume()
{
	// This function being called implies that there's a reference on State.
	const int& UserData = static_cast<FTwoLives*>(State)->UserData;
	checkf(UserData == 0 || UserData == 1, TEXT("Unexpected user data"));
	// ExecuteLink sets this, otherwise it's 0. This is the only usage currently.
	return UserData == 1;
}
