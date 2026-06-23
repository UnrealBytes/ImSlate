// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImCheckBox.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

namespace ImSlate
{

// Self-painted tri-state check mark, used instead of the default themed check images (which are
// small/grey and make Checked vs Unchecked vs Undetermined hard to tell apart). Draws:
//   Unchecked    → empty rounded box (grey outline)
//   Checked      → solid blue box + white tick
//   Undetermined → grey box + horizontal dash
// State is read from the owning SImCheckBox via a getter.
class SImCheckMark : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SImCheckMark)
		: _AccentColor(FLinearColor(0.10f, 0.45f, 0.90f, 1.f))  // default blue
		{}
		SLATE_ARGUMENT(TFunction<ECheckBoxState()>, StateGetter)
		SLATE_ARGUMENT(FLinearColor, AccentColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		StateGetter = InArgs._StateGetter;
		AccentColor = InArgs._AccentColor;
		SetCanTick(false);
		// The mark reads its state via a getter in OnPaint and has NO TAttribute dependency, so a
		// state change (click) doesn't invalidate it on its own. Make it volatile so it repaints
		// every frame and always reflects the current tri-state. (It's a tiny leaf — negligible cost.)
		ForceVolatile(true);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const float S = 18.f * ImSlate::GetImSlateEffectiveScale();
		return FVector2D(S, S);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull,
		FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InStyle, bool bParentEnabled) const override
	{
		const ECheckBoxState State = StateGetter ? StateGetter() : ECheckBoxState::Unchecked;
		const FVector2D Sz = G.GetLocalSize();
		const float Side = FMath::Min(Sz.X, Sz.Y);
		const FVector2D TL((Sz.X - Side) * 0.5f, (Sz.Y - Side) * 0.5f);  // center the square
		const auto PG = G.ToPaintGeometry();

		// Strongly-distinct tri-state colours that all read clearly on the light-blue panel bg:
		//   Unchecked    → SOLID dark-grey box (was transparent → invisible on light bg)
		//   Checked      → accent fill (blue default / green for enable toggles) + white tick
		//   Undetermined → orange fill + white dash (clearly different from both above)
		// Softer, less-saturated tri-state palette.
		//   Unchecked    → empty: faint translucent fill (just an outlined box, no solid colour)
		//   Checked      → soft accent fill + white tick
		//   Undetermined → soft amber fill + white dash
		const FLinearColor UncheckedFill(0.25f, 0.25f, 0.28f, 0.35f);  // faint → reads as "empty box"
		const FLinearColor SoftAccent(AccentColor.R * 0.8f, AccentColor.G * 0.8f, AccentColor.B * 0.8f, 1.f);  // dim RGB, keep opaque
		const FLinearColor SoftAmber(0.85f, 0.62f, 0.30f, 1.f);        // muted amber, not bright orange
		const float LineW = 2.f * ImSlate::GetImSlateEffectiveScale();

		const FLinearColor FillColor = (State == ECheckBoxState::Checked)
			? SoftAccent
			: (State == ECheckBoxState::Undetermined)
				? SoftAmber
				: UncheckedFill;

		// Use a STATIC white RoundedBox brush and pass the per-state colour via MakeBox's InTint
		// (last arg). Building a fresh FSlateRoundedBoxBrush each paint and relying on its internal
		// TintColor did NOT update on screen (the brush's resource handle is rebuilt every frame and
		// the colour didn't take) — tinting a stable brush works, same as SImFoldLine does it.
		static FSlateBrush BoxBrush = []() {
			FSlateBrush B;
			B.DrawAs = ESlateBrushDrawType::RoundedBox;
			B.TintColor = FLinearColor::White;  // tinted per-call via InTint
			B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			B.OutlineSettings.CornerRadii = FVector4(3.f, 3.f, 3.f, 3.f);
			B.OutlineSettings.Color = FLinearColor(0.7f, 0.7f, 0.7f, 1.f);
			B.OutlineSettings.Width = 1.f;
			return B;
		}();

		auto BoxGeom = G.MakeChild(FVector2f(Side, Side), FSlateLayoutTransform(FVector2f(TL))).ToPaintGeometry();
		FSlateDrawElement::MakeBox(Out, LayerId, BoxGeom, &BoxBrush, ESlateDrawEffect::None, FillColor);

		// Inner glyph on top of the box.
		if (State == ECheckBoxState::Checked)
		{
			// White tick.
			const float x0 = TL.X + Side * 0.24f, y0 = TL.Y + Side * 0.52f;
			const float x1 = TL.X + Side * 0.42f, y1 = TL.Y + Side * 0.72f;
			const float x2 = TL.X + Side * 0.76f, y2 = TL.Y + Side * 0.30f;
			FSlateDrawElement::MakeLines(Out, LayerId + 1, PG,
				{FVector2D(x0, y0), FVector2D(x1, y1), FVector2D(x2, y2)},
				ESlateDrawEffect::None, FLinearColor::White, true, LineW);
		}
		else if (State == ECheckBoxState::Undetermined)
		{
			// White horizontal dash.
			const float y = TL.Y + Side * 0.5f;
			FSlateDrawElement::MakeLines(Out, LayerId + 1, PG,
				{FVector2D(TL.X + Side * 0.24f, y), FVector2D(TL.X + Side * 0.76f, y)},
				ESlateDrawEffect::None, FLinearColor::White, true, LineW);
		}
		return LayerId + 2;
	}

