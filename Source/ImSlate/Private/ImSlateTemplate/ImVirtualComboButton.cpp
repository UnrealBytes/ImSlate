// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImVirtualComboButton.h"
//
#include "ImSlateFwd.h"
#include "ImListDataComboImpl.h"
#include "ImSlateTemplate/ImText.h"
#include "ImSlateVirtualList.h"
#include "PrivateFieldAccessor.h"
#include "ProtectFieldAccessor.h"
#include "UnrealCompatibility.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace ImSlate
{

//
class SXVirtualSearchBox : public SSearchBox
{
public:
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { return SEditableTextBox::OnKeyDown(MyGeometry, InKeyEvent).PreventThrottling(); }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) { return SWidget::OnMouseButtonDown(MyGeometry, MouseEvent).PreventThrottling(); }
};

DECLARE_DELEGATE_OneParam(FOnMouseButtonUp, int32);

typedef ImSlate::SImSlateVirtualList SXImSlateVirtualList;
//namespace ImVirtualComboBox
//{
//GS_PRIVATEACCESS_FUNCTION(SXImSlateVirtualList, GetDataCount, int32()const);
//GS_PRIVATEACCESS_FUNCTION(SXImSlateVirtualList, LowerDataIndex, int32(float)const);
//GS_PRIVATEACCESS_FUNCTION(SXImSlateVirtualList, UpperDataIndex, int32(float)const);
//}
//
#if 0
class SXMouseEventBorder : public SBorder
{
public:
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { return SBorder::OnKeyDown(MyGeometry, InKeyEvent).PreventThrottling(); }

	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { return SBorder::OnKeyUp(MyGeometry, InKeyEvent).PreventThrottling(); }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) { return SBorder::OnMouseButtonDown(MyGeometry, MouseEvent).PreventThrottling(); }

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (auto Pinned = WeakVirtualList.Pin())
		{
			int32 CurrentIndex = -1;
			auto Ptr = GS_ACCESS_PROTECT(Pinned.Get(), SXImSlateVirtualList, GetDataCount, LowerDataIndex, UpperDataIndex, GetVirtualPos);
			//auto DataCount = ImVirtualComboBox::PrivateAccess::GetDataCount(*Pinned.Get());
			auto DataCount = Ptr->GetDataCount();
			if (Pinned.IsValid() && DataCount > 0)
			{
				auto CalcPos = Ptr->GetVirtualPos() + GetCachedGeometry().AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).Y;
				auto LowIndex = Ptr->LowerDataIndex(CalcPos);
				auto UpperIndex = Ptr->UpperDataIndex(CalcPos);
				//auto LowIndex = ImVirtualComboBox::PrivateAccess::LowerDataIndex(*Pinned.Get(), CalcPos);
				//auto UpperIndex = ImVirtualComboBox::PrivateAccess::UpperDataIndex(*Pinned.Get(), CalcPos);
				if (LowIndex <= UpperIndex)
				{
					CurrentIndex = FMath::Clamp(LowIndex, 0, DataCount - 1);
				}
			}
			if (OnMouseUp.IsBound())
				OnMouseUp.Execute(CurrentIndex);
		}
		return SBorder::OnMouseButtonUp(MyGeometry, MouseEvent).PreventThrottling();
	}

public:
	FOnMouseButtonUp OnMouseUp;
	TWeakPtr<ImSlate::SImSlateVirtualList> WeakVirtualList = nullptr;
};
#endif

}  // namespace ImSlate  (segment 1: SX inner classes; reopened below after the global PrivateAccess macros)

namespace ImVirtualComboBox
{
GS_PRIVATEACCESS_FUNCTION(SWidget, Prepass_Internal, void(float))
GS_PRIVATEACCESS_FUNCTION(SWidget, SetDesiredSize, void(const FVector2D&))

void PrepassInternal(const TSharedRef<SWidget>& InWidget, float LayoutScaleMultiplier)
{
	PrivateAccess::Prepass_Internal(InWidget.Get(), LayoutScaleMultiplier);
}
void SetDesiredSize(SWidget* InWidget, const ImVec2& InSize)
{
	if (InSize.HasValidSize())
		PrivateAccess::SetDesiredSize(*InWidget, InSize);
}
}  // namespace ImVirtualComboBox

