// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateVirtualList.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Engine/World.h"
#include "ImSlateListDataInc.h"
#include "Misc/ScopeExit.h"
#include "PrivateFieldAccessor.h"
#include "ProtectFieldAccessor.h"
#include "SImSlateLayout.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Views/STableViewBase.h"
#include "HAL/PlatformMath.h"

namespace ImSlate
{
static bool bEnableTouchEvent = false;

extern void PrepassInternal(const TSharedRef<SWidget>& InWidget, float LayoutScaleMultiplier);
static void SlatePrepassWidget(const TSharedRef<SWidget>& WidgetRef)
{
	WidgetRef->SlatePrepass();
}
class FVirtualListSlot
	: public TSlotBase<FVirtualListSlot>
#if UE_5_00_OR_LATER
	, public TAlignmentWidgetSlotMixin<FVirtualListSlot>
#else
	, public TSupportsContentAlignmentMixin<FVirtualListSlot>
#endif
{
public:
	const int32 DataIndex;
	mutable float CachedAxis = 0.f;

public:
	FVirtualListSlot(int32 InIndex)
		: TSlotBase<FVirtualListSlot>()
#if UE_5_00_OR_LATER
		, TAlignmentWidgetSlotMixin<FVirtualListSlot>(HAlign_Fill, VAlign_Fill)
#else
		, TSupportsContentAlignmentMixin<FVirtualListSlot>(HAlign_Fill, VAlign_Fill)
#endif
		, DataIndex(InIndex)
	{
	}

	template<typename S = SWidget>
	auto& GetTypedWidget() const
	{
		return const_cast<TSharedRef<S>&>(GetWidget());
	}
};

struct FVirtualListSlots
{
public:
	FVirtualListSlots(SWidget* InOwner)
		: MyChildren(InOwner)
	{
	}
	void Empty() { MyChildren.Empty(); }
	FVirtualListSlot& AppendSlot(int32 InDataIndex)
	{
		auto NewSlot = new FVirtualListSlot(InDataIndex);
		MyChildren.Add(NewSlot);
		return *NewSlot;
	}
	FVirtualListSlot& InsertSlot(int32 InDataIndex)
	{
		auto NewSlot = new FVirtualListSlot(InDataIndex);
		MyChildren.Insert(NewSlot);
		return *NewSlot;
	}
	bool RemoveLast(int32 InCnt = 1) { return MyChildren.RemoveLast(InCnt); }

	FVirtualListSlot& At(int32 Index) { return MyChildren[Index]; }
	const FVirtualListSlot& At(int32 Index) const { return MyChildren[Index]; }
	int32 FirstDataIndex(int32 InOutIndex)
	{
		if (GetChildren().Num() > 0)
			return FMath::Min(GetChildren().First().DataIndex - 1, InOutIndex);
		return InOutIndex;
	}
	int32 LastDataIndex(int32 InOutIndex)
	{
		if (GetChildren().Num() > 0)
			return FMath::Max(GetChildren().Last().DataIndex + 1, InOutIndex);
		return InOutIndex;
	}
	int32 FirstDataIndex() const
	{
		if (GetChildren().Num() > 0)
			return GetChildren().First().DataIndex;
		return INDEX_NONE;
	}
	int32 LastDataIndex() const
	{
		if (GetChildren().Num() > 0)
			return GetChildren().Last().DataIndex;
		return INDEX_NONE;
	}
	TChildrenLayout<FVirtualListSlot>& GetChildren() { return MyChildren; }
	const TChildrenLayout<FVirtualListSlot>& GetChildren() const { return MyChildren; }
	TChildrenLayout<FVirtualListSlot> MyChildren;

	const FVirtualListSlot* FindByDataIndex(int32 InDataIndex) const
	{
		do
		{
			auto SlotNum = MyChildren.Num();
			if (SlotNum == 0)
				break;
			auto& FirstSlot = MyChildren.First();
			if (FirstSlot.DataIndex > InDataIndex)
				break;

			auto SlotIndex = InDataIndex - FirstSlot.DataIndex;
			if (SlotIndex >= SlotNum)
				break;

			if (!ensure(MyChildren[SlotIndex].DataIndex == InDataIndex))
			{
				for (auto i = 0; i < GetChildren().Num(); ++i)
				{
					auto DataIdx = At(i).DataIndex;
					if (DataIdx == InDataIndex)
					{
						return &MyChildren[i];
					}
				}
				break;
			}

			return &MyChildren[SlotIndex];
		} while (false);

		return nullptr;
	}

	template<typename F>
	float GetAxisByDataIndex(int32 InDataIndex, float MinAxis, const F& OnGetAxisRef) const
	{
#if WITH_EDITOR && 0
		if (auto Ptr = FindByDataIndex(InDataIndex))
		{
			auto ItemAxis = GetOrientationAxis(Ptr->GetWidget()->GetCachedGeometry().GetLocalSize());
			return ItemAxis <= 0.f ? OnGetAxisRef() : FMath::Max(MinAxis, ItemAxis);
		}
		else
#endif
		{
			auto ItemAxis = OnGetAxisRef();
			return FMath::Max(MinAxis, ItemAxis);
		}
	}

