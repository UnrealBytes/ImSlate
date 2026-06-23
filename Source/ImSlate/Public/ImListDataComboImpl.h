// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Engine/World.h"
#include "GMPCore.h"
#include "ImSlateListDataInc.h"
#include "ImSlateVirtualList.h"
#include "Templates/SharedPointer.h"
#include "UnrealCompatibility.h"
#include "Widgets/Layout/SBorder.h"

namespace ImSlate
{
class SImVirtualComboBox;  // in-namespace fwd decl (SImVirtualComboBox lives in namespace ImSlate)


class SImComboEntryBorder : public SBorder
{
public:
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { return SBorder::OnKeyDown(MyGeometry, InKeyEvent).PreventThrottling(); }

	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { return SBorder::OnKeyUp(MyGeometry, InKeyEvent).PreventThrottling(); }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) { return SBorder::OnMouseButtonDown(MyGeometry, MouseEvent).PreventThrottling(); }

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return SBorder::OnMouseButtonUp(MyGeometry, MouseEvent).PreventThrottling(); }
};

struct IMSLATE_API FImListDataComboData : public TSharedFromThis<FImListDataComboData>
{
public:
	FImListDataComboData(const FText& InText, int64 InVal = 0)
		: Name(InText)
		, Val(InVal)
	{
	}

	bool IsSelected() const { return bSelected; }
	void SetSelected(bool bInSelected) { bSelected = bInSelected; }
	FText GetText() const { return Name; }

protected:
	FText Name;
	int64 Val;
	bool bSelected = false;
};

class SImSlateVirtualList;
class IMSLATE_API FImListDataComboImpl : public TImSlateListArray<TSharedPtr<FImListDataComboData>, SWidget>
{
public:
	FImListDataComboImpl()
		: TImSlateListArray<TSharedPtr<FImListDataComboData>, SWidget>(false)
	{
		NormalBrush.TintColor = FLinearColor::Gray;
		SelectedBrush.TintColor = FLinearColor::Green;
	}

	void Init()
	{
		SetOnBindingData(FOnBindingDataDelegate::CreateSP(this, &FImListDataComboImpl::DefaultBindingData));
		SetWidgetFactory(FWidgetFactoryDelegate::CreateSP(this, &FImListDataComboImpl::DefaultWidgetFactory));
		SetItemAxisBinding(FItemAxisBindingDelegate::CreateSP(this, &FImListDataComboImpl::DefaultItemHeight));
		SetFilterExpr(FFilterExpr::CreateSP(this, &FImListDataComboImpl::DefaultItemFilter));
	};

	TSharedRef<FImListDataComboImpl> MakeShared() { return ::MakeShared<FImListDataComboImpl>(); }

public:
	bool IsMultiSelect() const { return bSupportMultiSelect; }
	void SetMultiSelect(bool bInMultiSelect);

	bool IsEnableSearchBox() const { return bEnableSearchBox; }
	void SetEnableSearchBox(bool bInEnable);

	int32 FilterIndex2TrueIndex(int32 InIndex) const;
	int32 TrueIndex2FilterIndex(int32 InIndex) const;

	void SetCurrentSelected(int32 InIndex);
	TArray<int32> GetCurrentSelectedIndexes() const;
	TArray<int32> GetOrignalCurrentSelectedIndexes() const;
	FText GetDataTextByOrignalIndex(int32 InIndex) const;
	void ClearCurrentSelection();
	void SetDataSelected(int32 InIndex, bool bInSelected);

	void SetRelateComboButton(TSharedRef<class SImVirtualComboBox> InRelateComboBox);

	using TImSlateListArray<TSharedPtr<FImListDataComboData>, SWidget>::GenerateDataWidget;

protected:
	bool bSupportMultiSelect = false;
	bool bEnableSearchBox = false;

	TSet<int32, DefaultKeyFuncs<int32>, TInlineSetAllocator<4>> CurrentSelectedItems;

	FSlateBrush NormalBrush;
	FSlateBrush SelectedBrush;

	TWeakPtr<SImVirtualComboBox> OwnerComboBox = nullptr;

protected:
	void DefaultBindingData(int32 InIndex, const TSharedRef<SWidget>& InWidget, TSharedPtr<FImListDataComboData>& InData);
	void DefaultWidgetFactory(TSharedPtr<FImListDataComboData>& InData, TSharedRef<SWidget>& InOutWidget);
	float DefaultItemHeight(int32 InIndex) { return 22.f; }
	bool DefaultItemFilter(const TSharedPtr<FImListDataComboData>& InData, const FGMPStructUnion& InFilter);
};
}  // namespace ImSlate