namespace ImSlate
{
//
void SImVirtualComboBox::Construct(const FArguments& InArgs)
{
	check(InArgs._ComboBoxStyle);
	ItemStyle = InArgs._ItemStyle;
	SearchBoxStyle = InArgs._SearchBoxStyle;
	SearchBoxHeight = InArgs._SearchBoxHeight;

	FComboSearchType FilterParam;
	FilterInfo.SetDynamicStruct(FilterParam);

	// Work out which values we should use based on whether we were given an override, or should use the style's version
	const FComboButtonStyle& OurComboButtonStyle = InArgs._ComboBoxStyle->ComboButtonStyle;
	const FButtonStyle* const OurButtonStyle = InArgs._ButtonStyle ? InArgs._ButtonStyle : &OurComboButtonStyle.ButtonStyle;
	PressedSound = InArgs._PressedSoundOverride.Get(InArgs._ComboBoxStyle->PressedSlateSound);
	SelectionChangeSound = InArgs._SelectionChangeSoundOverride.Get(InArgs._ComboBoxStyle->SelectionChangeSlateSound);

	MenuVerticalBox = SNew(SVerticalBox);
	//if (InArgs._bHasSearchBox)
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
				SAssignNew(MenuSearchBox, SXVirtualSearchBox)
				.Style(SearchBoxStyle)
				.SelectAllTextWhenFocused(true)
				.OnTextChanged(this, &SImVirtualComboBox::OnFilterTextChanged)
				.OnTextCommitted(this, &SImVirtualComboBox::OnFilterTextCommitted)
				.AddMetaData<FTagMetaData>(TEXT("Details.Search"))
			]
		];
	}
#if 0
	MenuVerticalBox->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[SAssignNew(VirtualList, ImSlate::SImSlateVirtualList)];
#else
	MenuVerticalBox->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		//SAssignNew(MouseEventBorder, SXMouseEventBorder)
		//.Content()
		//[
		SAssignNew(VirtualList, ImSlate::SImSlateVirtualList)
		//]
	];
	//MouseEventBorder->OnMouseUp = CreateWeakLambda(this, [this](int32 InIndex) {
	//	if (DataStore.IsValid())
	//	{
	//		DataStore->SetCurrentSelected(InIndex);
	//		if (!DataStore->IsMultiSelect())
	//		{
	//			this->SetIsOpen(false);
	//		}
	//	}
	//});

	VirtualList->SetOnMouseButtonUp(CreateWeakLambda(this, [this](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) -> FReply {
		if (!VirtualList.IsValid())
		{
			return FReply::Handled().PreventThrottling();
		}

		int32 CurrentIndex = -1;
		auto DataCount = VirtualList->GetDataCount();
		if (DataCount > 0)
		{
			auto CalcPos = VirtualList->GetVirtualPos() + VirtualList->GetCachedGeometry().AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).Y;
			CurrentIndex = VirtualList->DataIndexFromOffset(CalcPos);
#if WITH_EDITOR
			auto UpperIndex = VirtualList->UpperDataIndex(CalcPos);
			ensure(CurrentIndex <= UpperIndex);
#endif
		}

		if (CurrentIndex != -1 && DataStore.IsValid())
		{
			DataStore->SetCurrentSelected(CurrentIndex);
			if (!DataStore->IsMultiSelect())
			{
				this->SetIsOpen(false);
			}
			GetValueChangedRef() = true;
		}
		return FReply::Handled().PreventThrottling();
	}));

	//MouseEventBorder->WeakVirtualList = VirtualList;
