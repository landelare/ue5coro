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

#if WITH_GAMEPLAY_DEBUGGER_MENU
#include "Category.h"
#include "UE5Coro.h"

#define LOCTEXT_NAMESPACE "UE5Coro"

namespace UE5Coro::Private::Debug
{
TAutoConsoleVariable<int> CVarMaxDisplayedCoroutines(
	TEXT("UE5Coro.MaxDisplayedCoroutines"), 20,
	TEXT("Show at most this many coroutines in the Gameplay Debugger."));

FTextFormat FUE5CoroCategory::CoroutineInfoFormatAsync;
FTextFormat FUE5CoroCategory::CoroutineInfoFormatLatent;
FTextFormat FUE5CoroCategory::HiddenCoroutinesFormat;

void FUE5CoroCategory::FDataPack::Serialize(FArchive& Ar)
{
	Ar << RunningCoroutines;
	Ar << HiddenCoroutines;
}

FUE5CoroCategory::FUE5CoroCategory()
{
	bShowOnlyWithDebugActor = false;
	SetDataPackReplication<FDataPack>(&DataPack);
}

void FUE5CoroCategory::InitLocalization()
{
	// Compile text formats after the modifier has been registered
	CoroutineInfoFormatAsync = LOCTEXT("CoroutineInfoAsync",
		"Async #{ID}{Name}|ue5coro_conditional( \"{Name}\")"
		"{Ticking}|ue5coro_conditional( [Ticking])");
	CoroutineInfoFormatLatent = LOCTEXT("CoroutineInfoLatent",
		"Latent #{ID}{Name}|ue5coro_conditional( \"{Name}\")"
		"{Object}|ue5coro_conditional( on {Object})"
		"{Detached}|ue5coro_conditional( [Detached])");
	HiddenCoroutinesFormat = LOCTEXT("HiddenCoroutines",
		"({Count} more coroutine{Count}|plural(other=s) not shown)");
}

void FUE5CoroCategory::CollectData(APlayerController* PlayerController,
                                   AActor* Actor)
{
	Super::CollectData(PlayerController, Actor);

#if UE5CORO_ENABLE_COROUTINE_TRACKING
	int LinesLeft = CVarMaxDisplayedCoroutines.GetValueOnGameThread();

	DataPack = {};

	UE::TUniqueLock Lock(GTrackerLock);
	for (FPromise* Promise : GPromises)
	{
		if (!LinesLeft--)
			break;

		auto* Extras = Promise->Extras.get();

		FText CoroInfo;
		if (FCString::Strcmp(Extras->DebugPromiseType, TEXT("Latent")))
		{
			auto* AsyncPromise = static_cast<FAsyncPromise*>(Promise);
			bool bTicking = GTickingAsyncPromises.Contains(AsyncPromise);
			CoroInfo = FText::FormatNamed(CoroutineInfoFormatAsync,
				TEXT("ID"), Extras->DebugID,
				TEXT("Name"), FText::FromString(Extras->DebugName),
				TEXT("Ticking"), bTicking);
		}
		else
		{
			auto* LatentPromise = static_cast<FLatentPromise*>(Promise);
			UObject* Target = LatentPromise->GetCallbackTarget();
			CoroInfo = FText::FormatNamed(CoroutineInfoFormatLatent,
				TEXT("ID"), Extras->DebugID,
				TEXT("Name"), FText::FromString(Extras->DebugName),
				TEXT("Object"), FText::FromString(Target->GetName()),
				TEXT("Detached"), !LatentPromise->IsOnGameThread());
		}
		DataPack.RunningCoroutines.Add(std::move(CoroInfo));
	}
	DataPack.HiddenCoroutines =
		FMath::Max(0, GPromises.Num() - DataPack.RunningCoroutines.Num());
#endif
}

void FUE5CoroCategory::DrawData(APlayerController* PlayerController,
                                FGameplayDebuggerCanvasContext& Canvas)
{
	Super::DrawData(PlayerController, Canvas);

#if UE5CORO_ENABLE_COROUTINE_TRACKING
	for (const auto& CoroInfo : DataPack.RunningCoroutines)
		Canvas.Print(CoroInfo.ToString());
	if (DataPack.HiddenCoroutines)
		Canvas.Print(FText::FormatNamed(HiddenCoroutinesFormat,
			TEXT("Count"), DataPack.HiddenCoroutines).ToString());
#else
	Canvas.Print(FColor::Red, LOCTEXT("NoCoroutineTracking",
		"Debugger unavailable: UE5Coro was not built with coroutine tracking."
	).ToString());
#endif
}
}
#endif
