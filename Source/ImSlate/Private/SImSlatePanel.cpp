// Copyright ImSlate, Inc. All Rights Reserved.
#include "SImSlatePanel.h"

#include "Layout/LayoutUtils.h"
#include "Layout/Margin.h"
#include "SImSlateViewport.h"
#include "SImSlateWindow.h"
#include "Types/SlateEnums.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "XConsoleManager.h"
#include "GenericPlatform/GenericPlatformApplicationMisc.h"
#include "HAL/PlatformApplicationMisc.h"

float GImSlateLayoutScale = (PLATFORM_ANDROID || PLATFORM_IOS) ? 2.f : 1.f;
static FAutoConsoleVariableRef CVar_ImSlateLayoutScale(
	TEXT("imslate.LayoutScale"),
	GImSlateLayoutScale,
	TEXT("Content layout scale for ImSlate panels. 1.0 = default. >1.0 = larger widgets/text."));

int32 GImSlateDragScrollContent = 1;
static FAutoConsoleVariableRef CVar_ImSlateDragScrollContent(
	TEXT("imslate.DragScrollContent"),
	GImSlateDragScrollContent,
	TEXT("Drag/touch-to-scroll on panel content (e.g. drag a fold header to scroll). 1=on, 0=off."));

// Horizontal-drag-to-move-window threshold factor. On a SCROLLABLE panel, a vertical drag scrolls content
// while a HORIZONTAL drag in the blank area moves the whole window (the thin titlebar is hard to grab on
// mobile). The horizontal trigger distance = GetDragTriggerDistance() * this factor — made larger than the
// vertical scroll trigger so a small sideways wobble during a vertical scroll doesn't accidentally move the
// window. 0 disables horizontal-drag-to-move (vertical scroll only).
float GImSlatePanelHSlideMoveFactor = 2.5f;
static FAutoConsoleVariableRef CVar_ImSlatePanelHSlideMoveFactor(
	TEXT("imslate.PanelHSlideMoveFactor"),
	GImSlatePanelHSlideMoveFactor,
	TEXT("Horizontal blank-area drag on a scrollable panel moves the window past this *DragTriggerDistance. 0=off."));
XMetaVar(TEXT("imslate.LayoutScale"), DisplayName, TEXT("UI Scale"))(ClampMin, 0.5)(ClampMax, 4.0)(UIMin, 0.5)(UIMax, 4.0);

// Reference physical density (PPI). On desktop, scale=1 looks right and desktops sit around
// ~96 PPI (UE's logical-inch baseline), so we treat ~96 PPI as "scale 1.0 physical size".
// On mobile we scale by realPPI/ReferencePPI so a UI element keeps the SAME physical size
// across devices (reverse-derived from the desktop baseline, per design decision).
static float GImSlateReferencePPI = 96.f;
static FAutoConsoleVariableRef CVar_ImSlateReferencePPI(
	TEXT("imslate.ReferencePPI"),
	GImSlateReferencePPI,
	TEXT("Reference PPI that maps to scale 1.0 for physical-size-consistent mobile UI."));

// Physical-size clamp for the effective scale: a "standard touch unit" (one tappable cell, ~the key height)
// must render between TouchUnitMinMM and TouchUnitMaxMM physically. On very high-PPI phones ReferencePPI=96
// makes the raw scale push elements oversized (titlebar buttons pushed off-screen, text too big); clamping
// the unit's physical size keeps the WHOLE UI (controls + text scale together) within a finger-friendly band.
// Only applies on mobile when the real screen density is known. CVars let you tune the band at runtime.
static float GImSlateTouchUnitLogical = 32.f;  // logical height of the reference tappable unit (the key base)
static FAutoConsoleVariableRef CVar_ImSlateTouchUnitLogical(
	TEXT("imslate.TouchUnitLogical"),
	GImSlateTouchUnitLogical,
	TEXT("Logical height of the reference tappable unit used for the physical-size scale clamp."));
static float GImSlateTouchUnitMinMM = 4.f;
static FAutoConsoleVariableRef CVar_ImSlateTouchUnitMinMM(
	TEXT("imslate.TouchUnitMinMM"),
	GImSlateTouchUnitMinMM,
	TEXT("Minimum physical size (mm) of the reference tappable unit; clamps the effective scale up to this."));
static float GImSlateTouchUnitMaxMM = 8.f;
static FAutoConsoleVariableRef CVar_ImSlateTouchUnitMaxMM(
	TEXT("imslate.TouchUnitMaxMM"),
	GImSlateTouchUnitMaxMM,
	TEXT("Maximum physical size (mm) of the reference tappable unit; clamps the effective scale down to this."));

// Extra multiplier on top of the physical scale, for fine-tuning hand feel on mobile.
float GImSlateMobileScaleTweak = 1.f;
static FAutoConsoleVariableRef CVar_ImSlateMobileScaleTweak(
	TEXT("imslate.MobileScaleTweak"),
	GImSlateMobileScaleTweak,
	TEXT("Extra multiplier on the physical-based mobile UI scale (1.0 = pure physical match)."));

