// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Math/Transform.h"

namespace ImSlate
{
class SImSlate3DArrow;
class SImSlate3DConstraintHandle;
class IImSlate3DHittable;

/** How the handles are oriented for RENDERING (the drag math stays world-axis either way). */
enum class EImAxisSpace : uint8
{
	World,   // handles always face world X/Y/Z (rotation stripped from the render transform)
	Local,   // handles spin with the object's rotation (full transform → ring turns, box swings)
};

/** What kind of edit a dragging handle performs — drives the HUD's per-kind overlay (axis ruler / plane outline /
 *  rotation arc). None = nothing dragging. */
enum class EImDragKind : uint8
{
	None,
	Move,    // axis arrow / single axis → axis ruler with ticks
	Plane,   // plane square (two axes) → start-position outline
	Scale,   // axis box → axis ruler (factor)
	Rotate,  // ring (one axis) → swept-angle arc + degrees
};

/** A live drag in progress: its origin (Start, the ghost) and current value (Now). bDragging=false when idle.
 *  Aggregated by the gizmo from whichever handle/arrow is dragging; consumed by the HUD overlay. Kind + AxisU/V
 *  describe WHICH handle so the HUD can draw the matching ruler/outline/arc; CursorStartVP/NowVP feed the rotate
 *  arc (it needs the actual cursor sweep, not just the transform delta). */
struct FImGizmoDragState
{
	bool        bDragging = false;
	EImDragKind Kind = EImDragKind::None;
	FTransform  Start = FTransform::Identity;  // transform at mouse-down (ghost origin)
	FTransform  Now   = FTransform::Identity;  // live transform
	FVector     AxisU = FVector(1, 0, 0);      // primary world axis (Move/Scale/Rotate use this; Plane uses U&V)
	FVector     AxisV = FVector(0, 1, 0);      // secondary world axis (Plane only)
	FVector2f   CursorStartVP = FVector2f::ZeroVector;  // rotate arc: cursor sweep start (viewport px)
	FVector2f   CursorNowVP   = FVector2f::ZeroVector;  // rotate arc: cursor sweep now
};

/**
 * FImSlate3DGizmo — a standalone 3-axis world transform gizmo (Stage A: MOVE). It owns three independent
 * SImSlate3DArrow widgets (world X/Y/Z) anchored at one shared world point and keeps them in sync.
 *
 * DATA CONTRACT (the "all-in-one" form the user asked for): the gizmo does NOT own the transform — it talks
 * to the owner through two callbacks built on ONE FTransform:
 *   QueryTransform()       → the gizmo reads the current transform each refresh to place the axes.
 *   EditTransform(Wanted)  → the gizmo hands the transform it WANTS after a drag; the owner may CLAMP it
 *                            (grid snap / range / axis lock / collision) and returns the value actually applied;
 *                            the gizmo shows that. Move/rotate/scale all flow through this one FTransform channel
 *                            (Stage A only touches Location; B/C/D fill Rotation/Scale — contract unchanged).
 * With no callbacks set, an internal FTransform is used (simple fallback).
 *
 * ENGINE BOUNDARY: this class is pure ImSlate3D logic — it builds the arrows and syncs them, but it does NOT
 * add them to the viewport (that's an Engine touch). The caller takes GetArrowWidgets() and adds/removes them
 * (and registers them with FImSlate3DHitManager), then calls Refresh() each frame (or on camera/transform change).
 */
class IMSLATE3D_API FImSlate3DGizmo : public TSharedFromThis<FImSlate3DGizmo>
{
public:
	DECLARE_DELEGATE_RetVal(FTransform, FOnQueryTransform);                            // read current
	DECLARE_DELEGATE_RetVal_OneParam(FTransform, FOnEditTransform, const FTransform&); // write+clamp → actual

	FImSlate3DGizmo();

	/** Build the three axis arrows (world X/Y/Z, red/green/blue). Call once. The arrows are owned here; the
	 *  caller adds the returned widgets to the viewport + registers them with the hit manager. */
	void BuildAxes();

	/** The three axis arrow widgets, in X,Y,Z order, for the caller to add to the viewport + register. */
	const TArray<TSharedPtr<SImSlate3DArrow>>& GetArrowWidgets() const { return Arrows; }

	/** Per-frame (or on change): re-anchor all three axes at the current transform's location (QueryTransform,
	 *  else the internal fallback). Dragging an axis already moved the shared location; this propagates it to
	 *  the non-dragged axes. */
	void Refresh();

	/** Owner data hooks (optional). When unset, the gizmo uses its internal transform. */
	void SetQueryTransform(const FOnQueryTransform& In) { QueryTransform = In; }
	void SetEditTransform(const FOnEditTransform& In)   { EditTransform = In; }

