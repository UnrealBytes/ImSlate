// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "ImSlate3DHitManager.h"        // IImSlate3DHittable
#include "ImSlate3DDragInput.h"         // FImDragInputState
#include "ImSlate3DGizmo.h"             // EImDragKind (HUD descriptor)
#include "Math/Transform.h"

namespace ImSlate
{
class IImGizmoConstraint;
class IImHandleShape;

/**
 * SImSlate3DConstraintHandle — a standalone gizmo handle assembled from three orthogonal pieces:
 *   • IImHandleShape    — what it looks like + where you can click (box / ring / ...).
 *   • IImGizmoConstraint — what a drag does (axis / plane / scale / rotate), the reverse-transform math.
 *   • FImDragInputState  — the input-device layer (frozen projector / raw-delta scrub / fake-touch / multi-touch).
 *
 * It self-projects from the scene camera (like SImSlate3DArrow standalone), registers as an IImSlate3DHittable
 * unit, and on drag: snapshots the current transform (via QueryStart), feeds frozen-projector + cursor to the
 * constraint each frame, and pushes the resulting FTransform out through OnTransformEdited (→ gizmo's clamp).
 * Hover/highlight read the manager's GlobalHoveredPath (single source). The arrow keeps its own widget for
 * move; this is the shared body for scale/plane/rotate (and could host move later).
 */
class IMSLATE3D_API SImSlate3DConstraintHandle : public SLeafWidget, public IImSlate3DHittable
{
public:
	DECLARE_DELEGATE_RetVal(FTransform, FOnQueryStartTransform);          // gizmo: current transform (drag basis)
	DECLARE_DELEGATE_OneParam(FOnTransformEdited, const FTransform&);     // gizmo: apply the constrained transform

	SLATE_BEGIN_ARGS(SImSlate3DConstraintHandle)
		: _Color(FLinearColor::White), _OverlapOrder(0), _WorldUnitScale(0.f), _HighPrecision(false)
		, _DragKind(EImDragKind::None), _AxisU(FVector(1, 0, 0)), _AxisV(FVector(0, 1, 0)) {}
		SLATE_ARGUMENT(FLinearColor, Color)
		SLATE_ARGUMENT(int32, OverlapOrder)
		SLATE_ARGUMENT(float, WorldUnitScale)     // 0 = screen-constant size
		SLATE_ARGUMENT(bool, HighPrecision)       // mouse path: raw-delta infinite scrub (false for scale/rotate is fine)
		SLATE_ARGUMENT(EImDragKind, DragKind)     // HUD descriptor: which overlay to draw while dragging
		SLATE_ARGUMENT(FVector, AxisU)            // HUD descriptor: primary world axis (Move/Scale/Rotate)
		SLATE_ARGUMENT(FVector, AxisV)            // HUD descriptor: secondary world axis (Plane only)
		SLATE_EVENT(FOnQueryStartTransform, OnQueryStartTransform)
		SLATE_EVENT(FOnTransformEdited, OnTransformEdited)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<IImHandleShape>& InShape,
		const TSharedRef<IImGizmoConstraint>& InConstraint);
	virtual ~SImSlate3DConstraintHandle();

