// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

namespace ImSlate
{
class FImSlate3DGizmo;

/**
 * SImSlate3DGizmoHud — a fullscreen, HitTestInvisible overlay owned by FImSlate3DGizmo. While any handle/arrow
 * is dragging, it draws (in ONE place, shared by every handle) the ghost of the drag's START transform plus a
 * numeric readout of the start→now delta (Δloc / Δscale / Δangle). It never claims input — it sits on top purely
 * to render. It self-builds a projector from the scene camera (same as the handles) and reads the live drag
 * state from the gizmo each paint via QueryDragState(); zero per-frame cost when nothing is dragging.
 */
class IMSLATE3D_API SImSlate3DGizmoHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlate3DGizmoHud) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FImSlate3DGizmo>& InGizmo);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }

private:
	TWeakPtr<FImSlate3DGizmo> Gizmo;
	mutable TSharedPtr<class FIm3DShaderElement, ESPMode::ThreadSafe> KeepAliveElem;  // outlive OnPaint (TWeakPtr in draw elem)
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
