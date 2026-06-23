// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"

namespace ImSlate
{
class FImProjector;
struct FIm3DQuad;

/**
 * IImHandleShape — the RENDER + HIT-TEST strategy of a gizmo handle, ORTHOGONAL to its drag math
 * (IImGizmoConstraint). A constraint handle pairs one shape (what it looks like / where you can click) with
 * one constraint (what a drag does). Box = scale/plane handle; Ring = rotate handle; (Arrow stays in the
 * existing SImSlate3DArrow for now). All work in world space → screen via the projector, screen-constant size.
 *
 * AnchorW = the gizmo's world anchor; WorldScale = world units per handle px (≈ Projector.WorldUnitsPerScreenPx
 * for screen-constant size). bHovered tints brighter.
 */
class IMSLATE3D_API IImHandleShape
{
public:
	virtual ~IImHandleShape() = default;

	// Xform = the gizmo's FULL current transform (location + rotation + scale). The shape applies ALL of it so
	// it tracks edits: its anchor = Xform.Location, its axes = Xform.Rotation × local axes (rotate handle's ring
	// turns, scale box swings with rotation), its scale-handle offset = × Xform.Scale (box moves out as you
	// scale). HandlePx = world-units-per-handle-px for screen-constant on-screen size. (Before: only AnchorW
	// was passed → rotate/scale handles never moved when you dragged them.)
	virtual void BuildQuads(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
		const FLinearColor& Color, bool bHovered, TArray<FIm3DQuad>& OutQuads) const = 0;
	virtual bool HitTestVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
		FVector2f CursorVP) const = 0;
	virtual FBox2D GetScreenBoundsVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx) const = 0;
};

/**
 * FImBoxShape — a flat square billboard sitting at AnchorW + AxisU/AxisV offset. Used for the SCALE handle
 * (one axis: a square out along that axis) and the PLANE handle (two axes: a square in the U×V corner). The
 * square's two in-plane edge directions are AxisU/AxisV (world); for a single-axis scale handle pass the axis
 * as U and a camera-facing perpendicular as V (or let the ctor derive one).
 */
class IMSLATE3D_API FImBoxShape : public IImHandleShape
{
public:
	// AxisU/AxisV are LOCAL axes (e.g. world X/Y); BuildQuads rotates them by Xform.Rotation each frame.
	// OffsetAlongU/V (handle px) place the square's CENTRE; HalfSizePx is the square half-extent (handle px).
	// bScaleByU/V: multiply the offset along that axis by Xform.Scale (the scale handle moves out as you scale).
	FImBoxShape(const FVector& InAxisU, const FVector& InAxisV,
		float InOffsetAlongU, float InOffsetAlongV, float InHalfSizePx,
		bool InScaleByU = false, bool InScaleByV = false)
		: AxisU(InAxisU.GetSafeNormal()), AxisV(InAxisV.GetSafeNormal())
		, OffsetU(InOffsetAlongU), OffsetV(InOffsetAlongV), HalfSizePx(InHalfSizePx)
		, bScaleByU(InScaleByU), bScaleByV(InScaleByV) {}

	virtual void BuildQuads(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
		const FLinearColor& Color, bool bHovered, TArray<FIm3DQuad>& OutQuads) const override;
	virtual bool HitTestVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
		FVector2f CursorVP) const override;
	virtual FBox2D GetScreenBoundsVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx) const override;

private:
	void WorldCorners(const FTransform& Xform, double HandlePx, const FVector& CamPos, FVector OutC[4]) const;
	FVector AxisU, AxisV;          // LOCAL axes (rotated by Xform.Rotation at render)
	float   OffsetU, OffsetV, HalfSizePx;
	bool    bScaleByU, bScaleByV;  // offset scaled by Xform.Scale along U/V (scale handle visual feedback)
};

/**
 * FImDiscShape — a SOLID world disc (centre = anchor + offset along Axis, normal = Axis) drawn as a triangle fan;
 * the SCALE handle. Sits at the axis tip facing along the axis (the disc plane is ⊥ the axis) so it reads as a
 * round cap on the arrow, distinct from the flat plane squares. Offset (handle px) optionally ×Xform.Scale so it
 * slides out as you scale (bScaleByAxis). Hit = cursor inside the projected disc polygon.
 */
class IMSLATE3D_API FImDiscShape : public IImHandleShape
{
public:
	FImDiscShape(const FVector& InAxis, float InOffsetAlongAxis, float InRadiusPx,
		bool InScaleByAxis = false, int32 InSegments = 24)
		: Axis(InAxis.GetSafeNormal()), OffsetPx(InOffsetAlongAxis), RadiusPx(InRadiusPx)
		, bScaleByAxis(InScaleByAxis), Segments(FMath::Clamp(InSegments, 6, 128)) {}

	virtual void BuildQuads(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
		const FLinearColor& Color, bool bHovered, TArray<FIm3DQuad>& OutQuads) const override;
	virtual bool HitTestVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
		FVector2f CursorVP) const override;
	virtual FBox2D GetScreenBoundsVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx) const override;

private:
	// Disc centre (world) + the projected rim points; shared by build/hit/bounds.
	bool ProjectDisc(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
		FVector2f& OutCentre, TArray<FVector2f>& OutRim) const;
	FVector Axis;                  // LOCAL axis (rotated by Xform.Rotation); disc normal + offset dir
	float   OffsetPx, RadiusPx;
	bool    bScaleByAxis;          // offset ×Xform.Scale along Axis (slides out as you scale)
	int32   Segments;
};

/**
 * FImRingShape — a world circle (centre AnchorW, normal = Axis) drawn as a screen ribbon; the ROTATE handle.
 * Hit-test = cursor near the projected ring band (within a px threshold of the polyline).
 */
class IMSLATE3D_API FImRingShape : public IImHandleShape
{
public:
	// Axis is the LOCAL rotation axis; ProjectRing rotates it by Xform.Rotation so the ring turns as you rotate.
	FImRingShape(const FVector& InAxis, float InRadiusPx, float InThicknessPx = 4.f, int32 InSegments = 48)
		: Axis(InAxis.GetSafeNormal()), RadiusPx(InRadiusPx), ThicknessPx(InThicknessPx)
		, Segments(FMath::Clamp(InSegments, 8, 256)) {}

	virtual void BuildQuads(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
		const FLinearColor& Color, bool bHovered, TArray<FIm3DQuad>& OutQuads) const override;
	virtual bool HitTestVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
		FVector2f CursorVP) const override;
	virtual FBox2D GetScreenBoundsVP(const FImProjector& Proj, const FTransform& Xform, double HandlePx) const override;

private:
	bool ProjectRing(const FImProjector& Proj, const FTransform& Xform, double HandlePx,
		TArray<FVector2f>& OutScreenPts) const;
	FVector Axis;                  // LOCAL axis (rotated by Xform.Rotation at render)
	float   RadiusPx, ThicknessPx;
	int32   Segments;
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
