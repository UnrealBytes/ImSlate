// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3DDragInput.h"

#if defined(IMSLATE3D_API)

namespace ImSlate
{
void FImDragInputState::Begin(const FPointerEvent& Event, const FImProjector& LiveProjector,
	bool bInHighPrecision, TFunctionRef<FVector2f(const FVector2f&)> ScreenToVP)
{
	bDragging = true;
	bHighPrecision = bInHighPrecision;
	PointerIndex = (int32)Event.GetPointerIndex();
	FrozenProjector = LiveProjector;   // snapshot: constant px↔world calibration for the whole drag
	bFrozenValid = true;
	AnchorScreenPos = (FVector2f)Event.GetScreenSpacePosition();
	VirtualCursorVP = ScreenToVP(AnchorScreenPos);
	// Defer CursorStart to the first move frame (fake-touch jumps between down and first move).
	CursorStartVP = VirtualCursorVP;
	bCursorStartPending = true;
}

FVector2f FImDragInputState::ComputeCursorNowVP(const FPointerEvent& Event,
	TFunctionRef<FVector2f(const FVector2f&)> ScreenToVP,
	TFunctionRef<FVector2f(const FPointerEvent&)> RawDeltaToVP)
{
	FVector2f NowVP;
	if (bHighPrecision)
	{
		VirtualCursorVP += RawDeltaToVP(Event);   // accumulate raw delta → infinite scrub past screen edge
		NowVP = VirtualCursorVP;
	}
	else
	{
		NowVP = ScreenToVP((FVector2f)Event.GetScreenSpacePosition());
	}
	if (bCursorStartPending)
	{
		CursorStartVP = NowVP;   // first frame: start == now → first Apply is a no-op (no down→move jump)
		bCursorStartPending = false;
	}
	return NowVP;
}

void FImDragInputState::End()
{
	bDragging = false;
	bFrozenValid = false;
	PointerIndex = INDEX_NONE;
	bCursorStartPending = false;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
