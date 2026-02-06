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

using UnrealBuildTool;

public class UE5CoroDebug : UE5CoroModuleRules
{
	/// <summary>
	/// If set to <see langword="true" /> from a Target.cs, the
	/// <c>|conditional</c> text format modifier is made available globally.
	/// It is recommended to either enable this in every Target.cs, or none of them.
	/// </summary>
	/// <remarks>
	/// Ordinarily, a longer alias is used internally, which is removed from
	/// Shipping builds.
	/// </remarks>
	public static bool bRegisterConditionalTextFormatArgumentModifier = false;

	public UE5CoroDebug(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"UE5Coro",
		});

		if (bRegisterConditionalTextFormatArgumentModifier)
			PrivateDefinitions.Add("UE5CORO_PRIVATE_REGISTER_SHORT_MODIFIER");

		if (Target.bBuildDeveloperTools ||
			(Target.Configuration != UnrealTargetConfiguration.Shipping &&
			 Target.Configuration != UnrealTargetConfiguration.Test))
		{
			PrivateDependencyModuleNames.Add("GameplayDebugger");
			PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=0");
		}
	}
}
