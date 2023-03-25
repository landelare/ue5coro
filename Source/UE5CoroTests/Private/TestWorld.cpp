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

#include "TestWorld.h"
#include "HAL/ThreadManager.h"

using namespace UE5Coro::Private::Test;

FTestWorld::FTestWorld()
	: World(UWorld::CreateWorld(EWorldType::Game, false))
{
	check(IsInGameThread());
	auto& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	PrevWorld = GWorld;
	OldFrameCounter = GFrameCounter;
	GWorld = World;
	World->InitializeActorsForPlay(FURL());
	auto* Settings = World->GetWorldSettings();
	Settings->MinUndilatedFrameTime = 0.0001;
	Settings->MaxUndilatedFrameTime = 10;
	World->BeginPlay();
}

FTestWorld::~FTestWorld()
{
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(true);
	GWorld = PrevWorld;
	GFrameCounter = OldFrameCounter;
	CollectGarbage(RF_NoFlags);
}

void FTestWorld::Tick(float DeltaSeconds)
{
	check(IsInGameThread());
	StaticTick(DeltaSeconds);
	World->Tick(LEVELTICK_All, DeltaSeconds);
	EndTick();

	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

	// Other things that need ticking - FTSTicker is used by FHttpModule
	// Reference: FEngineLoop::Tick()
	FTSTicker::GetCoreTicker().Tick(FApp::GetDeltaTime());
	FThreadManager::Get().Tick();
	GEngine->TickDeferredCommands();
}

void FTestWorld::EndTick()
{
	check(IsInGameThread());
	++GFrameCounter;
}

void FTestHelper::PumpGameThread(FTestWorld& World,
                                 std::function<bool()> ExitCondition)
{
	while (!ExitCondition())
		World.Tick();
}
