// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "ImSlate3DShapedHit.h"   // IImSlate3DPlaneShaped (per-pixel polygon hit in plane-local space)
#include "SImSlate3DArrow.h"      // FOnGizmoPositionChanged (drag write-back delegate, shared)

namespace ImSlate
{
class SImSlate3DTransformBox;
class FImProjector;

/**
 * SImSlate3DPlaneArrow — ONE transparent in-plane arrow widget for a 3D window. A self-contained, composable
 * primitive: drop N of them into a SImSlate3DTransformBox's content (one per axis) to build a move gizmo, or
 * use one alone. Each carries its own world drag axis + appearance; it self-computes its in-plane direction
 * each frame from the owner box's projector (no box "SetAxis" push). Draws its arrow (shaft bar + triangle
 * head + optional pseudo-3D end-cap ellipse) in widget-LOCAL px via MakeLines; the box's capture→Project
 * pipeline projects those lines onto the 3D plane for free.
 *
 * Transparent: OnPaint only draws the arrow shape (never a background), HitTestPlaneLocal returns true only
 * on the solid body — gaps fall through. Hover/press/drag are standard Slate events routed via the box's hit
 * walk. Dragging captures the mouse, screen-projection-calibrates against a frozen projector, and writes the
 * new world position back through OnPositionChanged (the box, or a shared FVector, owns the actual position).
 */
class IMSLATE3D_API SImSlate3DPlaneArrow : public SLeafWidget, public IImSlate3DPlaneShaped
{
public:
	SLATE_BEGIN_ARGS(SImSlate3DPlaneArrow)
		: _WorldAxis(FVector(0, 1, 0))
		, _Color(FLinearColor::Red)
		, _ShaftLength(90.f), _ShaftWidth(8.f), _HeadLength(28.f), _HeadWidth(26.f), _CapBulge(0.f)
	{}
		SLATE_ARGUMENT(FVector, WorldAxis)          // real world drag axis (also projected to the in-plane dir)
		SLATE_ARGUMENT(FLinearColor, Color)
		SLATE_ARGUMENT(float, ShaftLength)
		SLATE_ARGUMENT(float, ShaftWidth)
		SLATE_ARGUMENT(float, HeadLength)
		SLATE_ARGUMENT(float, HeadWidth)
		SLATE_ARGUMENT(float, CapBulge)             // pseudo-3D end-cap ellipse bulge (0=off)
		SLATE_EVENT(FOnGizmoPositionChanged, OnPositionChanged)  // drag writes the new world position back
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// --- IImSlate3DPlaneShaped: per-pixel arrow hit in widget-local px (the space the box's MapScreenToPlane
	// yields). True only on the solid arrow body; gaps return false → window hit walk falls through. ---
	virtual bool HitTestPlaneLocal(FVector2f PlaneLocalPt) const override { return HitTestSelf(PlaneLocalPt); }
	// Arrow's full plane-local extent (shaft + head + cap ellipse), which sticks out past the panel edge.
	virtual bool GetPlaneLocalBounds(FVector2f& OutMin, FVector2f& OutMax) const override;

	void SetHighlightColor(const FLinearColor& In) { HighlightColor = In; bHasHighlightColor = true; }
	void SetHighlightScale(float In) { HighlightScale = FMath::Max(In, 1.f); }

	// Hover lives in the manager (HitManager::IsWidgetHovered), not on the arrow. Defined in .cpp.
	bool IsHovered2D() const;
	bool IsDragging() const { return bActive; }

	// 7-vertex polygon (shaft quad P0..P3 + head tri P4,P5,P6) in widget-local px. Single source of truth
	// for render (OnPaint) and hit (HitTestSelf).
	void BuildArrowLocalPoly(FVector2f OutPoly[7]) const;
	// End-cap ellipse (centre/major/minor) in widget-local; false if CapBulge<=0.
	bool GetCapEllipse(FVector2f& OutCenter, FVector2f& OutMajor, FVector2f& OutMinor) const;
	// Exact per-pixel hit against this arrow's polygon + cap ellipse.
	bool HitTestSelf(FVector2f WidgetLocalPt) const;