namespace ImSlate
{

float GetImSlateEffectiveScale()
{
#if PLATFORM_IOS || PLATFORM_ANDROID
	// Physical-size based: use the engine's real screen density (iOS uses nativeScale / a device
	// table; Android uses xdpi → density bucket internally) so UI keeps a consistent physical
	// size across devices. Fall back to the old SystemDPI*LayoutScale when density is Unknown.
	int32 ScreenDensity = 0;
	const EScreenPhysicalAccuracy Accuracy = FPlatformApplicationMisc::GetPhysicalScreenDensity(ScreenDensity);
	if (Accuracy != EScreenPhysicalAccuracy::Unknown && ScreenDensity > 0 && GImSlateReferencePPI > 0.f)
	{
		float PhysicalScale = ((float)ScreenDensity / GImSlateReferencePPI) * GImSlateMobileScaleTweak;
		PhysicalScale = FMath::Max(PhysicalScale, 1.f);

		// Physical-size clamp: keep the reference tappable unit (TouchUnitLogical × scale) within
		// [TouchUnitMinMM, TouchUnitMaxMM] physically. px = mm/25.4 × ScreenDensity, so the scale that puts
		// the unit at a given mm = (mm/25.4 × ScreenDensity) / TouchUnitLogical. This pulls the whole UI
		// (controls AND text scale by the same value) into a finger-friendly band on very high-PPI screens.
		if (GImSlateTouchUnitLogical > 0.f && GImSlateTouchUnitMaxMM > 0.f)
		{
			const float MMToScale = ((float)ScreenDensity / 25.4f) / GImSlateTouchUnitLogical;
			const float ScaleMin = FMath::Max(GImSlateTouchUnitMinMM, 0.f) * MMToScale;
			const float ScaleMax = GImSlateTouchUnitMaxMM * MMToScale;
			PhysicalScale = FMath::Clamp(PhysicalScale, ScaleMin, ScaleMax);
		}
		return FMath::Max(PhysicalScale, 1.f);
	}
	// Fallback (density unknown): the previous heuristic, which is "good enough" per design.
	float SystemDPI = SImSlateViewport::StaticGetDPIScaleFactorAtPoint(FVector2D::ZeroVector);
	return FMath::Max(SystemDPI * GImSlateLayoutScale, 1.f);
#else
	float SystemDPI = SImSlateViewport::StaticGetDPIScaleFactorAtPoint(FVector2D::ZeroVector);
	return FMath::Max(SystemDPI * GImSlateLayoutScale, 1.f);
#endif
}
extern void PrepassInternal(const TSharedRef<SWidget>& InWidget, float LayoutScaleMultiplier);

//////////////////////////////////////////////////////////////////////////

int32 SImSlatePanel::RemoveSlot(const TSharedRef<SWidget>& SlotWidget)
{
	for (int32 SlotIdx = 0; SlotIdx < Children.Num(); ++SlotIdx)
	{
		if (SlotWidget == Children[SlotIdx].GetWidget())
		{
			Children.RemoveAt(SlotIdx);
			if (SlotWidget->GetVisibility() != EVisibility::Collapsed)
			{
				Invalidate(EInvalidateWidget::LayoutAndVolatility);
				MarkPrepassAsDirty();
			}
			return SlotIdx;
		}
	}

	return -1;
}

int32 SImSlatePanel::RemoveSlot(uint32 InKey)
{
	for (int32 SlotIdx = 0; SlotIdx < Children.Num(); ++SlotIdx)
	{
		if (InKey == Children[SlotIdx].Hash)
		{
			auto SlotWidget = Children[SlotIdx].GetWidget();
			Children.RemoveAt(SlotIdx);
			if (SlotWidget->GetVisibility() != EVisibility::Collapsed)
			{
				Invalidate(EInvalidateWidget::LayoutAndVolatility);
				MarkPrepassAsDirty();
			}
			return SlotIdx;
		}
	}

	return -1;
}

void SImSlatePanel::ClearChildren()
{
	Children.Empty();
	FrameOrder.Reset();

	Invalidate(EInvalidateWidget::LayoutAndVolatility);
	MarkPrepassAsDirty();
}

