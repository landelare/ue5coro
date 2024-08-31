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

#include "K2Node_UE5CoroCallCoroutine.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "Misc/EngineVersionComparison.h"
#include "UE5Coro.h"

#if UE_VERSION_OLDER_THAN(5, 5, 0)
using ObjectTools = UK2Node_CallFunction;
#else
#include "ObjectTools.h"
#endif

void UK2Node_UE5CoroCallCoroutine::CustomizeNode(UEdGraphNode* NewNode, bool,
                                                 UFunction* Function)
{
	CastChecked<ThisClass>(NewNode)->SetFromFunction(Function);
}

void UK2Node_UE5CoroCallCoroutine::GetMenuActions(
	FBlueprintActionDatabaseRegistrar& BlueprintActionDatabaseRegistrar) const
{
	auto* Struct = FVoidCoroutine::StaticStruct();
	// Sign up for every BPCallable UFUNCTION returning FVoidCoroutine
	for (auto* Fn : TObjectRange<UFunction>())
		if (auto* Return = CastField<FStructProperty>(Fn->GetReturnProperty());
		    Return && Return->Struct == Struct) [[unlikely]]
		{
			if (!Fn->HasAllFunctionFlags(FUNC_BlueprintCallable))
				continue;

			// Patch the UFunction object to hide the regular function call
			Fn->SetMetaData(FBlueprintMetadata::MD_BlueprintInternalUseOnly,
			                TEXT("true"));

			// Sign up to create a coroutine call K2Node instead
			auto* BNS = UBlueprintNodeSpawner::Create(GetClass());
			BNS->CustomizeNodeDelegate.BindWeakLambda(Fn, &CustomizeNode, Fn);

			auto& Menu = BNS->DefaultMenuSignature;
			Menu.MenuName = Fn->GetDisplayNameText();
			Menu.Category = GetDefaultCategoryForFunction(Fn, FText::GetEmpty());
			if (Menu.Category.IsEmpty())
				Menu.Category = NSLOCTEXT("UE5Coro", "CallCoroutine",
				                          "Call Coroutine");
			// The engine does the same FString->FText conversion
			Menu.Tooltip = FText::FromString(
				ObjectTools::GetDefaultTooltipForFunction(Fn));
			Menu.Keywords = GetKeywordsForFunction(Fn);
			Menu.Icon = GetIconAndTint(Menu.IconTint);
			Menu.DocLink = GetDocumentationLink();
			Menu.DocExcerptTag = GetDocumentationExcerptName();

			BlueprintActionDatabaseRegistrar.AddBlueprintAction(Struct, BNS);
		}
}

void UK2Node_UE5CoroCallCoroutine::PostParameterPinCreated(UEdGraphPin* Pin)
{
	Super::PostParameterPinCreated(Pin);

	UObject* Type = Pin->PinType.PinSubCategoryObject.Get();

	// Is this an output FVoidCoroutine pin?
	if (Pin->Direction == EGPD_Output && Type == FVoidCoroutine::StaticStruct())
		Pin->SafeSetHidden(true);

	// Is this an input FForceLatentCoroutine pin?
	if (Pin->Direction == EGPD_Input &&
	    Type == FForceLatentCoroutine::StaticStruct())
		Pin->SafeSetHidden(true);
}
