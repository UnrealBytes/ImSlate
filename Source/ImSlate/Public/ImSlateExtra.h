// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "ImSlate.h"
#include "ImSlateTemplates.h"
#include "Styling/SlateTypes.h"
#include "UObject/StrongObjectPtr.h"

namespace ImSlate
{
IMSLATE_API void BeginFrame(float DeltaTime = 1.f / 60.f);
IMSLATE_API void EndFrame();

IMSLATE_API bool ComboBox(ImStr Label, TSharedPtr<class IImComboBoxItem>& InOutSelected, const struct FComboSourceType& InSource, bool bHasSearchBox = false, ImSlateComboFlags_ Flags = ImSlateComboFlags_None, const ImVec2& InSize = ImVec2(0, 0));

template<typename DataType>
bool ListView(ImStr Label, TSharedPtr<DataType>& InOutSelected, const TArray<TSharedPtr<DataType>>& InSource, const TSharedPtr<TImSlateDataStorage<DataType>>& InDataStore, const ImVec2& InSize = ImVec2(0, 0), bool bClearSelection = false)
{
	if (InSource.Num() <= 0)
		return false;

	using Internal::Item;
	if (auto ItemPtr = Item<SImSlateListViewBase<DataType>>(Label, [&](FItemSlotPod& InItem) {
			InItem.bFillWidth = true;
			InItem.StretchValue = 1.f;
			InItem.HAlignment = HAlign_Fill;
			InItem.VAlignment = VAlign_Fill;

			TSharedRef<SImSlateListViewBase<DataType>> WidgetRef = SNew(SImSlateListViewBase<DataType>)
																	.World(nullptr);
			WidgetRef->SetData(InDataStore);
			WidgetRef->SetItems(InSource);
			if (InSize.HasValidSize())
				WidgetRef->SetMaxSize(InSize);
			return WidgetRef;
		}))
	{
		ItemPtr->SetData(InDataStore);
		ItemPtr->SetItems(InSource);

		TArray<TSharedPtr<DataType>> SelectedItems = ItemPtr->GetSelectedItems();
		if (SelectedItems.Num() > 0)
		{
			InOutSelected = SelectedItems[0];

			if (bClearSelection)
				ItemPtr->ClearSelection();

			return true;
		}
	}

	return false;
}

template<typename DataType>
bool TreeView(ImStr Label, TSharedPtr<DataType>& InOutSelected, const TArray<TSharedPtr<DataType>>& InSource, const TSharedPtr<TImSlateDataStorage<DataType>>& InDataStore, const ImVec2& InSize = ImVec2(0, 0), bool bClearSelection = false)
{
	if (InSource.Num() <= 0)
		return false;

	using Internal::Item;
	if (auto ItemPtr = Item<SImSlateTreeViewBase<DataType>>(Label, [&](FItemSlotPod& InItem) {
			InItem.bFillWidth = true;
			InItem.StretchValue = 1.f;
			InItem.HAlignment = HAlign_Fill;
			InItem.VAlignment = VAlign_Fill;

			TSharedRef<SImSlateTreeViewBase<DataType>> WidgetRef = SNew(SImSlateTreeViewBase<DataType>)
																	.World(GWorld);
			WidgetRef->SetData(InDataStore);
			WidgetRef->SetItems(InSource);
			if (InSize.HasValidSize())
				WidgetRef->SetMaxSize(InSize);
			return WidgetRef;
		}))
	{
		ItemPtr->SetData(InDataStore);
		ItemPtr->SetItems(InSource);

		TArray<TSharedPtr<DataType>> SelectedItems = ItemPtr->GetSelectedItems();
		if (SelectedItems.Num() > 0)
		{
			InOutSelected = SelectedItems[0];

			if (bClearSelection)
				ItemPtr->ClearSelection();

			return true;
		}
	}

	return false;
}
// Example: ListView_String
IMSLATE_API bool ListView_String(ImStr Label, TSharedPtr<FString>& InOutSelected, const TArray<TSharedPtr<FString>>& InSource, const ImVec2& InSize = ImVec2(0, 0), bool bClearSelection = false);  // ListView_String

}  // namespace ImSlate
