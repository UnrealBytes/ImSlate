// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Layout/Geometry.h"   // FGeometry (MapDesktopToViewportPx)
#include "Layout/ArrangedWidget.h"   // FArrangedWidget (hover path entries)

namespace ImSlate
{
/**
 * Screen-space pointer (mouse cursor OR touch point — FPointerEvent::GetScreenSpacePosition, unified for both,
 * desktop and mobile alike) → viewport PHYSICAL px (the ConstrainedViewRect space FImProjector lives in).
 * The SINGLE source of this conversion — every 3D hit path must go through it so the pointer space matches the
 * projector's. `RefGeom` must be a FULLSCREEN widget's geometry (its local origin == viewport origin), so the
 * result is absolute viewport px independent of where any individual 3D unit sits. (When RefGeom is fullscreen,
 * AbsoluteToLocal*Scale == absolute viewport px; a non-fullscreen widget's geometry would subtract its own
 * offset — the bug this function exists to avoid. Mirrors FSceneViewport::UpdateCachedCursorPos.)
 */
inline FVector2f MapScreenToViewportPx(const FGeometry& RefGeom, const FIntRect& ViewRect,
	const UE::Slate::FDeprecateVector2DParameter& ScreenPos)
{
	const FVector2f Local = (FVector2f)RefGeom.AbsoluteToLocal(ScreenPos);
	return Local * RefGeom.Scale + FVector2f((float)ViewRect.Min.X, (float)ViewRect.Min.Y);
}

/**
 * IImSlate3DHittable — anything that participates in the shared 3D hit dispatch (independent 3D widgets:
 * the panel, each gizmo arrow, any future 3D unit). Each provides a per-pixel/shaped hit-test and a way to
 * handle the event when it (and only it) is the front-most pixel-hit.
 *
 * Why a manager instead of plain Slate routing: Slate's hit grid routes only the front-most overlapping
 * widget down a fixed path, and an off-shape Unhandled does NOT fall through to a lower OVERLAPPING widget
 * (HittestGrid GetBubblePath picks one widget + parent chain; SlateApplication bubbles that fixed path only).
 * So N full-screen 3D widgets would let only the top one ever respond. This mirrors JanSeliv/CustomShapeButton:
 * one front-most widget receives the event and asks the manager to walk ALL registered hittables in overlap
 * order, letting the first PIXEL-hit one handle it — true per-pixel + correct fall-through across overlaps.
 */
class IImSlate3DHittable
{
public:
	virtual ~IImSlate3DHittable() = default;

	/** Higher = on top (checked first). Stable sort key for overlap resolution. */
	virtual int32 GetOverlapOrder() const = 0;

	/** BROAD phase: this unit's screen-space (viewport physical px) axis-aligned bounding box this frame.
	 *  The manager rejects the cursor against this cheap AABB before running the (costly) per-pixel test.
	 *  Return an empty/invalid box (bIsValid=false) to always fall through to the per-pixel test. */
	virtual FBox2D GetScreenBoundsVP() const = 0;

	/** NARROW phase: per-pixel / shaped hit-test. CursorVP = the cursor already in viewport physical px (the
	 *  space GetScreenBoundsVP() / FImProjector live in), computed ONCE by the gateway via MapScreenToViewportPx
	 *  — units must NOT re-derive it from their own (possibly non-fullscreen) geometry. Only called when the AABB
	 *  broad phase passed. */
	virtual bool IsPixelHovered(const FPointerEvent& Event, FVector2f CursorVP) const = 0;

	/** Called by the manager (in overlap order) for CLICKS (down/up). If OutReply is already handled (an
	 *  earlier, higher unit won), bow out. Otherwise, if IsPixelHovered (or actively dragging), run the action
	 *  (via Callback) and set OutReply = Handled. CursorVP = viewport-px cursor (see IsPixelHovered).
	 *  NOTE: this no longer maintains hover — hover is owned wholly by the manager (UpdateGlobalHover). */
	virtual void HandleHitEvent(FReply& OutReply, const FPointerEvent& Event, FVector2f CursorVP,
		const TFunctionRef<FReply(IImSlate3DHittable&, FVector2f)>& Callback) = 0;

	/** Action methods the Callback dispatches to (one per mouse event kind). CursorVP = viewport-px cursor. */
	virtual FReply OnHitMouseButtonDown(const FPointerEvent& Event, FVector2f CursorVP) = 0;
	virtual FReply OnHitMouseMove(const FPointerEvent& Event, FVector2f CursorVP) = 0;
	virtual FReply OnHitMouseButtonUp(const FPointerEvent& Event, FVector2f CursorVP) = 0;

	/** Wheel routed by the manager to the front-most pixel-hit unit. The unit bubbles it through its own hit
	 *  chain (Slate parity). Default Unhandled (a leaf unit with nothing scrollable, e.g. an arrow). */
	virtual FReply OnHitMouseWheel(const FPointerEvent& Event, FVector2f CursorVP) { return FReply::Unhandled(); }

