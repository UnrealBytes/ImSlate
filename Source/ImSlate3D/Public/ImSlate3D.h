// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

// ============================================================================
// ImSlate 3D availability gate.
//
// The ImSlate3D module is always built (it's a self-contained "3D-ify any Slate"
// module). UBT defines IMSLATE3D_API in this module and in any module that depends
// on it; it is undefined elsewhere. So `#if defined(IMSLATE3D_API)` means "the 3D
// module is linked + available" — no manual feature flag to keep in sync.
// ============================================================================
#if defined(IMSLATE3D_API)

namespace ImSlate
{
/** imslate.3d.Debug >= 1 — event logs only (hit / register), not per-frame. */
IMSLATE3D_API bool IsImSlate3DDebug();
/** imslate.3d.Debug >= 2 — verbose per-frame dumps (projector axes, element counts). */
IMSLATE3D_API bool IsImSlate3DDebugVerbose();
/** imslate.3d.rotlog — gizmo rotate-drag sign diagnostics, only on a rotate move (no per-frame noise). */
IMSLATE3D_API bool IsImSlate3DRotLog();
/** imslate.3d.draglog — gizmo plane/scale constraint-drag math, only on a move (no per-frame noise). */
IMSLATE3D_API bool IsImSlate3DDragLog();

/**
 * Where the widget plane sits in the 3D world. The widget is a flat sheet; this
 * places + orients it. UE world axes: +X forward, +Y right, +Z up (left-handed).
 *
 *   - widget local X (right) -> plane Right axis = Rotation applied to world +Y
 *   - widget local Y (down)  -> plane Down  axis = Rotation applied to world -Z
 *   - widget normal (toward viewer) -> Rotation applied to world +X (forward)
 *
 * So WorldRotation == identity means the sheet stands upright facing +X, like a
 * billboard a forward-facing observer would read. Pivot is which normalized point
 * of the widget the WorldLocation pins (0,0 = top-left, 0.5,0.5 = centre).
 */
struct FImWorldPlacement
{
	FVector   WorldLocation = FVector::ZeroVector;
	// WorldRotation == ZeroRotator (and bUseExplicitAxes == false) → BILLBOARD: the sheet faces the camera
	// every frame (normal = sheet→camera, right/down from world up). Any non-zero rotation → fixed world
	// orientation (local X(right)->+Y, Y(down)->-Z, normal(front)->+X rotated by this).
	FRotator  WorldRotation = FRotator::ZeroRotator;
	FVector2f Pivot         = FVector2f(0.5f, 0.5f);  // anchor point in normalized widget space
	// World units per widget pixel. SPECIAL: WorldScale == 0 → SCREEN-SPACE size: the sheet is sized in
	// SCREEN PIXELS directly (1 widget px = 1 screen px, exactly, at any distance), only WorldLocation is
	// projected for the anchor. Use for click-target gizmos / labels that must stay a constant on-screen
	// size no matter how far away. Any positive value → normal world-locked sizing (shrinks with distance).
	float     WorldScale    = 1.0f;

	// Optional EXPLICIT world axes. When bUseExplicitAxes is true these are used directly and
	// WorldRotation is ignored — this avoids any FRotator round-trip ambiguity when the caller
	// already has the exact world directions (e.g. from the camera basis). Semantics:
	//   ExplicitRight  = world dir of widget local +X (right)
	//   ExplicitDown   = world dir of widget local +Y (down)
	//   ExplicitNormal = world dir of the widget's front normal (toward viewer)
	bool    bUseExplicitAxes = false;
	FVector ExplicitRight  = FVector(0, 1, 0);
	FVector ExplicitDown   = FVector(0, 0, -1);
	FVector ExplicitNormal = FVector(1, 0, 0);

	// Opacity — ONE float encoding cull / general-transparency / back-only-transparency by sign:
	//   == 0  → CULL: the sheet is not rendered and not hit-tested (fully transparent == invisible == gone).
	//   >  0  → general opacity applied to BOTH sides (1 = opaque; 0.4 = 40% see-through, e.g. a glass window).
	//   <  0  → BACK-ONLY transparency: front stays fully opaque, the back uses |Opacity| (e.g. -0.4 = back 40%).
	// The front is always shown normally; only Opacity == 0 suppresses hit-testing.
	float Opacity = 1.0f;