private:
	TFunction<ECheckBoxState()> StateGetter;
	FLinearColor AccentColor = FLinearColor(0.10f, 0.45f, 0.90f, 1.f);
};

SImCheckBox::SImCheckBox()
{
}

void SImCheckBox::Construct(const FArguments& InArgs)
{
	check(InArgs._Style != nullptr);
	Style = InArgs._Style;

	UncheckedImage = InArgs._UncheckedImage;
	UncheckedHoveredImage = InArgs._UncheckedHoveredImage;
	UncheckedPressedImage = InArgs._UncheckedPressedImage;

	CheckedImage = InArgs._CheckedImage;
	CheckedHoveredImage = InArgs._CheckedHoveredImage;
	CheckedPressedImage = InArgs._CheckedPressedImage;

	UndeterminedImage = InArgs._UndeterminedImage;
	UndeterminedHoveredImage = InArgs._UndeterminedHoveredImage;
	UndeterminedPressedImage = InArgs._UndeterminedPressedImage;

	PaddingOverride = InArgs._Padding;
	ForegroundColorOverride = InArgs._ForegroundColor;
	BorderBackgroundColorOverride = InArgs._BorderBackgroundColor;
	CheckBoxTypeOverride = InArgs._Type;

	HorizontalAlignment = InArgs._HAlign;
	bCheckBoxContentUsesAutoWidth = InArgs._CheckBoxContentUsesAutoWidth;

	bIsPressed = false;
	bIsFocusable = InArgs._IsFocusable;

	BuildCheckBox(InArgs._Content.Widget);

	IsCheckboxChecked = InArgs._IsChecked;
	OnCheckStateChanged = InArgs._OnCheckStateChanged;

	ClickMethod = InArgs._ClickMethod;
	TouchMethod = InArgs._TouchMethod;
	PressMethod = InArgs._PressMethod;

	OnGetMenuContent = InArgs._OnGetMenuContent;

	HoveredSound = InArgs._HoveredSoundOverride.Get(InArgs._Style->HoveredSlateSound);
	CheckedSound = InArgs._CheckedSoundOverride.Get(InArgs._Style->CheckedSlateSound);
	UncheckedSound = InArgs._UncheckedSoundOverride.Get(InArgs._Style->UncheckedSlateSound);
}

FReply SImCheckBox::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = SCheckBox::OnMouseButtonUp(MyGeometry, MouseEvent);
	// The base may have toggled the state here; repaint so the self-painted mark updates.
	Invalidate(EInvalidateWidgetReason::Paint);
	return Reply;
}

FReply SImCheckBox::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	FReply Reply = SCheckBox::OnTouchEnded(MyGeometry, InTouchEvent);
	Invalidate(EInvalidateWidgetReason::Paint);
	return Reply;
}

