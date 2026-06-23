// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Input/HittestGrid.h"          // ICustomHitTestPath
#include "Layout/ArrangedWidget.h"      // FWidgetAndPointer

namespace ImSlate
{
class SImSlate3DTransformBox;

/**
 * FIm3DHitTestPath — one per game-viewport SViewport, shared by ALL SImSlate3DTransformBox widgets
 * (same pattern as UMG's FWidget3DHitTester). When the cursor is over the SViewport it asks each
 * registered box to unproject the cursor (real-perspective world ray, the SAME FImProjector used to
 * render). The first box that contains the cursor returns the deep widget path into its child subtree,
 * each entry carrying a virtual pointer position = the unprojected child-local pixel. The engine then
 * runs hover / click / tooltip automatically along that corrected path — no SVirtualWindow, no RT.
 */
class FIm3DHitTestPath : public ICustomHitTestPath
{
public:
	void RegisterBox(const TSharedRef<SImSlate3DTransformBox>& Box);
	void UnregisterBox(const SImSlate3DTransformBox* Box);
	int32 NumRegistered() const { return Boxes.Num(); }

	// ICustomHitTestPath
	virtual TArray<FWidgetAndPointer> GetBubblePathAndVirtualCursors(const FGeometry& InGeometry, FVector2D DesktopSpaceCoordinate, bool bIgnoreEnabledStatus) const override;
	virtual void ArrangeCustomHitTestChildren(FArrangedChildren& ArrangedChildren) const override;
	virtual TOptional<FVirtualPointerPosition> TranslateMouseCoordinateForCustomHitTestChild(const SWidget& ChildWidget, const FGeometry& MyGeometry, const FVector2D ScreenSpaceMouseCoordinate, const FVector2D LastScreenSpaceMouseCoordinate) const override;

private:
	TArray<TWeakPtr<SImSlate3DTransformBox>> Boxes;
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
