// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"
#include "Brushes/SlateBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Engine/World.h"
#include "ProtectFieldAccessor.h"
#include "Slate/SObjectWidget.h"
#include "Styling/SlateBrush.h"
#include "Types/SlateEnums.h"
#include "UnrealCompatibility.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"

namespace ImSlate
{

template<typename T>
class SListView_Sized : public SListView<T>
{
public:
	// Begin SWidget overrides.
	virtual FVector2D ComputeDesiredSize(float In) const override
	{
		if (bSetMaxSize)
			return MaxSize;
		return SCompoundWidget::ComputeDesiredSize(In);
	}
	// End SWidget overrides.

	void SetMaxSize(const FVector2D& InSize)
	{
		if (InSize.IsZero())
		{
			bSetMaxSize = false;
		}
		else
		{
			bSetMaxSize = true;
			MaxSize = InSize;
		}
	}

public:
	bool bSetMaxSize = false;
	FVector2D MaxSize;
};

template<typename T>
class STreeView_Sized : public STreeView<T>
{
public:
	// Begin SWidget overrides.
	virtual FVector2D ComputeDesiredSize(float In) const override
	{
		if (bSetMaxSize)
			return MaxSize;
		return SCompoundWidget::ComputeDesiredSize(In);
	}
	// End SWidget overrides.

	void SetMaxSize(const FVector2D& InSize)
	{
		if (InSize.IsZero())
		{
			bSetMaxSize = false;
		}
		else
		{
			bSetMaxSize = true;
			MaxSize = InSize;
		}
	}

public:
	bool bSetMaxSize = false;
	FVector2D MaxSize;
};

template<typename DataType>
class TImSlateDataStorage
{
public:
	TImSlateDataStorage() {}
	virtual ~TImSlateDataStorage() {}

	using DataBinddingDelegate = TDelegate<void(TSharedPtr<DataType>, TSharedPtr<SWidget>&)>;
	void SetDataBindding(TFunction<void(TSharedPtr<DataType>, TSharedPtr<SWidget>&)> f) { OnDataBindding = DataBinddingDelegate::CreateLambda(MoveTemp(f)); }

	using GetChildrenDelegate = TDelegate<void(const DataType&, TArray<TSharedPtr<DataType>>&)>;
	void SetGetChildrenBinding(TFunction<void(const DataType&, TArray<TSharedPtr<DataType>>&)> f) { OnGetChildrenBinding = GetChildrenDelegate::CreateLambda(MoveTemp(f)); }

	using ExpansionChangedDelegate = TDelegate<void(TSharedPtr<DataType>, bool)>;
	void SetExpansionChangedBinding(TFunction<void(TSharedPtr<DataType>, bool)> f) { OnExpansionChangedBinding = ExpansionChangedDelegate::CreateLambda(MoveTemp(f)); }

	using MouseButtonClickDelegate = TDelegate<void(TSharedPtr<DataType>)>;
	void SetMouseButtonClickBinding(TFunction<void(TSharedPtr<DataType>)> f) { OnMouseButtonClickBinding = MouseButtonClickDelegate::CreateLambda(MoveTemp(f)); }

	using MouseButtonDoubleClickDelegate = TDelegate<void(TSharedPtr<DataType>)>;
	void SetMouseButtonDoubleClickBinding(TFunction<void(TSharedPtr<DataType>)> f) { OnMouseButtonDoubleClickBinding = MouseButtonDoubleClickDelegate::CreateLambda(MoveTemp(f)); }

	using SelectionChangedDelegate = TDelegate<void(TSharedPtr<DataType>, ESelectInfo::Type)>;
	void SetSelectionChangedBinding(TFunction<void(TSharedPtr<DataType>, ESelectInfo::Type)> f) { OnSetSelectionChangedBinding = SelectionChangedDelegate::CreateLambda(MoveTemp(f)); }

	using OnKeyDownDelegate = TDelegate<void(const FGeometry&, const FKeyEvent&, bool&)>;
	void SetOnKeyDownBinding(TFunction<void(const FGeometry&, const FKeyEvent&, bool&)> f) { OnKeyDownBinding = OnKeyDownDelegate::CreateLambda(MoveTemp(f)); }