	template<typename F>
	void RefreshSlotedWidgets(const F& OnSetDataRef) const
	{
		for (auto i = 0; i < GetChildren().Num(); ++i)
		{
			auto DataIdx = MyChildren[i].DataIndex;
			auto& DataWidget = MyChildren[i].GetTypedWidget();
			OnSetDataRef(DataIdx, DataWidget);
		}
	}
};

SImSlateVirtualList::SImSlateVirtualList()
	: SlotCollection(this)
{
	VerticalScrollBarSlot = nullptr;
	bClippingProxy = true;
	LastScrollTime = 0.0;
}

SImSlateVirtualList::~SImSlateVirtualList() = default;

void SImSlateVirtualList::Construct(const FArguments& InArgs)
{
	ScrollBarStyle = InArgs._ScrollBarStyle;
	DesiredScrollOffset = 0.f;
	bIsScrolling = false;
	bAnimateScroll = false;
	AmountScrolledWhileRightMouseDown = 0.f;
	PendingScrollTriggerAmount = 0.f;
	bShowSoftwareCursor = false;
	SoftwareCursorPosition = FVector2f::ZeroVector;
	OnUserScrolled = InArgs._OnUserScrolled;
	OnScrollBarVisibilityChanged = InArgs._OnScrollBarVisibilityChanged;
	Orientation = InArgs._Orientation;
	bScrollToEnd = false;
	bIsScrollingActiveTimerRegistered = false;
	bAllowsRightClickDragScrolling = true;
	ConsumeMouseWheel = InArgs._ConsumeMouseWheel;
	TickScrollDelta = 0.f;
	AllowOverscroll = InArgs._AllowOverscroll;
	bBackPadScrolling = InArgs._BackPadScrolling;
	bFrontPadScrolling = InArgs._FrontPadScrolling;
	bAnimateWheelScrolling = InArgs._AnimateWheelScrolling;
	WheelScrollMultiplier = InArgs._WheelScrollMultiplier;
	NavigationScrollPadding = InArgs._NavigationScrollPadding;
	NavigationDestination = InArgs._NavigationDestination;
	ScrollWhenFocusChanges = InArgs._ScrollWhenFocusChanges;
	bTouchPanningCapture = false;
	bVolatilityAlwaysInvalidatesPrepass = true;
	OnVirtualPosChanged = InArgs._OnVirtualPosChanged;

	bStartedTouchInteraction = false;
	bAllowOverBoundDraw = true;
	bSameContentLayerId = true;

	// SetClipping(EWidgetClipping::OnDemand);
	// SetCanTick(true);

	if (InArgs._WorldCtxPtr)
		ImSlateWorldCtx = InArgs._WorldCtxPtr;

	if (InArgs._ExternalScrollbar.IsValid())
	{
		// An external scroll bar was specified by the user
		ScrollBar = InArgs._ExternalScrollbar;
		ScrollBar->SetOnUserScrolled(FOnUserScrolled::CreateSP(this, &SImSlateVirtualList::ScrollBar_OnUserScrolled));
		ScrollBar->SetOnScrollBarVisibilityChanged(FOnScrollBarVisibilityChanged::CreateSP(this, &SImSlateVirtualList::ScrollBar_OnScrollBarVisibilityChanged));
		bScrollBarIsExternal = true;
	}
	else
	{
		// Make a scroll bar
		ScrollBar = ConstructScrollBar();
		ScrollBar->SetDragFocusCause(InArgs._ScrollBarDragFocusCause);
		ScrollBar->SetThickness(InArgs._ScrollBarThickness);
		ScrollBar->SetUserVisibility(InArgs._ScrollBarVisibility);
		ScrollBar->SetScrollBarAlwaysVisible(InArgs._ScrollBarAlwaysVisible);
		ScrollBar->SetOnScrollBarVisibilityChanged(FOnScrollBarVisibilityChanged::CreateSP(this, &SImSlateVirtualList::ScrollBar_OnScrollBarVisibilityChanged));
		ScrollBarSlotPadding = InArgs._ScrollBarPadding;
		bScrollBarIsExternal = false;
	}
	{
		auto TmpScrollBar = GS_ACCESS_PROTECT(ScrollBar, SScrollBar, Orientation, bHideWhenNotInUse);
		TmpScrollBar->bHideWhenNotInUse = true;
	}
	ScrollBar->SetState(0.0f, 1.0f);
}

FChildren* SImSlateVirtualList::GetChildren()
{
	return &SlotCollection->GetChildren();
}

void SImSlateVirtualList::SetData(TSharedPtr<IImSlateListData> InDataBinding, float InVirtualPos, float InTileAxisVal)
{
	if (!InDataBinding.IsValid())
		return;

	bool bShouldReload = false;
	if (InDataBinding != DataBinding)
	{
		if (DataBinding.IsValid())
			DataBinding->SetVirtualList(nullptr);

		DataBinding = InDataBinding;
		DataBinding->SetVirtualList(ToSharedRef());
		TileOrthValue = InTileAxisVal;
		bShouldReload = true;
	}
	else if (TileOrthValue != InTileAxisVal)
	{
		TileOrthValue = InTileAxisVal;
		bShouldReload = true;
	}

	if (bShouldReload)
		ReloadToPos(InVirtualPos);
}

void SImSlateVirtualList::ReloadToPos(float InVirtualPos, bool bItemAlign)
{
	if (!HasValidCol())
	{
		IndexRange.Reset(true);
		DelayScrollToPos(InVirtualPos, bItemAlign);
	}
	else
	{
		auto WidgetOrth = GetOrientationOrth(CachedGeometry.GetLocalSize());
		if (WidgetOrth > 0.f)
		{
			TotalCol = GetTileOrth() > 0.f ? FMath::Max(1, int32(WidgetOrth / GetTileOrth())) : 1;
		}
		InnerReloadToPos(InVirtualPos, bItemAlign);
	}
}

bool SImSlateVirtualList::InnerReloadToPos(float InVirtualPos, bool bItemAlign)
{
	ResetAxises();

	if (!HasValidCol())
		return false;

	return InnerScrollToPos(InVirtualPos, GetVisibleAxis(), bItemAlign);
}

void SImSlateVirtualList::DelayScrollToPos(float InVirtualPos, bool bItemAlign)
{
	ScrollIntoViewRequest = [=, this](const FGeometry& MyGeometry) {
		if (!HasValidCol())
		{
			auto WidgetOrth = GetOrientationOrth(MyGeometry.GetLocalSize());
			if (WidgetOrth > 0.f)
			{
				TotalCol = GetTileOrth() > 0.f ? FMath::Max(1, int32(WidgetOrth / GetTileOrth())) : 1;
			}
		}
		InnerScrollToPos(InVirtualPos, GetVisibleAxis(), bItemAlign);
	};
}

void SImSlateVirtualList::ReloadToItem(int32 InIndex, bool bCenterAlign)
{
	ResetAxises();
	if (!HasValidCol())
	{
		IndexRange.Reset(true);
		DelayScrollToItem(InIndex, bCenterAlign);
	}
	else
	{
		auto WidgetOrth = GetOrientationOrth(CachedGeometry.GetLocalSize());
		if (WidgetOrth > 0.f)
		{
			TotalCol = GetTileOrth() > 0.f ? FMath::Max(1, int32(WidgetOrth / GetTileOrth())) : 1;
		}
		ScrollToItem(InIndex, bCenterAlign);
	}
}

void SImSlateVirtualList::DelayScrollToItem(int32 InIndex, bool bCenterAlign)
{
	ScrollIntoViewRequest = [=, this](const FGeometry& MyGeometry) {
		if (!HasValidCol())
		{
			auto WidgetOrth = GetOrientationOrth(MyGeometry.GetLocalSize());
			if (WidgetOrth > 0.f)
			{
				TotalCol = GetTileOrth() > 0.f ? FMath::Max(1, int32(WidgetOrth / GetTileOrth())) : 1;
			}
		}
		ScrollToItem(InIndex, bCenterAlign);
	};
}

bool SImSlateVirtualList::ScrollToPos(float InVirtualPos, bool bItemAlign)
{
	if (!HasValidCol())
	{
		DelayScrollToPos(InVirtualPos, bItemAlign);
		return false;
	}

	return InnerScrollToPos(InVirtualPos, GetVisibleAxis(), bItemAlign);
}

bool SImSlateVirtualList::InnerScrollToPos(float InVirtualPos, float InListAxis, bool bItemAlign)
{
	if (!ImEnsure(InListAxis > 0.f))
		return false;

	auto OldVal = OverrideAxis;
	OverrideAxis = InListAxis;
	ON_SCOPE_EXIT
	{
		OverrideAxis = OldVal;
	};

	if (GetDataCount() > 0)
	{
		EnsureDataAxises(InVirtualPos < 0.f ? std::numeric_limits<float>::max() : InVirtualPos);
		auto TargetPos = InVirtualPos;
		auto TotalAxis = GetCachedTotalAxis();
		if (InVirtualPos == 0.f && FMath::IsNegativeOrNegativeZero(InVirtualPos))  // do not adjust pos
			TargetPos = DesiredScrollOffset;
		else if (InVirtualPos < 0.f)
			TargetPos = std::numeric_limits<float>::max();
		TargetPos = FMath::Clamp(TargetPos, 0.f, FMath::Max(0.f, TotalAxis - InListAxis));
		if (bItemAlign)
			TargetPos = GetDataOffset(UpperDataIndex(TargetPos));
		DesiredScrollOffset = TargetPos;
	}
	else
	{
		// TODO? delay to deal with virtual pos
		ActualVirtualPos = 0.f;
		DesiredScrollOffset = 0.f;
	}
	EndInertialScrolling();
	return true;
}

void SImSlateVirtualList::UpdateScrollState() const
{
	if (ScrollBar)
	{
		auto ListAxis = GetVisibleAxis();
		auto TotalAxis = GetCachedTotalAxis(IsShowScrollbar());
		auto TotalOffset = TotalAxis - ListAxis;
		if (TotalOffset <= 0.f)
			ScrollBar->SetState(0.f, 1.f);
		else
			ScrollBar->SetState(ActualVirtualPos / TotalAxis, ListAxis / TotalAxis);
	}
}

void SImSlateVirtualList::RefreshVisibleWidget()
{
	SlotCollection->RefreshSlotedWidgets([this](auto& Index, auto& Widget) { OnSetData(Index, Widget); });
}

void SImSlateVirtualList::Update(int32 InDataIndex, bool bReConstruct)
{
	if (!DataBinding.IsValid())
		return;

	if (!bReConstruct || !IsHeterogeneous())
	{
		if (!HasValidCol())
		{
			IndexRange.SetChanged(true);
		}
		else if (InDataIndex < 0)
		{
			RefreshVisibleWidget();
			IndexRange.AppendChanged(EnsureDataAxises(-1.f));
		}
		else if (InDataIndex >= DataAxises.Num())
		{
			IndexRange.AppendChanged(EnsureDataAxises(-1.f) && InDataIndex <= DataAxises.Num());
		}
		else if (GetVisibleAxis(false) > 0.f)
		{
			UpdateData(InDataIndex, bReConstruct);
		}
		return;
	}

	// bReConstruct && IsHeterogeneous()
	if (InDataIndex < 0)
	{
		//reload all
		InnerReloadToPos();
		return;
	}
	else
	{
		// not in cache, test if in range
		if (InDataIndex >= DataAxises.Num())
		{
			IndexRange.AppendChanged(EnsureDataAxises(-1.f) && InDataIndex <= DataAxises.Num());
			return;
		}
	}

	// refresh widget
	UpdateData(InDataIndex, bReConstruct);
}

void SImSlateVirtualList::SetBackgroundContent(TSharedRef<SWidget> InWidget)
{
	if (!BackgroundBox)
	{
		BackgroundBox = SNew(SBox);
	}
	BackgroundBox->SetContent(InWidget);
}

// Begin IImSlateListData Wrapper
void IImSlateListData::SetVirtualPos(float VirtualPos, bool bItemAlign, bool bReset)
{
	if (auto Pined = GetVirtualList())
	{
		if (bReset)
			Pined->ReloadToPos(VirtualPos, bItemAlign);
		else
			Pined->ScrollToPos(VirtualPos, bItemAlign);
	}
}

float IImSlateListData::GetVirtualPos() const
{
	if (auto Pined = GetVirtualList())
	{
		return Pined->GetVirtualPos();
	}
	return 0.f;
}

void IImSlateListData::UpdateItem(int32 InIndex, bool bReConstructWidget)
{
	if (auto Pined = GetVirtualList())
	{
		Pined->Update(InIndex, bReConstructWidget || IsHeterogeneous());
	}
}

void IImSlateListData::ScrollToItem(int32 InIndex, bool bCenterAlign)
{
	if (auto Pined = GetVirtualList())
	{
		Pined->ScrollToItem(InIndex, bCenterAlign);
	}
}

int32 SImSlateVirtualList::GetDataCount() const
{
	return DataBinding->GetDataCount();
}

void SImSlateVirtualList::OnPosChanged(float InVirtualPos)
{
	OnVirtualPosChanged.ExecuteIfBound(InVirtualPos);
	DataBinding->OnPosChanged(InVirtualPos);
}

void SImSlateVirtualList::OnSetData(int32 Index, TSharedRef<SWidget> Widget)
{
	return DataBinding->OnSetData(Index, Widget);
}

float SImSlateVirtualList::GetItemAxis(int32 InIndex) const
{
	static FVector2D MinItemSize{6.f, 6.f};
	return SlotCollection->GetAxisByDataIndex(InIndex, GetOrientationAxis(MinItemSize), [&] { return DataBinding->GetItemAxis(InIndex); });
}

bool SImSlateVirtualList::IsShowScrollbar() const
{
	return ScrollBar->ShouldBeVisible().IsVisible();
}

void SImSlateVirtualList::GenerateDataWidget(int32 InIndex, TSharedRef<SWidget>& InOutWidget)
{
	if (InOutWidget == SNullWidget::NullWidget || DataBinding->IsHeterogeneous())
	{
		DataBinding->GenerateDataWidget(InIndex, InOutWidget);
	}
}

void SImSlateVirtualList::ReleaseDataWidget(int32 InIndex, TSharedRef<SWidget>& ReleasedWidget)
{
	if (ReleasedWidget != SNullWidget::NullWidget)
	{
		DataBinding->ReleaseDataWidget(InIndex, ReleasedWidget);
	}
}

bool SImSlateVirtualList::IsHeterogeneous() const
{
	return DataBinding->IsHeterogeneous();
}

bool SImSlateVirtualList::NeedPrepassItem() const
{
	return DataBinding->NeedPrepassItem();
}

// End IImSlateListData Wrapper

void SImSlateVirtualList::ScrollToItem(int32 InIndex, bool bCenterAlign)
{
	if (!HasValidCol())
	{
		DelayScrollToItem(InIndex, bCenterAlign);
		return;
	}

	auto ListAxis = GetVisibleAxis();
	if (ListAxis <= 0.f)
	{
		DelayScrollToItem(InIndex, bCenterAlign);
		return;
	}

	InIndex = FMath::Clamp(InIndex, 0, GetDataCount() - 1);
	EnsureDataAxises(InIndex);
	auto Offset = GetDataOffset(InIndex);
	if (bCenterAlign)
	{
		Offset -= (ListAxis - GetDataAxis(InIndex)) / 2;
		Offset = FMath::Max(Offset, 0.f);
	}
	InnerScrollToPos(Offset, ListAxis, false);
}

void SImSlateVirtualList::SetOverCountRowNum(int32 InNum)
{
	ensure(InNum >= 0);
	OverCountRowNum = FMath::Max(0, InNum);
}

void SImSlateVirtualList::SetScrollbarUserVisibility(TAttribute<EVisibility> InUserVisibility)
{
	if (ScrollBar)
	{
		ScrollBar->SetUserVisibility(MoveTemp(InUserVisibility));
	}
}

void SImSlateVirtualList::EnableWidgetClipping(bool bClippingWidget)
{
	bAllowOverBoundDraw = !bClippingWidget;
	//SetClipping(bAllowOverBoundDraw ? EWidgetClipping::Inherit : EWidgetClipping::ClipToBounds);
}

void SImSlateVirtualList::SetAnimateScrollDuration(float InAnimateDuration = 1.f)
{
	if (InAnimateDuration == 0.f)
	{
		if (FMath::IsNegativeOrNegativeZero(InAnimateDuration))
		{
			// disable it
			LeftAnimateDuration = 0.f;
			bAnimateScroll = false;
		}
		else
		{
			// restore it
			AnimateDuration = FMath::Abs(AnimateDuration);
			LeftAnimateDuration = AnimateDuration;
			AnimationStartPos = GetVirtualPos();
			bAnimateScroll = true;
		}
	}
	else
	{
		// set new duration
		AnimateDuration = FMath::Abs(InAnimateDuration);
		LeftAnimateDuration = AnimateDuration;
		AnimationStartPos = GetVirtualPos();
		bAnimateScroll = true;
	}
}

bool SImSlateVirtualList::IsItemOffsetVisible(int32 InIndex, float InRelativePos) const
{
	if (!DataOffsets.IsValidIndex(InIndex) || !DataAxises.IsValidIndex(InIndex) || !HasValidCol() || GetVisibleAxis(false) <= 0.f)
		return false;

	auto ItemOffset = DataOffsets[InIndex];
	auto PosMin = GetVirtualPos();
	auto PosMax = PosMin + GetVisibleAxis();
	if (FMath::IsNegativeOrNegativeZero(InRelativePos))
	{
		return (ItemOffset <= PosMax) || (ItemOffset + GetDataAxis(InIndex) >= PosMin);
	}
	return FMath::IsWithin(ItemOffset + InRelativePos, PosMin, PosMax);
}

float SImSlateVirtualList::GetVirtualPos() const
{
	if (!IndexRange.HasValidRange() || ActualVirtualPos < 0.f)
		return 0.f;

	return ActualVirtualPos;
}

bool SImSlateVirtualList::UpdateRange(float InListAxis, float InVirtualPos, float InOverscrollAmount, bool bForce)
{
	if (UpdateRangeIndex(InListAxis, InVirtualPos, InOverscrollAmount, bForce) || ActualVirtualPos != InVirtualPos)
	{
		ActualVirtualPos = InVirtualPos;
		OnPosChanged(ActualVirtualPos);
		return true;
	}
	return false;
}

bool SImSlateVirtualList::UpdateRangeIndex(float InListAxis, float& InOutVirtualPos, float InOverscrollAmount, bool bForce) const
{
	do
	{
		if (!DataBinding)
			break;

		check(InListAxis > 0.f);
		EnsureDataAxises(InOutVirtualPos + InListAxis);
		bool bAllowOverScroll = IsAllowOverScroll();
		if (!bForce && !IndexRange.HasChanged())
		{
			if (FMath::IsNearlyEqual(ActualVirtualPos, InOutVirtualPos))
				break;

			if (!bAllowOverScroll)
			{
				if (InOutVirtualPos <= 0.f && ActualVirtualPos <= 0.f)
				{
					InOutVirtualPos = InOverscrollAmount;
					break;
				}
			}
		}
		const auto TotalDataCount = GetDataCount();
		if (TotalDataCount <= 0 || GetDataAxis(0) <= 0.1f)
			break;
		if (!ImEnsure(GetCachedTotalAxis() > 0.f))
			break;
		const auto MaxOffset = GetCachedTotalAxis() - InListAxis;
		const auto ClampVirtualPos = FMath::Clamp(InOutVirtualPos, 0.f, MaxOffset);

		bool bNoMore = !bForce && !IndexRange.HasChanged() && !bAllowOverScroll && InOutVirtualPos >= MaxOffset && ActualVirtualPos >= MaxOffset;
		if (bNoMore)
			break;

		auto TmpStartIndex = DataIndexFromOffset(ClampVirtualPos, 0);
		auto TmpEndIndex = ClampVirtualPos >= MaxOffset ? TotalDataCount - 1 : UpperDataIndex(ClampVirtualPos + InListAxis);

#if WITH_EDITOR
		if (!ImEnsure(TmpEndIndex < TotalDataCount))
		{
			const auto Offset = TmpEndIndex - TotalDataCount + 1;
			TmpEndIndex = TotalDataCount - 1;
			TmpStartIndex -= Offset;
		}
		else if (!ImEnsure(TmpEndIndex >= 0))
		{
			TmpEndIndex = 0;
		}

		if (!ImEnsure(TmpStartIndex >= 0))
			TmpStartIndex = 0;
#endif
		// UE_LOG(LogSlate, Log, TEXT("Pos %f <- %f"), VirtualPos, InVirtualPos);
		InOutVirtualPos = ClampVirtualPos + InOverscrollAmount;

		if (!IndexRange.SetRange(TmpStartIndex, TmpEndIndex))
			break;
		return true;
	} while (false);
	return false;
}

bool SImSlateVirtualList::EnsureDataAxises(int32 InIndex) const
{
	check(DataOffsets.Num() == DataAxises.Num());
	const auto TotalDataCount = GetDataCount();
	auto Max = InIndex >= TotalDataCount ? 0 : (InIndex < 0 ? TotalDataCount : FMath::Min(InIndex + OverCountRowNum * FMath::Max(1, GetTotalCol()) + 1, TotalDataCount));

	const auto OldNum = DataAxises.Num();
	for (auto i = DataAxises.Num(); i < Max; ++i)
	{
		MutableThis()->AppendDataAxis(i);
	}

	return OldNum != DataAxises.Num();
}

bool SImSlateVirtualList::EnsureDataAxises(float InOffset) const
{
	check(DataOffsets.Num() == DataAxises.Num());
	InOffset = FMath::Max(GetScrollOffsetOfEnd(), InOffset);

	const auto OldNum = DataAxises.Num();
	auto Max = GetDataCount();
	auto i = DataAxises.Num();
	for (; i < Max && GetCachedTotalAxis() < InOffset; ++i)
	{
		MutableThis()->AppendDataAxis(i);
	}
	Max = FMath::Min(Max, i + OverCountRowNum * FMath::Max(1, GetTotalCol()));
	for (; i < Max; ++i)
	{
		MutableThis()->AppendDataAxis(i);
	}
	return OldNum != DataAxises.Num();
}

void SImSlateVirtualList::AppendDataAxis(int32 InDataIndex)
{
	auto ItemAxis = GetItemAxis(InDataIndex);
	ImEnsure(HasValidCol());

	if (GetTotalCol() <= 1 || (InDataIndex % GetTotalCol()) == 0)
	{
		LastRowOffset += LastRowAxis;
		LastRowAxis = ItemAxis;
	}
	else
	{
		LastRowAxis = FMath::Max(ItemAxis, LastRowAxis);
	}
	DataAxises.Add(ItemAxis);
	DataOffsets.Add(LastRowOffset);
	CachedWidgets.Add(false);
}

bool SImSlateVirtualList::UpdateSlotedAxis(const FVirtualListSlot* Slot, bool bPrepass)
{
	check(Slot && DataAxises.IsValidIndex(Slot->DataIndex));
	const auto DataIdx = Slot->DataIndex;
	auto NewAxis = GetItemAxis(Slot->DataIndex);
	if (bPrepass || NewAxis <= 0.f)
	{
		SlatePrepassWidget(Slot->GetTypedWidget());
		NewAxis = GetOrientationAxis(Slot->GetTypedWidget()->GetDesiredSize());
	}
	Slot->CachedAxis = NewAxis;
	auto Delta = NewAxis - GetDataAxis(DataIdx);
	return UpdateSlotedAxisDelta(Slot, Delta);
}

bool SImSlateVirtualList::UpdateSlotedAxisDelta(const FVirtualListSlot* Slot, float Delta)
{
	checkSlow(Slot && DataAxises.IsValidIndex(Slot->DataIndex));
	const auto DataIdx = Slot->DataIndex;
	CachedWidgets[DataIdx] = true;
	if (Delta != 0.f)
	{
		DataAxises[DataIdx] += Delta;
		const auto NextColIdx = (DataIdx / GetTotalCol() + 1) * GetTotalCol();
		if (NextColIdx < DataOffsets.Num())
		{
			for (auto i = NextColIdx; i < DataOffsets.Num(); ++i)
			{
				DataOffsets[i] += Delta;
			}
			LastRowOffset += Delta;
		}
		else
		{
			for (auto i = (DataIdx / GetTotalCol()) * GetTotalCol(); i < NextColIdx; ++i)
			{
				LastRowAxis = FMath::Max(DataAxises[i], LastRowAxis);
			}
		}

		// keep first widgets offset
		if (DataIdx < IndexRange.StartIndex)
			DesiredScrollOffset += Delta;

		Invalidate(EInvalidateWidget::ChildOrder);
		return true;
	}
	return false;
}

void SImSlateVirtualList::ResetAxises(int32 FromIdx)
{
	FromIdx = !HasValidCol() ? FromIdx : (FromIdx / GetTotalCol()) * GetTotalCol();
	if (FromIdx <= 0)
	{
		const auto TotalDataCount = GetDataCount();
		DataAxises.Empty(TotalDataCount);
		DataOffsets.Empty(TotalDataCount);
		CachedWidgets.Empty(TotalDataCount);

		LastRowOffset = 0.f;
		LastRowAxis = 0.f;

		{
			TArray<TSharedRef<SWidget>> ReleasedWidgets;
			auto EndIdx = SlotCollection->GetChildren().Num() - 1;
			auto StartIdx = 0;
			for (auto i = StartIdx; i < EndIdx; ++i)
			{
				ReleasedWidgets.Add(SlotCollection->GetChildren().GetChildAt(i));
			}
			for (auto i = EndIdx - 1; i >= StartIdx; --i)
			{
				ReleaseDataWidget(i, ReleasedWidgets[i - StartIdx]);
			}
		}
		SlotCollection->Empty();
		EnsureDataAxises(0);
		IndexRange.Reset(true);
	}
	else if (FromIdx < DataAxises.Num())
	{
		auto ReducedNum = DataAxises.Num() - FromIdx;

		DataAxises.SetNum(FromIdx);
		DataOffsets.SetNum(FromIdx);
		CachedWidgets.Empty(FromIdx);

		LastRowAxis = DataAxises.Last();
		LastRowOffset = DataOffsets.Last();

		{
			TArray<TSharedRef<SWidget>> ReleasedWidgets;
			auto EndIdx = SlotCollection->GetChildren().Num() - 1;
			auto StartIdx = EndIdx - ReducedNum;
			for (auto i = StartIdx; i < EndIdx; ++i)
			{
				ReleasedWidgets.Add(SlotCollection->GetChildren().GetChildAt(i));
			}
			for (auto i = EndIdx - 1; i >= StartIdx; --i)
			{
				ReleaseDataWidget(i, ReleasedWidgets[i - StartIdx]);
			}
		}
		SlotCollection->RemoveLast(ReducedNum);
		UpdateRangeIndex(GetVisibleAxis(), DesiredScrollOffset, 0.f, true);
	}
}

bool SImSlateVirtualList::UpdateData(int32 InDataIndex, bool bReConstruct)
{
	check(InDataIndex >= 0);
	float Delta = 0.f;
	if (InDataIndex >= IndexRange.StartIndex && InDataIndex <= IndexRange.EndIndex)
	{
		if (auto ElmPtr = SlotCollection->FindByDataIndex(InDataIndex))
		{
			auto& DataWidget = ElmPtr->GetTypedWidget();
			bool bNeedPrepassAxis = NeedPrepassItem();
			if (bReConstruct && IsHeterogeneous())
			{
				auto OldWidget = DataWidget;
				GenerateDataWidget(ElmPtr->DataIndex, DataWidget);
				if (OldWidget != DataWidget)
					bNeedPrepassAxis = true;
			}

			if (DataWidget != SNullWidget::NullWidget)
			{
				if ((bReConstruct || bNeedPrepassAxis) && UpdateSlotedAxis(ElmPtr, bNeedPrepassAxis))
				{
					UpdateRangeIndex(GetVisibleAxis(), DesiredScrollOffset, 0.f, true);
					UpdateScrollState();
				}
			}
			OnSetData(ElmPtr->DataIndex, DataWidget);
			return true;
		}
	}
	else if (CachedWidgets.IsValidIndex(InDataIndex))
	{
		CachedWidgets[InDataIndex] = (NeedPrepassItem() || (bReConstruct && IsHeterogeneous())) ? false : true;
	}

	return false;
}

void SImSlateVirtualList::SetTileOrthVal(float InTileOrthVal)
{
	if (TileOrthValue != InTileOrthVal)
	{
		TileOrthValue = InTileOrthVal;
		ReloadToPos(ActualVirtualPos);
	}
}

void SImSlateVirtualList::SetItemPadding(FMargin InPadding)
{
	if (ItemPadding != InPadding)
	{
		ItemPadding = InPadding;
		ReloadToPos(ActualVirtualPos);
	}
}

//  <=
//     ^
int32 SImSlateVirtualList::DataIndexFromOffset(float InOffset, int32 OutRangeIndex) const
{
	EnsureDataAxises(InOffset);
	auto Ret = (InOffset < 0.f || DataOffsets.Num() == 0 || InOffset > (LastRowOffset + LastRowAxis))  //
				   ? OutRangeIndex
				   : (InOffset > DataOffsets.Last() ? DataOffsets.Num() - 1 : FMath::Max(0, Algo::LowerBound(DataOffsets, InOffset) - GetTotalCol()));
	ImEnsure(OutRangeIndex == Ret || (Ret >= 0 && Ret < GetDataCount()));
	return Ret;
}
//   <
//     ^
int32 SImSlateVirtualList::UpperDataIndex(float InOffset) const
{
	const auto TotalDataCount = GetDataCount();
	check(TotalDataCount > 0);
	EnsureDataAxises(InOffset);
	auto Ret = (InOffset < 0.f || DataOffsets.Num() == 0 || InOffset > (LastRowOffset + LastRowAxis))  //
				   ? TotalDataCount - 1
				   : ((InOffset > DataOffsets.Last()) ? DataOffsets.Num() - 1 : Algo::UpperBound(DataOffsets, InOffset) - GetTotalCol());
	ImEnsure(Ret >= 0 && Ret < TotalDataCount);
	return Ret;
}

bool SImSlateVirtualList::RowIndexRange(int32 InDataIdx, int32& Lower, int32& Upper) const
{
	if (GetTotalCol() <= 1)
	{
		Lower = InDataIdx;
		Upper = InDataIdx + 1;
		return true;
	}
	else if (ImEnsure(IndexRange.IsInRange(InDataIdx)))
	{
		Lower = (InDataIdx / GetTotalCol()) * GetTotalCol();
		Upper = FMath::Min(GetDataCount(), Lower + GetTotalCol() + 1);
		return true;
	}
	return false;
}

void SImSlateVirtualList::RowIndexRange(int32 InDataIdx, const TFunctionRef<void(int32)>& Func) const
{
	ImEnsure(HasValidCol());
	if (GetTotalCol() <= 1)
	{
		Func(InDataIdx);
	}
	else if (ImEnsure(IndexRange.IsInRange(InDataIdx)))
	{
		auto Lower = (InDataIdx / GetTotalCol()) * GetTotalCol();
		auto Upper = FMath::Min(GetDataCount(), Lower + GetTotalCol() + 1);
		for (auto i = Lower; i < Upper; ++i)
			Func(i);
	}
}

float SImSlateVirtualList::GetDataOffset(int32 InIndex) const
{
	check(InIndex >= 0 && InIndex < GetDataCount());
	EnsureDataAxises(InIndex);
	return DataOffsets[InIndex];
}

float SImSlateVirtualList::GetDataAxis(int32 InIndex) const
{
	check(InIndex >= 0 && InIndex < GetDataCount());
	EnsureDataAxises(InIndex);
	return DataAxises[InIndex];
}

float SImSlateVirtualList::GetCachedTotalAxis(bool bFullItems) const
{
	if (bFullItems)
	{
		EnsureDataAxises(-1);
	}

	return LastRowOffset + LastRowAxis;
}

bool SImSlateVirtualList::CustomPrepass(float LayoutScaleMultiplier)
{
	const auto ListAxis = GetOrientationAxis(CachedGeometry.GetLocalSize());
	if (ListAxis <= 0.f)
		return false;

	if (!IndexRange.HasValidRange())
	{
		if (!InnerScrollToPos(ActualVirtualPos, ListAxis, false))
			return false;
	}

	bHasCustomPrepass = false;
	return GenerateWidgetIfNeeded();
}

bool SImSlateVirtualList::GenerateWidgetIfNeeded()
{
	bool bItemChanged = false;
	const auto ItemCount = GetDataCount();
	if (IndexRange.OldStartIndex >= 0 && IndexRange.StartIndex > IndexRange.OldStartIndex)
	{
		int32 Inner = FMath::Max(IndexRange.StartIndex - OverCountRowNum, 0);
		Inner = FMath::Min(Inner, SlotCollection->GetChildren().Last().DataIndex + 1);
		int32 Outer = FMath::Max(IndexRange.OldStartIndex - OverCountRowNum, 0) - 1;
		Outer = SlotCollection->FirstDataIndex(Outer);

		if (Outer + 1 < Inner)
		{
			auto Count = Inner - (Outer + 1);
			{
				TArray<TSharedRef<SWidget>> ReleasedWidgets;
				for (auto i = 0; i < Count; ++i)
				{
					ReleasedWidgets.Add(SlotCollection->GetChildren().GetChildAt(i));
				}
				for (auto i = Count - 1; i >= 0; --i)
				{
					ReleaseDataWidget(i, ReleasedWidgets[i]);
				}
			}
			bItemChanged = SlotCollection->GetChildren().RemoveFirst(Count);
		}
	}

	if (IndexRange.EndIndex < IndexRange.OldEndIndex)
	{
		int32 Inner = FMath::Min(IndexRange.EndIndex + OverCountRowNum, ItemCount - 1);
		Inner = FMath::Max(Inner, SlotCollection->GetChildren().First().DataIndex - 1);
		int32 Outer = FMath::Min(IndexRange.OldEndIndex + OverCountRowNum, ItemCount - 1) + 1;
		Outer = SlotCollection->LastDataIndex(Outer);

		if (Outer - 1 > Inner)
		{
			auto Count = (Outer - 1) - Inner;
			{
				TArray<TSharedRef<SWidget>> ReleasedWidgets;
				auto EndIdx = SlotCollection->GetChildren().Num() - 1;
				auto StartIdx = FMath::Max(EndIdx - Count, 0);
				for (auto i = StartIdx; i < EndIdx; ++i)
				{
					ReleasedWidgets.Add(SlotCollection->GetChildren().GetChildAt(i));
				}
				for (auto i = EndIdx - 1; i >= StartIdx; --i)
				{
					ReleaseDataWidget(i, ReleasedWidgets[i - StartIdx]);
				}
			}
			bItemChanged = SlotCollection->GetChildren().RemoveLast(Count);
		}
	}

	if (IndexRange.StartIndex < IndexRange.OldStartIndex)
	{
		int32 Outer = FMath::Max(IndexRange.StartIndex - OverCountRowNum, 0);
		int32 Inner = FMath::Min(IndexRange.EndIndex + OverCountRowNum, ItemCount - 1);
		Inner = SlotCollection->FirstDataIndex(Inner);

		for (auto i = Inner; i >= Outer; --i)
		{
			TSharedRef<SWidget> DataWidget = SNullWidget::NullWidget;
			GenerateDataWidget(i, DataWidget);
			auto& NewSlot = SlotCollection->InsertSlot(i)[DataWidget];
			if (NeedPrepassItem() && !CachedWidgets[i] && DataWidget != SNullWidget::NullWidget)
			{
				UpdateSlotedAxis(&NewSlot, true);
			}
			OnSetData(i, DataWidget);
			bItemChanged = true;
		}
	}

	if ((IndexRange.OldEndIndex >= 0 && IndexRange.EndIndex > IndexRange.OldEndIndex) || (IndexRange.OldStartIndex == -1 && IndexRange.OldEndIndex == 0))
	{
		int32 Outer = FMath::Min(IndexRange.EndIndex + OverCountRowNum, ItemCount - 1);
		int32 Inner = FMath::Max(IndexRange.StartIndex - OverCountRowNum, 0);
		Inner = SlotCollection->LastDataIndex(Inner);

		for (auto i = Inner; i <= Outer; ++i)
		{
			TSharedRef<SWidget> DataWidget = SNullWidget::NullWidget;
			GenerateDataWidget(i, DataWidget);
			auto& NewSlot = SlotCollection->AppendSlot(i)[DataWidget];
			if (NeedPrepassItem() && !CachedWidgets[i] && DataWidget != SNullWidget::NullWidget)
			{
				UpdateSlotedAxis(&NewSlot, true);
			}

			OnSetData(i, DataWidget);
			bItemChanged = true;
		}
	}
	IndexRange.StoreRange();
	UpdateScrollState();

	return bItemChanged;
}

ImVec2 SImSlateVirtualList::GetVisibleSize(bool bEnsureExist) const
{
	ImVec2 LocalSize = (FVector2D)CachedGeometry.GetLocalSize();
	ImEnsureMsgf(!bEnsureExist || LocalSize.HasValidSize(), TEXT("too early to get size"));
	return LocalSize;
}

float SImSlateVirtualList::GetVisibleAxis(bool bEnsureExist) const
{
	auto ListAxis = GetOrientationAxis(GetVisibleSize(bEnsureExist));
	auto ClampListAxis = (ListAxis <= 0.f) ? GetOrientationAxis(GetDesiredSize()) : ListAxis;
	ClampListAxis = (ClampListAxis > 0.f) ? ClampListAxis : 22.f;
	return OverrideAxis.Get(ClampListAxis);
}

float SImSlateVirtualList::GetVisibleOrth(bool bEnsureExist) const
{
	auto WidgetOrth = GetOrientationOrth(GetVisibleSize(bEnsureExist));
	auto ClampWidgetOrth = (WidgetOrth <= 0.f) ? GetOrientationOrth(GetDesiredSize()) : WidgetOrth;
	ClampWidgetOrth = (ClampWidgetOrth > 0.f) ? ClampWidgetOrth : 22.f;
	return WidgetOrth;
}

void SImSlateVirtualList::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	if (!DataBinding)
		return;

#if WITH_EDITOR
	if (SlotCollection->GetChildren().Num() == 0 || (SlotCollection->GetChildren().Num() > 0 && SlotCollection->At(0).DataIndex >= GetDataCount()))
	{
		return;
	}
	WidgetIndexes.Empty();
#endif
	const auto ColLimit = ImEnsure(HasValidCol()) ? GetTotalCol() : 1;
	const auto ScrollBarSize = GS_ACCESS_PROTECT(ScrollBar, SScrollBar, ThicknessSpacer)->ThicknessSpacer->GetSize();
	const auto PanelSize = AllottedGeometry.GetLocalSize();
	const auto PanelOrth = GetOrientationOrth(PanelSize) - (ColLimit > 1 ? GetOrientationOrth(ScrollBarSize) : 0.f);
	const auto PanelAxis = GetOrientationAxis(PanelSize);

