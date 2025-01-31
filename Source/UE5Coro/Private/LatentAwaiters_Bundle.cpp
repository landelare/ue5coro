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

#include "UE5Coro/LatentAwaiter.h"
#include "Engine/AssetManager.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
class FBundleChangeState
{
	TSharedPtr<FStreamableHandle> Handle;

public:
	FBundleChangeState(const TArray<FPrimaryAssetId>& AssetsToChange,
	                   const TArray<FName>& AddBundles,
	                   const TArray<FName>& RemoveBundles,
	                   bool bRemoveAllBundles, TAsyncLoadPriority Priority)
		: Handle(UAssetManager::Get().ChangeBundleStateForPrimaryAssets(
		         AssetsToChange, AddBundles, RemoveBundles, bRemoveAllBundles,
		         FStreamableDelegate(), Priority))
	{
	}

	FBundleChangeState(const TArray<FName>& NewBundles,
	                   const TArray<FName>& OldBundles,
	                   TAsyncLoadPriority Priority)
		: Handle(UAssetManager::Get().ChangeBundleStateForMatchingPrimaryAssets(
		         NewBundles, OldBundles, FStreamableDelegate(), Priority))
	{
	}

	static bool ShouldResume(void* State, bool bCleanup)
	{
		auto* This = static_cast<FBundleChangeState*>(State);
		if (bCleanup)
		{
			delete This;
			return false;
		}

		// This logic matches FLoadAssetActionBase::UpdateOperation(), which is
		// not a perfect match for bundle changes, but
		// UAsyncActionChangePrimaryAssetBundles seems to broadly follow the
		// same logic, except that it does not handle WasCanceled at all.
		auto& Handle = This->Handle;
		return !Handle || Handle->HasLoadCompleted() || Handle->WasCanceled();
	}
};
}

FLatentAwaiter Latent::AsyncChangeBundleStateForPrimaryAssets(
	const TArray<FPrimaryAssetId>& AssetsToChange,
	const TArray<FName>& AddBundles, const TArray<FName>& RemoveBundles,
	bool bRemoveAllBundles, TAsyncLoadPriority Priority)
{
	return FLatentAwaiter(new FBundleChangeState(AssetsToChange, AddBundles,
	                                             RemoveBundles,
	                                             bRemoveAllBundles, Priority),
	                      &FBundleChangeState::ShouldResume, std::false_type());
}

FLatentAwaiter Latent::AsyncChangeBundleStateForMatchingPrimaryAssets(
	const TArray<FName>& NewBundles, const TArray<FName>& OldBundles,
	TAsyncLoadPriority Priority)
{
	return FLatentAwaiter(new FBundleChangeState(NewBundles, OldBundles,
	                                             Priority),
	                      &FBundleChangeState::ShouldResume, std::false_type());
}