#endif
	DataStore = InArgs._DataStore;
	VirtualList->SetData(DataStore);
	ImVirtualComboBox::SetDesiredSize(VirtualList.Get(), FVector2D(200.f, 200.f));

	TSharedRef<SWidget> ComboBoxMenuContent = SNew(SBox)
												.MaxDesiredHeight(InArgs._MaxListHeight)
												.Content()
												[
													MenuVerticalBox.ToSharedRef()
												];

	// Set up content
	//ButtonContent = InArgs._Content.Widget;
	//
	ButtonContentBoxText = ImSlate::ImFactoryCreate<UImTextBlock>();
	ButtonContentBoxText->SetText(FText::GetEmpty());

	//if (InArgs._Content.Widget == SNullWidget::NullWidget)
	{
		//SAssignNew(ButtonContent, STextBlock).Text(NSLOCTEXT("SComboBox", "ContentWarning", "No Content Provided")).ColorAndOpacity(FLinearColor::Red);
		SAssignNew(ButtonContent, SBorder)
		.Content()
		[
			SAssignNew(ButtonContentBox, SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				//SAssignNew(ButtonContentBoxText, STextBlock)
				//.Text(NSLOCTEXT("SComboBox", "ContentWarning", "No Content Provided"))
				//.ColorAndOpacity(FLinearColor::Red)
				ButtonContentBoxText.ToSharedRef()
			]
		];
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
	.OnMenuOpenChanged(this, &SImVirtualComboBox::OnMenuOpenChanged)
	.IsFocusable(InArgs._IsFocusable)
							/*.CollapseMenuOnParentFocus(InArgs._CollapseMenuOnParentFocus)*/);
	SetMenuContentWidgetToFocus(VirtualList);

#if defined(WITH_THROTTLE_MODIFY)
	bIgnoreThrottle = true;
#endif
}

void SImVirtualComboBox::OnMenuOpenChanged(bool bOpen)
{
	if (bOpen && VirtualList.IsValid())
	{
		//if (DataStore.IsValid())
		//	DataStore->ClearFilterMode(true);
		VirtualList->Update(-1, true);

		int32 CurrentSelectedIndex = GetCurrentSelectedIndex();
		if (CurrentSelectedIndex != -1)
			VirtualList->ScrollToItem(CurrentSelectedIndex);
	}

	//SetIsOpen(true);
	//GetCachedGeometry().GetAbsolutePosition();GetMouseMovePointsEx
}

void SImVirtualComboBox::SetDataStore(const TSharedPtr<ImSlate::FImListDataComboImpl>& InDataStore)
{
	DataStore = InDataStore;
	if (VirtualList.IsValid())
	{
		VirtualList->SetData(DataStore);
	}

	DataStore->SetRelateComboButton(StaticCastSharedRef<SImVirtualComboBox>(AsShared()));

	if (DataStore.IsValid())
	{
		SetEnableSearchBox(DataStore->IsEnableSearchBox());
	}
}