	using OnOpenContextMenuDelegate = TDelegate<void(TSharedPtr<SWidget>&)>;
	void SetOpenContextMenuBinding(TFunction<void(TSharedPtr<SWidget>&)> f) { OnOpenContextMenuBinding = OnOpenContextMenuDelegate::CreateLambda(MoveTemp(f)); }

	using CountBinddingFunc = TFunction<int32(const TArray<TSharedPtr<DataType>>&)>;
	void SetCountBindding(CountBinddingFunc f) { OnCountBindding = MoveTemp(f); }

public:
	virtual void OnSetData(TSharedPtr<DataType> Data, TSharedPtr<SWidget>& OutWidget) { OnDataBindding.ExecuteIfBound(Data, OutWidget); }

	virtual void OnGetChildren(const DataType& Data, TArray<TSharedPtr<DataType>>& Children) { OnGetChildrenBinding.ExecuteIfBound(Data, Children); }

	virtual void OnExpansionChanged(TSharedPtr<DataType> Data, bool bInExpanded) { OnExpansionChangedBinding.ExecuteIfBound(Data, bInExpanded); }

	virtual void OnMouseButtonClick(TSharedPtr<DataType> Data) { OnMouseButtonClickBinding.ExecuteIfBound(Data); }

	virtual void OnMouseButtonDoubleClick(TSharedPtr<DataType> Data) { OnMouseButtonDoubleClickBinding.ExecuteIfBound(Data); }

	virtual void OnSelectionChanged(TSharedPtr<DataType> Data, ESelectInfo::Type SelectInfo) { OnSetSelectionChangedBinding.ExecuteIfBound(Data, SelectInfo); }

	virtual void OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent, bool& bHandled) { OnKeyDownBinding.ExecuteIfBound(MyGeometry, InKeyEvent, bHandled); }

	virtual TSharedPtr<SWidget> OnOpenContextMenu()
	{
		TSharedPtr<SWidget> Menu;
		OnOpenContextMenuBinding.ExecuteIfBound(Menu);
		return Menu;
	}

	DataBinddingDelegate OnDataBindding;
	GetChildrenDelegate OnGetChildrenBinding;
	ExpansionChangedDelegate OnExpansionChangedBinding;
	MouseButtonClickDelegate OnMouseButtonClickBinding;
	MouseButtonDoubleClickDelegate OnMouseButtonDoubleClickBinding;
	SelectionChangedDelegate OnSetSelectionChangedBinding;
	OnKeyDownDelegate OnKeyDownBinding;
	OnOpenContextMenuDelegate OnOpenContextMenuBinding;
	CountBinddingFunc OnCountBindding;
};

template<typename DataType>
class SImSlateTreeViewBase : public SCompoundWidget
{
	using STreeViewType = STreeView_Sized<TSharedPtr<DataType>>;
	using DataStorageType = TImSlateDataStorage<DataType>;

public:
	SLATE_BEGIN_ARGS(SImSlateTreeViewBase) {}
	SLATE_ARGUMENT_DEFAULT(UWorld*, World){nullptr};
	SLATE_END_ARGS()

	~SImSlateTreeViewBase() {}

	void SetData(TSharedPtr<DataStorageType> InDataBindding)
	{
		if (!InDataBindding.IsValid())
		{
			return;
		}

		DataBindding = InDataBindding;
	}

	void RefreshWidget()
	{
		if (TreeView.IsValid())
		{
			TreeView->RequestTreeRefresh();
		}
	}

	void AddItem(const TSharedPtr<DataType>& Data)
	{
		RootTreeList.Add(Data);
		RefreshWidget();
	}

	void RemoveItem(const TSharedPtr<DataType>& Data) { RootTreeList.Remove(Data); }

	void SetItems(const TArray<TSharedPtr<DataType>> Items)
	{
		RootTreeList.Reset();
		RootTreeList.Append(Items);
		RefreshWidget();
	}

	void ExpandItem(const TSharedPtr<DataType>& Item, bool bShouldExpand) { TreeView->SetItemExpansion(Item, bShouldExpand); }

	TArray<TSharedPtr<DataType>> GetSelectedItems() { return TreeView->GetSelectedItems(); }