	if (!GetCachedGeometry().GetLocalSize().IsZero() && GetCachedGeometry().GetLocalSize() != AllottedGeometry.GetLocalSize())
	{
		MutableThis()->InnerScrollToPos(ActualVirtualPos, PanelAxis, false);
	}

	// Arrange
	if (SlotCollection->GetChildren().Num() > 0)
	{
		if (ScrollBar->ShouldBeVisible() != EVisibility::Collapsed)
		{
			ArrangedChildren.AddWidget(ScrollBar->GetVisibility(),
									   AllottedGeometry.MakeChild(ScrollBar.ToSharedRef(), GetOrientationOrthVec(PanelSize) - GetOrientationOrthVec(ScrollBarSize), GetOrientationOrthVec(ScrollBarSize) + GetOrientationOrthVec(PanelSize)));
		}
		// Scroll
		float ArrangingOffset = GetDataOffset(SlotCollection->At(0).DataIndex);

		struct FStretchInfo
		{
			float ColLeft = 0.f;
			float ColRight = 0.f;
			TArray<const FVirtualListSlot*> Slots;
		};

		struct FRowInfo
		{
			TArray<FStretchInfo, TInlineAllocator<4>> ColumnInfo = {{}};
			float CurColLeft = 0.f;
			float CurColRight = 0.f;
			float RowTop = 0.f;
			float RowMaxAxis = 0.f;
			FStretchInfo& AddColumn(float InColRight)
			{
				if (ColumnInfo.Num() > 0)
				{
					Column().ColLeft = CurColLeft;
					Column().ColRight = CurColRight;
					CurColLeft = CurColRight;
				}
				else
				{
					CurColLeft = 0.f;
				}
				CurColRight = InColRight;

				FStretchInfo& Ref = ColumnInfo.Emplace_GetRef(FStretchInfo());
				Ref.ColLeft = CurColLeft;
				Ref.ColRight = CurColRight;
				return Ref;
			}
			FStretchInfo& Column() { return ColumnInfo.Last(); }
		};

		FRowInfo CurRowInfo;
		const auto ColOrth = PanelOrth / ColLimit;
		auto ArrangeCurrentRow = [&] /* return should continue next */ {
			ON_SCOPE_EXIT
			{
				ArrangingOffset += CurRowInfo.RowMaxAxis;
			};

			if (!this->bAllowOverBoundDraw)
			{
				if (ArrangingOffset + CurRowInfo.RowMaxAxis < ActualVirtualPos)
					return true;

				if (ArrangingOffset >= ActualVirtualPos + PanelAxis)
					return false;
			}
			for (auto i = 0; i < CurRowInfo.ColumnInfo.Num(); ++i)
			{
				const auto& ColInfo = CurRowInfo.ColumnInfo[i];
				float OffsetInCol = 0.f;
				for (auto ChildSlot : ColInfo.Slots)
				{
					const auto& CurChild = *ChildSlot;

					EVisibility ChildVisibility = CurChild.GetWidget()->GetVisibility();
					ChildVisibility = (ChildVisibility == EVisibility::Collapsed) ? EVisibility::Hidden : ChildVisibility;

					// const ImVec2 ChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();
					ImVec2 SlotSize = ImVec2(ColOrth, GetDataAxis(CurChild.DataIndex));

					// Figure out the size and local position of the child within the slot
					AlignmentArrangeResult XAlignmentResult = AlignChild<Orient_Horizontal>(SlotSize.X, CurChild, ItemPadding);
					AlignmentArrangeResult YAlignmentResult = AlignChild<Orient_Vertical>(SlotSize.Y, CurChild, ItemPadding);

					const ImVec2 LocalPosition = ImVec2(ColInfo.ColLeft + OffsetInCol + XAlignmentResult.Offset, CurRowInfo.RowTop + YAlignmentResult.Offset);
					const ImVec2 LocalSize = ImVec2(XAlignmentResult.Size, YAlignmentResult.Size);

					// Add the information about this child to the output list (ArrangedChildren)
					ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(CurChild.GetWidget(), LocalPosition, LocalSize));

					OffsetInCol += GetOrientationOrth(SlotSize);
#if WITH_EDITOR
					WidgetIndexes.Emplace(CurChild.GetWidget(), CurChild.DataIndex);
#endif
				}
			}
			return true;
		};

