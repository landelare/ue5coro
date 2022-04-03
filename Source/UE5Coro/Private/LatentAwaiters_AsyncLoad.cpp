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

#include "Engine/StreamableManager.h"
#include "UE5Coro/LatentAwaiters.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
struct FLatentLoader
{
	FStreamableManager Manager;
	TSharedPtr<FStreamableHandle> Handle;

	explicit FLatentLoader(const auto& Path)
	{
		Handle = Manager.RequestAsyncLoad(Path.ToSoftObjectPath());
	}
};

bool ShouldResume(void*& Loader, bool bCleanup)
{
	auto* This = static_cast<FLatentLoader*>(Loader);

	if (bCleanup) [[unlikely]]
	{
		delete This;
		return false;
	}

	auto& Handle = This->Handle;

	// This is the same logic that Kismet uses
	return !Handle.IsValid() ||
		Handle->HasLoadCompleted() ||
		Handle->WasCanceled();
}
}

FLatentAwaiter Latent::AsyncLoadObject(TSoftObjectPtr<UObject> Ptr)
{
	return FLatentAwaiter(new FLatentLoader(Ptr), &ShouldResume);
}

FLatentAwaiter Latent::AsyncLoadClass(TSoftClassPtr<UObject> Ptr)
{
	return FLatentAwaiter(new FLatentLoader(Ptr), &ShouldResume);
}