void SImVirtualComboBox::SetSearchBoxDisplay(bool bShow)
{
	if (MenuVerticalBox.IsValid())
	{
		if (bShow)
		{
			if (MenuVerticalBox->NumSlots() == 2)
				return;
			if (!MenuSearchBox.IsValid())
			{
				SAssignNew(MenuSearchBox, SXVirtualSearchBox)
				.Style(SearchBoxStyle)
				.SelectAllTextWhenFocused(true)
				.OnTextChanged(this, &SImVirtualComboBox::OnFilterTextChanged)
				.OnTextCommitted(this, &SImVirtualComboBox::OnFilterTextCommitted)
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

void SImVirtualComboBox::SetEnableSearchBox(bool bEnable)
{
	if (MenuSearchBox.IsValid())
	{
		MenuSearchBox->SetVisibility(bEnable ? EVisibility::Visible : EVisibility::Collapsed);
	}
}

void SImVirtualComboBox::SetCurrentSelectedIndex(int32 InIndex, bool bInSelected)
{
	if (!ButtonContentBoxText.IsValid())
		return;
	if (InIndex < 0)
	{
		if (DataStore.IsValid())
		{
			DataStore->ClearCurrentSelection();
		}
		ButtonContentBoxText->SetText(FText::FromString(TEXT("Multi Values")));
	}
	else if (VirtualList.IsValid())
	{
		VirtualList->Update(InIndex);
		if (DataStore.IsValid())
		{
			auto Selects = DataStore->GetOrignalCurrentSelectedIndexes();
			if (Selects.Num() == 1)
			{
				ButtonContentBoxText->SetText(DataStore->GetDataTextByOrignalIndex(Selects[0]));
			}
			else if (Selects.Num() == 0)
			{
				ButtonContentBoxText->SetText(FText::GetEmpty());
			}
			else
			{
				ButtonContentBoxText->SetText(FText::FromString(TEXT("Multi Selected")));
			}
		}
	}
}

int32 SImVirtualComboBox::GetCurrentSelectedIndex() const
{
	if (DataStore.IsValid())
	{
		auto Indexes = DataStore->GetCurrentSelectedIndexes();
		return Indexes.Num() > 0 ? Indexes[0] : -1;
	}

	return -1;
}

FReply SImVirtualComboBox::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return SComboButton::OnKeyDown(MyGeometry, InKeyEvent).PreventThrottling();
}

FReply SImVirtualComboBox::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return SWidget::OnMouseButtonDown(MyGeometry, MouseEvent).PreventThrottling();
}

void SImVirtualComboBox::OnFilterTextCommitted(const FText& InFilterText, ETextCommit::Type InCommitType)
{
	OnFilterTextChanged(InFilterText);
}

void SImVirtualComboBox::OnFilterTextChanged(const FText& InFilterText)
{
	if (DataStore.IsValid())
	{
		FComboSearchType FilterParam;
		FilterParam.FilterText = InFilterText;
		FilterInfo.SetDynamicStruct(FilterParam);
		//if (InFilterText.IsEmpty())
		//{
		//	DataStore->ClearFilterMode(true);
		//}
		//else
		{
			DataStore->StartFiltering(ImSlate::GetWorldChecked(), InFilterText.IsEmpty() ? FGMPStructUnion{} : FilterInfo);
		}
	}
}
}  // namespace ImSlate

TSharedRef<ImSlate::SImVirtualComboBox> UImVirtualComboButton::ConstructImWidget() const
{
	TSharedRef<STextBlock> TextWidgetRef = ImSlate::ImFactoryCreate<UImTextBlock>(this);
	TextWidgetRef->SetAutoWrapText(true);
	ReSetSearchBoxStyle();

	auto WidgetRef = SNew(ImSlate::SImVirtualComboBox)
						.bHasSearchBox(bHasSearchBox)
						.ComboBoxStyle(&ComboBoxStyle)
						.ButtonStyle(&ButtonStyle)
						.ItemStyle(&ItemStyle)
						.SearchBoxStyle(&SearchBoxStyle)
						.DataStore(nullptr)
						.IsFocusable(bIsFocusable)
						.Content()
						[
							TextWidgetRef
						];

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	WidgetRef->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif
	return WidgetRef;
}

void UImVirtualComboButton::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyComboBox.Reset();
}

TSharedRef<SWidget> UImVirtualComboButton::RebuildWidget()
{
	if (MyComboBox.IsValid())
		return MyComboBox.ToSharedRef();

	TSharedRef<STextBlock> WidgetRef = ImSlate::ImFactoryCreate<UImTextBlock>(this);
	WidgetRef->SetAutoWrapText(true);

	ReSetSearchBoxStyle();
	SAssignNew(MyComboBox, ImSlate::SImVirtualComboBox)
	.bHasSearchBox(bHasSearchBox)
	.ComboBoxStyle(&ComboBoxStyle)
	.ButtonStyle(&ButtonStyle)
	.ItemStyle(&ItemStyle)
	.SearchBoxStyle(&SearchBoxStyle)
	.DataStore(nullptr)
	.IsFocusable(bIsFocusable)
	.Content()
	[
		WidgetRef
	];
#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyComboBox->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif
	return MyComboBox.ToSharedRef();
}

void UImVirtualComboButton::ReSetSearchBoxStyle() const
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

