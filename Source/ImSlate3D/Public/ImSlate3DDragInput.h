// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Input/Events.h"
#include "ImSlate3D.h"   // FImProjector (frozen snapshot)

namespace ImSlate
{
/**
 * FImDragInputState — the INPUT-DEVICE layer of a gizmo drag, factored out of SImSlate3DArrow so any handle
 * reuses it without rewriting: frozen projector snapshot (constant drag calibration), high-precision raw-delta
 * accumulation onto a virtual cursor (infinite scrub past screen edge), fake-touch absolute cursor, first-frame
 * CursorStart anchoring (kills the down→first-move jump), and multi-touch pointer-index isolation. It is
 * widget-agnostic: the desktop→viewport-px maps are injected as TFunctionRef so this carries no Slate/Engine dep.
 *
 * Produces the two cursor values an IImGizmoConstraint::Apply needs: GetCursorStartVP() and the per-frame
 * ComputeCursorNowVP(). The constraint also takes GetFrozenProjector().
 */
struct IMSLATE3D_API FImDragInputState
{
	// Mouse-down: snapshot the live projector, record pointer index + high-precision mode, seed the virtual
	// cursor. CursorStart is deferred to the first ComputeCursorNowVP frame (fake-touch jumps down→first-move).
	// ScreenToVP: desktop px → viewport px.  bHighPrecision: caller decides (mouse=true / touch=false).
	void Begin(const FPointerEvent& Event, const FImProjector& LiveProjector, bool bInHighPrecision,
		TFunctionRef<FVector2f(const FVector2f&)> ScreenToVP);

	// Per drag frame → the cursor in viewport px for the constraint. High precision: accumulate raw delta onto
	// the virtual cursor. Touch: absolute cursor. First call also fixes CursorStartVP (= this frame) so the
	// first Apply is a no-op (no jump).
	FVector2f ComputeCursorNowVP(const FPointerEvent& Event,
		TFunctionRef<FVector2f(const FVector2f&)> ScreenToVP,
		TFunctionRef<FVector2f(const FPointerEvent&)> RawDeltaToVP);

	void End();

	bool IsDragging() const { return bDragging; }
	const FImProjector& GetFrozenProjector() const { return FrozenProjector; }
	bool IsFrozenProjectorValid() const { return bFrozenValid; }
	FVector2f GetCursorStartVP() const { return CursorStartVP; }
	int32 GetPointerIndex() const { return PointerIndex; }
	bool IsHighPrecision() const { return bHighPrecision; }
	FVector2f GetAnchorScreenPos() const { return AnchorScreenPos; }  // warp-back on release (high-prec)

private:
	bool      bDragging = false;
	bool      bHighPrecision = false;
	int32     PointerIndex = INDEX_NONE;
	FImProjector FrozenProjector;
	bool      bFrozenValid = false;
	FVector2f CursorStartVP = FVector2f::ZeroVector;
	bool      bCursorStartPending = false;   // first ComputeCursorNowVP frame sets CursorStartVP
	FVector2f VirtualCursorVP = FVector2f::ZeroVector;  // high-prec: accumulates raw delta (infinite scrub)
	FVector2f AnchorScreenPos = FVector2f::ZeroVector;  // desktop px at down (release warp-back)
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