	// Push the gizmo's full current transform (location + rotation + scale). The shape renders/hit-tests against
	// ALL of it (anchor + rotated axes + scaled offset), so the handle visually tracks the edit it drives. Frozen
	// while this handle is the one dragging (it edits off its own DragStartTransform snapshot, not the live one).
	void SetTransform(const FTransform& In) { if (!Input.IsDragging()) { GizmoTransform = In; } }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }

	// 2D entry points (like SImSlate3DArrow): forward to the shared hit manager; Unhandled when nothing hit
	// → click passes through (no fullscreen blocking despite being a viewport-filling leaf).
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// Capture lost (Alt+Tab / focus loss / dragged off) → end the drag, else Input stays "dragging" forever and
	// SetTransform refuses every refresh → the handle freezes at its drag-start position. (R: stuck handle)
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	// IImSlate3DHittable
	virtual int32 GetOverlapOrder() const override { return OverlapOrder; }
	virtual FBox2D GetScreenBoundsVP() const override;
	virtual bool IsPixelHovered(const FPointerEvent& Event, FVector2f CursorVP) const override;
	virtual void HandleHitEvent(FReply& OutReply, const FPointerEvent& Event, FVector2f CursorVP,
		const TFunctionRef<FReply(IImSlate3DHittable&, FVector2f)>& Callback) override;
	virtual FReply OnHitMouseButtonDown(const FPointerEvent& Event, FVector2f CursorVP) override;
	virtual FReply OnHitMouseMove(const FPointerEvent& Event, FVector2f CursorVP) override;
	virtual FReply OnHitMouseButtonUp(const FPointerEvent& Event, FVector2f CursorVP) override;
	virtual bool IsActivelyDragging() const override { return Input.IsDragging(); }
	virtual bool BuildHoverPath(FVector2f CursorVP, TArray<FArrangedWidget>& OutPath) const override;

	/** The transform snapshot taken at mouse-down (the drag's origin). Valid while IsActivelyDragging(); used by
	 *  the gizmo HUD to draw the ghost + the start→now delta. */
	const FTransform& GetDragStartTransform() const { return DragStartTransform; }

	// HUD descriptors (set at Construct): which overlay to draw + the world axes involved + the live cursor sweep.
	EImDragKind GetDragKind() const { return DragKind; }
	const FVector& GetAxisU() const { return AxisU; }
	const FVector& GetAxisV() const { return AxisV; }
	FVector2f GetCursorStartVP() const { return Input.GetCursorStartVP(); }
	FVector2f GetCursorNowVP() const { return LastCursorNowVP; }

private:
	TSharedPtr<IImHandleShape> Shape;
	TSharedPtr<IImGizmoConstraint> Constraint;
	FImDragInputState Input;

	FLinearColor Color = FLinearColor::White;
	int32 OverlapOrder = 0;
	float WorldUnitScale = 0.f;
	bool  bHighPrecision = false;
	FTransform GizmoTransform = FTransform::Identity;      // gizmo's live transform (render/hit basis)
	FTransform DragStartTransform = FTransform::Identity;  // snapshot at mouse-down
	EImDragKind DragKind = EImDragKind::None;             // HUD descriptor
	FVector AxisU = FVector(1, 0, 0), AxisV = FVector(0, 1, 0);  // HUD descriptor axes
	FVector2f LastCursorNowVP = FVector2f::ZeroVector;    // cached each drag frame (HUD rotate arc)

	FOnQueryStartTransform OnQueryStartTransform;
	FOnTransformEdited     OnTransformEdited;

	mutable FImProjector SelfProjector;
	mutable bool bSelfProjectorValid = false;
	// MUST keep the shader element alive past OnPaint: the draw element holds it as a TWeakPtr, so a local
	// TSharedPtr would die at OnPaint's end and Draw_RenderThread's Pin() would get null → nothing drawn.
	// (This is why scale/plane/rotate handles were invisible despite OnPaint running + quads built.)
	mutable TSharedPtr<class FIm3DShaderElement, ESPMode::ThreadSafe> KeepAliveElem;

	// Build/refresh the standalone projector from the scene camera at AnchorWorld; false if no camera.
	bool ResolveProjector(const FImProjector*& OutProj) const;
	// World units per handle px for screen-constant size (WorldUnitScale==0) or the fixed scale.
	double HandleWorldScale(const FImProjector& Proj) const;
	// Desktop px → viewport px (standalone: via the host ViewRect, mirrors arrow's standalone mapping).
	FVector2f ScreenToVP(const FVector2f& ScreenPx) const;
	FVector2f RawDeltaToVP(const FPointerEvent& Event) const;
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