		int32 LastChildVisibleIdx = -1;
		int32 ColumnIdx = 0;
		for (int32 ChildIndex = 0; ChildIndex < SlotCollection->GetChildren().Num(); ++ChildIndex)
		{
			const auto& CurChild = SlotCollection->At(ChildIndex);
			const bool bArrangeOnNewRow = (ColLimit <= 1 || !(CurChild.DataIndex % ColLimit));
			if (bArrangeOnNewRow)
				ColumnIdx = 0;

			if (CurChild.GetWidget()->GetVisibility() != EVisibility::Collapsed)
			{
				auto ChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();

				if (bArrangeOnNewRow)
				{
					if (LastChildVisibleIdx >= 0)
					{
						if (!ArrangeCurrentRow())
							return;
					}

					CurRowInfo.RowTop = GetDataOffset(CurChild.DataIndex) - ActualVirtualPos;
					CurRowInfo.RowMaxAxis = 0.f;
					CurRowInfo.CurColLeft = 0.f;
					CurRowInfo.CurColRight = ColOrth + ColumnIdx * ColOrth;
					CurRowInfo.ColumnInfo.Empty();
					CurRowInfo.AddColumn(CurRowInfo.CurColRight);
				}

				CurRowInfo.Column().Slots.Add(&CurChild);
				CurRowInfo.RowMaxAxis = FMath::Max(CurRowInfo.RowMaxAxis, GetDataAxis(CurChild.DataIndex));

				if (LastChildVisibleIdx < 0)
					LastChildVisibleIdx = ChildIndex;
			}

			++ColumnIdx;
		}

