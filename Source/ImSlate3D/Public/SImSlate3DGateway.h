// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

namespace ImSlate
{

/**
 * SImSlate3DGateway — the SINGLE full-screen mouse entry point for all 3D UI.
 *
 * Why: 3D units (SImSlate3DTransformBox panels, gizmo arrows) project their content onto world planes that
 * can land ANYWHERE on screen, decoupled from the unit's own 2D widget geometry. Slate's hit-grid only routes
 * an event to a widget whose 2D arranged geometry contains the cursor — so a non-fullscreen unit (e.g. an
 * immediate-mode 3D window) never receives clicks landing on its (elsewhere-projected) content. Routing every
 * 3D event through ONE fullscreen gateway removes that 2D-geometry constraint: the gateway forwards to
 * FImSlate3DHitManager, which dispatches in GetOverlapOrder() and lets each unit per-pixel-test against its
 * OWN projector. Coordinates go desktop → viewport-px via the gateway's fullscreen geometry (single, correct
 * source), so every unit gets the same absolute viewport-px cursor regardless of where it sits.
 *
 * Visible (not SelfHitTestInvisible): only a widget that is itself in the bubble path receives OnMouse*.
 * Unmatched events return Unhandled → Slate keeps bubbling to the game UI below (transparent pass-through).
 * OnPaint draws nothing.
 */
class IMSLATE3D_API SImSlate3DGateway : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlate3DGateway) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** Add a single shared gateway to the game viewport (idempotent). Engine-coupled (AddViewportWidgetContent),
	 *  so the caller (main module / demo) supplies the add via the lambda. ZOrder should sit above 3D content. */
	static void Install(const TFunctionRef<void(const TSharedRef<SWidget>&)>& AddToViewport);
	static void Uninstall(const TFunctionRef<void(const TSharedRef<SWidget>&)>& RemoveFromViewport);
	static bool IsInstalled();
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