	/** Fallback transform when no callbacks are bound (or to seed the initial location). */
	void SetTransform(const FTransform& In) { InternalXform = In; }
	FTransform GetTransform() const;

	/** Appearance (applied at BuildAxes). */
	void SetAxisLengths(float ShaftLen, float ShaftWid, float HeadLen, float HeadWid);

	/** Uniform on-screen scale for the WHOLE gizmo (arrows + rings + handles). 1 = the built-in pixel sizes; set
	 *  >1 to make the gizmo bigger on screen. Apply BEFORE BuildAxes/Build*Handles (it multiplies their pixel
	 *  sizes). Demos derive it from the view height so the gizmo occupies a target fraction of the screen. */
	void SetUniformScale(float In) { UniformScale = FMath::Max(In, 0.01f); }
	float GetUniformScale() const { return UniformScale; }

	/** Local = handles spin with the object's rotation; World = handles stay axis-aligned. Takes effect on the
	 *  next Refresh (the render transform pushed to handles is stripped of rotation in World mode). */
	void SetAxisSpace(EImAxisSpace In) { AxisSpace = In; }
	EImAxisSpace GetAxisSpace() const { return AxisSpace; }

	// --- Constraint-driven handles (scale / plane / rotate) — the SImSlate3DConstraintHandle path. Each is a
	// shape (box/ring) + a constraint (scale/plane/rotate). They share OnConstraintEdited (one FTransform clamp)
	// and re-anchor via RefreshConstraintHandles. Caller adds GetConstraintHandleWidgets() to the viewport +
	// registers them with the hit manager (same as the arrows). ---
	void BuildScaleHandles();   // three world-axis box handles (X/Y/Z) bound to FImScaleConstraint
	void BuildPlaneHandles();   // three plane box handles (XY/YZ/ZX) bound to FImPlaneConstraint
	void BuildRotateHandles();  // three rings (about X/Y/Z) bound to FImRotateConstraint
	const TArray<TSharedPtr<SImSlate3DConstraintHandle>>& GetConstraintHandleWidgets() const { return ConstraintHandles; }
	void RefreshConstraintHandles();  // re-anchor all constraint handles at the current transform location

	// --- Drag HUD (ghost origin + start→now delta). The gizmo owns a fullscreen HitTestInvisible overlay that
	// draws, while any handle/arrow is dragging, a faint ghost at the drag's start transform plus a numeric
	// readout (Δloc / Δscale / Δangle). Caller adds GetHudWidget() to the viewport on TOP (no hit registration). ---
	void BuildHud();                                       // create the overlay widget (call once, after Build*Handles)
	const TSharedPtr<class SImSlate3DGizmoHud>& GetHudWidget() const { return Hud; }
	FImGizmoDragState QueryDragState() const;              // who's dragging + start/now (HUD reads this each paint)

private:
	TArray<TSharedPtr<SImSlate3DArrow>> Arrows;  // [0]=X [1]=Y [2]=Z (move, existing path)
	TArray<TSharedPtr<SImSlate3DConstraintHandle>> ConstraintHandles;  // scale/plane/rotate (new path)
	TSharedPtr<class SImSlate3DGizmoHud> Hud;     // fullscreen ghost/delta overlay (no hit-testing)

	// Add one constraint handle (shared by Build*Handles). Wires QueryStart=GetTransform, Edited=OnConstraintEdited.
	// Kind/AxisU/AxisV are HUD descriptors so the overlay can draw the matching ruler/outline/arc.
	void AddConstraintHandle(const TSharedRef<class IImHandleShape>& Shape,
		const TSharedRef<class IImGizmoConstraint>& Constraint, const FLinearColor& Color, int32 Order,
		EImDragKind Kind, const FVector& AxisU, const FVector& AxisV);
	// One clamp point for ALL constraint handles: run the wanted transform through EditTransform, commit.
	void OnConstraintEdited(const FTransform& Wanted);

	FOnQueryTransform QueryTransform;
	FOnEditTransform  EditTransform;
	FTransform InternalXform = FTransform::Identity;
	EImAxisSpace AxisSpace = EImAxisSpace::Local;

	// The transform handles should RENDER against: full in Local, rotation-stripped in World. HitTest uses the
	// same (so what you see is what you click). The drag math (constraint) is unaffected — always world-axis.
	FTransform RenderTransformForHandles() const;

	float AxisShaftLength = 120.f, AxisShaftWidth = 10.f, AxisHeadLength = 40.f, AxisHeadWidth = 40.f;
	float UniformScale = 1.f;   // multiplies every part's on-screen pixel size (whole-gizmo screen scale)

	// Drag write-back: an axis dragged to NewLoc → build the wanted transform (current rotation/scale + NewLoc)
	// → run it through EditTransform (clamp) → store the actual. Refresh() then re-anchors all axes there.
	void OnAxisMoved(const FVector& NewLoc);
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
