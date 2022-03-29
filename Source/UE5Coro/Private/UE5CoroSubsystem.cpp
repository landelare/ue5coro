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

#include "UE5Coro/UE5CoroSubsystem.h"

FLatentActionInfo UUE5CoroSubsystem::MakeLatentInfo()
{
	checkf(IsInGameThread(), TEXT("Unexpected latent info off the game thread"));
	// Using INDEX_NONE linkage and next as the UUID is marginally faster due
	// to an early exit in FLatentActionManager::TickLatentActionForObject.
	return {INDEX_NONE, NextLinkage++, TEXT("None"), GetWorld()};
}

FLatentActionInfo UUE5CoroSubsystem::MakeLatentInfo(bool* Done)
{
	checkf(IsInGameThread(), TEXT("Unexpected latent info off the game thread"));
	int32 Linkage = NextLinkage++;
	checkf(!Targets.Contains(Linkage), TEXT("Unexpected linkage collision"));
	Targets.Add(Linkage, Done);
	return {Linkage, Linkage, TEXT("ExecuteLink"), this};
}

void UUE5CoroSubsystem::ExecuteLink(int32 Link)
{
	*Targets.FindAndRemoveChecked(Link) = true;
}
