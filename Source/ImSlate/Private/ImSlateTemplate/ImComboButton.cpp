// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImComboButton.h"

#include "ImSlateFwd.h"
#include "ImSlateTemplate/ImText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace ImSlate
{

//
class SXSearchBox : public SSearchBox
{
public:
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { return SEditableTextBox::OnKeyDown(MyGeometry, InKeyEvent).PreventThrottling(); }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) { return SWidget::OnMouseButtonDown(MyGeometry, MouseEvent).PreventThrottling(); }
};

//
void SImComboBox::Construct(const FArguments& InArgs)
{
	check(InArgs._ComboBoxStyle);
	ItemStyle = InArgs._ItemStyle;
	SearchBoxStyle = InArgs._SearchBoxStyle;
	SearchBoxHeight = InArgs._SearchBoxHeight;

	// Work out which values we should use based on whether we were given an override, or should use the style's version
	const FComboButtonStyle& OurComboButtonStyle = InArgs._ComboBoxStyle->ComboButtonStyle;
	const FButtonStyle* const OurButtonStyle = InArgs._ButtonStyle ? InArgs._ButtonStyle : &OurComboButtonStyle.ButtonStyle;
	PressedSound = InArgs._PressedSoundOverride.Get(InArgs._ComboBoxStyle->PressedSlateSound);
	SelectionChangeSound = InArgs._SelectionChangeSoundOverride.Get(InArgs._ComboBoxStyle->SelectionChangeSlateSound);

	//this->OnComboBoxOpening = InArgs._OnComboBoxOpening;
	//this->OnSelectionChanged = InArgs._OnSelectionChanged;

	OptionsSource = InArgs._OptionsSource;
	OptionsSourceForShow.Reset();
	if (OptionsSource.Options)
		OptionsSourceForShow = *(OptionsSource.Options);
	CustomScrollbar = InArgs._CustomScrollbar;

	MenuVerticalBox = SNew(SVerticalBox);
	if (InArgs._bHasSearchBox)
	{
		MenuVerticalBox->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(SearchBoxHeight)
			.Content()
			[
				SAssignNew(MenuSearchBox, SXSearchBox)
				.Style(SearchBoxStyle)
				.SelectAllTextWhenFocused(true)
				.OnTextChanged(this, &SImComboBox::OnFilterTextChanged)
				.OnTextCommitted(this, &SImComboBox::OnFilterTextCommitted)
				.AddMetaData<FTagMetaData>(TEXT("Details.Search"))
			]
		];
	}
	MenuVerticalBox->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SAssignNew(this->ComboListView, ListType)
		.ListItemsSource(&OptionsSourceForShow)
		.OnGenerateRow(this, &SImComboBox::GenerateMenuItemRow)
		.OnSelectionChanged(this, &SImComboBox::OnSelectionChanged_Internal)
		.SelectionMode(ESelectionMode::Single)
		.ExternalScrollbar(InArgs._CustomScrollbar)
	];

	TSharedRef<SWidget> ComboBoxMenuContent = SNew(SBox)
												.MaxDesiredHeight(InArgs._MaxListHeight)
												.Content()
												[
													MenuVerticalBox.ToSharedRef()
												];

	// Set up content
	TSharedPtr<SWidget> ButtonContent = InArgs._Content.Widget;
	if (InArgs._Content.Widget == SNullWidget::NullWidget)
	{
		SAssignNew(ButtonContent, STextBlock)
		.Text(NSLOCTEXT("SComboBox", "ContentWarning", "No Content Provided"))
		.ColorAndOpacity(FLinearColor::Red);
	}

	SComboButton::Construct(SComboButton::FArguments()
	.ComboButtonStyle(&OurComboButtonStyle)
	.ButtonStyle(OurButtonStyle)
	.Method(InArgs._Method)
	.ButtonContent()[ButtonContent.ToSharedRef()]
	.MenuContent()[ComboBoxMenuContent]
	.HasDownArrow(InArgs._HasDownArrow)
	.ContentPadding(InArgs._ContentPadding)
	.ForegroundColor(InArgs._ForegroundColor)
	.OnMenuOpenChanged(this, &SImComboBox::OnMenuOpenChanged)
	.IsFocusable(InArgs._IsFocusable)
							/*.CollapseMenuOnParentFocus(InArgs._CollapseMenuOnParentFocus)*/);
	SetMenuContentWidgetToFocus(ComboListView);

#if defined(WITH_THROTTLE_MODIFY)
	bIgnoreThrottle = true;
#endif

	// Need to establish the selected item at point of construction so its available for querying
	// NB: If you need a selection to fire use SetItemSelection rather than setting an IntiallySelectedItem
	SelectedItem = InArgs._InitiallySelectedItem;
	if (SelectedItem.IsValid())
	{
		ComboListView->SetSelection(SelectedItem, ESelectInfo::OnNavigation);
		ComboListView->RequestScrollIntoView(SelectedItem, 0);
	}
}