		if (LastChildVisibleIdx >= 0)
			ArrangeCurrentRow();
	}
}

int32 SImSlateVirtualList::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
	const
{
	FArrangedChildren ArrangedChildren(EVisibility::All);

	const bool bHasBackground = BackgroundBox && BackgroundBox->GetVisibility() != EVisibility::Collapsed;
	if (bHasBackground)
	{
		const auto PanelSize = AllottedGeometry.GetLocalSize();
		ArrangedChildren.AddWidget(BackgroundBox->GetVisibility(), AllottedGeometry.MakeChild(BackgroundBox.ToSharedRef(), ImVec2(0.f, 0.f), PanelSize));
	}

	this->ArrangeChildren(AllottedGeometry, ArrangedChildren);

	const bool bShouldBeEnabled = ShouldBeEnabled(bParentEnabled);
	int32 ContentLayerId = LayerId + 1;
	int32 MaxLayerId = ContentLayerId;

	if (bHasBackground)
	{
		const FPaintArgs NewArgs = Args.WithNewParent(this);
		const int32 CurWidgetsMaxLayerId = BackgroundBox->Paint(NewArgs, AllottedGeometry, MyCullingRect, OutDrawElements, bSameContentLayerId ? ContentLayerId : MaxLayerId + 1, InWidgetStyle, bShouldBeEnabled);
		MaxLayerId = FMath::Max(MaxLayerId, CurWidgetsMaxLayerId);
	}

	if (ArrangedChildren.Num() > 0)
	{
		// Because we paint multiple children, we must track the maximum layer id that they produced in case one of our parents wants to an overlay for all of its contents.
		const FPaintArgs NewArgs = Args.WithNewParent(this);
		if (!bAllowOverBoundDraw)
		{
			for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
			{
				const FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];
				const int32 CurWidgetsMaxLayerId = CurWidget.Widget->Paint(NewArgs, CurWidget.Geometry, MyCullingRect, OutDrawElements, bSameContentLayerId ? ContentLayerId : MaxLayerId + 1, InWidgetStyle, bShouldBeEnabled);
				MaxLayerId = FMath::Max(MaxLayerId, CurWidgetsMaxLayerId);
#if WITH_EDITOR
				if(auto Find = WidgetIndexes.FindByPredicate([&](auto&Pair){return Pair.Key == CurWidget.Widget;}))
				{
					//Find->Value
				}
#endif
			}
		}
		else
		{
			FSlateRect MyNewCullingRect(AllottedGeometry.GetRenderBoundingRect(CullingBoundsExtension));
			FSlateClippingZone ClippingZone(MyNewCullingRect);
			ClippingZone.SetShouldIntersectParent(true);
			ClippingZone.SetAlwaysClip(true);
			OutDrawElements.PushClip(ClippingZone);
			ON_SCOPE_EXIT
			{
				OutDrawElements.PopClip();
			};

			for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
			{
				const FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];

				if (!IsChildWidgetCulled(MyNewCullingRect, CurWidget))
				{
					const int32 CurWidgetsMaxLayerId = CurWidget.Widget->Paint(NewArgs, CurWidget.Geometry, MyNewCullingRect, OutDrawElements, bSameContentLayerId ? ContentLayerId : MaxLayerId + 1, InWidgetStyle, bShouldBeEnabled);
					MaxLayerId = FMath::Max(MaxLayerId, CurWidgetsMaxLayerId);
				}
			}
		}
	}

	if (bShowSoftwareCursor)
	{
		const FSlateBrush* Brush = FCoreStyle::Get().GetBrush(TEXT("SoftwareCursor_Grab"));
		auto InLocalOffset = SoftwareCursorPosition - (Brush->ImageSize / 2);
		float InLocalScale = 1.f;
		FSlateDrawElement::MakeBox(OutDrawElements, ++MaxLayerId, AllottedGeometry.ToPaintGeometry(Brush->ImageSize, FSlateLayoutTransform(InLocalScale, TransformPoint(InLocalScale, UE::Slate::CastToVector2f(InLocalOffset)))), Brush);
	}

	return MaxLayerId;
}

extern void SetDesiredSize(SWidget* InWidget, const ImVec2& InSize);
void SImSlateVirtualList::CacheDesiredSize(float LayoutScaleMultiplier)
{
	if (ScrollBar->GetVisibility() != EVisibility::Collapsed)
	{
		PrepassInternal(ScrollBar.ToSharedRef(), LayoutScaleMultiplier);
	}

	if (BackgroundBox && BackgroundBox->GetVisibility() != EVisibility::Collapsed)
	{
		PrepassInternal(BackgroundBox.ToSharedRef(), LayoutScaleMultiplier);
	}

	ImVec2 Size = (FVector2D)GetDesiredSize();
	Size = Size.HasValidSize() ? Size : Super::ComputeDesiredSize(LayoutScaleMultiplier);

	ImSlate::SetDesiredSize(this, Size);
}

bool SImSlateVirtualList::IsRightClickScrolling() const
{
	return FSlateApplication::IsInitialized() && AmountScrolledWhileRightMouseDown >= FSlateApplication::Get().GetDragTriggerDistance() && this->ScrollBar->IsNeeded();
}

bool SImSlateVirtualList::CanUseInertialScroll(float ScrollAmount) const
{
	const auto CurrentOverscroll = Overscroll.GetOverscroll(CachedGeometry);

	// We allow sampling for the inertial scroll if we are not in the overscroll region,
	// Or if we are scrolling outwards of the overscroll region
	return CurrentOverscroll == 0.f || FMath::Sign(CurrentOverscroll) != FMath::Sign(ScrollAmount);
}

EAllowOverscroll SImSlateVirtualList::GetAllowOverscroll() const
{
	return AllowOverscroll;
}

void SImSlateVirtualList::SetAllowOverscroll(EAllowOverscroll NewAllowOverscroll)
{
	AllowOverscroll = NewAllowOverscroll;

	if (!IsAllowOverScroll())
	{
		Overscroll.ResetOverscroll();
	}
}

void SImSlateVirtualList::SetAnimateWheelScrolling(bool bInAnimateWheelScrolling)
{
	bAnimateWheelScrolling = bInAnimateWheelScrolling;
}

void SImSlateVirtualList::SetWheelScrollMultiplier(float NewWheelScrollMultiplier)
{
	WheelScrollMultiplier = NewWheelScrollMultiplier;
}

void SImSlateVirtualList::SetScrollWhenFocusChanges(EScrollWhenFocusChanges NewScrollWhenFocusChanges)
{
	ScrollWhenFocusChanges = NewScrollWhenFocusChanges;
}

float SImSlateVirtualList::GetScrollOffset() const
{
	return DesiredScrollOffset;
}

float SImSlateVirtualList::GetViewFraction() const
{
	return FMath::Clamp<float>(GetScrollComponentFromVector(CachedGeometry.GetLocalSize()) > 0 ? DesiredScrollOffset / GetCachedTotalAxis(true) : 1, 0.0f, 1.0f);
}

float SImSlateVirtualList::GetViewOffsetFraction() const
{
	const float ViewFraction = GetViewFraction();
	return FMath::Clamp(GetVisibleAxis() / GetCachedTotalAxis(true), 0.f, 1.f - ViewFraction);
}

float SImSlateVirtualList::GetScrollOffsetOfEnd() const
{
	return FMath::Max(GetCachedTotalAxis(IsShowScrollbar()) - GetVisibleAxis(), 0.0f);
}

void SImSlateVirtualList::SetScrollOffset(float NewScrollOffset)
{
	ScrollToPos(NewScrollOffset);
}

void SImSlateVirtualList::ScrollToStart()
{
	ScrollToPos(0.f);
}

void SImSlateVirtualList::ScrollToEnd()
{
	ScrollToPos(std::numeric_limits<float>::max());
}

void SImSlateVirtualList::BeginInertialScrolling()
{
	if (!UpdateInertialScrollHandle.IsValid())
	{
		bIsScrolling = true;
		bIsScrollingActiveTimerRegistered = true;
		UpdateInertialScrollHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SImSlateVirtualList::UpdateInertialScroll));
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

