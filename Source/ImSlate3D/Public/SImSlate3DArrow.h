// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ImSlate3DHitManager.h"   // IImSlate3DHittable

namespace ImSlate
{
class SImSlate3DTransformBox;
class FIm3DShaderElement;

/** Arrow appearance. Switchable at runtime via imslate.3d.GizmoStyle.
 *  Flat        = a flat arrow (shaft bar + triangle head) on a camera-facing sheet at the anchor.
 *  Volumetric3D= a real 3D cylinder + cone standing out along the world axis (engine-gizmo style). */
enum class EImArrowStyle : uint8 { Flat = 0, Volumetric3D = 1 };
IMSLATE3D_API EImArrowStyle GetIm3DArrowStyle();

// Standalone-mode drag callback: fired each frame with the new world position while the arrow is dragged.
DECLARE_DELEGATE_OneParam(FOnGizmoPositionChanged, const FVector& /*NewWorldPos*/);

/**
 * SImSlate3DArrow — ONE transform-gizmo arrow, an INDEPENDENT 3D widget added to the viewport on its own.
 * Dragging it translates the target panel's world location along a world axis. Each arrow is composable and
 * the render can later be batched.
 *
 * Overlap / hit-test: arrows (and the panel) are full-screen viewport widgets that overlap, and Slate won't
 * fall an off-shape Unhandled through to a lower OVERLAPPING widget (see ImSlate3DHitManager.h). So every 3D
 * unit registers with FImSlate3DHitManager and implements IImSlate3DHittable; whichever unit is front-most
 * receives the raw mouse event and forwards it to the manager, which walks all units in overlap order and
 * lets the first PER-PIXEL hit one act. So all three arrows respond, gaps fall through. (JanSeliv/
 * CustomShapeButton manager pattern.)
 *
 * Per-pixel shape = the arrow polygon (shaft quad + head triangle) projected to screen, point-in-polygon —
 * equivalent to the alpha-texture sampling CustomShapeButton does, but exact for a known shape.
 *
 * Borrows the target panel's CachedProjector (same camera) to project its world geometry. Event-driven only.
 */
class IMSLATE3D_API SImSlate3DArrow : public SLeafWidget, public IImSlate3DHittable
{
public:
	SLATE_BEGIN_ARGS(SImSlate3DArrow)
		: _WorldAxis(FVector(0, 1, 0))
		, _Color(FLinearColor::Red)
		, _OverlapOrder(0)
		, _ShaftLength(120.f), _ShaftWidth(10.f), _HeadLength(40.f), _HeadWidth(40.f)
		, _VolumeHint(true), _VolumeStrength(1.f)
	{}
		SLATE_ARGUMENT(TSharedPtr<SImSlate3DTransformBox>, Target)  // embedded mode: borrow panel projector+anchor
		SLATE_ARGUMENT(FVector, WorldAxis)
		SLATE_ARGUMENT(FLinearColor, Color)
		SLATE_ARGUMENT(int32, OverlapOrder)
		SLATE_ARGUMENT(float, ShaftLength)
		SLATE_ARGUMENT(float, ShaftWidth)
		SLATE_ARGUMENT(float, HeadLength)
		SLATE_ARGUMENT(float, HeadWidth)
		SLATE_ARGUMENT(bool, VolumeHint)        // draw the pseudo-3D end-cap ellipses
		SLATE_ARGUMENT(float, VolumeStrength)   // 0..1 — scales cap axial bulge AND cap opacity (0=off, 1=full)
		// --- Standalone mode (no Target): the arrow self-projects from the scene camera and pivots about a
		// caller-supplied world point, dragging it along WorldAxis (viewport-top A-line, imslate.3d demo). ---
		SLATE_ARGUMENT(bool, SelfProjector)     // true = standalone: build own projector, anchor at AnchorWorld
		SLATE_ARGUMENT(FVector, AnchorWorld)    // standalone: the world point the gizmo sits at / drags
		SLATE_ARGUMENT(float, WorldUnitScale)   // standalone: world units per arrow px (embedded uses panel's)
		// When the axis points toward the camera (back-facing), draw the arrow along -WorldAxis instead so it
		// reads as the visible (near) half of the world axis. RENDER + HIT use this flipped axis together (so
		// what you see is what you click); DRAG is unaffected (ComputeAxisT projects the axis LINE, sign-agnostic).
		SLATE_ARGUMENT(bool, ReverseWhenBackface)  // gizmo axes set this true; default false keeps old behavior
		SLATE_EVENT(FOnGizmoPositionChanged, OnPositionChanged)  // standalone: fired with the new world position during drag
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SImSlate3DArrow();