void SImComboBox::SetOptionsSoure(const FComboSourceType& InOptionsSource)
{
	if (!IsSameSource(InOptionsSource))
	{
		OptionsSource = InOptionsSource;
		OnFilterTextChanged(FText::GetEmpty());
	}
}

void SImComboBox::SetSelectedItem(const DataType& InItem)
{
	if (InItem != SelectedItem)
	{
		SelectedItem = InItem;
		if (ButtonContentSlot)
			(*ButtonContentSlot)[SelectedItem.IsValid() ? SelectedItem->GenWidget() : SNullWidget::NullWidget];
		if (SelectedItem.IsValid() && ComboListView.IsValid())
		{
			ComboListView->Private_SetItemSelection(SelectedItem, true);
			ComboListView->RequestScrollIntoView(SelectedItem, 0);
		}
	}
}

SImComboBox::DataType SImComboBox::GetSelectedItem() const
{
	return SelectedItem;
}

bool SImComboBox::GetAndResetChangedState()
{
	auto State = bIsSelectedChanged;
	bIsSelectedChanged = false;
	return State;
}

void SImComboBox::SetSearchBoxDisplay(bool bShow)
{
	if (MenuVerticalBox.IsValid())
	{
		if (bShow)
		{
			if (MenuVerticalBox->NumSlots() == 2)
				return;
			if (!MenuSearchBox.IsValid())
			{
				SAssignNew(MenuSearchBox, SXSearchBox)
				.Style(SearchBoxStyle)
				.SelectAllTextWhenFocused(true)
				.OnTextChanged(this, &SImComboBox::OnFilterTextChanged)
				.OnTextCommitted(this, &SImComboBox::OnFilterTextCommitted)
				.AddMetaData<FTagMetaData>(TEXT("Details.Search"));
			}
			MenuVerticalBox->InsertSlot(0)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Top)
				.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(SearchBoxHeight)
						.Content()
						[
							MenuSearchBox.ToSharedRef()
						]
					];
		}
		else
		{
			if (MenuVerticalBox->NumSlots() == 2 && MenuSearchBox.IsValid())
			{
				MenuVerticalBox->RemoveSlot(MenuSearchBox.ToSharedRef());
			}
		}
	}
}

template<typename OptionType>
class SXComboRow : public STableRow<OptionType>
{
public:
	SLATE_BEGIN_ARGS(SXComboRow)
		: _Style(&FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
		, _Content()
	{
	}
	SLATE_STYLE_ARGUMENT(FTableRowStyle, Style)
	SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

public:
	/**
	 * Constructs this widget.
	 */
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
	{
		STableRow<OptionType>::Construct(typename STableRow<OptionType>::FArguments().Style(InArgs._Style).Content()[InArgs._Content.Widget], InOwnerTable);
	}

	// handle case where user clicks on an existing selected item
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			TSharedPtr<ITypedTableView<OptionType>> OwnerWidget = this->OwnerTablePtr.Pin();

			const OptionType* MyItem = OwnerWidget->Private_ItemFromWidget(this);
			const bool bIsSelected = OwnerWidget->Private_IsItemSelected(*MyItem);

			if (bIsSelected)
			{
				// Reselect content to ensure selection is taken
				OwnerWidget->Private_SignalSelectionChanged(ESelectInfo::Direct);
				return FReply::Handled().PreventThrottling();
			}
		}
		return STableRow<OptionType>::OnMouseButtonDown(MyGeometry, MouseEvent).PreventThrottling();
	}
};

TSharedRef<ITableRow> SImComboBox::GenerateMenuItemRow(DataType InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SXComboRow<DataType>, OwnerTable)
			.Style(ItemStyle)
			[
				InItem->GenWidget()
			];
}

void SImComboBox::OnSelectionChanged_Internal(DataType ProposedSelection, ESelectInfo::Type SelectInfo)
{
	if (!ProposedSelection.IsValid() && SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	// Ensure that the proposed selection is different
	if (SelectInfo != ESelectInfo::OnNavigation)
	{
		// Ensure that the proposed selection is different from selected
		if (ProposedSelection != SelectedItem)
		{
			bIsSelectedChanged = true;
			//PlaySelectionChangeSound();
			if (SelectedItem.IsValid())
				SelectedItem->SelectionChanged(false, SelectInfo);
			SelectedItem = ProposedSelection;
			if (SelectedItem.IsValid())
			{
				SelectedItem->SelectionChanged(true, SelectInfo);
				if (ButtonContentSlot)
					(*ButtonContentSlot)[SelectedItem->GenWidget()];
			}
			//OnSelectionChanged.ExecuteIfBound(ProposedSelection, SelectInfo);
		}
		// close combo even if user reselected item
		this->SetIsOpen(false);
	}
}

void SImComboBox::OnMenuOpenChanged(bool bOpen)
{
	if (bOpen == false)
	{
		//bControllerInputCaptured = false;

		if (SelectedItem.IsValid())
		{
			ComboListView->SetSelection(SelectedItem, ESelectInfo::OnNavigation);
			ComboListView->RequestScrollIntoView(SelectedItem, 0);
		}

		// Set focus back to ComboBox for users focusing the ListView that just closed
		// 		TSharedRef<SWidget> ThisRef = AsShared();
		// 		FSlateApplication::Get().ForEachUser([&ThisRef](FSlateUser& User) {
		// 			if (User.HasFocusedDescendants(ThisRef))
		// 			{
		// 				User.SetFocus(ThisRef, EFocusCause::SetDirectly);
		// 			}
		// 		});
	}
}

FReply SImComboBox::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return SComboButton::OnKeyDown(MyGeometry, InKeyEvent).PreventThrottling();
}

