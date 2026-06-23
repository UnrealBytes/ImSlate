// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImSearchBox.h"
#include "ImSlateTemplate/ImEditableText.h"
#include "Framework/Application/SlateApplication.h"
#include "ProtectFieldAccessor.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#include "ImSlateTemplate/ImPopup.h"
#include "ImSlatePrivate.h"
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "SImSlateWindow.h"
namespace ImSlate
{
static const FButtonStyle& GetSuggestionButtonStyle()
{
	static FButtonStyle Style;
	static bool bInit = false;
	if (!bInit)
	{
		Style = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
		auto MakeBrush = [](const FLinearColor& Color) {
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Brush.TintColor = Color;
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			Brush.OutlineSettings.CornerRadii = FVector4(3, 3, 3, 3);
			return Brush;
		};
		Style.SetNormal(MakeBrush(FLinearColor(0.15f, 0.15f, 0.15f, 0.9f)));
		Style.SetHovered(MakeBrush(FLinearColor(0.25f, 0.25f, 0.25f, 0.95f)));
		Style.SetPressed(MakeBrush(FLinearColor(0.35f, 0.35f, 0.35f, 1.f)));
		bInit = true;
	}
	return Style;
}

static const FButtonStyle& GetSuggestionHighlightStyle()
{
	static FButtonStyle Style;
	static bool bInit = false;
	if (!bInit)
	{
		Style = GetSuggestionButtonStyle();
		auto MakeBrush = [](const FLinearColor& Color) {
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Brush.TintColor = Color;
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			Brush.OutlineSettings.CornerRadii = FVector4(3, 3, 3, 3);
			return Brush;
		};
		Style.SetNormal(MakeBrush(FLinearColor(0.2f, 0.35f, 0.6f, 1.f)));
		Style.SetHovered(MakeBrush(FLinearColor(0.25f, 0.4f, 0.65f, 1.f)));
		bInit = true;
	}
	return Style;
}

void SImSearchBox::SetKeyboardSuggestionProvider(FImSearchBoxSuggestionProvider InProvider)
{
	KeyboardSuggestionProvider = InProvider;
	// Forward to the inner editable so its focus-triggered keyboard Show() also
	// carries the provider (not just the keyboard-button path).
	if (EditText.IsValid())
		StaticCastSharedPtr<SImEditableText>(EditText)->SetVirtualKeyboardSuggestionProvider(InProvider);
}

void SImSearchBox::SetHistoryKey(const FString& InKey)
{
	HistoryKey = InKey;
	// CRUCIAL: this SearchBox pops the keyboard on FOCUS via the inner SImEditableText, not (only) via
	// the keyboard button. So the history key must reach the editable too, else its focus-Show() passes
	// an empty HistoryKey and the keyboard never records/shows history (no X). Mirrors the provider path.
	if (EditText.IsValid())
		StaticCastSharedPtr<SImEditableText>(EditText)->SetHistoryKey(InKey);
}

void SImSearchBox::Construct(const FArguments& InArgs)
{
	bUseInlineSuggestions = InArgs._bUseInlineSuggestions;
	bShowKeyboardButton = InArgs._bShowKeyboardButton;
	SuggestionList = SNew(SVerticalBox);

	auto MyEditText = SNew(SImEditableText)
		.Font(ImSlate::GetImSlateDefaultFont())
		.ClearKeyboardFocusOnCommit(false);

	GS_ACCESS_PROTECT(MyEditText, SEditableText, OnTextChangedCallback)->OnTextChangedCallback =
		FOnTextChanged::CreateSP(this, &SImSearchBox::OnTextChanged);
	GS_ACCESS_PROTECT(MyEditText, SEditableText, OnTextCommittedCallback)->OnTextCommittedCallback =
		FOnTextCommitted::CreateSP(this, &SImSearchBox::OnTextCommitted);

	static FSlateBrush EditBgBrush;
	static bool bInitBrush = false;
	if (!bInitBrush)
	{
		EditBgBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		EditBgBrush.TintColor = FLinearColor(0.05f, 0.05f, 0.05f, 0.8f);
		EditBgBrush.OutlineSettings.Color = FLinearColor(0.3f, 0.3f, 0.3f, 1.f);
		EditBgBrush.OutlineSettings.Width = 1.f;
		EditBgBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		EditBgBrush.OutlineSettings.CornerRadii = FVector4(4, 4, 4, 4);
		bInitBrush = true;
	}
	MyEditText->SetBorderImage(&EditBgBrush);

	EditText = MyEditText;
	// If a provider / history key was already set before Construct, forward it to the inner editable now.
	if (KeyboardSuggestionProvider)
		MyEditText->SetVirtualKeyboardSuggestionProvider(KeyboardSuggestionProvider);
	if (!HistoryKey.IsEmpty())
		MyEditText->SetHistoryKey(HistoryKey);

	TSharedRef<SHorizontalBox> EditRow = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			MyEditText
		];