	void SetItemSelection(const TSharedPtr<DataType>& Item, bool bSelected) { TreeView->SetItemSelection(Item, bSelected); }

	void ClearSelection() { TreeView->ClearSelection(); }

	void ClearExpandedItems() { TreeView->ClearExpandedItems(); }

	void ScrollToBottom() { TreeView->ScrollToBottom(); }

	void RequestScrollIntoView(TSharedPtr<DataType> Item) { TreeView->RequestScrollIntoView(Item); }

	void Construct(const FArguments& InArgs)
	{
		World = InArgs._World? InArgs._World: (UWorld*)GWorld;
		ChildSlot
		[
			SAssignNew(TreeView, STreeViewType)
			.TreeItemsSource(&RootTreeList)
			.OnGenerateRow(this, &SImSlateTreeViewBase::OnGenerateRow)
			.OnGetChildren(this, &SImSlateTreeViewBase::OnGetGroupChildren)
			.OnExpansionChanged(this, &SImSlateTreeViewBase::OnExpansionChanged)
			.OnMouseButtonClick(this, &SImSlateTreeViewBase::OnMouseButtonClick)
			.OnMouseButtonDoubleClick(this, &SImSlateTreeViewBase::OnMouseButtonDoubleClicked)
			.OnSelectionChanged(this, &SImSlateTreeViewBase::OnSelectionChanged)
			.OnContextMenuOpening(this, &SImSlateTreeViewBase::OnOpenContextMenu)
			.HighlightParentNodesForSelection(true)
		];
	}

	void SetMaxSize(const FVector2D& InSize)
	{
		if (TreeView.IsValid())
		{
			TreeView->SetMaxSize(InSize);
		}
	}

protected:
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<DataType> Item, const TSharedRef<STableViewBase>& OwnerTable)
	{
		TSharedPtr<SWidget> Widget = nullptr;
		DataBindding->OnSetData(Item, Widget);
		if (!Widget.IsValid())
		{
			//Widget = NewWidgetFunc();
			Widget = SNullWidget::NullWidget;
		}

		TSharedPtr<STableRow<TSharedPtr<DataType>>> TableRowWidget;
		SAssignNew(TableRowWidget, STableRow<TSharedPtr<DataType>>, OwnerTable)
		.Cursor(EMouseCursor::Default)
		.Padding(FMargin(0))
		//.OnDragDetected_Lambda([InWidget = Widget](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) { return GS_ACCESS_PROTECT(InWidget, UWidget, MyGCWidget)->MyGCWidget.Pin()->OnDragDetected(MyGeometry, MouseEvent); })
		//.OnDragEnter_Lambda([InWidget = Widget](const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) { return GS_ACCESS_PROTECT(InWidget, UWidget, MyGCWidget)->MyGCWidget.Pin()->OnDragEnter(MyGeometry, DragDropEvent); })
		//.OnDragLeave_Lambda([InWidget = Widget](const FDragDropEvent& DragDropEvent) { return GS_ACCESS_PROTECT(InWidget, UWidget, MyGCWidget)->MyGCWidget.Pin()->OnDragLeave(DragDropEvent); })
		[
			Widget.ToSharedRef()
		];

		return TableRowWidget.ToSharedRef();
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		bool bHandled = false;
		if (DataBindding.IsValid())
		{
			DataBindding->OnKeyDown(MyGeometry, InKeyEvent, bHandled);
		}

		return bHandled ? FReply::Handled() : FReply::Unhandled();
	}

	void OnGetGroupChildren(TSharedPtr<DataType> Node, TArray<TSharedPtr<DataType>>& OutNodes)
	{
		if (DataBindding.IsValid())
		{
			DataBindding->OnGetChildren(*Node, OutNodes);
		}
	}

	void OnExpansionChanged(TSharedPtr<DataType> InItem, bool bInExpanded)
	{
		if (DataBindding.IsValid())
		{
			DataBindding->OnExpansionChanged(InItem, bInExpanded);
		}
	}

	void OnMouseButtonClick(TSharedPtr<DataType> InItem)
	{
		bJustClicked = true;
		if (DataBindding.IsValid())
		{
			DataBindding->OnMouseButtonClick(InItem);
		}
	}

	void OnMouseButtonDoubleClicked(TSharedPtr<DataType> InItem)
	{
		bJustClicked = true;
		if (DataBindding.IsValid())
		{
			DataBindding->OnMouseButtonDoubleClick(InItem);
		}
	}

	void OnSelectionChanged(TSharedPtr<DataType> InItem, ESelectInfo::Type SelectInfo)
	{
		if (DataBindding.IsValid())
		{
			DataBindding->OnSelectionChanged(InItem, SelectInfo);
		}
	}

	TSharedPtr<SWidget> OnOpenContextMenu()
	{
		if (DataBindding.IsValid())
		{
			return DataBindding->OnOpenContextMenu();
		}

		return TSharedPtr<SWidget>();
	}

	float GetItemHeight() const { return ItemHeight; }