bool SImSlateVirtualList::InternalScrollDescendantIntoView(const FGeometry& MyGeometry, const TSharedPtr<SWidget>& WidgetToFind, bool InAnimateScroll, EDescendantScrollDestination InDestination, float InScrollPadding)
{
	if (!WidgetToFind.IsValid())
		return false;

	// We need to safely find the one WidgetToFind among our descendants.
	TSet<TSharedRef<SWidget>> WidgetsToFind{WidgetToFind.ToSharedRef()};
	TMap<TSharedRef<SWidget>, FArrangedWidget> Result;
	FindChildGeometries(MyGeometry, WidgetsToFind, Result);
	if (FArrangedWidget* WidgetGeometry = Result.Find(WidgetToFind.ToSharedRef()))
	{
		float ScrollOffset = 0.0f;
		if (InDestination == EDescendantScrollDestination::TopOrLeft)
		{
			// Calculate how much we would need to scroll to bring this to the top/left of the list
			const float WidgetPosition = GetScrollComponentFromVector(MyGeometry.AbsoluteToLocal(WidgetGeometry->Geometry.GetAbsolutePosition()));
			const float MyPosition = InScrollPadding;
			ScrollOffset = WidgetPosition - MyPosition;
		}
		else if (InDestination == EDescendantScrollDestination::BottomOrRight)
		{
			// Calculate how much we would need to scroll to bring this to the bottom/right of the list
			const float WidgetPosition = GetScrollComponentFromVector(MyGeometry.AbsoluteToLocal(WidgetGeometry->Geometry.GetAbsolutePosition() + WidgetGeometry->Geometry.GetAbsoluteSize()) - MyGeometry.GetLocalSize());
			const float MyPosition = InScrollPadding;
			ScrollOffset = WidgetPosition - MyPosition;
		}
		else if (InDestination == EDescendantScrollDestination::Center)
		{
			// Calculate how much we would need to scroll to bring this to the top/left of the list
			const float WidgetPosition = GetScrollComponentFromVector(MyGeometry.AbsoluteToLocal(WidgetGeometry->Geometry.GetAbsolutePosition()) + (WidgetGeometry->Geometry.GetLocalSize() / 2));
			const float MyPosition = GetScrollComponentFromVector(MyGeometry.GetLocalSize() * FVector2f(0.5f, 0.5f));
			ScrollOffset = WidgetPosition - MyPosition;
		}
		else
		{
			const float WidgetStartPosition = GetScrollComponentFromVector(MyGeometry.AbsoluteToLocal(WidgetGeometry->Geometry.GetAbsolutePosition()));
			const float WidgetEndPosition = WidgetStartPosition + GetScrollComponentFromVector(WidgetGeometry->Geometry.GetLocalSize());
			const float ViewStartPosition = InScrollPadding;
			const float ViewEndPosition = GetScrollComponentFromVector(MyGeometry.GetLocalSize() - InScrollPadding);

			const float ViewDelta = (ViewEndPosition - ViewStartPosition);
			const float WidgetDelta = (WidgetEndPosition - WidgetStartPosition);

			if (WidgetStartPosition < ViewStartPosition)
			{
				ScrollOffset = WidgetStartPosition - ViewStartPosition;
			}
			else if (WidgetEndPosition > ViewEndPosition)
			{
				ScrollOffset = (WidgetEndPosition - ViewDelta) - ViewStartPosition;
			}
		}

		if (ScrollOffset != 0.0f)
		{
			DesiredScrollOffset = ActualVirtualPos;
			ScrollBy(MyGeometry, ScrollOffset, EAllowOverscroll::No, InAnimateScroll);
		}

		return true;
	}

	return false;
}

TSharedPtr<SWidget> SImSlateVirtualList::GetKeyboardFocusableWidget(TSharedPtr<SWidget> InWidget)
{
	if (EVisibility::DoesVisibilityPassFilter(InWidget->GetVisibility(), EVisibility::Visible))
	{
		if (InWidget->SupportsKeyboardFocus())
		{
			return InWidget;
		}
		else
		{
			FChildren* Children = InWidget->GetChildren();
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				TSharedPtr<SWidget> ChildWidget = Children->GetChildAt(i);
				TSharedPtr<SWidget> FoucusableWidget = GetKeyboardFocusableWidget(ChildWidget);
				if (FoucusableWidget.IsValid() && EVisibility::DoesVisibilityPassFilter(FoucusableWidget->GetVisibility(), EVisibility::Visible))
				{
					return FoucusableWidget;
				}
			}
		}
	}
	return nullptr;
}

void SImSlateVirtualList::EndInertialScrolling()
{
	bIsScrolling = false;
	bIsScrollingActiveTimerRegistered = false;
	Invalidate(EInvalidateWidget::LayoutAndVolatility);
	if (UpdateInertialScrollHandle.IsValid())
	{
		UnRegisterActiveTimer(UpdateInertialScrollHandle.ToSharedRef());
		UpdateInertialScrollHandle.Reset();
	}

	// Zero the scroll velocity so the panel stops immediately on mouse down, even if the user does not drag
	InertialScrollManager.ClearScrollVelocity();
}

void SImSlateVirtualList::ExternalPanMove(FVector2D PressPos, FVector2D CurPos)
{
	// Driven by FImSlatePanelScrollProcessor when a child holds capture. Mirrors the list's own
	// OnMouseMove scroll path (GetOrientationAxis → ScrollBy + AddScrollSample) so vertical AND
	// horizontal lists both work, and inertia is sampled exactly like a normal drag.
	if (!IsPanEnabled() || !CanScroll())
		return;

	if (!bExternalPanActive)
	{
		EndInertialScrolling();              // a fresh drag cancels any leftover coast
		bExternalPanActive = true;
		ExternalPanLastScreenPos = CurPos;   // origin; first frame applies no delta (no jump)
		return;
	}

	const FGeometry& Geo = GetCachedGeometry();
	const float Scale = Geo.Scale > 0.f ? Geo.Scale : 1.f;
	const FVector2D ScreenDelta = CurPos - ExternalPanLastScreenPos;
	ExternalPanLastScreenPos = CurPos;

	// Project onto the list's scroll axis (Y for vertical, X for horizontal).
	const float AxisScreen = GetOrientationAxis(ScreenDelta);
	if (AxisScreen != 0.f)
	{
		ScrollBy(Geo, -AxisScreen / Scale, EAllowOverscroll::Yes, false);
		InertialScrollManager.AddScrollSample(-AxisScreen, FSlateApplication::Get().GetCurrentTime());
	}
}

void SImSlateVirtualList::ExternalPanEnd(FVector2D CurPos)
{
	const bool bWasPanning = bExternalPanActive;
	bExternalPanActive = false;
	if (bWasPanning)
		BeginInertialScrolling();  // coast with the flick velocity sampled during the forwarded drag
}

void SImSlateVirtualList::ScrollDescendantIntoView(const TSharedPtr<SWidget>& WidgetToScrollIntoView, bool InAnimateScroll, EDescendantScrollDestination InDestination, float InScrollPadding)
{
	ScrollIntoViewRequest = [this, WidgetToScrollIntoView, InAnimateScroll, InDestination, InScrollPadding](const FGeometry& AllottedGeometry) {
		InternalScrollDescendantIntoView(AllottedGeometry, WidgetToScrollIntoView, InAnimateScroll, InDestination, InScrollPadding);
	};

	BeginInertialScrolling();
}

EOrientation SImSlateVirtualList::GetOrientation()
{
	return Orientation;
}

void SImSlateVirtualList::SetOrientation(EOrientation InOrientation)
{
	if (Orientation != InOrientation)
	{
		Orientation = InOrientation;
		if (!bScrollBarIsExternal)
		{
			ScrollBar = ConstructScrollBar();
		}
	}
}

void SImSlateVirtualList::SetScrollBarVisibility(EVisibility InVisibility)
{
	ScrollBar->SetUserVisibility(InVisibility);
}

void SImSlateVirtualList::SetScrollBarAlwaysVisible(bool InAlwaysVisible)
{
	ScrollBar->SetScrollBarAlwaysVisible(InAlwaysVisible);
}

void SImSlateVirtualList::SetScrollBarTrackAlwaysVisible(bool InAlwaysVisible)
{
	ScrollBar->SetScrollBarTrackAlwaysVisible(InAlwaysVisible);
}

void SImSlateVirtualList::SetScrollBarThickness(UE::Slate::FDeprecateVector2DParameter InThickness)
{
	ScrollBar->SetThickness(InThickness);
}

void SImSlateVirtualList::SetScrollBarPadding(const FMargin& InPadding)
{
	ScrollBarSlotPadding = InPadding;

	if (Orientation == Orient_Vertical)
	{
		if (VerticalScrollBarSlot)
		{
			VerticalScrollBarSlot->SetPadding(ScrollBarSlotPadding);
		}
	}
	else
	{
		if (HorizontalScrollBarSlot)
		{
			HorizontalScrollBarSlot->SetPadding(ScrollBarSlotPadding);
		}
	}
}

void SImSlateVirtualList::SetScrollBarRightClickDragAllowed(bool bIsAllowed)
{
	bAllowsRightClickDragScrolling = bIsAllowed;
}

void SImSlateVirtualList::SetScrollBarStyle(const FScrollBarStyle* InBarStyle)
{
	if (InBarStyle != ScrollBarStyle)
	{
		ScrollBarStyle = InBarStyle;
		if (!bScrollBarIsExternal && ScrollBar.IsValid())
		{
			ScrollBar->SetStyle(ScrollBarStyle);
		}
	}
}

void SImSlateVirtualList::InvalidateScrollBarStyle()
{
	if (ScrollBar.IsValid())
	{
		ScrollBar->InvalidateStyle();
	}
}

TSharedPtr<SScrollBar> SImSlateVirtualList::ConstructScrollBar()
{
	auto Ret = SNew(SScrollBar)
				.Orientation(Orientation)
				.Padding(0.0f)
				.OnUserScrolled(this, &SImSlateVirtualList::ScrollBar_OnUserScrolled);
	if (ScrollBarStyle)
	{
		Ret->SetStyle(ScrollBarStyle);
	}
	return Ret;
}

bool SImSlateVirtualList::ScrollBy(const FGeometry& AllottedGeometry, float LocalScrollAmount, EAllowOverscroll Overscrolling, bool InAnimateScroll)
{
	Invalidate(EInvalidateWidget::LayoutAndVolatility);

	bAnimateScroll = InAnimateScroll;

	const float PreviousScrollOffset = DesiredScrollOffset;

	if (LocalScrollAmount != 0)
	{
		const float ScrollMin = 0.0f;
		const float ScrollMax = GetScrollOffsetOfEnd();

		if (IsAllowOverScroll() && Overscrolling == EAllowOverscroll::Yes &&  //
			Overscroll.ShouldApplyOverscroll(FMath::IsNearlyEqual(DesiredScrollOffset, 0.f), FMath::IsNearlyEqual(DesiredScrollOffset, ScrollMax), LocalScrollAmount))
		{
			Overscroll.ScrollBy(AllottedGeometry, LocalScrollAmount);
			// UE_LOG(LogSlate, Log, TEXT("OverScrollby %f"), LocalScrollAmount);
		}
		else
		{
			DesiredScrollOffset = FMath::Clamp(DesiredScrollOffset + LocalScrollAmount, ScrollMin, ScrollMax);
			// UE_LOG(LogSlate, Log, TEXT("NormalScrollby %f %f %f"), LocalScrollAmount, DesiredScrollOffset, ScrollMax);
		}
	}

	OnUserScrolled.ExecuteIfBound(DesiredScrollOffset);

	return ConsumeMouseWheel == EConsumeMouseWheel::Always || DesiredScrollOffset != PreviousScrollOffset;
}

void SImSlateVirtualList::ScrollBar_OnUserScrolled(float InScrollOffsetFraction)
{
	bAnimateScroll = false;
	LeftAnimateDuration = 0.f;
	// Clamp to max scroll offset
	DesiredScrollOffset = FMath::Min(InScrollOffsetFraction * GetVisibleAxis(), GetScrollOffsetOfEnd());
	OnUserScrolled.ExecuteIfBound(DesiredScrollOffset);
}

void SImSlateVirtualList::ScrollBar_OnScrollBarVisibilityChanged(EVisibility NewVisibility)
{
	OnScrollBarVisibilityChanged.ExecuteIfBound(NewVisibility);
}

const float ShadowFadeDistance = 32.0f;

FSlateColor SImSlateVirtualList::GetStartShadowOpacity() const
{
	// The shadow should only be visible when the user needs a hint that they can scroll up.
	const float ShadowOpacity = FMath::Clamp(ActualVirtualPos / ShadowFadeDistance, 0.0f, 1.0f);

	return FLinearColor(1.0f, 1.0f, 1.0f, ShadowOpacity);
}

FSlateColor SImSlateVirtualList::GetEndShadowOpacity() const
{
	// The shadow should only be visible when the user needs a hint that they can scroll down.
	const float ShadowOpacity = (ScrollBar->DistanceFromBottom() * GetVisibleAxis() / ShadowFadeDistance);

	return FLinearColor(1.0f, 1.0f, 1.0f, ShadowOpacity);
}

