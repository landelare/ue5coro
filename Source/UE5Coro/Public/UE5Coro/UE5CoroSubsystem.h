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
#include "UE5Coro/Definitions.h"
#include "Engine/LatentActionManager.h"
#include "Subsystems/WorldSubsystem.h"
#include "UE5CoroSubsystem.generated.h"

namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FTwoLives
{
	std::atomic<int> RefCount = 2;

public:
	int UserData = 0;

	bool Release(); // Dangerous! Only call externally exactly once!

	// Generic implementation for FLatentAwaiter
	static bool ShouldResume(void*& State, bool bCleanup);
};
}

/**
 * Subsystem supporting some async coroutine functionality.<br>
 * You never need to interact with it directly.
 */
UCLASS(Hidden)
class UE5CORO_API UUE5CoroSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<int32, class UUE5CoroChainCallbackTarget*> ChainCallbackTargets;
	int32 NextLinkage = 0;
	FDelegateHandle LatentActionsChangedHandle;

public:
	/** Creates a unique LatentInfo that does not lead anywhere. */
	FLatentActionInfo MakeLatentInfo();

	/** Creates a LatentInfo suitable for the Latent::Chain* functions. */
	FLatentActionInfo MakeLatentInfo(UE5Coro::Private::FTwoLives* State);

#pragma region UTickableWorldSubsystem overrides
	virtual void Deinitialize() override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
#pragma endregion

private:
	void LatentActionsChanged(UObject* Object, ELatentActionChangeType Change);
};