	FVector2f GetAnchorLocal() const { return AnchorLocal; }

	// The box wires itself in so the arrow can borrow the projector / cursor-map + read the panel size to
	// anchor itself. Each paint, ResolveFromOwner() refreshes AnchorLocal + the in-plane Dir from it.
	void SetOwnerBox(const TSharedPtr<SImSlate3DTransformBox>& InBox) { OwnerBox = InBox; }

	// Set/replace the drag write-back delegate after construction (when the owner box — captured by the
	// callback — only exists after the box is built around this arrow).
	void SetOnPositionChanged(const FOnGizmoPositionChanged& In) { OnPositionChanged = In; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	// The arrow anchors at the CENTRE and extends ShaftLength+HeadLength outward along an in-plane direction
	// that depends on the owner's projector (not known at layout time). A square of side 2*reach (centre ±reach)
	// contains the arrow for ANY direction → the 3D box projects the content onto a plane this size, keeping the
	// arrow's local coords inside [0..Size] (so bInsideRect-gated hit-testing works). Also drives window
	// auto-size: a window sized 0 fits to this.
	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const float Reach = ShaftLength + HeadLength + FMath::Max(HeadWidth, ShaftWidth) * 0.5f;
		return FVector2D(2.f * Reach, 2.f * Reach);
	}

	// Only drag (Move while active) / Down / Up. NO OnMouseEnter/OnMouseLeave: the arrow keeps no hover flag;
	// hover is the manager's single GlobalHoveredPath, read via IsWidgetHovered in OnPaint. Can't desync.
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TWeakPtr<SImSlate3DTransformBox> OwnerBox;
	FOnGizmoPositionChanged OnPositionChanged;

	// This arrow's fixed config (from Construct).
	FVector      WorldAxis = FVector(0, 1, 0);  // real world drag axis
	FLinearColor Color = FLinearColor::Red;
	float        ShaftLength = 90.f, ShaftWidth = 8.f, HeadLength = 28.f, HeadWidth = 26.f, CapBulge = 0.f;

	// Per-frame, refreshed from the owner box's projector (ResolveFromOwner): widget-local anchor (bottom-left
	// of the panel) + the world axis projected to the in-plane unit dir. mutable: read in const OnPaint/hit.
	mutable FVector2f AnchorLocal = FVector2f(0.f, 0.f);
	mutable FVector2f Dir = FVector2f(1.f, 0.f);
	// NO bHover — hover is the manager's GlobalHoveredPath (IsWidgetHovered). Only the drag state lives here.
	bool  bActive = false;            // dragging
	int32 DragPointerIndex = INDEX_NONE;  // which pointer/finger owns the active drag (multi-touch isolation)

	float HighlightScale = 1.6f;
	bool  bHasHighlightColor = false;
	FLinearColor HighlightColor = FLinearColor::Yellow;

	// Refresh AnchorLocal + Dir from the owner box's cached projector + size. Returns false if no owner/proj.
	bool ResolveFromOwner() const;
	FLinearColor ResolveColor() const;
	FVector2f AxisTip() const { return AnchorLocal + Dir * (ShaftLength + HeadLength); }

	// Drag state (active while bActive). World axis line through the gizmo anchor's world point; screen-
	// projection calibration against a FROZEN projector so moving the box mid-drag doesn't shift it.
	FImProjector* DragProj = nullptr;
	FVector  DragStartWorldLoc = FVector::ZeroVector;
	FVector  DragAnchorWorld   = FVector::ZeroVector;
	FVector  DragWorldAxis     = FVector::ZeroVector;
	double   DragStartT        = 0.0;
	bool     bDragStartTPending = false;
	void EndDrag();
	bool ComputeDragT(FVector2f CursorVP, double& OutT) const;
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