EActiveTimerReturnType SImSlateVirtualList::UpdateInertialScroll(double InCurrentTime, float InDeltaTime)
{
	bool bKeepTicking = bIsScrolling;

	if (bIsScrolling)
	{
		InertialScrollManager.UpdateScrollVelocity(InDeltaTime);
		const float ScrollVelocityLocal = InertialScrollManager.GetScrollVelocity() / CachedGeometry.Scale;

		if (ScrollVelocityLocal != 0.f)
		{
			if (CanUseInertialScroll(ScrollVelocityLocal))
			{
				bKeepTicking = true;
				ScrollBy(CachedGeometry, ScrollVelocityLocal * InDeltaTime, AllowOverscroll, false);
			}
			else
			{
				InertialScrollManager.ClearScrollVelocity();
			}
		}
	}

	if (IsAllowOverScroll())
	{
		// If we are currently in overscroll, the list will need refreshing.
		// Do this before UpdateOverscroll, as that could cause GetOverscroll() to be 0
		if (Overscroll.GetOverscroll(CachedGeometry) != 0.0f)
		{
			bKeepTicking = true;
		}

		Overscroll.UpdateOverscroll(InDeltaTime);
	}

	TickScrollDelta = 0.f;

	if (!bKeepTicking)
	{
		bIsScrolling = false;
		bIsScrollingActiveTimerRegistered = false;
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
		UpdateInertialScrollHandle.Reset();
	}

	return bKeepTicking ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
}

bool SImSlateVirtualList::IsUserScrolling() const
{
	bool bUserScroll = this->ScrollBar->IsNeeded() && this->ScrollBar->IsScrolling();
	return bUserScroll || IsRightClickScrolling();
}

FVector2D SImSlateVirtualList::GetScrollDistance()
{
	return FVector2D(0, ScrollBar->DistanceFromTop());
}

FVector2D SImSlateVirtualList::GetScrollDistanceRemaining()
{
	return FVector2D(0, ScrollBar->DistanceFromBottom());
}

TSharedRef<SWidget> SImSlateVirtualList::GetScrollWidget()
{
	return SharedThis(this);
}

void SImSlateVirtualList::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsTouchEvent())
	{
		if (!bFingerOwningTouchInteraction.IsSet())
		{
			// If we currently do not have touch capture, allow this widget to begin scrolling on pointer enter events
			// if it comes from a child widget
			if (MyGeometry.IsUnderLocation(MouseEvent.GetLastScreenSpacePosition()))
			{
				bFingerOwningTouchInteraction = MouseEvent.GetPointerIndex();
			}
		}
	}
}

void SImSlateVirtualList::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (HasMouseCapture() == false)
	{
		// No longer scrolling (unless we have mouse capture)
		if (AmountScrolledWhileRightMouseDown != 0)
		{
			AmountScrolledWhileRightMouseDown = 0;
			Invalidate(EInvalidateWidget::Layout);
		}

		if (MouseEvent.IsTouchEvent())
		{
			bFingerOwningTouchInteraction.Reset();
		}
	}
}

FReply SImSlateVirtualList::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const bool bHighPrecision = true;
	float ScrollByAmountScreen = bHighPrecision ? GetOrientationAxis(MouseEvent.GetScreenSpacePosition() - LastMouseMovedScreenSpacePosition) : GetScrollComponentFromVector(MouseEvent.GetCursorDelta());
	LastMouseMovedScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
	const float ScrollByAmountLocal = ScrollByAmountScreen / MyGeometry.Scale;

	if (MouseEvent.IsTouchEvent())
	{
		FReply Reply = FReply::Unhandled();

		if (!bTouchPanningCapture)
		{
			if (bFingerOwningTouchInteraction.IsSet() && MouseEvent.IsTouchEvent() && !HasMouseCapture())
			{
				PendingScrollTriggerAmount += ScrollByAmountScreen;

				if (FMath::Abs(PendingScrollTriggerAmount) > FSlateApplication::Get().GetDragTriggerDistance())
				{
					bTouchPanningCapture = true;
					ScrollBar->BeginScrolling();

					// The user has moved the list some amount; they are probably
					// trying to scroll. From now on, the list assumes the user is scrolling
					// until they lift their finger.
					Reply = FReply::Handled().CaptureMouse(AsShared());
				}
				else
				{
					Reply = FReply::Handled();
				}
			}
		}
		else
		{
			if (bFingerOwningTouchInteraction.IsSet() && HasMouseCaptureByUser(MouseEvent.GetUserIndex(), MouseEvent.GetPointerIndex()))
			{
				LastScrollTime = FSlateApplication::Get().GetCurrentTime();
				InertialScrollManager.AddScrollSample(-ScrollByAmountScreen, FSlateApplication::Get().GetCurrentTime());
				ScrollBy(MyGeometry, -ScrollByAmountLocal, EAllowOverscroll::Yes, false);

				Reply = FReply::Handled();
			}
		}

		return Reply;
	}
	else
	{
		if (MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton) && bAllowsRightClickDragScrolling)
		{
			// If scrolling with the right mouse button, we need to remember how much we scrolled.
			// If we did not scroll at all, we will bring up the context menu when the mouse is released.
			AmountScrolledWhileRightMouseDown += FMath::Abs(ScrollByAmountScreen);

			// Has the mouse moved far enough with the right mouse button held down to start capturing
			// the mouse and dragging the view?
			if (IsRightClickScrolling())
			{
				InertialScrollManager.AddScrollSample(-ScrollByAmountScreen, FPlatformTime::Seconds());
				const bool bDidScroll = ScrollBy(MyGeometry, -ScrollByAmountLocal, AllowOverscroll, false);

				FReply Reply = FReply::Handled();

				// Capture the mouse if we need to
				if (HasMouseCapture() == false)
				{
					Reply.CaptureMouse(AsShared()).UseHighPrecisionMouseMovement(AsShared());
					SoftwareCursorPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
					bShowSoftwareCursor = true;
				}

				// Check if the mouse has moved.
				if (bDidScroll)
				{
					SetScrollComponentOnVector(SoftwareCursorPosition, GetScrollComponentFromVector(SoftwareCursorPosition) + ScrollByAmountLocal);
				}

				return Reply;
			}
		}
	}

	return FReply::Unhandled();
}

FReply SImSlateVirtualList::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ((ScrollBar->IsNeeded() && ConsumeMouseWheel != EConsumeMouseWheel::Never) || ConsumeMouseWheel == EConsumeMouseWheel::Always)
	{
		// Make sure scroll velocity is cleared so it doesn't fight with the mouse wheel input
		InertialScrollManager.ClearScrollVelocity();

		const bool bScrollWasHandled = ScrollBy(MyGeometry, -MouseEvent.GetWheelDelta() * GetGlobalScrollAmount() * WheelScrollMultiplier, EAllowOverscroll::No, bAnimateWheelScrolling);

		if (bScrollWasHandled && !bIsScrollingActiveTimerRegistered)
		{
			// Register the active timer to handle the inertial scrolling
			CachedGeometry = MyGeometry;
			BeginInertialScrolling();
		}

		return bScrollWasHandled ? FReply::Handled() : FReply::Unhandled();
	}
	else
	{
		return FReply::Unhandled();
	}
}

FReply SImSlateVirtualList::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsTouchEvent() && !bFingerOwningTouchInteraction.IsSet())
	{
		// Clear any inertia
		InertialScrollManager.ClearScrollVelocity();
		// We have started a new interaction; track how far the user has moved since they put their finger down.
		AmountScrolledWhileRightMouseDown = 0;
		PendingScrollTriggerAmount = 0;
		// Someone put their finger down in this list, so they probably want to drag the list.
		bFingerOwningTouchInteraction = MouseEvent.GetPointerIndex();

		LastMouseMovedScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
		PressedScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
		PressedVirtualPos = ActualVirtualPos;

		Invalidate(EInvalidateWidget::Layout);
	}
	return FReply::Unhandled();
}

FCursorReply SImSlateVirtualList::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (IsRightClickScrolling())
	{
		// We hide the native cursor as we'll be drawing the software EMouseCursor::GrabHandClosed cursor
		return FCursorReply::Cursor(EMouseCursor::None);
	}
	else
	{
		return FCursorReply::Unhandled();
	}
}

FReply SImSlateVirtualList::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bFingerOwningTouchInteraction.IsSet())
	{
		EndInertialScrolling();
	}

	if (MouseEvent.IsTouchEvent())
	{
		return FReply::Handled();
	}
	else
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && ScrollBar->IsNeeded() && bAllowsRightClickDragScrolling)
		{
			AmountScrolledWhileRightMouseDown = 0;
			OnRightMouseButtonDown(MouseEvent);
			Invalidate(EInvalidateWidget::Layout);

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FReply SImSlateVirtualList::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& MouseEvent)
{
	if (this->HasMouseCapture())
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SImSlateVirtualList::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && bAllowsRightClickDragScrolling)
	{
		OnRightMouseButtonUp(MouseEvent);
		if (!bIsScrollingActiveTimerRegistered && IsRightClickScrolling())
		{
			// Register the active timer to handle the inertial scrolling
			CachedGeometry = MyGeometry;
			BeginInertialScrolling();
		}

		AmountScrolledWhileRightMouseDown = 0;

		Invalidate(EInvalidateWidget::Layout);

		FReply Reply = FReply::Handled().ReleaseMouseCapture();
		bShowSoftwareCursor = false;

		if (this->HasMouseCapture())
		{
			FSlateRect PanelScreenSpaceRect = MyGeometry.GetLayoutBoundingRect();
			FVector2f CursorPosition = MyGeometry.LocalToAbsolute(SoftwareCursorPosition);

			FIntPoint BestPositionInPanel(FMath::RoundToInt(FMath::Clamp(CursorPosition.X, PanelScreenSpaceRect.Left, PanelScreenSpaceRect.Right)),
										  FMath::RoundToInt(FMath::Clamp(CursorPosition.Y, PanelScreenSpaceRect.Top, PanelScreenSpaceRect.Bottom)));

			Reply.SetMousePos(BestPositionInPanel);
		}

		return Reply;
	}
	return FReply::Unhandled();
}

void SImSlateVirtualList::OnRightMouseButtonDown(const FPointerEvent& MouseEvent)
{
}

void SImSlateVirtualList::OnRightMouseButtonUp(const FPointerEvent& MouseEvent)
{
	const FVector2D& SummonLocation = MouseEvent.GetScreenSpacePosition();
	const bool bShouldOpenContextMenu = !IsRightClickScrolling();
	const bool bContextMenuOpeningBound = OnContextMenuOpening.IsBound();

	if (bShouldOpenContextMenu && bContextMenuOpeningBound)
	{
		if (TSharedPtr<SWidget> MenuContent = OnContextMenuOpening.Execute())
		{
			bShowSoftwareCursor = false;
			FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
			FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuContent.ToSharedRef(), SummonLocation, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		}
	}

	AmountScrolledWhileRightMouseDown = 0;
}

FReply SImSlateVirtualList::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	FReply Reply = FReply::Unhandled();
	if (bEnableTouchEvent)
	{
		PressedScreenSpacePosition = TouchEvent.GetScreenSpacePosition();
		PressedVirtualPos = ActualVirtualPos;
		bStartedTouchInteraction = true;
		Reply = FReply::Handled().CaptureMouse(AsShared()).UseHighPrecisionMouseMovement(AsShared()).PreventThrottling();
	}
	return Reply;
}