	if (bShowKeyboardButton)
	{
		TSharedPtr<SButton> KbBtn;
		EditRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(4.f, 0.f))
		[
			SAssignNew(KbBtn, SButton)
			.ClickMethod(EButtonClickMethod::MouseDown)
			.OnClicked_Lambda([this]() {
				UE_LOG(LogImSlate, Log, TEXT("Keyboard button clicked, VK instance: %s"), ImSlate::SImSlateVirtualKeyboard::Get().IsValid() ? TEXT("valid") : TEXT("null"));
				if (auto Kb = ImSlate::SImSlateVirtualKeyboard::Get())
				{
					ImSlate::FVirtualKeyboardShowParams Params;
					Params.InitialText = GetText().ToString();
					Params.OnTextChanged = [this](const FString& T) { SetText(FText::FromString(T)); bNeedsSuggestionRefresh = true; };
					Params.CommitCallback = [this](const FString& T, ETextCommit::Type Type) {
						if (Type != ETextCommit::OnCleared)
						{
							CommittedText = T;
							bCommitted = true;
							SetText(FText::FromString(T));
							ClearSuggestions();
						}
					};
					Params.SuggestionProvider = KeyboardSuggestionProvider;
					Params.HistoryKey = HistoryKey;  // keyboard uses its own persistent history (rows get X)
					Kb->Show(Params);
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("\x2328")))
				.Font(ImSlate::GetImSlateDefaultFont())
			]
		];
		KeyboardButtonWidget = KbBtn;
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			EditRow
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(SuggestionContainer, SBox)
			.MaxDesiredHeight(250.f)
			.Visibility(EVisibility::Collapsed)
		]
	];
}

bool SImSearchBox::IsVirtualKeyboardActiveForMe() const
{
	if (!EditText.IsValid()) return false;
	if (auto Kb = ImSlate::SImSlateVirtualKeyboard::Get())
		return Kb->IsShowing() && Kb->IsOwnedBy(EditText.Get());
	return false;
}

void SImSearchBox::SetText(const FText& InText)
{
	if (EditText) EditText->SetText(InText);
}

FText SImSearchBox::GetText() const
{
	return EditText ? EditText->GetText() : FText::GetEmpty();
}

void SImSearchBox::ShowSuggestions(bool bShow)
{
	if (bShow)
	{
		// Detect mode once when opening
		bool bShouldInline = bUseInlineSuggestions;
		if (GImSlate && GImSlate->CurrentWindow)
			bShouldInline = GImSlate->CurrentWindow->IsViewportGame();

		// If switching mode while visible, close the old one first
		if (bSuggestionsVisible && bShouldInline != bUseInlineSuggestions)
		{
			if (bUseInlineSuggestions) ShowInline(false);
			else ShowFloating(false);
		}
		bUseInlineSuggestions = bShouldInline;
		if (KeyboardButtonWidget.IsValid())
			KeyboardButtonWidget->SetVisibility(bShouldInline ? EVisibility::Visible : EVisibility::Collapsed);
	}

	if (bUseInlineSuggestions)
		ShowInline(bShow);
	else
		ShowFloating(bShow);
}

