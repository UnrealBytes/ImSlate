// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3DAPI.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"

class SViewport;

namespace ImSlate
{

// Camera projection data the 3D widgets need to build their FImProjector for the current frame. Pure
// math types only — NO Engine types — so this module never links Engine. The host (main ImSlate module,
// which DOES link Engine) fills this from the scene camera (LocalPlayer GetProjectionData).
struct FImCameraProjData
{
	bool     bValid = false;
	FMatrix  ViewProj = FMatrix::Identity;   // world -> clip (ComputeViewProjectionMatrix)
	FIntRect ViewRect = FIntRect(0, 0, 0, 0);// constrained view rect (clip -> screen px)
};

/**
 * FImSlate3DHost — the dependency-inversion seam that lets the 3D widgets live in this low-level module
 * (SlateCore/Slate/RHI only, loadable at PostConfigInit) while the handful of Engine-coupled operations
 * (scene camera query, game-viewport hit-path registration, input settings) are supplied by the main
 * ImSlate module (which links Engine). The main module calls Install() at startup; the widgets call the
 * hooks. Every hook is null-safe: if not installed the widgets degrade gracefully (no projector / no
 * custom hit-path), never crash.
 */
struct IMSLATE3D_API FImSlate3DHost
{
	// Build camera projection data for player 0. Returns {bValid=false} if no world/camera yet.
	TFunction<FImCameraProjData()> GetCameraProjData;

	// The game viewport's SViewport (for ICustomHitTestPath register/unregister). Null if unavailable.
	TFunction<TSharedPtr<SViewport>()> GetGameViewport;

	// UInputSettings::bUseMouseForTouch — selects high-precision-mouse vs absolute-cursor drag.
	TFunction<bool()> IsMouseForTouch;

	// Global accessor. Always returns a valid (possibly empty) instance.
	static FImSlate3DHost& Get();

	// Called once by the main module at startup to wire the Engine-backed implementations.
	static void Install(FImSlate3DHost&& InHooks);
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