public:
	TWeakObjectPtr<::UWorld> World;

	float ItemHeight = 20;

	TArray<TSharedPtr<DataType>> RootTreeList;

	UPROPERTY(EditAnywhere)
	class TSubclassOf<UWidget> CellTemplateClass = UUserWidget::StaticClass();

	bool bJustClicked = false;

protected:
	TSharedPtr<STreeViewType> TreeView = nullptr;

	TSharedPtr<DataStorageType> DataBindding = nullptr;
};

template<typename DataType>
class SImSlateListViewBase : public SCompoundWidget
{
	using SListViewType = SListView_Sized<TSharedPtr<DataType>>;
	using DataStorageType = TImSlateDataStorage<DataType>;

public:
	SLATE_BEGIN_ARGS(SImSlateListViewBase) {}
	SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
	SLATE_END_ARGS()

	~SImSlateListViewBase() {}

	void SetData(TSharedPtr<DataStorageType> InDataBindding)
	{
		if (!InDataBindding.IsValid())
		{
			return;
		}

		DataBindding = InDataBindding;
		if (CellTemplateClass->IsChildOf(UUserWidget::StaticClass()))
		{
			NewWidgetFunc = [this]() -> UWidget* { return CreateWidget<UUserWidget>(World.Get(), CellTemplateClass.Get()); };
		}
		else
		{
			NewWidgetFunc = [this]() -> UWidget* { return NewObject<UWidget>(World.Get(), CellTemplateClass.Get()); };
		}
	}

	void RefreshWidget()
	{
		if (ListView.IsValid())
		{
			ListView->RebuildList();
		}
	}

	void AddItem(const TSharedPtr<DataType>& Data)
	{
		RootListList.Add(Data);
		RefreshWidget();
	}

	void RemoveItem(const TSharedPtr<DataType>& Data) { RootListList.Remove(Data); }

	void SetItems(const TArray<TSharedPtr<DataType>> Items)
	{
		RootListList.Reset();
		RootListList.Append(Items);
		RefreshWidget();
	}

	void ExpandItem(const TSharedPtr<DataType>& Item, bool bShouldExpand) { ListView->SetItemExpansion(Item, bShouldExpand); }

	TArray<TSharedPtr<DataType>> GetSelectedItems() { return ListView->GetSelectedItems(); }

	void SetItemSelection(const TSharedPtr<DataType>& Item, bool bSelected) { ListView->SetItemSelection(Item, bSelected); }

	void ClearSelection() { ListView->ClearSelection(); }

	void ClearExpandedItems() { ListView->ClearExpandedItems(); }

	void ScrollToBottom() { ListView->ScrollToBottom(); }

	void RequestScrollIntoView(TSharedPtr<DataType> Item) { ListView->RequestScrollIntoView(Item); }

	void Construct(const FArguments& InArgs)
	{
		World = InArgs._World;
		ChildSlot
		[
			SAssignNew(ListView, SListViewType)
			.ListItemsSource(&RootListList)
			.OnGenerateRow(this, &SImSlateListViewBase::OnGenerateRow)
			.SelectionMode(ESelectionMode::Single)
			.OnMouseButtonClick(this, &SImSlateListViewBase::OnMouseButtonClick)
			.OnMouseButtonDoubleClick(this, &SImSlateListViewBase::OnMouseButtonDoubleClicked)
			.OnSelectionChanged(this, &SImSlateListViewBase::OnSelectionChanged)
			.OnContextMenuOpening(this, &SImSlateListViewBase::OnOpenContextMenu)
		];
	}

