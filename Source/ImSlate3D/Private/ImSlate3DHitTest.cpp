// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3DHitTest.h"

#if defined(IMSLATE3D_API)

#include "SImSlate3DTransformBox.h"

namespace ImSlate
{
void FIm3DHitTestPath::RegisterBox(const TSharedRef<SImSlate3DTransformBox>& Box)
{
	Boxes.AddUnique(Box);
	Boxes.RemoveAll([](const TWeakPtr<SImSlate3DTransformBox>& W) { return !W.IsValid(); });
}

void FIm3DHitTestPath::UnregisterBox(const SImSlate3DTransformBox* Box)
{
	Boxes.RemoveAll([Box](const TWeakPtr<SImSlate3DTransformBox>& W)
	{
		const TSharedPtr<SImSlate3DTransformBox> P = W.Pin();
		return !P.IsValid() || P.Get() == Box;
	});
}

TArray<FWidgetAndPointer> FIm3DHitTestPath::GetBubblePathAndVirtualCursors(
	const FGeometry& InGeometry, FVector2D DesktopSpaceCoordinate, bool bIgnoreEnabledStatus) const
{
	// Ask each registered box (top-most last registered wins on overlap) to unproject + build a path.
	// Desktop(S1) → viewport-physical(S3) must use the box's own mapper (AbsoluteToLocal * DPIScale + ViewRect.Min),
	// NOT a bare AbsoluteToLocal — the latter dropped the DPI scale + letterbox offset (broke when DPI≠1 or the
	// box doesn't fill the viewport). Per-box because each box has its own projector/ViewRect.
	for (int32 i = Boxes.Num() - 1; i >= 0; --i)
	{
		const TSharedPtr<SImSlate3DTransformBox> Box = Boxes[i].Pin();
		if (!Box.IsValid())
		{
			continue;
		}
		const FVector2f Cursor = Box->MapDesktopCursorToViewportPx(DesktopSpaceCoordinate);
		TArray<FWidgetAndPointer> Path;
		if (Box->BuildHitPath(Cursor, bIgnoreEnabledStatus, Path) && Path.Num() > 0)
		{
			return Path;
		}
	}
	return TArray<FWidgetAndPointer>();
}

void FIm3DHitTestPath::ArrangeCustomHitTestChildren(FArrangedChildren& ArrangedChildren) const
{
	// Expose each box's child so the engine knows these widgets exist for input routing.
	for (const TWeakPtr<SImSlate3DTransformBox>& W : Boxes)
	{
		const TSharedPtr<SImSlate3DTransformBox> Box = W.Pin();
		if (Box.IsValid())
		{
			Box->ArrangeForCustomHitTest(ArrangedChildren);
		}
	}
}

TOptional<FVirtualPointerPosition> FIm3DHitTestPath::TranslateMouseCoordinateForCustomHitTestChild(
	const SWidget& ChildWidget, const FGeometry& MyGeometry,
	const FVector2D ScreenSpaceMouseCoordinate, const FVector2D LastScreenSpaceMouseCoordinate) const
{
	for (const TWeakPtr<SImSlate3DTransformBox>& W : Boxes)
	{
		const TSharedPtr<SImSlate3DTransformBox> Box = W.Pin();
		if (Box.IsValid())
		{
			// Desktop(S1) → viewport-physical(S3) via the box's mapper (DPI + letterbox aware), per-box.
			const FVector2f Cursor = Box->MapDesktopCursorToViewportPx(ScreenSpaceMouseCoordinate);
			TOptional<FVector2f> Local = Box->TranslateCursor(ChildWidget, Cursor);
			if (Local.IsSet())
			{
				const FVector2D L(Local->X, Local->Y);
				return FVirtualPointerPosition(L, L);
			}
		}
	}
	return TOptional<FVirtualPointerPosition>();
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