 SImSlatePanel::SImSlatePanel()
	: Children(this)
{
	SetCanTick(false);
	bCanSupportFocus = false;
}

SImSlatePanel::~SImSlatePanel()
{
	Children.ResetSlots();
}

bool SImSlatePanel::CommitItemFrame()
{
	if (!bFrameStarted)
		return false;
	bFrameStarted = false;

	const int32 NumFrame = FrameOrder.Num();
	const int32 NumChildren = Children.Num();

	const bool bHasNewItems = (NumChildren > FrameBaseChildCount);

	// Fast path: all active items = total children, same order, no new items
	if (NumFrame == NumChildren && !bHasNewItems && bFrameOrderIsSequential)
	{
		if (bLayoutDirty)
		{
			Invalidate(EInvalidateWidget::Layout);
			MarkPrepassAsDirty();
		}
		return false;
	}

	// Determine what actually changed
	TSet<int32> UsedSet;
	UsedSet.Reserve(NumFrame);
	for (int32 Idx : FrameOrder)
		UsedSet.Add(Idx);

	const bool bNeedsRemove = (UsedSet.Num() < NumChildren);
	const bool bNeedsReorder = !bFrameOrderIsSequential;

	if (bNeedsReorder || bNeedsRemove)
	{
		// Steal pointers, reorder active items, delete unused
		TArray<FSlot*> OldPtrs;
		OldPtrs.SetNumUninitialized(NumChildren);
		FSlot** RawData = Children.Slots.GetData();
		for (int32 i = 0; i < NumChildren; ++i)
		{
			OldPtrs[i] = RawData[i];
			RawData[i] = nullptr;
		}
		Children.Empty();

		// Re-add active items in frame order
		for (int32 Idx : FrameOrder)
		{
			Children.Add(OldPtrs[Idx]);
			OldPtrs[Idx] = nullptr;
		}

		// Delete unused — collapsed children NOT protected, recreated on expand
		for (FSlot* Ptr : OldPtrs)
		{
			if (Ptr)
			{
				ItemParentMap.Remove(Ptr->Hash);
				ResetSlotBase(Ptr);
				delete Ptr;
			}
		}
	}
	// else: items were just appended in order — Children is already correct, no Empty+Add needed

	// Rebuild cache (always, since indices may have changed or new items added)
	CachedItems = FCachedItem();
	for (int32 i = 0; i < Children.Num(); ++i)
	{
		CachedItems.Add(Children[i].Hash, i);
	}

	Invalidate(EInvalidateWidgetReason::ChildOrder | EInvalidateWidgetReason::Paint);
	return true;
}

bool SImSlatePanel::IsInCollapsedSubtree(ImSlateId ItemId) const
{
	const ImSlateId* ParentPtr = ItemParentMap.Find(ItemId);
	if (!ParentPtr || *ParentPtr == 0)
		return false;  // root level

	ImSlateId ParentId = *ParentPtr;
	if (CollapsedFoldIds.Contains(ParentId))
		return true;  // direct parent is collapsed

	return IsInCollapsedSubtree(ParentId);  // check ancestors
}

void SImSlatePanel::SetItemParent(ImSlateId ItemId)
{
	ImSlateId ParentId = FoldContextStack.Num() > 0 ? FoldContextStack.Last() : 0;
	ItemParentMap.FindOrAdd(ItemId) = ParentId;
}

void SImSlatePanel::PushFoldContext(ImSlateId FoldId)
{
	// Opening a fold — children will be created fresh this frame
	CollapsedFoldIds.Remove(FoldId);
	FoldContextStack.Push(FoldId);
}

void SImSlatePanel::PopFoldContext()
{
	if (FoldContextStack.Num() > 0)
		FoldContextStack.Pop();
}

void SImSlatePanel::SetFoldCollapsed(ImSlateId FoldId, bool bCollapsed)
{
	if (bCollapsed)
	{
		CollapsedFoldIds.Add(FoldId);
		// Children will be deleted by CommitItemFrame (not in FrameOrder next frame)
		bLayoutDirty = true;
	}
	else
	{
		if (CollapsedFoldIds.Remove(FoldId) > 0)
		{
			// Children will be recreated next frame when BeginFold returns true
			bLayoutDirty = true;
		}
	}
}

void SImSlatePanel::Construct(const FArguments& InArgs, SImSlateWindow* InParent)
{
#if UE_5_00_OR_LATER
	const int32 NumSlots = InArgs._Slots.Num();
	for (int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
	{
		Children.Add(InArgs._Slots[SlotIndex].GetSlot());
	}
#else
	const int32 NumSlots = InArgs.Slots.Num();
	for (int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
	{
		Children.Add(InArgs.Slots[SlotIndex]);
	}
#endif
	Window = InParent;

	// Visible (not SelfHitTestInvisible) so the panel itself receives preview/down/move over blank areas
	// and can drive content-pan (drag-to-scroll / drag-move). Blank-area press no longer passes through to
	// the window; the panel takes over scroll/move-window itself (see OnMouseMove/TryStartPan). Children
	// still hit-test normally and a sub-threshold press is never consumed, so their clicks are unaffected.
	SetVisibility(EVisibility::Visible);
	bHideScrollBar = InArgs._HideScollBar || (Window->Flags & ImSlateWindowFlags_NoScrollbar);

	SetClipping(InParent->GetClipping());

	SAssignNew(ScrollHandle, SScrollBar)
	.AlwaysShowScrollbar(false)
	.Padding(FMargin(1.f, 0.f))
	.HideWhenNotInUse(true)
	.Orientation(EOrientation::Orient_Vertical)
	.OnUserScrolled(this, &SImSlatePanel::OnScrollFraction);
}

void SImSlatePanel::OnScrollFraction(float InFraction)
{
	OnScrollOffset(InFraction * TotalActualSize.Y);
}

bool SImSlatePanel::OnScrollOffset(float NewOffset)
{
	if (!FMath::IsNearlyEqual(ScrollOffset, NewOffset))
	{
		NewOffset = FMath::Clamp(NewOffset, 0.f, TotalActualSize.Y - GetCachedGeometry().GetLocalSize().Y);
		ScrollOffset = NewOffset;
		Invalidate(EInvalidateWidget::Layout);
		MarkPrepassAsDirty();
		return true;
	}
	return false;
}

void SImSlatePanel::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	CachedGroupRects.Reset();
	if (Children.Num() <= 0)
		return;

	const_cast<SImSlatePanel*>(this)->ScrollBarWidth = 12.f * FMath::Max(GImSlateLayoutScale, 1.f);

	const auto PanelSize = AllottedGeometry.GetLocalSize();
	const auto PanelHeight = PanelSize.Y;

	const float SafeTotalHeight = FMath::Max(TotalActualSize.Y, 1.f);
	ScrollHandle->SetState(ScrollOffset / SafeTotalHeight, PanelHeight / SafeTotalHeight);

	const bool bShowScrollBar = !bHideScrollBar && ScrollHandle->IsNeeded();
	const auto PanelWidth = PanelSize.X - (bShowScrollBar ? ScrollBarWidth : 0.f);
	ON_SCOPE_EXIT
	{
		ScrollHandle->SetState(ScrollOffset / SafeTotalHeight, PanelHeight / SafeTotalHeight);
		if (bShowScrollBar)
		{
			ArrangedChildren.AddWidget(EVisibility::Visible, AllottedGeometry.MakeChild(ScrollHandle.ToSharedRef(), FVector2D(PanelWidth, 0.f), FVector2D(ScrollBarWidth, PanelHeight)));
		}
	};

	float ItemArrangingOffset = 0.f;
	struct FStretchInfo
	{
		float Factor = 0.f;
		float Occupied = 0.f;

		float ColLeft = 0.f;
		float ColRight = 0.f;
		TArray<const FSlot*> Slots;
	};

	struct FRowInfo
	{
		TArray<FStretchInfo, TInlineAllocator<4>> ColumnInfo = {{}};
		float CurColLeft = 0.f;
		float CurColRight = 0.f;
		float RowTop = 0.f;
		float RowMaxHeight = 0.f;
		bool bHasAspectRatio = false;
		FStretchInfo& AddColumn(float InColRight)
		{
			if (ColumnInfo.Num() > 0)
			{
				Column().ColLeft = CurColLeft;
				Column().ColRight = CurColRight;
				CurColLeft = CurColRight;
				CurColRight = InColRight;
			}
			else
			{
				CurColLeft = 0.f;
				CurColRight = InColRight;
			}

			FStretchInfo& Ref = ColumnInfo.Emplace_GetRef(FStretchInfo());
			Ref.ColLeft = CurColLeft;
			Ref.ColRight = CurColRight;
			return Ref;
		}
		FStretchInfo& Column() { return ColumnInfo.Last(); }
	};

	FRowInfo CurRowInfo;
	auto ArrangeCurrentRow = [&] /* return should continue next */ {
		ON_SCOPE_EXIT
		{
			ItemArrangingOffset += CurRowInfo.RowMaxHeight;
			CurRowInfo.bHasAspectRatio = false;
		};
		if (ItemArrangingOffset + CurRowInfo.RowMaxHeight < ScrollOffset)
			return true;

		if (ItemArrangingOffset >= ScrollOffset + PanelHeight)
			return false;

		if (CurRowInfo.bHasAspectRatio)
			CurRowInfo.RowMaxHeight = 0.f;
		TArray<FSlateRect> ItemLayouts;
		for (auto i = 0; i < CurRowInfo.ColumnInfo.Num(); ++i)
		{
			const auto& ColInfo = CurRowInfo.ColumnInfo[i];
			float OffsetInCol = 0.f;
			for (auto ChildSlot : ColInfo.Slots)
			{
				const auto& CurChild = *ChildSlot;
				const FVector2D ChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();
				FVector2D ChildSize(0.f, 0.f);
				ChildSize.Y = ChildDesiredSize.Y;
				if (CurChild.bFillWidth)
				{
					if (ColInfo.Factor > 0.f)
					{
						// Stretch widgets get a fraction of the space remaining after all the fixed-space requirements are met
						ChildSize.X = (PanelWidth - ColInfo.Occupied) * CurChild.StretchValue / ColInfo.Factor;
					}
				}
				else
				{
					// Auto-sized widgets get their desired-size value
					ChildSize.X = ChildDesiredSize.X;
				}

				ChildSize = CurChild.GetSize(ChildSize);
				const FMargin SlotPadding(CurChild.SlotPadding);
				FVector2D SlotSize = FVector2D(ChildSize.X + SlotPadding.GetTotalSpaceAlong<Orient_Horizontal>(), ChildSize.Y);

				const EVisibility ChildVisibility = CurChild.GetWidget()->GetVisibility();
				if (ChildVisibility != EVisibility::Collapsed)
				{
					// Figure out the size and local position of the child within the slot
					AlignmentArrangeResult XAlignmentResult = AlignChild<Orient_Horizontal>(SlotSize.X, CurChild, SlotPadding);
					AlignmentArrangeResult YAlignmentResult = AlignChild<Orient_Vertical>(SlotSize.Y, CurChild, SlotPadding);

					// remove paddings
					FVector2D LocalPosition = FVector2D(ColInfo.ColLeft + OffsetInCol + XAlignmentResult.Offset, CurRowInfo.RowTop + YAlignmentResult.Offset - ScrollOffset);
					FVector2D LocalSize = FVector2D(XAlignmentResult.Size, YAlignmentResult.Size);

					if (CurRowInfo.bHasAspectRatio && CurChild.AspectRatio > 0.f)
					{
						auto ActualRatio = LocalSize.X / LocalSize.Y;
						if (!FMath::IsNearlyEqual(ActualRatio, CurChild.AspectRatio))
						{
							LocalSize.Y = LocalSize.X / CurChild.AspectRatio;
						}
						CurRowInfo.RowMaxHeight = FMath::Max(CurRowInfo.RowMaxHeight, LocalSize.Y);
					}
					else
					{
						// CurRowInfo.RowMaxHeight = FMath::Max(CurRowInfo.RowMaxHeight, CurChild.GetHeight(ChildDesiredSize.Y) + SlotPadding.GetTotalSpaceAlong<Orient_Vertical>());
					}
					// Add the information about this child to the output list
					ItemLayouts.Emplace(LocalPosition, LocalPosition + LocalSize);
				}

				OffsetInCol += SlotSize.X;
			}
		}

		int32 ItemIndex = 0;
		for (auto i = 0; i < CurRowInfo.ColumnInfo.Num(); ++i)
		{
			const auto& ColInfo = CurRowInfo.ColumnInfo[i];
			for (auto ChildSlot : ColInfo.Slots)
			{
				const auto& CurChild = *ChildSlot;
				const EVisibility ChildVisibility = CurChild.GetWidget()->GetVisibility();
				if (ChildVisibility != EVisibility::Collapsed)
				{
					FVector2D LocalPosition = ItemLayouts[ItemIndex].GetTopLeft();
					FVector2D LocalSize = ItemLayouts[ItemIndex].GetSize();
					++ItemIndex;
					if (CurRowInfo.RowMaxHeight > LocalSize.Y)
					{
						switch (CurChild.VAlignment)
						{
							case VAlign_Center:
								LocalPosition.Y += (CurRowInfo.RowMaxHeight - LocalSize.Y) / 2;
								break;
							case VAlign_Fill:
								LocalSize.Y = CurRowInfo.RowMaxHeight;
								break;
							case VAlign_Bottom:
								LocalPosition.Y += (CurRowInfo.RowMaxHeight - LocalSize.Y);
								break;
							case VAlign_Top:
								break;
							default:
								break;
						}
					}
					ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(CurChild.GetWidget(), LocalPosition, LocalSize));

					// Accumulate group background rect: union of all items sharing CurChild.GroupId.
					// LocalPosition/LocalSize are final panel-local coords (already include -ScrollOffset).
					if (CurChild.GroupId != 0)
					{
						const FSlateRect ItemRect(LocalPosition.X, LocalPosition.Y, LocalPosition.X + LocalSize.X, LocalPosition.Y + LocalSize.Y);
						FGroupRect& GR = CachedGroupRects.FindOrAdd(CurChild.GroupId);
						if (!GR.bInit)
						{
							GR.Rect = ItemRect;
							GR.Color = CurChild.GroupColor;  // colour from the first slot of the group
							GR.bInit = true;
						}
						else
						{
							GR.Rect.Left = FMath::Min(GR.Rect.Left, ItemRect.Left);
							GR.Rect.Top = FMath::Min(GR.Rect.Top, ItemRect.Top);
							GR.Rect.Right = FMath::Max(GR.Rect.Right, ItemRect.Right);
							GR.Rect.Bottom = FMath::Max(GR.Rect.Bottom, ItemRect.Bottom);
						}
					}
				}
			}
		}
		return true;
	};

	const float RightBoundry = PanelWidth;
	int32 LastChildVisibleIdx = -1;
	bool bPrevBreak = false;
	int32 ColumnIdx = 0;
	for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
	{
		const auto& CurChild = Children[ChildIndex];
		const bool bArrangeOnNewRow = bPrevBreak || CurChild.bNewRow;
		if (bArrangeOnNewRow)
			ColumnIdx = 0;

		if (CurChild.GetWidget()->GetVisibility() != EVisibility::Collapsed)
		{
			auto& SlotPadding = CurChild.SlotPadding;
			FVector2D ChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();

			if (bArrangeOnNewRow)
			{
				if (LastChildVisibleIdx >= 0)
				{
					if (!ArrangeCurrentRow())
						return;
				}

				CurRowInfo.RowTop = CurRowInfo.RowTop + CurRowInfo.RowMaxHeight;
				CurRowInfo.RowMaxHeight = 0.f;
				CurRowInfo.CurColLeft = 0.f;
				CurRowInfo.CurColRight = RightBoundry;
				CurRowInfo.ColumnInfo.Empty();
				CurRowInfo.AddColumn(RightBoundry);
			}

			CurRowInfo.Column().Slots.Add(&CurChild);
			CurRowInfo.Column().Occupied += SlotPadding.GetTotalSpaceAlong<Orient_Horizontal>();
			CurRowInfo.RowMaxHeight = FMath::Max(CurRowInfo.RowMaxHeight, CurChild.GetHeight(ChildDesiredSize.Y) + SlotPadding.GetTotalSpaceAlong<Orient_Vertical>());
			CurRowInfo.bHasAspectRatio |= CurChild.AspectRatio > 0.f;

			if (CurChild.bFillWidth)
			{
				CurRowInfo.Column().Factor += CurChild.StretchValue;
			}
			else
			{
				CurRowInfo.Column().Occupied += CurChild.GetWidth(ChildDesiredSize.X);
			}

			if (CurChild.bAlignCol && ColumnAlignOff.IsValidIndex(CurChild.StretchToCol))
			{
				CurRowInfo.CurColRight = ColumnAlignOff[CurChild.StretchToCol];
				CurRowInfo.AddColumn(RightBoundry);
			}

			if (LastChildVisibleIdx < 0)
				LastChildVisibleIdx = ChildIndex;
		}

		++ColumnIdx;
		bPrevBreak = CurChild.bBreakLine;
	}

	if (LastChildVisibleIdx >= 0)
		ArrangeCurrentRow();
}

FChildren* SImSlatePanel::GetChildren()
{
	return &Children;
}

FVector2D SImSlatePanel::ComputeDesiredSize(float Scale) const
{
	return TotalActualSize;
}

void SImSlatePanel::CacheDesiredSize(float LayoutScaleMultiplier)
{
	TotalActualSize = FVector2D::ZeroVector;
	RowHeights.Empty();
	FVector2D AccDesiredSize = FVector2D::ZeroVector;

	auto UpdateDesiredSize = [&] {
		TotalActualSize.X = FMath::Max(AccDesiredSize.X, TotalActualSize.X);
		TotalActualSize.Y += AccDesiredSize.Y;
		RowHeights.Add(AccDesiredSize.Y);
	};

	bool bPrevBreak = false;
	int32 ColumnIdx = 0;
	for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
	{
		const auto& CurChild = Children[ChildIndex];
		const bool bArrangeOnNewRow = bPrevBreak || CurChild.bNewRow;
		if (bArrangeOnNewRow)
			ColumnIdx = 0;
		if (CurChild.GetWidget()->GetVisibility() != EVisibility::Collapsed)
		{
			FVector2D CurChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();
			const auto ActualDesiredSize = CurChild.GetSize(CurChildDesiredSize);

			float ChildRowDesiredWidth = ActualDesiredSize.X + CurChild.SlotPadding.GetTotalSpaceAlong<Orient_Horizontal>();

			if (CurChild.bAlignCol && ColumnAlignOff.IsValidIndex(CurChild.StretchToCol))
			{
				ChildRowDesiredWidth = FMath::Max(0.f, ColumnAlignOff[CurChild.StretchToCol] - AccDesiredSize.X);
			}

			float FinalChildDesiredHeight = ActualDesiredSize.Y + CurChild.SlotPadding.GetTotalSpaceAlong<Orient_Vertical>();

			if (bArrangeOnNewRow)
			{
				UpdateDesiredSize();
				AccDesiredSize.X = 0.f;
				AccDesiredSize.Y = 0.f;
			}
			AccDesiredSize.X += ChildRowDesiredWidth;
			AccDesiredSize.Y = FMath::Max(AccDesiredSize.Y, FinalChildDesiredHeight);
			++ColumnIdx;
		}
		bPrevBreak = CurChild.bBreakLine;
	}
	UpdateDesiredSize();

	// Clamp scroll offset when content size changes (e.g. fold/unfold)
	{
		float PanelHeight = GetCachedGeometry().GetLocalSize().Y;
		float MaxScroll = FMath::Max(0.f, TotalActualSize.Y - PanelHeight);
		ScrollOffset = FMath::Clamp(ScrollOffset, 0.f, MaxScroll);
	}

	RowHeights.Add(TotalActualSize.Y);
	if ((Window->Flags & ImSlateWindowFlags_NoScrollbar) == 0 && ScrollHandle->IsNeeded())
		PrepassInternal(ScrollHandle.ToSharedRef(), LayoutScaleMultiplier);

	SPanel::CacheDesiredSize(LayoutScaleMultiplier);
}

FReply SImSlatePanel::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ((Window->Flags & ImSlateWindowFlags_NoScrollWithMouse) == 0 && ScrollHandle->IsNeeded())
	{
		StopInertialScrolling();  // wheel input must not fight a leftover coast velocity
		auto NewScrollOffset = ScrollOffset;
		NewScrollOffset -= MouseEvent.GetWheelDelta() * GetGlobalScrollAmount();
		if (OnScrollOffset(NewScrollOffset))
		{
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

//------------------------------------------------------------------------------------------------------
// Drag/touch pan (SScrollBox-style). Step 1: mouse, scroll-only. MoveWindow added in Step 3, touch in Step 4.
//------------------------------------------------------------------------------------------------------

bool SImSlatePanel::IsPanEnabled() const
{
	if (!GImSlateDragScrollContent)
		return false;
	if (Window && (Window->Flags & ImSlateWindowFlags_NoMouseInputs))
		return false;
	return true;
}

void SImSlatePanel::BeginPanCandidate(const FPointerEvent& PointerEvent)
{
	StopInertialScrolling();  // pressing into the list halts the coast (like grabbing a spinning wheel)
	FingerOwningInteraction = PointerEvent.GetPointerIndex();
	PressScreenPos = PointerEvent.GetScreenSpacePosition();
	LastMoveScreenPos = PressScreenPos;
	PendingDragTrigger = 0.f;
	bPanningCapture = false;
	PanMode = EPanMode::None;
}

bool SImSlatePanel::TryStartPan(const FGeometry& MyGeometry, const FPointerEvent& PointerEvent, FReply& OutReply)
{
	if (CanScroll())
	{
		// Scroll: the panel captures and drives the scroll itself.
		PanMode = EPanMode::Scroll;
		bPanningCapture = true;
		LastMoveScreenPos = PointerEvent.GetScreenSpacePosition();  // capture origin for per-move delta
		LastScrollSampleTime = FSlateApplication::Get().GetCurrentTime();  // seed so Tick's pause check is sane
		SetScrollTickActive(true);  // run pause-detect Tick for this drag
		OutReply = FReply::Handled().CaptureMouse(AsShared());
		return true;
	}

	// Can't scroll: move-window is driven by the DetectDrag armed in OnMouseButtonDown (stable down-detect),
	// not started here in the middle of a move.
	return false;
}

void SImSlatePanel::ApplyPanDelta(const FGeometry& MyGeometry, FVector2D CurAbsScreenPos, FVector2D ScreenDelta)
{
	const float Scale = MyGeometry.Scale > 0.f ? MyGeometry.Scale : 1.f;

	if (PanMode == EPanMode::Scroll)
	{
		ScrollByDelta(-ScreenDelta.Y / Scale);  // content moves opposite to the finger
		// Sample velocity for flick-to-coast. Sign matches the offset delta above (-ScreenDelta.Y), so the
		// coast continues in the content-move direction. Sampled in screen space; converted by Scale on use.
		LastScrollSampleTime = FSlateApplication::Get().GetCurrentTime();
		InertialScrollManager.AddScrollSample(-ScreenDelta.Y, LastScrollSampleTime);
	}
	else if (PanMode == EPanMode::MoveWindow && Window)
	{
		// Move the whole window: window pos = start pos + total finger travel (screen→local via Scale). Using
		// the absolute travel-from-start (not per-frame delta) avoids drift accumulation. DragingWindowPos's
		// CacheDesiredSize clamp keeps the window inside the viewport.
		const FVector2D TravelLocal = (CurAbsScreenPos - MoveWindowStartScreenPos) / Scale;
		const FVector2D NewPos = MoveWindowStartWindowPos + TravelLocal;
		Window->DragingWindowPos(NewPos, NewPos);
	}
}

void SImSlatePanel::EndPan()
{
	FingerOwningInteraction.Reset();
	PendingDragTrigger = 0.f;
	bPanningCapture = false;
	PanMode = EPanMode::None;
	SetScrollTickActive(false);  // drag over → stop the pause-detect Tick (back to zero-cost idle)
}

//------------------------------------------------------------------------------------------------------
// Inertial scrolling (flick-to-coast). Same building blocks as SImSlateVirtualList: FInertialScrollManager
// + an active timer that only runs while coasting (no per-frame cost otherwise).
//------------------------------------------------------------------------------------------------------

// Inertial sample window (seconds). Matches FInertialScrollManager's default SampleTimeout. While dragging,
// if the finger pauses longer than this the residual velocity is cleared (in Tick, like SScrollBox), so a
// release after a pause coasts with zero velocity = no inertia.
static constexpr double GImInertialSampleTimeout = 0.1;

void SImSlatePanel::BeginInertialScrolling()
{
	// No gate here: a pause during the drag already zeroed the velocity (see Tick), so on release the
	// velocity is whatever the finger genuinely had. Start the coast timer; it stops itself when the
	// velocity decays to zero or hits an edge (so starting with ~0 velocity stops on the first tick).
	if (!InertialTimerHandle.IsValid())
	{
		bIsInertialScrolling = true;
		InertialTimerHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SImSlatePanel::UpdateInertialScroll));
	}
}

void SImSlatePanel::StopInertialScrolling()
{
	bIsInertialScrolling = false;
	if (InertialTimerHandle.IsValid())
	{
		if (TSharedPtr<FActiveTimerHandle> Pinned = InertialTimerHandle.Pin())
			UnRegisterActiveTimer(Pinned.ToSharedRef());
		InertialTimerHandle.Reset();
	}
	// Zero the velocity so a fresh press doesn't inherit leftover coast.
	InertialScrollManager.ClearScrollVelocity();
}

void SImSlatePanel::SetScrollTickActive(bool bActive)
{
	// Toggle Tick only for the duration of a scroll drag. Outside a drag the panel stays SetCanTick(false)
	// (the constructor's default), so there is no per-frame cost when idle.
	SetCanTick(bActive);
}

void SImSlatePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SPanel::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// SScrollBox::Tick mirror: while a scroll drag is captured, if the finger has paused longer than the
	// sample timeout, zero the residual velocity. Because this happens DURING the drag (screen already
	// static), clearing causes no visual jump — and a release after the pause then coasts with zero
	// velocity = no inertia. (SScrollBox.cpp: `if (bTouchPanningCapture && now-LastScrollTime > 0.10) ClearScrollVelocity()`.)
	if (bPanningCapture && PanMode == EPanMode::Scroll
		&& (FSlateApplication::Get().GetCurrentTime() - LastScrollSampleTime) > GImInertialSampleTimeout)
	{
		InertialScrollManager.ClearScrollVelocity();
	}
}