void SImCheckBox::SetStyle(const FCheckBoxStyle* InStyle, const FImCheckBoxExtraStyle* InExtraStyle)
{
	Style = InStyle;

	if (Style == nullptr)
	{
		FArguments Defaults;
		Style = Defaults._Style;
	}

	check(Style);

	ExtraStyle = InExtraStyle;
	check(ExtraStyle);

	BuildCheckBox(ContentContainer->GetContent());
}

void SImCheckBox::BuildCheckBox(TSharedRef<SWidget> InContent)
{
	if (ContentContainer.IsValid())
	{
		ContentContainer->SetContent(SNullWidget::NullWidget);
	}

	ESlateCheckBoxType::Type CheckBoxType = OnGetCheckBoxType();

	if (CheckBoxType == ESlateCheckBoxType::CheckBox)
	{
		// Check boxes use a separate check button to the side of the user's content (often, a text label or icon.)
		SHorizontalBox::FSlot* ContentSlot;
		this->ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(18.f * ImSlate::GetImSlateEffectiveScale())
				.HeightOverride(18.f * ImSlate::GetImSlateEffectiveScale())
				[
					// Self-painted tri-state mark (clearer than the default themed check images).
					SNew(SImCheckMark)
					.StateGetter([this]() { return IsCheckboxChecked.Get(); })
					.AccentColor(CheckAccentColor)
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(TAttribute<FMargin>(this, &SImCheckBox::OnGetPadding))
			.VAlign(VAlign_Center)
			.Expose(ContentSlot)
			[
				SAssignNew(ContentContainer, SBorder)
				.BorderImage(FStyleDefaults::GetNoBrush())
				.Padding(0.0f)
				[
					InContent
				]
			]
		];
		if (bCheckBoxContentUsesAutoWidth)
		{
#if UE_5_00_OR_LATER
			ContentSlot->SetAutoWidth();
#else
			ContentSlot->AutoWidth();
#endif
		}
	}
	else if (ensure(CheckBoxType == ESlateCheckBoxType::ToggleButton))
	{
		// Toggle buttons have a visual appearance that is similar to a Slate button
		this->ChildSlot
		[
			SAssignNew(ContentContainer, SBorder)
			.BorderImage(this, &SImCheckBox::OnGetCheckImage)
			.Padding(this, &SImCheckBox::OnGetPadding)
#if ENGINE_MAJOR_VERSION >= 5
			.ColorAndOpacity(this, &SImCheckBox::GetColorAndOpacity)
#else
			.ForegroundColor(this, &SImCheckBox::OnGetForegroundColor)
#endif
			.BorderBackgroundColor(this, &SImCheckBox::OnGetBorderBackgroundColor)
			.ShowEffectWhenDisabled(false)
			.HAlign(HorizontalAlignment)
			[
				InContent
			]
		];
	}
}

const FSlateBrush* SImCheckBox::OnGetCheckImage() const
{
	ECheckBoxState State = IsCheckboxChecked.Get();

	const FSlateBrush* ImageToUse = nullptr;
	switch (State)
	{
		case ECheckBoxState::Unchecked:
			if (IsEnabled())
			{
				if (IsPressed())
				{
					ImageToUse = GetUncheckedPressedImage();
				}
				else if (IsHovered())
				{
					ImageToUse = GetUncheckedHoveredImage();
				}
				else if (IsFocused())
				{
					ImageToUse = &ExtraStyle->FocusedUncheckedImage;
				}
				else
				{
					ImageToUse = GetUncheckedImage();
				}
			}
			else
			{
				ImageToUse = &ExtraStyle->DisabledUncheckedImage;
			}
			break;

		case ECheckBoxState::Checked:
			if (IsEnabled())
			{
				if (IsPressed())
				{
					ImageToUse = GetCheckedPressedImage();
				}
				else if (IsHovered())
				{
					ImageToUse = GetCheckedHoveredImage();
				}
				else if (IsFocused())
				{
					ImageToUse = &ExtraStyle->FocusedCheckedImage;
				}
				else
				{
					ImageToUse = GetCheckedImage();
				}
			}
			else
			{
				ImageToUse = &ExtraStyle->DisabledCheckedImage;
			}
			break;
		case ECheckBoxState::Undetermined:
			if (IsEnabled())
			{
				if (IsPressed())
				{
					ImageToUse = GetUndeterminedPressedImage();
				}
				else if (IsHovered())
				{
					ImageToUse = GetUndeterminedHoveredImage();
				}
				else if (IsFocused())
				{
					ImageToUse = &ExtraStyle->FocusedUndeterminedImage;
				}
				else
				{
					ImageToUse = GetUndeterminedImage();
				}
			}
			else
			{
				ImageToUse = &ExtraStyle->DisabledUndeterminedImage;
			}
		default:
			break;
	}

	return ImageToUse;
}