void SImSearchBox::ShowInline(bool bShow)
{
	if (bShow && SuggestionContainer.IsValid() && SuggestionContainer->GetChildren()->Num() == 0)
	{
		SuggestionContainer->SetContent(
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SuggestionList.ToSharedRef()
			]);
	}
	bSuggestionsVisible = bShow;
	if (SuggestionContainer.IsValid())
		SuggestionContainer->SetVisibility(bShow ? EVisibility::Visible : EVisibility::Collapsed);

	// Auto-show virtual keyboard in viewport mode
	if (bShow && bShowKeyboardButton)
	{
		if (auto Kb = ImSlate::SImSlateVirtualKeyboard::Get())
		{
			if (!Kb->IsShowing())
			{
				ImSlate::FVirtualKeyboardShowParams Params;
				Params.InitialText = GetText().ToString();
				Params.OnTextChanged = [this](const FString& T) { SetText(FText::FromString(T)); bNeedsSuggestionRefresh = true; };
				Params.CommitCallback = [this](const FString& T, ETextCommit::Type Type) {
					if (Type != ETextCommit::OnCleared)
					{
						CommittedText = T;
						bCommitted = true;
						SetText(FText::FromString(T));
						ClearSuggestions();
					}
				};
				Params.SuggestionProvider = KeyboardSuggestionProvider;
				Params.HistoryKey = HistoryKey;  // keyboard uses its own persistent history (rows get X)
				Kb->Show(Params);
			}
		}
	}
}

void SImSearchBox::ShowFloating(bool bShow)
{
	if (bShow && !bSuggestionsVisible)
	{
		if (!SuggestionWindow.IsValid())
		{
			TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
			if (!ParentWindow.IsValid()) { ShowInline(true); return; }

			SuggestionWindow = SNew(SWindow)
				.Type(EWindowType::Menu)
				.IsPopupWindow(true)
				.SizingRule(ESizingRule::Autosized)
				.AutoCenter(EAutoCenter::None)
				.SupportsTransparency(EWindowTransparency::PerWindow)
				.FocusWhenFirstShown(false)
				.ActivationPolicy(EWindowActivationPolicy::Always)
				.IsTopmostWindow(true)
				.HasCloseButton(false)
				.SupportsMinimize(false)
				.SupportsMaximize(false)
				.CreateTitleBar(false)
				[
					SNew(SBox)
					.MaxDesiredHeight(250.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SuggestionList.ToSharedRef()
						]
					]
				];

			FSlateApplication::Get().AddWindowAsNativeChild(SuggestionWindow.ToSharedRef(), ParentWindow.ToSharedRef(), true);
			SetWindowActivationPolicyNever(*SuggestionWindow);
		}

		FVector2D AbsPos = GetCachedGeometry().GetAbsolutePosition();
		FVector2D AbsSize = GetCachedGeometry().GetAbsoluteSize();
		SuggestionWindow->Resize(FVector2D(AbsSize.X, SuggestionWindow->GetSizeInScreen().Y));
		SuggestionWindow->MoveWindowTo(AbsPos + FVector2D(0, AbsSize.Y));
		SuggestionWindow->ShowWindow();
		bSuggestionsVisible = true;

		if (EditText.IsValid())
			FSlateApplication::Get().SetKeyboardFocus(EditText);

		if (!ClickOutsideHandle.IsValid())
		{
			ClickOutsideHandle = FSlateApplication::Get().GetPopupSupport().RegisterClickNotification(
				SuggestionWindow.ToSharedRef(),
				FOnClickedOutside::CreateLambda([this]() {
					if (GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos()))
						return;
					ClearSuggestions();
				}));
		}
	}
	else if (!bShow && bSuggestionsVisible)
	{
		bSuggestionsVisible = false;
		if (ClickOutsideHandle.IsValid())
		{
			FSlateApplication::Get().GetPopupSupport().UnregisterClickNotification(ClickOutsideHandle);
			ClickOutsideHandle.Reset();
		}
		if (SuggestionWindow.IsValid())
			SuggestionWindow->HideWindow();
		if (EditText.IsValid())
			FSlateApplication::Get().SetKeyboardFocus(EditText);
	}
}