EActiveTimerReturnType SImSlatePanel::UpdateInertialScroll(double InCurrentTime, float InDeltaTime)
{
	bool bKeepTicking = false;

	InertialScrollManager.UpdateScrollVelocity(InDeltaTime);
	const float Scale = GetCachedGeometry().Scale > 0.f ? GetCachedGeometry().Scale : 1.f;
	// Velocity is in screen space and already carries the content-move direction (we fed AddScrollSample
	// with the same sign ApplyPanDelta scrolls by). Convert to local space for the offset.
	const float VelocityLocal = InertialScrollManager.GetScrollVelocity() / Scale;

	if (!FMath::IsNearlyZero(VelocityLocal))
	{
		// OnScrollOffset returns false when clamped at an edge (nothing moved) → stop coasting.
		if (OnScrollOffset(ScrollOffset + VelocityLocal * InDeltaTime))
			bKeepTicking = true;
		else
			InertialScrollManager.ClearScrollVelocity();
	}

	if (!bKeepTicking)
	{
		bIsInertialScrolling = false;
		InertialTimerHandle.Reset();
	}
	return bKeepTicking ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
}

void SImSlatePanel::ExternalPanMove(FVector2D PressPos, FVector2D CurPos)
{
	// Scroll only: the child (e.g. fold header) forwards here only when content CAN scroll; when it can't,
	// the child begins a window drag-drop itself (move + viewport/host switch), so this is never the mover.
	if (!IsPanEnabled() || !CanScroll())
		return;

	if (!bPanningCapture)
	{
		StopInertialScrolling();        // a fresh drag cancels any leftover coast
		PanMode = EPanMode::Scroll;
		bPanningCapture = true;        // active (the child owns capture, not us)
		PressScreenPos = PressPos;
		LastMoveScreenPos = CurPos;    // origin; first frame applies no delta (avoids a jump)
		LastScrollSampleTime = FSlateApplication::Get().GetCurrentTime();  // seed pause check
		SetScrollTickActive(true);     // run pause-detect Tick for this forwarded drag
		return;
	}

	const FVector2D Delta = CurPos - LastMoveScreenPos;
	LastMoveScreenPos = CurPos;
	ApplyPanDelta(GetCachedGeometry(), CurPos, Delta);
}