	// Backface geometry: false (default) = the back shows a mirrored view of the front (look through glass);
	// true = flip RightAxis when facing away so the content reads CORRECTLY from the back too (a double-sided
	// sign printed right on both faces). Sheet stays in place; smooth across the 90° edge; cull still wins when
	// Opacity == 0. Independent of Opacity.
	bool bCorrectBackface = false;
};

/**
 * Result of mapping a screen point onto the widget's 3D UI plane (the sheet). One call gives BOTH
 * representations a caller usually needs: the 3D world point on the plane AND the plane-local (u,v) px.
 *   - bValid:      the deprojected ray hit the plane in front of the camera (and the plane faces us).
 *   - bInsideRect: the local (u,v) lies within [0..Size] — i.e. inside the widget's projected quad (for
 *                  hit-testing). The mapping itself is valid outside the rect too (bValid can be true while
 *                  bInsideRect is false); only hit-tests should require bInsideRect.
 *   - World:       the 3D world-space point where the screen ray meets the UI plane.
 *   - Local:       that point expressed in widget-local pixels (the plane's own 2D coordinate space).
 */
struct FImPlaneHit
{
	bool      bValid      = false;
	bool      bInsideRect = false;
	FVector   World       = FVector::ZeroVector;
	FVector2f Local       = FVector2f::ZeroVector;
};

/**
 * FImProjector — pure-math core for the no-RT perspective path (B2).
 *
 * Renders a Slate widget as if it were a sheet placed in the 3D world, projected
 * to the screen through the REAL scene camera (view-projection), then drawn on the
 * top Slate layer (no depth, no RT). Because each widget-local point maps to its
 * own world point, the four corners project to four independent screen points — a
 * true perspective trapezoid that tracks the camera, not a flat billboard.
 *
 * Rendering uses Project() (local px -> screen px; the camera does the perspective
 * divide, result is 2D for FSlateVertex). Hit-testing uses Unproject() (screen ->
 * local) via deproject-ray / widget-plane intersection — exact inverse inside the quad.
 */
class IMSLATE3D_API FImProjector
{
public:
	/**
	 * Build the projection for one widget for the current frame.
	 * @param InSize      widget local size in pixels
	 * @param Placement   where/how the widget sheet sits in the world
	 * @param ViewProj    scene camera view-projection (world -> clip), double precision
	 *                    as returned by FSceneViewProjectionData::ComputeViewProjectionMatrix()
	 * @param ViewRect    constrained view rect (clip -> screen px), in screen pixels
	 */
	void Build(FVector2f InSize, const FImWorldPlacement& Placement,
	           const FMatrix& ViewProj, const FIntRect& ViewRect);

	/** local (widget px) -> screen (viewport px). Returns unset if the point is behind the camera. */
	TOptional<FVector2f> Project(FVector2f Local) const;

	/**
	 * screen (viewport px) -> local (widget px).
	 * Returns unset when the deprojected ray misses the widget plane, the hit is
	 * behind the camera, or the plane is back-facing.
	 */
	TOptional<FVector2f> Unproject(FVector2f Screen) const;

	/**
	 * Unified screen → UI-plane mapping. Deprojects the screen point to a world ray, intersects it with the
	 * widget sheet plane, and returns BOTH the 3D world hit point AND the plane-local (u,v) px (plus validity
	 * + inside-rect flags). This is the single abstraction for "where on this 3D UI does the cursor land":
	 * hit-testing wants bInsideRect, drag/probe want World/Local. Unproject() is a thin wrapper over this.
	 */
	FImPlaneHit MapScreenToPlane(FVector2f Screen) const;

	/** True when the front side faces the camera. */
	bool IsFrontFacing() const { return bFrontFacing; }

	/** RENDER visibility: only Opacity == 0 culls (fully transparent == gone). Otherwise always drawn
	 *  (double-sided); back geometry governed by bCorrectBackface, back alpha by Opacity's sign. */
	bool IsVisible() const { return Opacity != 0.f; }

	/** HIT-TEST eligibility: route pointer events whenever the content reads CORRECTLY ("you see it the right
	 *  way up → you can interact"). That's the front, OR a back that was un-mirrored by bCorrectBackface (its
	 *  RightAxis is flipped, so render AND hit use the same axis → click maps to what you see). A MIRRORED back
	 *  (Show, axis not flipped) is display-only: clicking it would land left-right-swapped vs what's drawn, so
	 *  it doesn't route. Culled (Opacity 0) never routes. bContentReadable is set in Build per this rule. */
	bool IsHittable() const { return Opacity != 0.f && bContentReadable; }

	/** Effective alpha multiplier for THIS frame's facing, from Opacity's sign encoding:
	 *   Opacity > 0 → Opacity on both sides;  Opacity < 0 → front 1, back |Opacity|;  (== 0 is culled earlier). */
	float GetEffectiveAlpha() const
	{
		if (Opacity >= 0.f) { return Opacity; }
		return bFrontFacing ? 1.f : -Opacity;
	}

	FVector2f GetSize() const { return Size; }

	/** local px -> the corresponding 3D world point on the sheet. */
	FVector LocalToWorld(FVector2f Local) const;

	/** ARBITRARY world point -> screen (viewport px), via the same camera view-projection. Returns unset
	 *  if the point is behind the camera. Unlike Unproject this does NOT clip to the widget plane — used by
	 *  the gizmo drag to measure on-screen pixels-per-world-unit along a drag axis (the dragged point leaves
	 *  the plane, so Unproject can't be used). Respects the camera only, not Opacity/backface. */
	TOptional<FVector2f> ProjectWorld(const FVector& World) const;