void SImSearchBox::SetSuggestions(const TArray<FString>& InItems)
{
	if (!SuggestionList) return;
	SuggestionList->ClearChildren();
	SuggestionButtons.Reset();
	CurrentSuggestions = InItems;
	HighlightIndex = -1;
	TextBeforeNavigate.Reset();

	for (int32 i = 0; i < InItems.Num(); ++i)
	{
		FString ItemCopy = InItems[i];
		TSharedPtr<SButton> Btn;
		SuggestionList->AddSlot()
		.AutoHeight()
		[
			SAssignNew(Btn, SButton)
			.ButtonStyle(&GetSuggestionButtonStyle())
			.ClickMethod(EButtonClickMethod::MouseDown)
			.OnClicked_Lambda([this, ItemCopy]() {
				OnSuggestionSelected(ItemCopy);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(ItemCopy))
				.Font(ImSlate::GetImSlateDefaultFont())
				.ColorAndOpacity(FLinearColor::White)
			]
		];
		SuggestionButtons.Add(Btn);
	}

	ShowSuggestions(InItems.Num() > 0);
}

void SImSearchBox::SetSuggestions(const TArray<FString>& InItems, int32 HistoryCount, TFunction<void(const FString&)> OnDeleteHistory)
{
	if (!SuggestionList) return;
	SuggestionList->ClearChildren();
	SuggestionButtons.Reset();
	CurrentSuggestions = InItems;
	LastHistoryCount = HistoryCount;       // remember for Ctrl+Backspace delete-history
	DeleteHistoryFn = OnDeleteHistory;
	HighlightIndex = -1;
	TextBeforeNavigate.Reset();

	for (int32 i = 0; i < InItems.Num(); ++i)
	{
		FString ItemCopy = InItems[i];
		bool bIsHistory = i < HistoryCount;
		TSharedPtr<SButton> Btn;

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.f, 0.f, 0.f, 0.f))   // small left inset for the label; row itself is flush
			[
				// No ⏰ prefix (glyph missing from font → ◇? tofu); history shown via the trailing X.
				SNew(STextBlock)
				.Text(FText::FromString(ItemCopy))
				.Font(ImSlate::GetImSlateDefaultFont())
				.ColorAndOpacity(bIsHistory ? FLinearColor(0.7f, 0.85f, 1.f) : FLinearColor::White)
			];

		if (bIsHistory && OnDeleteHistory)
		{
			FString DelCopy = ItemCopy;
			// X delete button: flush to the row's RIGHT edge, filling full height. Uses a TRANSPARENT
			// flat style (no rounded box of its own) so it doesn't draw a second inset color block —
			// the glyph sits right in the top/right/bottom corner of the row.
			static FButtonStyle XStyle = []() {
				FButtonStyle S = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
				FSlateColorBrush Transparent(FLinearColor(0, 0, 0, 0));
				FSlateColorBrush Hover(FLinearColor(1, 1, 1, 0.12f));
				S.SetNormal(Transparent);
				S.SetHovered(Hover);
				S.SetPressed(Hover);
				S.SetNormalPadding(FMargin(0.f));
				S.SetPressedPadding(FMargin(0.f));
				return S;
			}();
			Row->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(0.f))
			[
				SNew(SButton)
				.ButtonStyle(&XStyle)
				.ContentPadding(FMargin(10.f, 0.f))   // click width; no vertical padding → fills full height
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Center)
				.ClickMethod(EButtonClickMethod::MouseDown)
				.OnClicked_Lambda([this, DelCopy, OnDeleteHistory]() {
					OnDeleteHistory(DelCopy);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("\x2715")))
					.Font(ImSlate::GetImSlateDefaultFont())
					.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))
					.Justification(ETextJustify::Center)
				]
			];
		}

		SuggestionList->AddSlot()
		.AutoHeight()
		[
			SAssignNew(Btn, SButton)
			.ButtonStyle(&GetSuggestionButtonStyle())
			.ContentPadding(FMargin(0.f))   // row fills the button so the X can sit flush in the corner
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.ClickMethod(EButtonClickMethod::MouseDown)
			.OnClicked_Lambda([this, ItemCopy]() {
				OnSuggestionSelected(ItemCopy);
				return FReply::Handled();
			})
			[
				Row
			]
		];
		SuggestionButtons.Add(Btn);
	}

	ShowSuggestions(InItems.Num() > 0);
}