void SImSlatePanel::ExternalPanEnd(FVector2D CurPos)
{
	const bool bWasScroll = bPanningCapture && PanMode == EPanMode::Scroll;
	EndPan();
	if (bWasScroll)
		BeginInertialScrolling();  // coast with the flick velocity sampled during the forwarded drag
}

FReply SImSlatePanel::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Tunnel phase: only mark a scroll candidate when content can scroll. When it can't, do NOT register —
	// the press must bubble to the window (DetectDrag(self) → move), exactly like the titlebar. Never consume.
	if (IsPanEnabled() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && CanScroll())
		BeginPanCandidate(MouseEvent);
	return FReply::Unhandled();
}

FReply SImSlatePanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Blank-area press (no child consumed).
	if (IsPanEnabled() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (CanScroll())
		{
			// Scrollable → consume the press (Handled) so it does NOT bubble to the window's DetectDrag;
			// we drive the scroll on move. Track a candidate for that.
			if (!FingerOwningInteraction.IsSet())
				BeginPanCandidate(MouseEvent);
			return FReply::Handled();
		}
		// Not scrollable → DON'T consume: let the press bubble to SImSlateWindow::OnMouseButtonDown, which
		// does DetectDrag(self) → OnDragDetected → window move. This is EXACTLY how the titlebar works
		// (SImHeaderArea returns Unhandled too). The window detecting on ITSELF is what carries content;
		// a child detecting the window did not work.
	}
	return FReply::Unhandled();
}