	// Standalone mode: update the anchored world point each frame (ignored while dragging — the drag owns it).
	void SetAnchorWorld(const FVector& InAnchorWorld);
	const FVector& GetAnchorWorld() const { return AnchorWorld; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }

	// Front-most arrow receives these and forwards to the manager (which walks all units in overlap order).
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	// Hide the cursor during a high-precision drag (pinned cursor). Conditional on high precision actually
	// being in effect (PIE blocks it → keep cursor visible so it isn't lost).
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	// --- IImSlate3DHittable ---
	virtual int32 GetOverlapOrder() const override { return OverlapOrder; }
	virtual FBox2D GetScreenBoundsVP() const override;
	virtual bool IsPixelHovered(const FPointerEvent& Event, FVector2f CursorVP) const override;
	virtual void HandleHitEvent(FReply& OutReply, const FPointerEvent& Event, FVector2f CursorVP,
		const TFunctionRef<FReply(IImSlate3DHittable&, FVector2f)>& Callback) override;
	virtual FReply OnHitMouseButtonDown(const FPointerEvent& Event, FVector2f CursorVP) override;
	virtual FReply OnHitMouseMove(const FPointerEvent& Event, FVector2f CursorVP) override;
	virtual FReply OnHitMouseButtonUp(const FPointerEvent& Event, FVector2f CursorVP) override;
	virtual bool IsActivelyDragging() const override { return bDragging; }
	// Hover is the manager's single GlobalHoveredPath; this leaf unit's hover path is just itself when pixel-hit.
	virtual bool BuildHoverPath(FVector2f CursorVP, TArray<FArrangedWidget>& OutPath) const override;

	/** The world location this axis sat at when its drag began (move gizmo ghost origin). Valid while dragging. */
	const FVector& GetDragStartWorldLoc() const { return DragStartWorldLoc; }
	/** The world axis this arrow moves along (HUD ruler descriptor). */
	const FVector& GetWorldAxis() const { return WorldAxis; }
	/** Re-orient the arrow's world axis (gizmo Local mode: axis = Rotation × base axis). Ignored while dragging
	 *  (the active axis owns its direction for the duration of the drag). Render + move both read WorldAxis → stay
	 *  consistent. */
	void SetWorldAxis(const FVector& In) { if (!bDragging) { WorldAxis = In.GetSafeNormal(); } }

private:
	TWeakPtr<SImSlate3DTransformBox> Target;
	// Standalone mode (no Target): self-built projector + caller-owned anchor.
	bool      bSelfProjector = false;
	FVector   AnchorWorld = FVector::ZeroVector;   // the world point this gizmo sits at / drags (updated on drag)
	float     WorldUnitScale = 1.f;                // world units per arrow px (standalone)
	FOnGizmoPositionChanged OnPositionChanged;     // standalone: drag writes the new position back through this
	mutable FImProjector SelfProjector;            // rebuilt each frame in standalone mode
	mutable bool bSelfProjectorValid = false;
	FVector WorldAxis = FVector(0, 1, 0);
	bool    bReverseWhenBackface = false;  // draw/hit along -WorldAxis when the axis faces the camera (gizmo)
	FLinearColor Color = FLinearColor::Red;
	int32 OverlapOrder = 0;
	float ShaftLength = 120.f, ShaftWidth = 10.f, HeadLength = 40.f, HeadWidth = 40.f;
	bool  bVolumeHint = true;       // pseudo-3D end-cap ellipses on/off (per arrow)
	float VolumeStrength = 1.f;     // 0..1 — cap bulge + opacity scale

	// NO bHovered — hover is the manager's GlobalHoveredPath (IsWidgetHovered). Only drag state lives here.
	bool      bDragging = false;
	int32     DragPointerIndex = INDEX_NONE;  // which pointer/finger owns the active drag (multi-touch isolation)
	bool      bDragHighPrecision = false;
	FVector   DragStartWorldLoc = FVector::ZeroVector;
	// World position of the ARROW's anchor (gizmo bottom-left) at mouse-down — the axis line for ComputeAxisT
	// must pass through THIS, not the panel's WorldLocation (pivot). They differ by a fixed world offset, which
	// was projecting to a fixed screen offset → grabReproj never matched the cursor (dGrab≈const). (R: drag anchor)
	FVector   DragStartAnchorW = FVector::ZeroVector;
	FVector2f DragAnchorScreenPos = FVector2f::ZeroVector;  // desktop px at mouse-down (warp-back on release, mouse path)
	double    DragStartAxisT = 0.0;   // axis parameter at the ANCHOR (first valid drag frame), absolute basis
	bool      bDragStartAxisTValid = false;
	// startT is captured on the FIRST UpdateDrag frame (not mouse-down): fake-touch jumps between the down
	// event and the first move event, so anchoring to the down position makes step jump on frame 1. Anchoring
	// to the first move frame → step starts at 0 and the grab point stays under the cursor. (R: drag jump)
	bool      bDragStartTPending = false;
	// Mouse (high-precision infinite-scrub) path: a virtual cursor in viewport px we accumulate raw deltas
	// onto (so it can leave the screen → infinite scrub). fake-touch path ignores this and uses the real cursor.
	FVector2f VirtualCursorVP = FVector2f::ZeroVector;
	mutable double LastLoggedStep = 0.0;   // [arrowDrag] log throttle (only on meaningful step change)
	// FROZEN projector snapshot taken at mouse-down. The drag updates the panel's WorldLocation every frame,
	// which rebuilds the panel's live CachedProjector → its screen axis line (and thus the px→world calibration)
	// would shift each frame, making step jump as the widget moves (positive feedback). Freezing the projector
	// for the drag's duration keeps the calibration constant: step then depends only on the cursor. (R: drag projector)
	ImSlate::FImProjector DragFrozenProjector;
	bool                  bDragFrozenProjectorValid = false;