FReply SImSlateVirtualList::OnTouchFirstMove(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	FReply Reply = FReply::Unhandled();
	if (bEnableTouchEvent)
	{
		//if (FSlateApplication::Get().HasTraveledFarEnoughToTriggerDrag(TouchEvent, PressedScreenSpacePosition))
		auto DragDist = (PressedScreenSpacePosition - TouchEvent.GetScreenSpacePosition());
		if (GetOrientationAxis(DragDist.GetAbs()) >= FSlateApplication::Get().GetDragTriggerDistance())
		{
			bStartedTouchInteraction = true;
			float TargetVirtualPos = PressedVirtualPos + GetOrientationAxis(DragDist) / MyGeometry.Scale;
			InnerScrollToPos(FMath::Max(0.f, TargetVirtualPos), GetOrientationAxis(MyGeometry.GetLocalSize()));
			ScrollBar->BeginScrolling();
			Reply = FReply::Handled().CaptureMouse(AsShared()).UseHighPrecisionMouseMovement(AsShared()).PreventThrottling();
		}
	}
	return Reply;
}

FReply SImSlateVirtualList::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	FReply Reply = FReply::Unhandled();
	if (bEnableTouchEvent)
	{
		auto DragDist = (PressedScreenSpacePosition - TouchEvent.GetScreenSpacePosition());
		//if (FSlateApplication::Get().HasTraveledFarEnoughToTriggerDrag(TouchEvent, PressedScreenSpacePosition))
		if (!bStartedTouchInteraction && (GetOrientationAxis(DragDist.GetAbs()) >= FSlateApplication::Get().GetDragTriggerDistance()))
		{
			bStartedTouchInteraction = true;
			ScrollBar->BeginScrolling();
			Reply = FReply::Handled().CaptureMouse(AsShared()).UseHighPrecisionMouseMovement(AsShared()).PreventThrottling();
		}

		if (bStartedTouchInteraction)
		{
			float TargetVirtualPos = PressedVirtualPos + GetOrientationAxis(DragDist) / MyGeometry.Scale;
			InnerScrollToPos(FMath::Max(0.f, TargetVirtualPos), GetOrientationAxis(MyGeometry.GetLocalSize()));
		}
	}
	return Reply;
}

FReply SImSlateVirtualList::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	CachedGeometry = MyGeometry;

	bStartedTouchInteraction = false;
	if (HasMouseCaptureByUser(TouchEvent.GetUserIndex(), TouchEvent.GetPointerIndex()))
	{
		ScrollBar->EndScrolling();
		Invalidate(EInvalidateWidget::Layout);

		BeginInertialScrolling();

		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

FReply SImSlateVirtualList::OnTouchForceChanged(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	return Super::OnTouchForceChanged(MyGeometry, TouchEvent);
}

void SImSlateVirtualList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (!DataBinding)
		return;

	IndexRange.AppendChanged(CachedGeometry != AllottedGeometry);

	CachedGeometry = AllottedGeometry;

	if (bTouchPanningCapture && (FSlateApplication::Get().GetCurrentTime() - LastScrollTime) > 0.10)
	{
		InertialScrollManager.ClearScrollVelocity();
	}

	// If we needed a widget to be scrolled into view, make that happen.
	if (ScrollIntoViewRequest)
	{
		TFunction<void(const FGeometry&)> OneShot;
		Swap(OneShot, ScrollIntoViewRequest);
		OneShot(AllottedGeometry);
	}
	if (bScrollToEnd)
	{
		DesiredScrollOffset = GetScrollOffsetOfEnd();
		bScrollToEnd = false;
	}

	// Update the scrollbar with the clamped version of the offset
	float TargetVirtualOffset = DesiredScrollOffset;
	if (bAnimateScroll)
	{
		if (LeftAnimateDuration > 0.f)
		{
			LeftAnimateDuration -= InDeltaTime;

			if (LeftAnimateDuration <= 0.f)
			{
				bAnimateScroll = false;
				TargetVirtualOffset = DesiredScrollOffset;
			}
			else
			{
				float LinearAlpha = 1.f - FMath::Clamp(LeftAnimateDuration / AnimateDuration, 0.0f, 1.0f);
				// InterpEaseInOut, InterpEaseOut, InterpEaseIn
				float EaseAlpha = FMath::InterpEaseOut(0.0f, 1.0f, LinearAlpha, 2.5f);
				TargetVirtualOffset = FMath::Lerp(AnimationStartPos, DesiredScrollOffset, EaseAlpha);
			}
			//UE_LOG(LogSlate, Log, TEXT("Animation Scrolled pos:%f, duration left:%f"), TargetVirtualOffset, LeftAnimateDuration);
		}
		else
		{
			TargetVirtualOffset = FMath::FInterpTo(ActualVirtualPos, DesiredScrollOffset, InDeltaTime, 15.f);
		}
	}

	float OverscrollAmount = IsAllowOverScroll() ? Overscroll.GetOverscroll(CachedGeometry) : 0.f;
	if (OverscrollAmount != 0.f)
	{
		// UE_LOG(LogSlate, Log, TEXT("OverscrollAmount %f"), OverscrollAmount);
		TargetVirtualOffset += OverscrollAmount;
	}

	const bool bWasScrolling = bIsScrolling;
	bIsScrolling = !FMath::IsNearlyEqual(TargetVirtualOffset, ActualVirtualPos, 0.001f);

	if (bWasScrolling && !bIsScrolling)
	{
		Invalidate(EInvalidateWidget::Layout);
	}

	UpdateScrollState();
	if (!ScrollBar->IsNeeded() && !IsAllowOverScroll())
	{
		// We cannot scroll, so ensure that there is no offset.
		TargetVirtualOffset = 0.0f;
	}
	if (TargetVirtualOffset != ActualVirtualPos)
	{
		// UE_LOG(LogSlate, Log, TEXT("Tick Pos %f <- %f"), TargetVirtualOffset, VirtualPos);
	}
	auto LocalSize = AllottedGeometry.GetLocalSize();
	auto LocalOrth = GetOrientationOrth(LocalSize);
	auto ListAxis = GetVisibleAxis();
	if (!HasValidCol() || IndexRange.HasChanged())
	{
		TotalCol = GetTileOrth() > 0.f ? FMath::Max(1, int32(LocalOrth / GetTileOrth())) : 1;
		if (UpdateRange(ListAxis, TargetVirtualOffset, OverscrollAmount))
			GenerateWidgetIfNeeded();
	}
	else if (UpdateRange(ListAxis, TargetVirtualOffset, OverscrollAmount))
	{
		auto OldSize = GetCachedGeometry().GetLocalSize();
		if (OldSize != LocalSize)
		{
			if (GetTileOrth() <= 0.f || GetTotalCol() == int32(LocalOrth / GetTileOrth()))
			{
				if (GetOrientationAxis(OldSize) != GetOrientationAxis(LocalSize) && ImEnsure(UpdateRange(GetOrientationAxis(LocalSize), ActualVirtualPos, OverscrollAmount, true)))
					GenerateWidgetIfNeeded();
			}
			else
			{
				TotalCol = FMath::Max(1, int32(LocalOrth / GetTileOrth()));
				ResetAxises();
				if (UpdateRange(ListAxis, TargetVirtualOffset, OverscrollAmount))
					GenerateWidgetIfNeeded();
			}
		}
	}

	// Super::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

bool SImSlateVirtualList::ComputeVolatility() const
{
	return bIsScrolling || IsRightClickScrolling();
}

FReply SImSlateVirtualList::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (OnItemKeyEvent.IsBound() && OnItemKeyEvent.Execute(this, MyGeometry, InKeyEvent))
	{
		return FReply::Handled().PreventThrottling();
	}
	else if (!IsHeterogeneous())
	{
		if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::Home)
		{
			ScrollToItem(0);
			return FReply::Handled().PreventThrottling();
		}
		else if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::End)
		{
			ScrollToItem(GetDataCount() - 1);
			return FReply::Handled().PreventThrottling();
		}
	}
	return Super::OnKeyDown(MyGeometry, InKeyEvent).PreventThrottling();
}

FReply SImSlateVirtualList::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (OnItemKeyEvent.IsBound() && OnItemKeyEvent.Execute(this, MyGeometry, InKeyEvent))
	{
		return FReply::Handled().PreventThrottling();
	}
	return Super::OnKeyUp(MyGeometry, InKeyEvent).PreventThrottling();
}

void SImSlateVirtualList::OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	if (ScrollWhenFocusChanges != EScrollWhenFocusChanges::NoScroll)
	{
		if (NewWidgetPath.IsValid() && NewWidgetPath.ContainsWidget(this))
		{
			ScrollDescendantIntoView(NewWidgetPath.GetLastWidget(), ScrollWhenFocusChanges == EScrollWhenFocusChanges::AnimatedScroll ? true : false, NavigationDestination, NavigationScrollPadding);
		}
	}
}

void SImSlateVirtualList::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
	AmountScrolledWhileRightMouseDown = 0;
	PendingScrollTriggerAmount = 0;
	bFingerOwningTouchInteraction.Reset();
	bTouchPanningCapture = false;
}

FNavigationReply SImSlateVirtualList::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	TSharedPtr<SWidget> FocusedChild;
	int32 FocusedChildIndex = -1;
	int32 FocusedChildDirection = 0;

	// Find the child with focus currently so that we can find the next logical child we're going to move to.
	auto& Children = SlotCollection->GetChildren();
	for (int32 SlotIndex = 0; SlotIndex < Children.Num(); ++SlotIndex)
	{
		if (Children[SlotIndex].GetWidget()->HasUserFocus(InNavigationEvent.GetUserIndex()).IsSet() || Children[SlotIndex].GetWidget()->HasUserFocusedDescendants(InNavigationEvent.GetUserIndex()))
		{
			FocusedChild = Children[SlotIndex].GetWidget();
			FocusedChildIndex = SlotIndex;
			break;
		}
	}

	if (FocusedChild.IsValid())
	{
		if (Orientation == Orient_Vertical)
		{
			switch (InNavigationEvent.GetNavigationType())
			{
				case EUINavigation::Up:
					FocusedChildDirection = -1;
					break;
				case EUINavigation::Down:
					FocusedChildDirection = 1;
					break;
				default:
					// If we don't handle this direction in our current orientation we can
					// just allow the behavior of the boundary rule take over.
					return SCompoundWidget::OnNavigation(MyGeometry, InNavigationEvent);
			}
		}
		else  // Orient_Horizontal
		{
			switch (InNavigationEvent.GetNavigationType())
			{
				case EUINavigation::Left:
					FocusedChildDirection = -1;
					break;
				case EUINavigation::Right:
					FocusedChildDirection = 1;
					break;
				default:
					// If we don't handle this direction in our current orientation we can
					// just allow the behavior of the boundary rule take over.
					return SCompoundWidget::OnNavigation(MyGeometry, InNavigationEvent);
			}
		}

		// If the focused child index is in a valid range we know we can successfully focus
		// the new child we're moving focus to.
		if (FocusedChildDirection != 0)
		{
			TSharedPtr<SWidget> NextFocusableChild;

			// Search in the direction we need to move for the next focusable child of the list.
			for (int32 ChildIndex = FocusedChildIndex + FocusedChildDirection; ChildIndex >= 0 && ChildIndex < Children.Num(); ChildIndex += FocusedChildDirection)
			{
				TSharedPtr<SWidget> PossiblyFocusableChild = GetKeyboardFocusableWidget(Children[ChildIndex].GetWidget());
				if (PossiblyFocusableChild.IsValid())
				{
					NextFocusableChild = PossiblyFocusableChild;
					break;
				}
			}

			// If we found a focusable child, scroll to it, and shift focus.
			if (NextFocusableChild.IsValid())
			{
				InternalScrollDescendantIntoView(MyGeometry, NextFocusableChild, false, NavigationDestination, NavigationScrollPadding);
				return FNavigationReply::Explicit(NextFocusableChild);
			}
		}
	}

	return SCompoundWidget::OnNavigation(MyGeometry, InNavigationEvent);
}

bool SImSlateVirtualList::SupportsKeyboardFocus() const
{
	return SCompoundWidget::SupportsKeyboardFocus();  // focusable.
}

}  // namespace ImSlate