void SImCheckBox::SetOnCheckStateChanged(FOnCheckStateChanged InDelegate)
{
	OnCheckStateChanged = InDelegate;
}
}  // namespace ImSlate

TSharedRef<ImSlate::SImCheckBox> UImCheckBox::ConstructImWidget() const
{
	auto MyStealCheckBox = SNew(ImSlate::SImCheckBox)
							//.OnCheckStateChanged(this, &UImCheckBox::SlateOnCheckStateChangedCallback);
							//.OnCheckStateChanged(BIND_UOBJECT_DELEGATE(FOnCheckStateChanged, SlateOnCheckStateChangedCallback))
							.Style(&GetWidgetStyle())
							.HAlign(HorizontalAlignment)
							.ClickMethod(GetClickMethod())
							.TouchMethod(GetTouchMethod())
							.PressMethod(GetPressMethod())
							.IsFocusable(GetIsFocusable());

	// UWidget

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealCheckBox->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealCheckBox->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealCheckBox->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealCheckBox->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealCheckBox->SetClipping(GetClipping());

	MyStealCheckBox->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealCheckBox->ForceVolatile(bIsVolatile);
	MyStealCheckBox->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealCheckBox->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealCheckBox->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealCheckBox->SetRenderTransformPivot(GetRenderTransformPivot());

	// 	if (ToolTipWidgetDelegate.IsBound() && !IsDesignTime())
	// 	{
	// 		TSharedRef<ImSlate::FDelegateToolTip> ToolTip = MakeShared<ImSlate::FDelegateToolTip>();
	// 		ToolTip->ToolTipWidgetDelegate = ToolTipWidgetDelegate;
	// 		MyStealEditableText->SetToolTip(GetToolTip());
	// 	}
	// 	else if (ToolTipWidget != nullptr)
	// 	{
	// 		TSharedRef<SToolTip> ToolTip = SNew(SToolTip)
	// 										.TextMargin(FMargin(0))
	// 										.BorderImage(nullptr)
	// 										[
	// 											ToolTipWidget->TakeWidget()
	// 										];
	//
	// 		MyStealEditableText->SetToolTip(GetToolTip());
	// 	}
	// 	else if (!ToolTipText.IsEmpty() || ToolTipTextDelegate.IsBound())
	// 	{
	// 		MyStealEditableText->SetToolTipText(PROPERTY_BINDING(FText, ToolTipText));
	// 	}

	// UWidget End

	MyStealCheckBox->SetStyle(&GetWidgetStyle(), &ExtraStyle);
	MyStealCheckBox->SetIsChecked(GetCheckedState());


	return MyStealCheckBox;
}

void UImCheckBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyImCheckbox.Reset();
}

TSharedRef<SWidget> UImCheckBox::RebuildWidget()
{
	MyImCheckbox = SNew(ImSlate::SImCheckBox)
					.Style(&GetWidgetStyle())
					.HAlign(HorizontalAlignment)
					.ClickMethod(GetClickMethod())
					.TouchMethod(GetTouchMethod())
					.PressMethod(GetPressMethod())
					.IsFocusable(GetIsFocusable());

	if (GetChildrenCount() > 0)
	{
		MyImCheckbox->SetContent(GetContentSlot()->Content ? GetContentSlot()->Content->TakeWidget() : SNullWidget::NullWidget);
	}
	MyImCheckbox->SetOnCheckStateChanged(BIND_UOBJECT_DELEGATE(FOnCheckStateChanged, SlateOnCheckStateChangedCallback));

	MyCheckbox = MyImCheckbox;

	return MyImCheckbox.ToSharedRef();
}

void UImCheckBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	MyImCheckbox->SetStyle(&GetWidgetStyle(), &ExtraStyle);
}