FReply SImComboBox::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return SWidget::OnMouseButtonDown(MyGeometry, MouseEvent).PreventThrottling();
}

void SImComboBox::OnFilterTextCommitted(const FText& InFilterText, ETextCommit::Type InCommitType)
{
	OnFilterTextChanged(InFilterText);
}

void SImComboBox::OnFilterTextChanged(const FText& InFilterText)
{
	if (OptionsSource.Options)
	{
		OptionsSourceForShow.Reset();
		for (auto& Option : *OptionsSource.Options)
		{
			if (Option->OnMeetConditions(InFilterText))
			{
				OptionsSourceForShow.Add(Option);
			}
		}
		if (ComboListView.IsValid())
		{
			ComboListView->SetItemsSource(&OptionsSourceForShow);
			ComboListView->RequestListRefresh();
			if (SelectedItem.IsValid())
			{
				ComboListView->SetSelection(SelectedItem, ESelectInfo::OnNavigation);
				ComboListView->RequestScrollIntoView(SelectedItem, 0);
			}
		}
	}
}

bool SImComboBox::IsSameSource(const FComboSourceType& InOptionsSource)
{
	if (OptionsSource.h != InOptionsSource.h)
		return false;

	if (OptionsSource.Options && InOptionsSource.Options)
	{
		if (OptionsSource.Options->Num() != InOptionsSource.Options->Num())
			return false;

		for (int32 i = 0; i < OptionsSource.Options->Num(); ++i)
		{
			if ((*OptionsSource.Options)[i] != (*InOptionsSource.Options)[i])
			{
				return false;
			}
		}
	}

	return true;
}

uint32 FComboSourceType::GetHash(const TArray<TSharedPtr<IImComboBoxItem>>& In)
{
	uint32 h = 0x811c9dd7;
	for (auto& Item : In)
	{
		h = PointerHash(Item.Get(), h);
	}
	return h;
}


}  // namespace ImSlate

TSharedRef<ImSlate::SImComboBox> UImComboButton::ConstructImWidget() const
{
	TSharedRef<STextBlock> WidgetRef = ImSlate::ImFactoryCreate<UImTextBlock>();
	WidgetRef->SetAutoWrapText(true);
	ReSetSearchBoxStyle();

	return SNew(ImSlate::SImComboBox)
			.bHasSearchBox(bHasSearchBox)
			.ComboBoxStyle(&ComboBoxStyle)
			.ButtonStyle(&ButtonStyle)
			.ItemStyle(&ItemStyle)
			.SearchBoxStyle(&SearchBoxStyle)
			.OptionsSource(EmptyOption)
			//.Method(EPopupMethod::CreateNewWindow)
			.IsFocusable(bIsFocusable)
			.Content()
			[
				WidgetRef
			];
}

void UImComboButton::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyWidgetPtr.Reset();
}

TSharedRef<SWidget> UImComboButton::RebuildWidget()
{
	TSharedRef<STextBlock> WidgetRef = ImSlate::ImFactoryCreate<UImTextBlock>(this);
	WidgetRef->SetAutoWrapText(true);

	ReSetSearchBoxStyle();
	return SAssignNew(MyWidgetPtr, ImSlate::SImComboBox)
			.bHasSearchBox(bHasSearchBox)
			.ComboBoxStyle(&ComboBoxStyle)
			.ButtonStyle(&ButtonStyle)
			.ItemStyle(&ItemStyle)
			.SearchBoxStyle(&SearchBoxStyle)
			.OptionsSource(EmptyOption)
			.IsFocusable(bIsFocusable)
			.Content()
			[
				WidgetRef
			];
}

void UImComboButton::ReSetSearchBoxStyle() const
{
	SearchBoxStyle.SetTextBoxStyle(TextBoxStyle);
	SearchBoxStyle.SetActiveFont(ActiveFontInfo);
	SearchBoxStyle.SetUpArrowImage(UpArrowImage);
	SearchBoxStyle.SetDownArrowImage(DownArrowImage);
	SearchBoxStyle.SetGlassImage(GlassImage);
	SearchBoxStyle.SetClearImage(ClearImage);
	SearchBoxStyle.SetImagePadding(ImagePadding);
	SearchBoxStyle.SetLeftAlignButtons(bLeftAlignButtons);
}
