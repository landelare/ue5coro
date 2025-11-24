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

#include "Internationalization/ITextFormatArgumentModifier.h"

namespace UE5Coro::Private::Debug
{
/** This class supports being used for localization only, without being tied to
 *  debugging. If it is enabled for global use (off by default):
 *
 *  Use "{Condition}|conditional(Format string)" to only insert the formatted
 *  parameter if Condition is truthy. Nested format strings are supported.
 *  Unreal will not evaluate this modifier if Condition is not provided, make
 *  sure to always provide some value for it!
 *
 *  Falsy values are false, 0, 0U, ±0.0f, ±0.0, "", all genders, and anything
 *  else that formats to an empty string. Everything else is truthy.
 *
 *  For example, to quote a string, without resulting in "" for an empty string:
 *  "{String}|conditional(\"{String}\")" */
class FConditionalModifier : public ITextFormatArgumentModifier
{
	FTextFormat Format;

	explicit FConditionalModifier(FTextFormat Format);

public:
	// Used by default to not collide with a user-defined |conditional
	static constexpr TCHAR LongKeyword[] = TEXT("ue5coro_conditional");
	static constexpr TCHAR ShortKeyword[] = TEXT("conditional");
	UE5CORODEBUG_API static TSharedPtr<ITextFormatArgumentModifier> Create(
		const FTextFormatString& Format,
		TSharedRef<const FTextFormatPatternDefinition> PatternDef);

	virtual bool Validate(const FCultureRef& Culture,
	                      TArray<FString>& OutValidationErrors) const override;
	virtual void Evaluate(const FFormatArgumentValue& Value,
	                      const FPrivateTextFormatArguments& FormatArgs,
	                      FString& OutResult) const override;
	virtual void GetFormatArgumentNames(TArray<FString>&) const override;
	virtual void EstimateLength(int32&, bool&) const override;
};
}