	/** True while this unit is mid-drag (must keep receiving move/up even off-shape). */
	virtual bool IsActivelyDragging() const = 0;

	/** HOVER (single source of truth lives in the manager): given the cursor in viewport px, append this
	 *  unit's hovered widget chain (root→leaf) to OutPath. Return true if the cursor hit this unit (OutPath
	 *  got entries). The manager calls this on the FRONT-MOST pixel-hit unit only, diffs the result against
	 *  this pointer's stored HoveredByPointer chain, and fires Enter/Leave. A unit with no sub-widgets (A-route arrow)
	 *  appends just itself. Default: no hover path. */
	virtual bool BuildHoverPath(FVector2f CursorVP, TArray<FArrangedWidget>& OutPath) const { return false; }
};

/**
 * FImSlate3DHitManager — process-wide registry + dispatcher for IImSlate3DHittable. A registered front-most
 * widget forwards each mouse event here; the manager walks all hittables in descending overlap order and the
 * first pixel-hit one handles it. Pure game-thread, event-driven (no tick / no polling).
 */
class IMSLATE3D_API FImSlate3DHitManager
{
public:
	static FImSlate3DHitManager& Get();

	void Register(const TWeakPtr<IImSlate3DHittable>& Hittable);
	void Unregister(const IImSlate3DHittable* Hittable);

	/** Walk registered hittables in overlap order; the first pixel-hit one runs Callback and wins. Returns the
	 *  combined reply (Unhandled if none hit → the click falls through to whatever is behind the 3D layer).
	 *  CursorVP = the cursor in viewport physical px, computed ONCE by the caller (gateway) via
	 *  MapScreenToViewportPx — the single coordinate source all units share. */
	FReply HandleEvent(const FPointerEvent& Event, FVector2f CursorVP, const TFunctionRef<FReply(IImSlate3DHittable&, FVector2f)>& Callback);

	/** Is any registered hittable pixel-hit by this cursor (viewport px)? Lets the caller decide whether to
	 *  claim the event at all (a click in a gap returns Unhandled and falls through to lower viewport widgets). */
	bool AnyPixelHovered(const FPointerEvent& Event, FVector2f CursorVP) const;

	/** SINGLE SOURCE OF TRUTH for 3D hover, PER POINTER INDEX (mouse = 0; each touch finger its own index).
	 *  Recompute the hovered widget chain for this pointer (front-most pixel-hit unit's BuildHoverPath; empty
	 *  if none), diff it against this pointer's stored path, fire OnMouseEnter / OnMouseLeave. Called every
	 *  move (by the entry). Always recomputes from the live pointer, so moving off ALL units → empty → Leave:
	 *  no residue is possible. Multiple fingers each keep an independent hover path. */
	void UpdateGlobalHover(const FPointerEvent& Event, FVector2f CursorVP);

	/** True if Widget is hovered by ANY pointer. Widgets (e.g. gizmo arrows) query this in OnPaint instead of
	 *  keeping their own hover flag — one state, cannot desync. */
	bool IsWidgetHovered(const SWidget* Widget) const;

	/** Clear the hover for this event's pointer index (fire OnMouseLeave on its widgets). Called when the
	 *  pointer leaves the 3D entry's rect (belt-and-suspenders for a teleport/finger-lift with no trailing move). */
	void ClearHover(const FPointerEvent& Event);

	/** Clear ALL pointers' hover (e.g. focus loss / suspend). */
	void ClearAllHover();

	/** Route a mouse-wheel event: walk units in overlap order, the front-most pixel-hit one's leaf widget gets
	 *  OnMouseWheel. No hover, no capture — a wheel is a one-shot hit-and-dispatch. Unhandled if none hit (the
	 *  wheel falls through to lower 2D widgets). CursorVP = viewport px (entry computes via MapScreenToViewportPx). */
	FReply HandleWheel(const FPointerEvent& Event, FVector2f CursorVP);

private:
	// Append order; re-sorted descending by GetOverlapOrder() per dispatch (EnsureSortedForDispatch). Weak so
	// dead widgets drop out. Mutable: the const AnyPixelHovered re-sorts before walking.
	mutable TArray<TWeakPtr<IImSlate3DHittable>> Registered;

	// THE hover state, keyed by pointer index (mouse=0, each finger its own). Value = widget chain (root→leaf)
	// under that pointer across all 3D units. Maintained only by UpdateGlobalHover's per-index set-diff. Weak
	// so a destroyed widget drops out safely.
	TMap<uint32, TArray<TWeakPtr<SWidget>>> HoveredByPointer;

	void EnsureSortedForDispatch() const;  // descending overlap order; called before each walk
	void Compact();  // drop expired weak entries

	// Walk units (overlap order); front-most pixel-hit one fills OutPath with its hover chain. Shared by
	// UpdateGlobalHover (per-pointer hover) and HandleWheel (one-shot route). Returns true if any unit hit.
	bool BuildFrontmostHoverPath(FVector2f CursorVP, TArray<FArrangedWidget>& OutPath) const;
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