	void SetMaxSize(const FVector2D& InSize)
	{
		if (ListView.IsValid())
		{
			ListView->SetMaxSize(InSize);
		}
	}

protected:
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<DataType> Item, const TSharedRef<STableViewBase>& OwnerTable)
	{
		TSharedPtr<SWidget> Widget = nullptr;
		DataBindding->OnSetData(Item, Widget);
		if (!Widget.IsValid())
		{
			//Widget = NewWidgetFunc();
			Widget = SNullWidget::NullWidget;
		}

		TSharedPtr<STableRow<TSharedPtr<DataType>>> TableRowWidget;
		SAssignNew(TableRowWidget, STableRow<TSharedPtr<DataType>>, OwnerTable)
		.Cursor(EMouseCursor::Hand)
		.Padding(FMargin(0))
		//.OnDragDetected_Lambda([InWidget = Widget](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) { return GS_ACCESS_PROTECT(InWidget, UWidget, MyGCWidget)->MyGCWidget.Pin()->OnDragDetected(MyGeometry, MouseEvent); })
		//.OnDragEnter_Lambda([InWidget = Widget](const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) { return GS_ACCESS_PROTECT(InWidget, UWidget, MyGCWidget)->MyGCWidget.Pin()->OnDragEnter(MyGeometry, DragDropEvent); })
		//.OnDragLeave_Lambda([InWidget = Widget](const FDragDropEvent& DragDropEvent) { return GS_ACCESS_PROTECT(InWidget, UWidget, MyGCWidget)->MyGCWidget.Pin()->OnDragLeave(DragDropEvent); })
		[
			Widget.ToSharedRef()
		];

		return TableRowWidget.ToSharedRef();
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		bool bHandled = false;
		if (DataBindding.IsValid())
		{
			DataBindding->OnKeyDown(MyGeometry, InKeyEvent, bHandled);
		}

		return bHandled ? FReply::Handled() : FReply::Unhandled();
	}

	void OnGetGroupChildren(TSharedPtr<DataType> Node, TArray<TSharedPtr<DataType>>& OutNodes)
	{
		if (DataBindding.IsValid())
		{
			DataBindding->OnGetChildren(*Node, OutNodes);
		}
	}

	void OnExpansionChanged(TSharedPtr<DataType> InItem, bool bInExpanded)
	{
		if (DataBindding.IsValid())
		{
			DataBindding->OnExpansionChanged(InItem, bInExpanded);
		}
	}

	void OnMouseButtonClick(TSharedPtr<DataType> InItem)
	{
		bJustClicked = true;
		if (DataBindding.IsValid())
		{
			DataBindding->OnMouseButtonClick(InItem);
		}
	}

	void OnMouseButtonDoubleClicked(TSharedPtr<DataType> InItem)
	{
		bJustClicked = true;
		if (DataBindding.IsValid())
		{
			DataBindding->OnMouseButtonDoubleClick(InItem);
		}
	}

	void OnSelectionChanged(TSharedPtr<DataType> InItem, ESelectInfo::Type SelectInfo)
	{
		if (DataBindding.IsValid())
		{
			DataBindding->OnSelectionChanged(InItem, SelectInfo);
		}
	}

	TSharedPtr<SWidget> OnOpenContextMenu()
	{
		if (DataBindding.IsValid())
		{
			return DataBindding->OnOpenContextMenu();
		}

		return TSharedPtr<SWidget>();
	}

	float GetItemHeight() const { return ItemHeight; }

public:
	TWeakObjectPtr<::UWorld> World;

	float ItemHeight = 20;

	TArray<TSharedPtr<DataType>> RootListList;

	UPROPERTY(EditAnywhere)
	class TSubclassOf<UWidget> CellTemplateClass = UUserWidget::StaticClass();

	bool bJustClicked = false;

protected:
	TSharedPtr<SListViewType> ListView;

	TSharedPtr<DataStorageType> DataBindding;

	TFunction<UWidget*()> NewWidgetFunc;
};

};  // namespace ImSlate
