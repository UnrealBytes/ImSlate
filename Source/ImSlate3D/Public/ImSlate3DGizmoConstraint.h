// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Math/Transform.h"

namespace ImSlate
{
class FImProjector;

/** Dump the projector's full replay baseline (ViewProj + ViewRect + cam) to the log — call once at drag mouse-down
 *  (gated by imslate.3d.draglog/rotlog) so the cursor→world chain can be recomputed offline. */
IMSLATE3D_API void ImDumpProjReplayBaseline(const FImProjector& Proj);

/**
 * IImGizmoConstraint — the HIGH-LEVEL algorithmic seam of the gizmo: "a screen-space cursor drag, mapped
 * (constrained to one mode: axis / plane / rotate / scale) into a new FTransform". PURE math — no widget, no
 * Engine; uses only the (frozen) projector's deproject/project + plane basis. One implementation per mode;
 * users can supply their own (e.g. snap-to-spline, surface-snap). All modes feed the SAME FTransform channel
 * that FImSlate3DGizmo then runs through its Edit/clamp callback.
 *
 * Apply(Start, FrozenProj, CursorStartVP, CursorNowVP):
 *   Start         — the transform at mouse-down (drag basis; rotation/scale preserved unless this mode edits them).
 *   FrozenProj    — projector snapshot taken at mouse-down (constant calibration; do NOT use a live projector
 *                   or the mapping shifts as the result moves → positive feedback).
 *   CursorStartVP / CursorNowVP — cursor in viewport physical px at mouse-down and now.
 *   returns       — the new (constrained) transform. Return Start unchanged when the drag is ill-conditioned
 *                   (e.g. an axis dead-on to the camera → no usable screen handle).
 */
class IMSLATE3D_API IImGizmoConstraint
{
public:
	virtual ~IImGizmoConstraint() = default;
	// bOutValid (default true) is set FALSE when this drag frame is ill-conditioned (axis/plane edge-on, ray miss).
	// The caller then SKIPS committing — the gizmo holds its last valid pose instead of snapping back to Start (a
	// degenerate frame must not jump the object back to the drag origin). Returns Start when invalid (ignored).
	virtual FTransform Apply(const FTransform& Start, const FImProjector& FrozenProj,
	                         FVector2f CursorStartVP, FVector2f CursorNowVP, bool& bOutValid) const = 0;
};

/**
 * FImAxisConstraint — move along ONE world axis. Screen-projection method (the robust ComputeAxisT algorithm):
 * project the world axis line to a 2D screen line, project the cursor onto it, map the pixel position linearly
 * to a world parameter. Never divides by sin²θ → no blow-up for a camera-facing axis. Edits Location only.
 */
class IMSLATE3D_API FImAxisConstraint : public IImGizmoConstraint
{
public:
	explicit FImAxisConstraint(const FVector& InWorldAxis, double InHandleSpanWorld = 160.0)
		: WorldAxis(InWorldAxis.GetSafeNormal()), HandleSpanWorld(InHandleSpanWorld) {}
	virtual FTransform Apply(const FTransform& Start, const FImProjector& FrozenProj,
	                         FVector2f CursorStartVP, FVector2f CursorNowVP, bool& bOutValid) const override;
private:
	FVector WorldAxis;
	double  HandleSpanWorld;  // world span used to build a stable screen direction (≈ arrow length)
};

/**
 * FImPlaneConstraint — move within the world plane spanned by AxisU & AxisV (e.g. world XY/YZ/ZX). Uses the SAME
 * robust screen-projection method as single-axis move, run on BOTH axes independently: map the cursor's screen
 * displacement to a world param along AxisU and along AxisV, then Δloc = U·Δt_U + V·Δt_V. This never intersects a
 * world ray with the plane, so it does NOT degenerate when the plane is edge-on to the view (the old ray∩plane
 * method returned Start whenever the plane normal was ⊥ the view → "plane handle won't drag"). Edits Location only.
 */
class IMSLATE3D_API FImPlaneConstraint : public IImGizmoConstraint
{
public:
	FImPlaneConstraint(const FVector& InAxisU, const FVector& InAxisV, double InHandleSpanWorld = 160.0)
		: AxisU(InAxisU.GetSafeNormal()), AxisV(InAxisV.GetSafeNormal()), HandleSpanWorld(InHandleSpanWorld) {}
	virtual FTransform Apply(const FTransform& Start, const FImProjector& FrozenProj,
	                         FVector2f CursorStartVP, FVector2f CursorNowVP, bool& bOutValid) const override;
private:
	FVector AxisU, AxisV;
	double  HandleSpanWorld;
};

/**
 * FImScaleConstraint — scale along ONE world axis. The "reverse transform" the user pinpointed: a screen drag
 * along the axis → a scale FACTOR (not a position). Reuses the same robust screen-projection axis math as move:
 * map cursor start/now to world params t along the axis, factor = (Ref + (tNow - tStart)) / Ref. Ref is a
 * positive reference length keeping the factor away from 0/negative. Edits Start's Scale3D on this axis only
 * (Location/Rotation preserved). Ill-conditioned axis (dead-on to camera) → returns Start.
 */
class IMSLATE3D_API FImScaleConstraint : public IImGizmoConstraint
{
public:
	explicit FImScaleConstraint(const FVector& InWorldAxis, double InRefLengthWorld = 160.0,
	                            double InHandleSpanWorld = 160.0)
		: WorldAxis(InWorldAxis.GetSafeNormal())
		, RefLengthWorld(FMath::Max(InRefLengthWorld, 1.0))
		, HandleSpanWorld(InHandleSpanWorld) {}
	virtual FTransform Apply(const FTransform& Start, const FImProjector& FrozenProj,
	                         FVector2f CursorStartVP, FVector2f CursorNowVP, bool& bOutValid) const override;
private:
	FVector WorldAxis;
	double  RefLengthWorld;   // world length that maps drag-delta to a unit factor (factor = (Ref+Δ)/Ref)
	double  HandleSpanWorld;  // span for the stable screen direction (≈ handle length)
};

/**
 * FImRotateConstraint — rotate about ONE world axis. The reverse transform for rotation: a screen drag → an
 * angle Δθ. Computed in SCREEN space (robust at every view, INCLUDING the ring seen edge-on — the old ray∩plane
 * method returned Start whenever the axis was ⊥ the view): the screen angle the cursor sweeps about the projected
 * anchor (start→now) IS Δθ; the world sign is flipped by whether the axis points toward/away from the camera.
 * NewRotation = Quat(axis, Δθ) * Start.Rotation. Location/Scale kept. SignedDeltaAngle is exposed so the HUD can
 * draw the same swept angle (the two arc lines + degree readout) without recomputing.
 */
class IMSLATE3D_API FImRotateConstraint : public IImGizmoConstraint
{
public:
	explicit FImRotateConstraint(const FVector& InWorldAxis) : WorldAxis(InWorldAxis.GetSafeNormal()) {}
	virtual FTransform Apply(const FTransform& Start, const FImProjector& FrozenProj,
	                         FVector2f CursorStartVP, FVector2f CursorNowVP, bool& bOutValid) const override;
	const FVector& GetWorldAxis() const { return WorldAxis; }
	// Signed world-axis angle (radians) for a start→now cursor drag about the projected anchor. Used by Apply and
	// reusable by the HUD to render the swept arc.
	double SignedDeltaAngle(const FImProjector& FrozenProj, const FVector& Anchor,
	                        FVector2f CursorStartVP, FVector2f CursorNowVP) const;
private:
	FVector WorldAxis;
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
