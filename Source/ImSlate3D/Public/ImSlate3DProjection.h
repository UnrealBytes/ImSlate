// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3DAPI.h"  // IMSLATE_3D_TRANSFORM gate

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"

class FSlateWindowElementList;
class FSlateFontCache;

namespace ImSlate
{
class FImProjector;
struct FIm3DQuad;
struct FImWorldPlacement;

/** A world point in front of the camera plus the camera's orientation — the common "where do I place a thing in
 *  view" result the demos need. Forward = camera look dir; Right/Up complete a right-handed view basis. */
struct FImViewPoint
{
	bool    bValid = false;
	FVector Location = FVector::ZeroVector;       // the point DistanceFromCamera along the screen-centre ray
	FVector CameraPos = FVector::ZeroVector;      // camera world position
	FQuat   Orientation = FQuat::Identity;        // camera orientation (GetForwardVector = look dir)
	FVector Forward = FVector::ForwardVector;     // camera look direction (unit)
	FVector Right = FVector::RightVector;         // camera right (unit)
	FVector Up = FVector::UpVector;               // camera up (unit)
};

/**
 * Where the current view frustum points: the world point DistanceFromCamera units down the screen-CENTRE ray,
 * plus the camera position + orientation. Derived purely from the current FImCameraProjData (inverse view-proj),
 * so any caller (every gizmo/sheet demo) can place content centred in view without touching Engine camera types.
 * Returns {bValid=false} if no camera this frame. Replaces the hand-rolled "CamLoc + Forward*D" in the demos.
 */
IMSLATE3D_API FImViewPoint ImGetViewCentre(double DistanceFromCamera);

/**
 * Build an FImProjector for a sheet of WidgetSize at Placement, using the CURRENT scene camera. The
 * camera matrices come through FImSlate3DHost (the Engine query lives in the main module). Returns false
 * if no camera yet, or the sheet is back-facing/invisible. This is the shared "make a projector" the
 * retained box and the standalone gizmo both use, so a gizmo can self-project without any panel.
 */
IMSLATE3D_API bool ImBuildProjector(FVector2f WidgetSize, const FImWorldPlacement& Placement, FImProjector& OutProj);

/**
 * Core "3D-ify any Slate subtree" projection — module-level, no widget / no Engine dependency.
 *
 * Walks a captured draw-element list (the elements a child subtree painted into a throwaway
 * FSlateWindowElementList in identity local space) and re-projects every supported element type
 * (Box / RoundedBox / Border / Text / ShapedText / Line / Spline / Gradient / CustomVerts) through
 * the given FImProjector onto the widget's 3D world plane, appending one FIm3DQuad per projected
 * primitive to OutQuads. Glyphs are shaped/atlased via the global FSlateFontCache (game thread).
 *
 * This is the reusable heart of the 3D feature: any host (the retained SImSlate3DTransformBox, an
 * immediate-mode window, a low-level DrawText3D helper) feeds a capture list + projector and gets
 * back screen-space perspective quads to submit as one FIm3DShaderElement. The host owns submission
 * (it must keep the FIm3DShaderElement alive past OnPaint) and any widget-specific extras (e.g. the
 * volumetric gizmo arrows), which is why this function only PRODUCES quads and never submits.
 */
IMSLATE3D_API void EmitCapturedElementQuads(
	const FSlateWindowElementList& CaptureList,
	const FImProjector& Proj,
	FSlateFontCache* FontCache,
	TArray<FIm3DQuad>& OutQuads);

/**
 * Append one solid screen-space quad for a WORLD-space line segment A→B of the given screen
 * thickness. Used by world-space wireframe drawing (the volumetric gizmo arrows, future DrawLine3D).
 * Returns false if either endpoint is behind the camera or the segment is zero-length on screen.
 */
IMSLATE3D_API bool AppendWorldSegmentQuad3D(
	const class FImProjector& Proj, const FVector& A, const FVector& B,
	float ScreenThickness, const FVector4f& Color, TArray<struct FIm3DQuad>& OutQuads);

/** One solid screen-space quad (TL,TR,BR,BL in viewport px) → FIm3DQuad (flat colour, no texture, InvW=0).
 *  Shared by gizmo handle shapes (box/ring fill) — the screen-space half of AppendWorldSegmentQuad3D. */
IMSLATE3D_API void AppendScreenQuad3D(const FVector2f& P0, const FVector2f& P1, const FVector2f& P2,
	const FVector2f& P3, const FVector4f& Color, TArray<struct FIm3DQuad>& OutQuads);

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