	// Ray–axis closest-point: world axis parameter t for a deprojected ray from CursorVP (viewport px).
	// False if degenerate (ray ~parallel to the axis). Robust for ALL axes (incl. camera-facing).
	bool ComputeAxisT(FVector2f CursorVP, double& OutT) const;
	// Raw cursor delta (desktop px) → viewport px (for the high-precision mouse path's virtual cursor).
	FVector2f RawDeltaToViewportPx(const FPointerEvent& Event) const;

	bool GetProjector(const class FImProjector*& OutProj, TSharedPtr<SImSlate3DTransformBox>& OutTarget) const;
	void GetWorldSegment(const FImProjector& Proj, FVector& OutAnchorW, FVector& OutTipW) const;
	// OutShaftBaseW / OutShaftEndW (optional): the WORLD points of the shaft's two ends (base = at the anchor side,
	// end = shaft/head junction), already accounting for flip & scale & PosDir. The volume end-caps reuse these so
	// their position is computed in ONE place (the body), never drifting from the drawn arrow.
	bool BuildArrowScreenShape(const FImProjector& Proj, TArray<FVector2f>& OutPoly,
		FVector* OutShaftBaseW = nullptr, FVector* OutShaftEndW = nullptr) const;
	// The axis to DRAW/HIT along this frame: WorldAxis, flipped to -WorldAxis when bReverseWhenBackface and the
	// axis faces the camera (so the visible near-half is drawn). Render + hit use this together; drag stays on
	// the raw WorldAxis. AnchorW = the gizmo's world anchor (for the facing test).
	FVector EffectiveRenderAxis(const FImProjector& Proj, const FVector& AnchorW) const;
	// Shaped hit-test for a DESKTOP-space cursor (converts to viewport px internally).
	bool HitTestArrowShapeScreen(const FVector2f& ScreenSpacePos) const;
	bool HitTestArrowShapeVP(const FVector2f& CursorVP) const;  // CursorVP already in viewport px
	FVector2f ScreenToViewportPx(const FVector2f& ScreenSpacePos) const;

	// Drag actions invoked via the manager callback.
	void BeginDrag(const FPointerEvent& Event);
	void UpdateDrag(const FPointerEvent& Event);
	void EndDrag();

	mutable TSharedPtr<FIm3DShaderElement, ESPMode::ThreadSafe> KeepAliveShaderElement;

public:
	/** The arrow's screen-space (viewport physical px) AABB this frame — used by SImSlate3DArrowHitClip to
	 *  push a clip so the arrow only occupies its bounds in the hit grid (everything else falls through). */
	FBox2D GetScreenBoundsForClip() const { return GetScreenBoundsVP(); }
	TSharedPtr<SImSlate3DTransformBox> GetTarget() const { return Target.Pin(); }
};

/**
 * SImSlate3DArrowHitClip — thin parent wrapper (SelfHitTestInvisible) that, each paint, pushes the arrow's
 * projected screen AABB as a clip BEFORE painting the arrow. The arrow records that clip as its
 * InitialClipState, so the hit grid skips the arrow OUTSIDE its bounds → clicks there fall through to lower
 * widgets (other arrows / panel / game UI) instead of the full-screen arrow widget eating everything. Same
 * mechanism as SImSlate3DHitClip for the panel (R011). Inside the bounds, overlaps are resolved per-pixel by
 * FImSlate3DHitManager. Without this, an independent full-screen arrow widget blocks all UI behind it.
 */
class IMSLATE3D_API SImSlate3DArrowHitClip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlate3DArrowHitClip) {}
		SLATE_ARGUMENT(TSharedPtr<SImSlate3DArrow>, Arrow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TSharedPtr<SImSlate3DArrow> Arrow;
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
