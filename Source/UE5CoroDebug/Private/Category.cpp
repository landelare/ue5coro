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
#include "Engine/Font.h"
#include "UE5Coro.h"

#define LOCTEXT_NAMESPACE "UE5Coro"

namespace UE5Coro::Private::Debug
{
TAutoConsoleVariable<int> CVarMaxDisplayedCoroutines(
	TEXT("UE5Coro.MaxDisplayedCoroutines"), 20,
	TEXT("Show at most this many coroutines in the Gameplay Debugger."));
TAutoConsoleVariable<int> CVarMaxDisplayedCoroutinesOnTarget(
	TEXT("UE5Coro.MaxDisplayedCoroutinesOnTarget"), 5,
	TEXT("Show at most this many coroutines above the selected actor in the "
	     "Gameplay Debugger."));

FTextFormat FUE5CoroCategory::ExcludedActorFormat;
FTextFormat FUE5CoroCategory::CoroutineInfoFormatAsync;
FTextFormat FUE5CoroCategory::CoroutineInfoFormatLatent;
FTextFormat FUE5CoroCategory::CoroutineInfoFormatManual;
FTextFormat FUE5CoroCategory::HiddenCoroutinesFormat;

void FUE5CoroCategory::FDataPack::Serialize(FArchive& Ar)
{
	Ar << ExcludedActorHeader;
	Ar << RunningCoroutines;
	Ar << RunningCoroutinesOnTarget;
	Ar << HiddenCoroutines;
	Ar << HiddenCoroutinesOnTarget;
}

FUE5CoroCategory::FUE5CoroCategory()
{
	bShowOnlyWithDebugActor = false;
	SetDataPackReplication<FDataPack>(&DataPack);
}

void FUE5CoroCategory::InitLocalization()
{
	// Compile text formats after the modifier has been registered
	ExcludedActorFormat = LOCTEXT("ExcludedActor",
		"Running coroutines, excluding {Actor}:");
	CoroutineInfoFormatAsync = LOCTEXT("CoroutineInfoAsync",
		"Async #{ID}{Name}|ue5coro_conditional( \"{Name}\")"
		"{Ticking}|ue5coro_conditional( [Ticking])");
	CoroutineInfoFormatLatent = LOCTEXT("CoroutineInfoLatent",
		"Latent #{ID}{Name}|ue5coro_conditional( \"{Name}\")"
		"{Object}|ue5coro_conditional( on {Object})"
		"{Detached}|ue5coro_conditional( [Detached])");
	CoroutineInfoFormatManual = LOCTEXT("CoroutineInfoManual",
		"Manual #{ID}{Name}|ue5coro_conditional( \"{Name}\")");
	HiddenCoroutinesFormat = LOCTEXT("HiddenCoroutines",
		"({Count} more coroutine{Count}|plural(other=s) not shown)");
}

void FUE5CoroCategory::CollectData(APlayerController* PlayerController,
                                   AActor* Actor)
{
	Super::CollectData(PlayerController, Actor);

#if UE5CORO_ENABLE_COROUTINE_TRACKING
	int MaxLines = CVarMaxDisplayedCoroutines.GetValueOnGameThread();
	int MaxLinesOnTarget =
		CVarMaxDisplayedCoroutinesOnTarget.GetValueOnGameThread();
	int OverflowLines = 0;
	int OverflowLinesOnTarget = 0;

	DataPack = {};
	if (Actor)
		DataPack.ExcludedActorHeader = FText::FormatNamed(ExcludedActorFormat,
			TEXT("Actor"), FText::FromString(Actor->GetName()));

	UE::TUniqueLock Lock(GTrackerLock);
	for (FPromise* Promise : GPromises)
	{
		// There is an early-return opportunity if there is no selected actor
		if (!Actor && DataPack.RunningCoroutines.Num() == MaxLines)
		{
			DataPack.HiddenCoroutines = GPromises.Num() -
			                            DataPack.RunningCoroutines.Num();
			return;
		}

		auto* Extras = Promise->Extras.get();

		switch (TCHAR First = Extras->DebugPromiseType[0])
		{
			case TEXT('A'): // Async
			case TEXT('M'): // Manual
			{
				auto* AsyncPromise = static_cast<FAsyncPromise*>(Promise);
				bool bTicking = GTickingAsyncPromises.Contains(AsyncPromise);
				// Async coroutines are never associated with an actor
				if (DataPack.RunningCoroutines.Num() < MaxLines)
					DataPack.RunningCoroutines.Add(FText::FormatNamed(
						First == TEXT('A') ? CoroutineInfoFormatAsync
						                   : CoroutineInfoFormatManual,
						TEXT("ID"), Extras->DebugID,
						TEXT("Name"), FText::FromString(Extras->DebugName),
						TEXT("Ticking"), bTicking));
				else
					++OverflowLines;
				break;
			}
			case TEXT('L'): // Latent
			{
				auto* LatentPromise = static_cast<FLatentPromise*>(Promise);
				if (UObject* Target = LatentPromise->GetCallbackTarget();
				    Actor && Actor == Target)
				{
					if (DataPack.RunningCoroutinesOnTarget.Num() <
					    MaxLinesOnTarget)
						DataPack.RunningCoroutinesOnTarget.Add(FText::FormatNamed(
							CoroutineInfoFormatLatent,
							TEXT("ID"), Extras->DebugID,
							TEXT("Name"), FText::FromString(Extras->DebugName),
							TEXT("Object"), false, // It's shown on the object
							TEXT("Detached"), !LatentPromise->IsOnGameThread()));
					else
						++OverflowLinesOnTarget;
				}
				else if (DataPack.RunningCoroutines.Num() < MaxLines)
					DataPack.RunningCoroutines.Add(FText::FormatNamed(
						CoroutineInfoFormatLatent,
						TEXT("ID"), Extras->DebugID,
						TEXT("Name"), FText::FromString(Extras->DebugName),
						TEXT("Object"), FText::FromString(Target->GetName()),
						TEXT("Detached"), !LatentPromise->IsOnGameThread()));
				else
					++OverflowLines;
				break;
			}
			default:
				check(!"Unexpected coroutine type");
				break;
		}
	}

	DataPack.HiddenCoroutines = OverflowLines;
	DataPack.HiddenCoroutinesOnTarget = OverflowLinesOnTarget;
#endif
}

void FUE5CoroCategory::DrawData(APlayerController* PlayerController,
                                FGameplayDebuggerCanvasContext& Canvas)
{
	Super::DrawData(PlayerController, Canvas);

#if UE5CORO_ENABLE_COROUTINE_TRACKING
	if (!DataPack.ExcludedActorHeader.IsEmpty())
		Canvas.Print(DataPack.ExcludedActorHeader.ToString());
	for (const auto& CoroInfo : DataPack.RunningCoroutines)
		Canvas.Print(CoroInfo.ToString());
	if (DataPack.HiddenCoroutines)
		Canvas.Print(FText::FormatNamed(HiddenCoroutinesFormat,
			TEXT("Count"), DataPack.HiddenCoroutines).ToString());

	if (auto* Actor = FindLocalDebugActor())
	{
		// Match FGameplayDebuggerCategory_AI's overhead text style...
		auto OverheadCanvas = Canvas;
		OverheadCanvas.Font = GEngine->GetSmallFont();
		OverheadCanvas.FontRenderInfo.bEnableShadow = true;

		auto Location = OverheadCanvas.ProjectLocation(
			Actor->GetActorLocation() +
			FVector(0, 0, Actor->GetSimpleCollisionHalfHeight()));
		// ... and its line offset
		Location.Y += OverheadCanvas.GetLineHeight() * 0.2f;

		auto Print = [&](const FText& Text)
		{
			const auto& String = Text.ToString();
			float Width, Height;
			OverheadCanvas.MeasureString(String, Width, Height);
			OverheadCanvas.PrintAt(Location.X - Width / 2,
			                       Location.Y - Height / 2, String);
			Location.Y += OverheadCanvas.GetLineHeight();
		};

		for (const auto& CoroInfo : DataPack.RunningCoroutinesOnTarget)
			Print(CoroInfo);
		if (DataPack.HiddenCoroutinesOnTarget)
			Print(FText::FormatNamed(HiddenCoroutinesFormat,
				TEXT("Count"), DataPack.HiddenCoroutinesOnTarget));
	}
#else
	Canvas.Print(FColor::Red, LOCTEXT("NoCoroutineTracking",
		"Debugger unavailable: UE5Coro was not built with coroutine tracking."
	).ToString());
#endif
}
}
#endif