FReply SImSlatePanel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!FingerOwningInteraction.IsSet() || FingerOwningInteraction.GetValue() != MouseEvent.GetPointerIndex())
		return FReply::Unhandled();

	// Only a left-button-held drag pans. A hover move (button up) means the press ended elsewhere — e.g. a
	// child button captured the click and we never saw OnMouseButtonUp — so drop the stale candidate.
	// Without this, hovering after expanding a control accumulates movement and spuriously captures.
	if (!bPanningCapture && !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		EndPan();
		return FReply::Unhandled();
	}

	const FVector2D Cur = MouseEvent.GetScreenSpacePosition();

	if (!bPanningCapture)
	{
		// Accumulate until past the drag threshold; below it, children keep receiving clicks.
		PendingDragTrigger += (Cur - LastMoveScreenPos).Size();
		LastMoveScreenPos = Cur;

		// Axis split (scrollable panel, blank area): a HORIZONTAL drag past the (larger) horizontal threshold
		// moves the WHOLE window — the thin mobile titlebar is hard to grab. A VERTICAL drag scrolls content.
		// Use the offset from the PRESS point for direction. Horizontal threshold is bigger so a small sideways
		// wobble during a vertical scroll doesn't trip it. (Only when there's scroll room; a non-scrollable
		// panel already bubbles the press to the window for dragging — handled in OnMouseButtonDown.)
		const FVector2D FromPress = Cur - PressScreenPos;
		const float BaseTrigger = FSlateApplication::Get().GetDragTriggerDistance();
		const float HMoveTrigger = BaseTrigger * FMath::Max(GImSlatePanelHSlideMoveFactor, 0.f);
		const bool bHorizontalDominant = FMath::Abs(FromPress.X) > FMath::Abs(FromPress.Y);

		if (CanScroll() && GImSlatePanelHSlideMoveFactor > 0.f && bHorizontalDominant
			&& FMath::Abs(FromPress.X) > HMoveTrigger && Window && Window->CanStartMoveDrag())
		{
			// Start moving the window. Panel keeps capture and drives SetWindowPos directly (no drag-drop), so
			// the (0,0) phantom-pointer issue that affects drag-drop can't occur here.
			PanMode = EPanMode::MoveWindow;
			bPanningCapture = true;
			LastMoveScreenPos = Cur;
			MoveWindowStartScreenPos = Cur;
			MoveWindowStartWindowPos = Window->GetWindowPos();
			return FReply::Handled().CaptureMouse(AsShared());
		}

		if (PendingDragTrigger > BaseTrigger)
		{
			FReply Reply = FReply::Unhandled();
			if (TryStartPan(MyGeometry, MouseEvent, Reply))
			{
				// Scroll captures (bPanningCapture set); move-window started a drag-drop — stop tracking here.
				if (!bPanningCapture)
					EndPan();
				return Reply;
			}
			EndPan();  // nothing to do
		}
		return FReply::Unhandled();
	}

	// Capturing: drive the pan with the per-move delta.
	const FVector2D Delta = Cur - LastMoveScreenPos;
	LastMoveScreenPos = Cur;
	ApplyPanDelta(MyGeometry, Cur, Delta);
	return FReply::Handled();
}

