// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImListDataComboImpl.h"

#include "ImSlate.h"
#include "Widgets/Text/STextBlock.h"
#include "ImSlateTemplate/ImText.h"
#include "ImSlateTemplate/ImVirtualComboButton.h"

namespace ImSlate
{
void FImListDataComboImpl::SetMultiSelect(bool bInMultiSelect)
{
	bSupportMultiSelect = bInMultiSelect;
}

void FImListDataComboImpl::SetEnableSearchBox(bool bInEnable)
{
	bEnableSearchBox = bInEnable;
}

int32 FImListDataComboImpl::FilterIndex2TrueIndex(int32 InIndex) const
{
	int32 TrueIndex = InIndex;
	if (IsFiltered() && CurDataArr.IsValidIndex(InIndex))
	{
		auto TrueChoosenData = CurDataArr[InIndex];
		TrueIndex = OrignalData.Find(TrueChoosenData);
		ensureAlways(OrignalData.IsValidIndex(TrueIndex));
	}

	return TrueIndex;
}

int32 FImListDataComboImpl::TrueIndex2FilterIndex(int32 InIndex) const
{
	int32 FilterIndex = InIndex;
	if (IsFiltered() && OrignalData.IsValidIndex(InIndex))
	{
		auto FilterChoosenData = OrignalData[InIndex];
		FilterIndex = CurDataArr.Find(FilterChoosenData);
		if (!CurDataArr.IsValidIndex(FilterIndex))
		{
			FilterIndex = -1;
		}
	}

	return FilterIndex;
}

void FImListDataComboImpl::SetCurrentSelected(int32 InIndex)
{
	if (InIndex < 0)
	{
		// ClearCurrentSelection();
		if (auto Pinned = OwnerComboBox.Pin())
		{
			Pinned->SetCurrentSelectedIndex(-1, false);
		}
		return;
	}

	int32 TrueIndex = InIndex;
	if (IsFiltered() && GetCurData().IsValidIndex(InIndex))
	{
		auto& TrueChoosenData = GetCurData()[InIndex];
		TrueIndex = GetOrignalData().Find(TrueChoosenData);
		ensureAlways(GetOrignalData().IsValidIndex(TrueIndex));
	}

	bool bContains = false;
	bool bSelected = false;
	if (CurrentSelectedItems.Contains(TrueIndex))
	{
		bContains = true;
	}

	if (IsMultiSelect())
	{
		bSelected = !bContains;
		if (bContains)
		{
			CurrentSelectedItems.Remove(TrueIndex);
		}
		else
		{
			CurrentSelectedItems.Add(TrueIndex);
		}
		SetDataSelected(TrueIndex, bSelected);
		if (auto Pinned = OwnerComboBox.Pin())
		{
			Pinned->SetCurrentSelectedIndex(InIndex, bSelected);
		}
	}
	else
	{
		bSelected = true;
		if (bContains && CurrentSelectedItems.Num() == 1)
			return;

		for (auto& Item : CurrentSelectedItems)
		{
			SetDataSelected(Item, false);
			if (auto Pinned = OwnerComboBox.Pin())
			{
				Pinned->SetCurrentSelectedIndex(TrueIndex2FilterIndex(Item), false);
			}
		}

		CurrentSelectedItems.Reset();
		CurrentSelectedItems.Add(TrueIndex);
		SetDataSelected(TrueIndex, bSelected);
		if (auto Pinned = OwnerComboBox.Pin())
		{
			Pinned->SetCurrentSelectedIndex(InIndex, true);
		}
	}
}

TArray<int32> FImListDataComboImpl::GetCurrentSelectedIndexes() const
{
	TArray<int32> SelectedIndexes;
	for (auto It = CurrentSelectedItems.CreateConstIterator(); It; ++It)
	{
		auto FilterIndex = TrueIndex2FilterIndex(*It);
		if (FilterIndex != -1)
		{
			SelectedIndexes.Add(FilterIndex);
		}
	}

	return SelectedIndexes;
}

TArray<int32> FImListDataComboImpl::GetOrignalCurrentSelectedIndexes() const
{
	return CurrentSelectedItems.Array();
}

FText FImListDataComboImpl::GetDataTextByOrignalIndex(int32 InIndex) const
{
	if (IsFiltered())
	{
		if (OrignalData.IsValidIndex(InIndex))
		{
			return OrignalData[InIndex]->GetText();
		}
	}
	else
	{
		if (CurDataArr.IsValidIndex(InIndex))
		{
			return CurDataArr[InIndex]->GetText();
		}
	}

	return FText::GetEmpty();
}

void FImListDataComboImpl::ClearCurrentSelection()
{
	CurrentSelectedItems.Reset();
	for (int32 i = 0; i < GetDataCount(); ++i)
	{
		SetDataSelected(i, false);
	}
}

void FImListDataComboImpl::SetDataSelected(int32 InIndex, bool bInSelected)
{
	if (IsFiltered() && OrignalData.IsValidIndex(InIndex))
	{
		OrignalData[InIndex]->SetSelected(bInSelected);
	}
	else if (CurDataArr.IsValidIndex(InIndex))
	{
		CurDataArr[InIndex]->SetSelected(bInSelected);
	}
}

void FImListDataComboImpl::SetRelateComboButton(TSharedRef<class SImVirtualComboBox> InRelateComboBox)
{
	OwnerComboBox = InRelateComboBox;
}

//

void FImListDataComboImpl::DefaultBindingData(int32 InIndex, const TSharedRef<SWidget>& InWidget, TSharedPtr<FImListDataComboData>& InData)
{
	check(InIndex >= 0 && InIndex < GetDataCount() && InData.IsValid());

	TSharedRef<SImComboEntryBorder> BorderRef = StaticCastSharedRef<SImComboEntryBorder>(InWidget);
	BorderRef->SetBorderImage(InData->IsSelected() ? &SelectedBrush : &NormalBrush);
	auto Content = BorderRef->GetContent();
	auto TextWidget = StaticCastSharedRef<STextBlock>(Content);
	TextWidget->SetText(InData->GetText());
}

void FImListDataComboImpl::DefaultWidgetFactory(TSharedPtr<FImListDataComboData>& InData, TSharedRef<SWidget>& InOutWidget)
{
	check(InData.IsValid());

	TSharedRef<STextBlock> WidgetRef = ImSlate::ImFactoryCreate<UImTextBlock>();
	WidgetRef->SetText(InData->GetText());

	auto BorderRef = SNew(SImComboEntryBorder)
						.Content()
						[
							WidgetRef
						];
	BorderRef->SetBorderImage(InData->IsSelected() ? &SelectedBrush : &NormalBrush);
	InOutWidget = BorderRef;
}

bool FImListDataComboImpl::DefaultItemFilter(const TSharedPtr<FImListDataComboData>& InData, const FGMPStructUnion& InFilter)
{
	if (auto Filter = InFilter.GetStruct<FComboSearchType>())
	{
		if (Filter->FilterText.IsEmpty())
		{
			return true;
		}

		auto FilterStr = Filter->FilterText.ToString();
		TArray<FString> SplitedStr;
		FilterStr.ParseIntoArray(SplitedStr, TEXT(" "));

		auto NameStr = InData->GetText().ToString();
		for (auto& Str : SplitedStr)
		{
			if (!NameStr.Contains(Str))
			{
				return false;
			}
		}
	}

	return true;
}
}  // namespace ImSlate