void SImSearchBox::ClearSuggestions()
{
	if (SuggestionList) SuggestionList->ClearChildren();
	SuggestionButtons.Reset();
	CurrentSuggestions.Reset();
	HighlightIndex = -1;
	TextBeforeNavigate.Reset();
	ShowSuggestions(false);
}

FString SImSearchBox::ConsumeCommit()
{
	bCommitted = false;
	return MoveTemp(CommittedText);
}

FReply SImSearchBox::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bSuggestionsVisible && CurrentSuggestions.Num() > 0)
	{
		if (InKeyEvent.GetKey() == EKeys::Down)
		{
			if (HighlightIndex < 0)
				TextBeforeNavigate = EditText.IsValid() ? EditText->GetText().ToString() : FString();
			UpdateHighlight(FMath::Min(HighlightIndex + 1, CurrentSuggestions.Num() - 1));
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::Up)
		{
			UpdateHighlight(HighlightIndex - 1);
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			if (HighlightIndex >= 0 && EditText.IsValid())
				EditText->SetText(FText::FromString(TextBeforeNavigate));
			ClearSuggestions();
			return FReply::Handled();
		}
		// Ctrl+Backspace deletes the highlighted HISTORY entry (only history rows are deletable).
		if (InKeyEvent.GetKey() == EKeys::BackSpace && InKeyEvent.IsControlDown())
		{
			if (HighlightIndex >= 0 && HighlightIndex < LastHistoryCount && DeleteHistoryFn
				&& CurrentSuggestions.IsValidIndex(HighlightIndex))
			{
				DeleteHistoryFn(CurrentSuggestions[HighlightIndex]);  // rebuilds the list (SetSuggestions)
				return FReply::Handled();
			}
		}
	}
	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

void SImSearchBox::UpdateHighlight(int32 NewIndex)
{
	// Preview-only highlight (like virtual keyboard): Up/Down only moves the highlight,
	// it does NOT write into the edit. The edit is committed only on Enter (OnTextCommitted)
	// or click (OnSuggestionSelected). This avoids navigation changing the live text/value.
	if (SuggestionButtons.IsValidIndex(HighlightIndex))
		SuggestionButtons[HighlightIndex]->SetButtonStyle(&GetSuggestionButtonStyle());

	HighlightIndex = NewIndex;

	if (SuggestionButtons.IsValidIndex(HighlightIndex))
		SuggestionButtons[HighlightIndex]->SetButtonStyle(&GetSuggestionHighlightStyle());
}

void SImSearchBox::OnTextChanged(const FText& NewText)
{
	bNeedsSuggestionRefresh = true;
}

void SImSearchBox::OnTextCommitted(const FText& Text, ETextCommit::Type Type)
{
	if (Type != ETextCommit::OnCleared)
	{
		if (HighlightIndex >= 0 && CurrentSuggestions.IsValidIndex(HighlightIndex))
		{
			// Enter with a highlighted suggestion → commit the highlight and sync it to the edit
			CommittedText = CurrentSuggestions[HighlightIndex];
			if (EditText.IsValid())
				EditText->SetText(FText::FromString(CommittedText));
		}
		else
		{
			CommittedText = Text.ToString();
		}
		bCommitted = true;
		ClearSuggestions();
	}
}

void SImSearchBox::OnSuggestionSelected(const FString& Item)
{
	CommittedText = Item;
	bCommitted = true;
	if (EditText) EditText->SetText(FText::FromString(Item));
	ClearSuggestions();
}
}

// UImSearchBox
TSharedRef<ImSlate::SImSearchBox> UImSearchBox::ConstructImWidget() const
{
	return SNew(ImSlate::SImSearchBox);
}

TSharedRef<SWidget> UImSearchBox::RebuildWidget()
{
	MyWidget = SNew(ImSlate::SImSearchBox);
	return MyWidget.ToSharedRef();
}

void UImSearchBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyWidget.Reset();
}