FReply SImSlatePanel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const bool bWasCapturing = bPanningCapture;  // only Scroll captures; move-window runs as drag-drop
	const bool bWasScroll = bWasCapturing && PanMode == EPanMode::Scroll;
	EndPan();
	if (bWasScroll)
		BeginInertialScrolling();  // coast with whatever flick velocity was sampled during the drag
	if (bWasCapturing)
		return FReply::Handled().ReleaseMouseCapture();
	return FReply::Unhandled();  // just a click — let it through
}

void SImSlatePanel::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SPanel::OnMouseCaptureLost(CaptureLostEvent);
	EndPan();
}

int32 SImSlatePanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FArrangedChildren ArrangedChildren(EVisibility::Visible);
	ArrangeChildren(AllottedGeometry, ArrangedChildren);

	FSlateRect MyNewCullingRect(AllottedGeometry.GetRenderBoundingRect(CullingBoundsExtension));
	FSlateClippingZone ClippingZone(MyCullingRect.IntersectionWith(MyNewCullingRect));
	OutDrawElements.PushClip(ClippingZone);
	ON_SCOPE_EXIT { OutDrawElements.PopClip(); };

	// Window content background — drawn HERE (inside the content panel), not by SImSlateWindow's outer OnPaint.
	// SImSlateWindow is the PARENT of the 3D box (Content3DBox); anything it paints is outside the projection
	// boundary and can't be perspective-projected. Painting the background here puts it INSIDE the box's capture
	// subtree, so it rides the same 3D projection as every item — no special-case, 2D and 3D share one path.
	PaintContentBackground(AllottedGeometry, OutDrawElements, LayerId);

	// Draw group backgrounds underneath children (same LayerId → drawn first = below).
	PaintGroupBackgrounds(AllottedGeometry, OutDrawElements, LayerId);

	return PaintArrangedChildren(Args, ArrangedChildren, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

void SImSlatePanel::PaintContentBackground(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!Window)
	{
		return;
	}
	// Fill the whole content panel with the window's content background colour, as a MakeBox at the panel's own
	// geometry. Inside the 3D box's capture subtree, so it projects with everything else (was an un-projectable
	// MakeCustomVerts in SImSlateWindow::OnPaint, the box's parent). Static brush tinted per-frame (a per-frame
	// new brush wouldn't update — see ImCheckBox / R: roundedbox tint).
	FLinearColor BgColor = Window->GetBgColor();
	BgColor.A = Window->GetBgAlpha();
	if (BgColor.A <= 0.f)
	{
		return;  // fully transparent → nothing to draw (e.g. a transparent gizmo window)
	}
	static FSlateBrush BgBrush = []() { FSlateBrush B; B.TintColor = FLinearColor::White; return B; }();
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &BgBrush,
		ESlateDrawEffect::None, BgColor);
}