	/** screen (viewport px) -> a world-space ray (origin + unit dir), the deproject without any plane
	 *  intersection. For gizmo axis drag via ray–line closest point (robust for ALL axes, including ones
	 *  nearly facing the camera where the screen-projection method degenerates). Returns false if degenerate. */
	bool DeprojectScreenToWorldRay(FVector2f Screen, FVector& OutOrigin, FVector& OutDir) const;

	/**
	 * local (widget px) -> HOMOGENEOUS CLIP coords (x,y,z,w) WITHOUT the perspective divide.
	 * This is what the renderMode=shader path needs: handed straight to the GPU as SV_POSITION
	 * so the hardware does the perspective divide + perspective-correct UV interpolation. W is
	 * the genuine camera clip-W (unlike Slate's path where W is locked to 1). Returns the raw
	 * FVector4f even when behind the camera (w<=0) — the GPU clips; the caller decides culling.
	 */
	FVector4f ProjectToClip(FVector2f Local) const;

	/** The constrained view rect the camera projects into (FImProjector's pixel basis). Exposed so the
	 *  shader path can compare it against ICustomSlateElement's SceneViewRect (they may differ by DPI). */
	FIntRect GetViewRect() const { return Rect; }

	// Replay/diagnostic accessors: dump these once per drag so the whole screen↔world chain can be recomputed
	// offline (no need to trust the engine's ProjectWorld — verify it from the raw matrix + basis).
	const FMatrix& GetViewProjMatrix() const { return ViewProjMtx; }
	FVector GetPlaneOrigin() const { return Origin; }
	FVector GetPlaneRight()  const { return RightAxis; }
	FVector GetPlaneDown()   const { return DownAxis; }
	FVector GetPlaneNormal() const { return Normal; }

	// --- LOW-LEVEL plane-basis write (the mapping matrix's columns). The camera (ViewProj/CamPos) is NOT
	// touched — only the world plane the sheet sits on. Render/hit/project all read these, so writing them
	// re-aims everything consistently. Gizmo constraints (or expert callers) compute a new basis and write it
	// back here; this is the algorithmic seam under IImGizmoConstraint. SetPlaneOrigin = pure translate (the
	// common move case). SetPlaneBasis replaces all four (rotate/scale). Caller owns facing semantics if it
	// changes Normal (bFrontFacing/IsVisible are NOT recomputed here — they were set at Build time). ---
	void SetPlaneOrigin(const FVector& InOrigin) { Origin = InOrigin; }
	void SetPlaneBasis(const FVector& InOrigin, const FVector& InRight, const FVector& InDown, const FVector& InNormal)
	{
		Origin = InOrigin; RightAxis = InRight; DownAxis = InDown; Normal = InNormal;
	}
	/** Camera world position (recovered from the inverse view-proj near plane). Used by the Flat arrow's
	 *  billboard-around-axis basis (the width direction faces the camera so the axis never collapses to a line). */
	FVector GetCamPos() const { return CamPos; }

	/** World units that project to ONE screen pixel at the given world point's distance (perspective:
	 *  larger when farther). Multiply a desired on-screen px size by this to get the world size that
	 *  renders at exactly that many screen px → screen-constant-size gizmos/labels at any distance. */
	double WorldUnitsPerScreenPx(const FVector& AtWorld) const;

private:
	FVector2f Size = FVector2f(0.f, 0.f);

	// Cached world-space basis of the sheet (from Placement), double precision (LWC-safe).
	FVector Origin    = FVector::ZeroVector;  // world point of widget local (0,0)
	FVector RightAxis = FVector::ZeroVector;  // world delta per +1 widget local X (incl. scale)
	FVector DownAxis  = FVector::ZeroVector;  // world delta per +1 widget local Y (incl. scale)
	FVector Normal    = FVector::ZeroVector;  // sheet normal (toward viewer)
	FVector CamPos    = FVector::ZeroVector;  // camera world pos (for Flat arrow billboard basis)

	// Camera, cached for projection + deprojection.
	FMatrix  ViewProjMtx    = FMatrix::Identity;
	FMatrix  InvViewProjMtx = FMatrix::Identity;
	FIntRect Rect           = FIntRect(0, 0, 0, 0);

	bool  bFrontFacing = true;
	bool  bContentReadable = true;  // content reads right-way-up this frame (front, or back un-mirrored by correct)
	float Opacity = 1.f;   // sign-encoded: 0=cull, >0=both-sides alpha, <0=back-only alpha (see GetEffectiveAlpha)

	// world point -> screen px via ViewProj (perspective divide); unset if behind camera.
	TOptional<FVector2f> WorldToScreen(const FVector& World) const;
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