void SImSlatePanel::PaintGroupBackgrounds(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (CachedGroupRects.Num() == 0)
		return;

	// STATIC white RoundedBox brush; per-group colour passed via MakeBox's InTint (last arg).
	// Rebuilding a brush each paint does NOT update on screen — tint a stable brush instead (see ImCheckBox.cpp).
	// Faint fill (tinted per-group via InTint) + a fixed subtle outline that draws a visible frame.
	// InTint only affects the fill; the outline colour comes from OutlineSettings and is NOT tinted
	// (verified by ImCheckBox: its box outline stays grey while the fill is tinted per state).
	static FSlateBrush GroupBrush = []() {
		FSlateBrush B;
		B.DrawAs = ESlateBrushDrawType::RoundedBox;
		B.TintColor = FLinearColor::White;
		B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		B.OutlineSettings.CornerRadii = FVector4(4.f, 4.f, 4.f, 4.f);
		B.OutlineSettings.Color = FLinearColor(0.06f, 0.30f, 0.12f, 0.85f);
		B.OutlineSettings.Width = 1.5f;
		return B;
	}();

	for (const auto& Pair : CachedGroupRects)
	{
		const FGroupRect& GR = Pair.Value;
		if (!GR.bInit || GR.Color.A <= 0.f)
			continue;
		// Expand outward so the group reads as a panel/frame around the controls instead of a colour
		// strip hidden behind their opaque backgrounds — a margin of bg is left visible on all sides.
		const float Pad = 3.f * FMath::Max(GImSlateLayoutScale, 1.f);
		const FVector2D Pos(GR.Rect.Left - Pad, GR.Rect.Top - Pad);
		const FVector2D Size(GR.Rect.Right - GR.Rect.Left + 2.f * Pad, GR.Rect.Bottom - GR.Rect.Top + 2.f * Pad);
		if (Size.X <= 0.f || Size.Y <= 0.f)
			continue;
		const auto BoxGeom = AllottedGeometry.MakeChild(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos))).ToPaintGeometry();
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, BoxGeom, &GroupBrush, ESlateDrawEffect::None, GR.Color);
	}
}

float SImSlatePanel::GetDesiredWidth() const
{
	return GetDesiredSize().X;
}

float SImSlatePanel::GetDesiredHeight() const
{
	return GetDesiredSize().Y;
}

}  // namespace ImSlate
