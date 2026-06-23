// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "Slate.h"
#include "SlateCore.h"

#include "Components/Widget.h"
#include "Delegates/DelegateCombinations.h"
#include "Fonts/SlateFontInfo.h"
#include "ImSlateFactory.h"
#include "InputCoreTypes.h"
#include "Layout/Margin.h"
#include "Math/NumericLimits.h"
#include "Misc/Attribute.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/Input/SEditableText.h"
#include "ImSlateTemplate/ImEditableText.h"  // SImEditableText (self-rendered virtual keyboard on focus)
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

#include "ImNumericWidget.generated.h"

namespace ImSlate {
/**
 * 
 */
template<typename NumericType>
class IMSLATE_API SImNumericWidget : public SCompoundWidget
{
public:
	static const FLinearColor RedLabelBackgroundColor;
	static const FLinearColor GreenLabelBackgroundColor;
	static const FLinearColor BlueLabelBackgroundColor;
	static const FText DefaultUndeterminedString;

	/** Notification for numeric value change */
	DECLARE_DELEGATE_OneParam(FOnValueChanged, NumericType /*NewValue*/);

	/** Notification for numeric value committed */
	DECLARE_DELEGATE_TwoParams(FOnValueCommitted, NumericType /*NewValue*/, ETextCommit::Type /*CommitType*/);

	/** Notification for change of undetermined values */
	DECLARE_DELEGATE_OneParam(FOnUndeterminedValueChanged, FText /*NewValue*/);

	/** Notification for committing undetermined values */
	DECLARE_DELEGATE_TwoParams(FOnUndeterminedValueCommitted, FText /*NewValue*/, ETextCommit::Type /*CommitType*/);

	/** Notification when the max/min spinner values are changed (only apply if SupportDynamicSliderMaxValue or SupportDynamicSliderMinValue are true) */
	DECLARE_DELEGATE_FourParams(FOnDynamicSliderMinMaxValueChanged, NumericType, TWeakPtr<SWidget>, bool, bool);

public:
	SLATE_BEGIN_ARGS(SImNumericWidget)
		: _Label()
		, _LabelVAlign(VAlign_Fill)
		, _LabelPadding(FMargin(3, 0))
		, _UndeterminedString(DefaultUndeterminedString)
		, _BorderForegroundColor(FCoreStyle::Get().GetSlateColor("InvertedForeground"))
		, _BorderBackgroundColor(FLinearColor::White)
		, _EditableTextBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
		, _MinDesiredValueWidth(0)
		, _AllowSpin(false)
		, _Delta(0)
		, _ShiftMultiplier(10.f)
		, _CtrlMultiplier(1.f)
		, _SupportDynamicSliderMaxValue(false)
		, _SupportDynamicSliderMinValue(false)
		, _MinSliderValue(0)
		, _MaxSliderValue(100)
		, _MinValue(TNumericLimits<NumericType>::Lowest())
		, _MaxValue(TNumericLimits<NumericType>::Max())
		, _SliderExponent(1.f)
		, _MinFractionalDigits(6)
		, _MaxFractionalDigits(6)
	{
	}

	/** The value that should be displayed.  This value is optional in the case where a value cannot be determined */
	SLATE_ATTRIBUTE(TOptional<NumericType>, Value)

	/** Slot for this button's content (optional) */
	SLATE_NAMED_SLOT(FArguments, Label)
	/** Vertical alignment of the label content */
	SLATE_ARGUMENT(EVerticalAlignment, LabelVAlign)
	/** Padding around the label content */
	SLATE_ARGUMENT(FMargin, LabelPadding)

	/** The string to display if the value cannot be determined */
	SLATE_ARGUMENT(FText, UndeterminedString)
	/** Border Foreground Color */
	SLATE_ARGUMENT(FSlateColor, BorderForegroundColor)
	/** Border Background Color */
	SLATE_ARGUMENT(FSlateColor, BorderBackgroundColor)
	/** Style to use for the editable text box within this widget */
	SLATE_STYLE_ARGUMENT(FEditableTextBoxStyle, EditableTextBoxStyle)

	/** Font color and opacity */
	SLATE_ATTRIBUTE(FSlateFontInfo, Font)
	/** Provide custom type conversion functionality to this spin box */
	SLATE_ARGUMENT(TSharedPtr<INumericTypeInterface<NumericType>>, TypeInterface)

	/** Called whenever the text is changed programmatically or interactively by the user */
	SLATE_EVENT(FOnValueChanged, OnValueChanged)
	/** Called whenever the text is committed.  This happens when the user presses enter or the text box loses focus. */
	SLATE_EVENT(FOnValueCommitted, OnValueCommitted)
	/** Called whenever the text is changed programmatically or interactively by the user */
	SLATE_EVENT(FOnUndeterminedValueChanged, OnUndeterminedValueChanged)
	/** Called whenever the text is committed.  This happens when the user presses enter or the text box loses focus. */
	SLATE_EVENT(FOnUndeterminedValueCommitted, OnUndeterminedValueCommitted)

	/** Menu extender for right-click context menu */
	SLATE_EVENT(FMenuExtensionDelegate, ContextMenuExtender)

	/** The text margin to use if overridden. */
	SLATE_ATTRIBUTE(FMargin, OverrideTextMargin)

	/** The minimum desired width for the value portion of the control. */
	SLATE_ATTRIBUTE(float, MinDesiredValueWidth)

	/** Style to use for the spin box within this widget */
	SLATE_STYLE_ARGUMENT(FSpinBoxStyle, SpinBoxStyle)
	/** Whether or not the user should be able to change the value by dragging with the mouse cursor */
	SLATE_ARGUMENT(bool, AllowSpin)
	/** Delta to increment the value as the slider moves.  If not specified will determine automatically */
	SLATE_ATTRIBUTE(NumericType, Delta)
	/** Multiplier to use when shift is held down */
	SLATE_ATTRIBUTE(float, ShiftMultiplier)
	/** Multiplier to use when ctrl is held down */
	SLATE_ATTRIBUTE(float, CtrlMultiplier)
	/** If we're an unbounded spinbox, what value do we divide mouse movement by before multiplying by Delta. Requires Delta to be set. */
	SLATE_ATTRIBUTE(int32, LinearDeltaSensitivity)
	/** Tell us if we want to support dynamically changing of the max value using ctrl  (only use if there is a spinbox allow) */
	SLATE_ATTRIBUTE(bool, SupportDynamicSliderMaxValue)
	/** Tell us if we want to support dynamically changing of the min value using ctrl  (only use if there is a spinbox allow) */
	SLATE_ATTRIBUTE(bool, SupportDynamicSliderMinValue)
	/** Called right after the spinner max value is changed (only relevant if SupportDynamicSliderMaxValue is true) */
	SLATE_EVENT(FOnDynamicSliderMinMaxValueChanged, OnDynamicSliderMaxValueChanged)
	/** Called right after the spinner min value is changed (only relevant if SupportDynamicSliderMinValue is true) */
	SLATE_EVENT(FOnDynamicSliderMinMaxValueChanged, OnDynamicSliderMinValueChanged)
	/** The minimum value that can be specified by using the slider */
	SLATE_ATTRIBUTE(TOptional<NumericType>, MinSliderValue)
	/** The maximum value that can be specified by using the slider */
	SLATE_ATTRIBUTE(TOptional<NumericType>, MaxSliderValue)
	/** The minimum value that can be entered into the text edit box */
	SLATE_ATTRIBUTE(TOptional<NumericType>, MinValue)
	/** The maximum value that can be entered into the text edit box */
	SLATE_ATTRIBUTE(TOptional<NumericType>, MaxValue)
	/** Use exponential scale for the slider */
	SLATE_ATTRIBUTE(float, SliderExponent)
	/** When using exponential scale specify a neutral value where we want the maximum precision (by default it is the smallest slider value)*/
	SLATE_ATTRIBUTE(NumericType, SliderExponentNeutralValue)
	/** Called right before the slider begins to move */
	SLATE_EVENT(FSimpleDelegate, OnBeginSliderMovement)
	/** Called right after the slider handle is released by the user */
	SLATE_EVENT(FOnValueChanged, OnEndSliderMovement)

	SLATE_ATTRIBUTE(TOptional<int32>, MinFractionalDigits)
	SLATE_ATTRIBUTE(TOptional<int32>, MaxFractionalDigits)

	SLATE_STYLE_ARGUMENT(FSlateBrush, BorderImageNormal)
	SLATE_STYLE_ARGUMENT(FSlateBrush, BorderImageHovered)
	SLATE_STYLE_ARGUMENT(FSlateBrush, BorderImageFocused)

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs)
	{
		check(InArgs._EditableTextBoxStyle);

		OnValueChanged = InArgs._OnValueChanged;
		OnValueCommitted = InArgs._OnValueCommitted;

		UndeterminedString = InArgs._UndeterminedString;
		OnUndeterminedValueChanged = InArgs._OnUndeterminedValueChanged;
		OnUndeterminedValueCommitted = InArgs._OnUndeterminedValueCommitted;
		ValueAttribute = InArgs._Value;
		const bool bAllowSpin = InArgs._AllowSpin;
		Interface = InArgs._TypeInterface.IsValid() ? InArgs._TypeInterface : MakeShareable(new TDefaultNumericTypeInterface<NumericType>);
		MinDesiredValueWidth = InArgs._MinDesiredValueWidth;

		TAttribute<FMargin> TextMargin = InArgs._OverrideTextMargin.IsSet() ? InArgs._OverrideTextMargin : InArgs._EditableTextBoxStyle->Padding;

		MinFractionalDigits = InArgs._MinFractionalDigits;
		MaxFractionalDigits = InArgs._MaxFractionalDigits;
		SetMinFractionalDigits(MinFractionalDigits);
		SetMaxFractionalDigits(MaxFractionalDigits);

		SpinBoxImage.TintColor = FLinearColor::White;
		//BorderImageNormal = &SpinBoxImage;
		//BorderImageHovered = &SpinBoxImage;
		//BorderImageFocused = &SpinBoxImage;
		BorderImageNormal = InArgs._BorderImageNormal;
		BorderImageHovered = InArgs._BorderImageHovered;
		BorderImageFocused = InArgs._BorderImageFocused;

		if (bAllowSpin)
		{
			//BackgroundBrush.Tint_DEPRECATED = FLinearColor::White;

			WhiteSpinBoxStyle = FSpinBoxStyle(FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("NumericEntrySpinBox"))
									.SetBackgroundBrush(SpinBoxImage)
									//.SetForegroundColor(FLinearColor::White)
									.SetInactiveFillBrush(SpinBoxImage)
									.SetHoveredBackgroundBrush(SpinBoxImage);
			//.SetArrowsImage(BackgroundBrush);

			SAssignNew(SpinBox, SSpinBox<NumericType>)
			.Style(/*&WhiteSpinBoxStyle*/ InArgs._SpinBoxStyle)
		#if ENGINE_MAJOR_VERSION < 5
			.Font(InArgs._Font.IsSet() ? InArgs._Font : InArgs._EditableTextBoxStyle->Font)
		#else
			.Font(InArgs._Font.IsSet() ? InArgs._Font : InArgs._EditableTextBoxStyle->TextStyle.Font)
		#endif
			.ContentPadding(TextMargin)
			.Value(this, &SImNumericWidget<NumericType>::OnGetValueForSpinBox)
			.Delta(InArgs._Delta)
			.ShiftMultiplier(InArgs._ShiftMultiplier)
			.CtrlMultiplier(InArgs._CtrlMultiplier)
			.LinearDeltaSensitivity(InArgs._LinearDeltaSensitivity)
			.SupportDynamicSliderMaxValue(InArgs._SupportDynamicSliderMaxValue)
			.SupportDynamicSliderMinValue(InArgs._SupportDynamicSliderMinValue)
			.OnDynamicSliderMaxValueChanged(InArgs._OnDynamicSliderMaxValueChanged)
			.OnDynamicSliderMinValueChanged(InArgs._OnDynamicSliderMinValueChanged)
			.OnValueChanged(OnValueChanged)
			.OnValueCommitted(OnValueCommitted)
			.MinFractionalDigits(InArgs._MinFractionalDigits)
			.MaxFractionalDigits(InArgs._MaxFractionalDigits)
			.MinSliderValue(InArgs._MinSliderValue)
			.MaxSliderValue(InArgs._MaxSliderValue)
			.MaxValue(InArgs._MaxValue)
			.MinValue(InArgs._MinValue)
			.SliderExponent(InArgs._SliderExponent)
			.SliderExponentNeutralValue(InArgs._SliderExponentNeutralValue)
			.OnBeginSliderMovement(InArgs._OnBeginSliderMovement)
			.OnEndSliderMovement(InArgs._OnEndSliderMovement)
			.MinDesiredWidth(InArgs._MinDesiredValueWidth)
			.TypeInterface(Interface)
			.ToolTipText(this, &SImNumericWidget<NumericType>::GetValueAsText);
		}

		// Always create an editable text box.  In the case of an undetermined value being passed in, we cant use the spinbox.
		// Use SImEditableText (not plain SEditableText) so focusing it pops the self-rendered ImSlate
		// keyboard; VirtualKeyboardType(Number) makes that a numeric pad (this is a numeric widget).
		SAssignNew(EditableText, SImEditableText)
		.VirtualKeyboardType(Keyboard_Number)
		.Text(this, &SImNumericWidget<NumericType>::OnGetValueForTextBox)
		.Visibility(bAllowSpin ? EVisibility::Collapsed : EVisibility::Visible)
#if ENGINE_MAJOR_VERSION < 5
		.Font(InArgs._Font.IsSet() ? InArgs._Font : InArgs._EditableTextBoxStyle->Font)
#else
		.Font(InArgs._Font.IsSet() ? InArgs._Font : InArgs._EditableTextBoxStyle->TextStyle.Font)
#endif
		.SelectAllTextWhenFocused(true)
		.ClearKeyboardFocusOnCommit(false)
		.OnTextChanged(this, &SImNumericWidget<NumericType>::OnTextChanged)
		.OnTextCommitted(this, &SImNumericWidget<NumericType>::OnTextCommitted)
		.SelectAllTextOnCommit(true)
		.ContextMenuExtender(InArgs._ContextMenuExtender)
		.MinDesiredWidth(InArgs._MinDesiredValueWidth)
		.ToolTipText(this, &SImNumericWidget<NumericType>::GetValueAsText);

		// Shape the virtual keyboard to the EXACT bound type (NumericType is known at compile time): integer
		// types hide the decimal point, unsigned types hide the sign (± key) entirely. Without this the
		// keypad fell back to its defaults (float, signed) for every numeric widget, so e.g. a uint32 field
		// still showed '.' and ±. Min/Max are intentionally left unset here — clamp range is driven by the
		// explicit SetMinValue/SetMaxValue calls, not by the type's numeric_limits.
		{
			const bool bIsFloat = TIsFloatingPoint<NumericType>::Value;
			const bool bIsSigned = TIsSigned<NumericType>::Value;  // false for uint32/uint64 → no ± key
			const int32 BitWidth = bIsFloat ? 0 : (int32)(sizeof(NumericType) * 8);  // 8/16/32/64; HEX two's-comp width
			// EditableText is declared as the base SEditableText; SetNumericParams lives on our SImEditableText
			// (it's always constructed as one via SAssignNew above), so cast to call it.
			if (TSharedPtr<SImEditableText> ImEdit = StaticCastSharedPtr<SImEditableText>(EditableText))
			{
				ImEdit->SetNumericParams(/*decimal*/ bIsFloat, /*negative*/ bIsSigned, /*hex*/ false,
					/*Min*/ TOptional<double>(), /*Max*/ TOptional<double>(), /*Step*/ TOptional<double>(),
					/*BitWidth*/ BitWidth);
			}
		}

		TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

		if (InArgs._Label.Widget != SNullWidget::NullWidget)
		{
			HorizontalBox->AddSlot().AutoWidth().HAlign(HAlign_Left).VAlign(InArgs._LabelVAlign).Padding(InArgs._LabelPadding)[InArgs._Label.Widget];
		}

		// Add the spin box if we have one
		if (bAllowSpin)
		{
			HorizontalBox->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Center).FillWidth(1)[SpinBox.ToSharedRef()];
		}

		HorizontalBox->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Center).Padding(TextMargin).FillWidth(1)[EditableText.ToSharedRef()];

		ChildSlot
		[
			// Populate the widget
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.BorderImage(this, &SImNumericWidget::GetBorderImage)
			.BorderBackgroundColor(InArgs._BorderBackgroundColor)
			.ForegroundColor(InArgs._BorderForegroundColor)
			.Padding(0)
			[
				HorizontalBox
			]
		];
	};

private:
	/**
	 * @return the Label that should be displayed                   
	 */
	FString GetLabel() const
	{
		// Should always be set if this is being called
		return LabelAttribute.Get().GetValue();
	}

	/**
	 * Called to get the value for the spin box                   
	 */
	NumericType OnGetValueForSpinBox() const
	{
		const auto& Value = ValueAttribute.Get();

		// Get the value or 0 if its not set
		if (Value.IsSet() == true)
		{
			return Value.GetValue();
		}

		return 0;
	}

	/**
	 * Called to get the value for the text box as FText                 
	 */
	FText OnGetValueForTextBox() const
	{
		FText NewText = FText::GetEmpty();

		if (EditableText->GetVisibility() == EVisibility::Visible)
		{
			const auto& Value = ValueAttribute.Get();

			// If the value was set convert it to a string, otherwise the value cannot be determined
			if (Value.IsSet() == true)
			{
				NewText = FText::FromString(Interface->ToString(Value.GetValue()));
			}
			else
			{
				NewText = UndeterminedString;
			}
		}

		// The box isnt visible, just return an empty string
		return NewText;
	}

	/**
	 * Called to get the border image of the box                   
	 */
	const FSlateBrush* GetBorderImage() const
	{
		TSharedPtr<const SWidget> EditingWidget;

		if (SpinBox.IsValid() && SpinBox->GetVisibility() == EVisibility::Visible)
		{
			EditingWidget = SpinBox;
		}
		else
		{
			EditingWidget = EditableText;
		}

		if (EditingWidget->HasKeyboardFocus())
		{
			return BorderImageFocused;
		}

		if (EditingWidget->IsHovered())
		{
			return BorderImageHovered;
		}

		return BorderImageNormal;
	}

	/**
	 * Calls the value commit or changed delegate set for this box when the value is set from a string
	 *
	 * @param NewValue	The new value as a string
	 * @param bCommit	Whether or not to call the commit or changed delegate
	 */
	void SendChangesFromText(const FText& NewValue, bool bCommit, ETextCommit::Type CommitInfo)
	{
		if (NewValue.IsEmpty())
		{
			return;
		}

		TOptional<NumericType> ExistingValue = ValueAttribute.Get();
		TOptional<NumericType> NumericValue = Interface->FromString(NewValue.ToString(), ExistingValue.Get(0));

		if (NumericValue.IsSet())
		{
			if (bCommit)
			{
				OnValueCommitted.ExecuteIfBound(NumericValue.GetValue(), CommitInfo);
			}
			else
			{
				OnValueChanged.ExecuteIfBound(NumericValue.GetValue());
			}
		}
	}

	/**
	 * Called when the text is committed from the text box                   
	 */
	void OnTextCommitted(const FText& NewValue, ETextCommit::Type CommitInfo)
	{
		const auto& Value = ValueAttribute.Get();

		if (Value.IsSet() || !OnUndeterminedValueCommitted.IsBound())
		{
			SendChangesFromText(NewValue, true, CommitInfo);
		}
		else
		{
			OnUndeterminedValueCommitted.Execute(NewValue, CommitInfo);
		}
	}

	/**
	 * Called when the text changes in the text box                   
	 */
	void OnTextChanged(const FText& NewValue)
	{
		const auto& Value = ValueAttribute.Get();

		if (Value.IsSet() || !OnUndeterminedValueChanged.IsBound())
		{
			SendChangesFromText(NewValue, false, ETextCommit::Default);
		}
		else
		{
			OnUndeterminedValueChanged.Execute(NewValue);
		}
	}

	FText GetValueAsText() const
	{
		const TOptional<NumericType>& Value = ValueAttribute.Get();
		if (Value.IsSet() == true)
		{
			return FText::FromString(Interface->ToString(Value.GetValue()));
		}

		return FText::GetEmpty();
	}

protected:
	/** Attribute for getting the label */
	TAttribute<TOptional<FString>> LabelAttribute;
	/** Attribute for getting the value.  If the value is not set we display the undetermined string */
	TAttribute<TOptional<NumericType>> ValueAttribute;

	/** Spinbox widget */
	TSharedPtr<SWidget> SpinBox;
	/** Editable widget */
	TSharedPtr<SEditableText> EditableText;

	/** Delegate to call when the value changes */
	FOnValueChanged OnValueChanged;
	/** Delegate to call when the value is committed */
	FOnValueCommitted OnValueCommitted;

	/** The undetermined string to display when needed */
	FText UndeterminedString;
	/** Delegate to call when an undetermined value changes */
	FOnUndeterminedValueChanged OnUndeterminedValueChanged;
	/** Delegate to call when an undetermined is committed */
	FOnUndeterminedValueCommitted OnUndeterminedValueCommitted;

	/** Styling: border image to draw when not hovered or focused */
	const FSlateBrush* BorderImageNormal;
	/** Styling: border image to draw when hovered */
	const FSlateBrush* BorderImageHovered;
	/** Styling: border image to draw when focused */
	const FSlateBrush* BorderImageFocused;
	// Spin box image
	FSlateBrush SpinBoxImage;
	// Spin box style
	FSpinBoxStyle WhiteSpinBoxStyle;

	/** Prevents the value portion of the control from being smaller than desired in certain cases. */
	TAttribute<float> MinDesiredValueWidth;

	/** Type interface that defines how we should deal with the templated numeric type. Always valid after construction. */
	TSharedPtr<INumericTypeInterface<NumericType>> Interface;

	TAttribute<TOptional<int32>> MinFractionalDigits;
	TAttribute<TOptional<int32>> MaxFractionalDigits;

public:
	int32 GetMinFractionalDigits() const { return Interface->GetMinFractionalDigits(); }
	void SetMinFractionalDigits(const TAttribute<TOptional<int32>>& InMinFractionalDigits) { Interface->SetMinFractionalDigits((InMinFractionalDigits.Get().IsSet()) ? InMinFractionalDigits.Get() : MinFractionalDigits); }

	/** See the MaxFractionalDigits attribute */
	int32 GetMaxFractionalDigits() const { return Interface->GetMaxFractionalDigits(); }
	void SetMaxFractionalDigits(const TAttribute<TOptional<int32>>& InMaxFractionalDigits) { Interface->SetMaxFractionalDigits((InMaxFractionalDigits.Get().IsSet()) ? InMaxFractionalDigits.Get() : MaxFractionalDigits); }

	void SetValue(const TAttribute<TOptional<NumericType>>& InValueAttr) { ValueAttribute = InValueAttr; }

public:
	static TSharedRef<SWidget> BuildLabel(TAttribute<FText> LabelText, const FSlateBrush* InLabelBrush, const FSlateColor& ForegroundColor, const FSlateColor& BackgroundColor)
	{
		return SNew(SBorder)
				.BorderImage(InLabelBrush ? InLabelBrush : FCoreStyle::Get().GetBrush("NumericEntrySpinBox.Decorator"))
				.BorderBackgroundColor(BackgroundColor)
				.ForegroundColor(ForegroundColor)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.Padding(FMargin(1, 0, 6, 0))
				[
					SNew(STextBlock)
					.Text(LabelText)
				];
	}

	void SetOnBeginSliderMovement(FSimpleDelegate InOnBeginSliderMovement);

	void SetOnValueChanged(FOnValueChanged InOnValueChanged);
	void SetOnValueCommitted(FOnValueCommitted InOnValueCommitted);
	void SetOnEndSliderMovement(FOnValueChanged InOnEndSliderMovement);

	//Max Min Interface
	void SetMinValue(const TAttribute<TOptional<NumericType>>& InMinValue);
	void SetMaxValue(const TAttribute<TOptional<NumericType>>& InMaxValue);
	void SetMaxSliderValue(const TAttribute<TOptional<NumericType>>& InMaxSliderValue);
	void SetMinSliderValue(const TAttribute<TOptional<NumericType>>& InMinSliderValue);

	void SetDelta(const NumericType& InDeltaValue);

	void SetSliderExponent(const TAttribute<float>& InSliderExponent);

	void SetMultValueVisible(bool IsVisible)
	{
		EditableText->SetVisibility(IsVisible ? EVisibility::Visible : EVisibility::Collapsed);
		SpinBox->SetVisibility(IsVisible ? EVisibility::Collapsed : EVisibility::Visible);
	}
};

template<typename NumericType>
const FLinearColor SImNumericWidget<NumericType>::RedLabelBackgroundColor(0.594f, 0.0197f, 0.0f);

template<typename NumericType>
const FLinearColor SImNumericWidget<NumericType>::GreenLabelBackgroundColor(0.1349f, 0.3959f, 0.0f);

template<typename NumericType>
const FLinearColor SImNumericWidget<NumericType>::BlueLabelBackgroundColor(0.0251f, 0.207f, 0.85f);

template<typename NumericType>
const FText SImNumericWidget<NumericType>::DefaultUndeterminedString = FText::FromString(TEXT("---"));

#define Z_IMSLATE_DECLARE_NUMERIC_WIDGET(TYPE, TYPEDEF)                                                                                   \
	using TYPEDEF = SImNumericWidget<TYPE>;                                                                                          \
	template<>                                                                                                                            \
	void SImNumericWidget<TYPE>::SetOnBeginSliderMovement(FSimpleDelegate InOnBeginSliderMovement);                                  \
	extern template IMSLATE_API void SImNumericWidget<TYPE>::SetOnBeginSliderMovement(FSimpleDelegate InOnBeginSliderMovement);      \
	template<>                                                                                                                            \
	void SImNumericWidget<TYPE>::SetOnValueChanged(FOnValueChanged InOnValueChanged);                                                \
	extern template IMSLATE_API void SImNumericWidget<TYPE>::SetOnValueChanged(FOnValueChanged InOnValueChanged);                    \
	template<>                                                                                                                            \
	void SImNumericWidget<TYPE>::SetOnValueCommitted(FOnValueCommitted InOnValueCommitted);                                          \
	extern template IMSLATE_API void SImNumericWidget<TYPE>::SetOnValueCommitted(FOnValueCommitted InOnValueCommitted);              \
	template<>                                                                                                                            \
	void SImNumericWidget<TYPE>::SetOnEndSliderMovement(FOnValueChanged InOnEndSliderMovement);                                      \
	extern template IMSLATE_API void SImNumericWidget<TYPE>::SetOnEndSliderMovement(FOnValueChanged InOnEndSliderMovement);          \
	template<>                                                                                                                            \
	void SImNumericWidget<TYPE>::SetMinValue(const TAttribute<TOptional<TYPE>>& InMinValue);                                         \
	extern template IMSLATE_API void SImNumericWidget<TYPE>::SetMinValue(const TAttribute<TOptional<TYPE>>& InMinValue);             \
	template<>                                                                                                                            \
	void SImNumericWidget<TYPE>::SetMaxValue(const TAttribute<TOptional<TYPE>>& InMaxValue);                                         \
	extern template IMSLATE_API void SImNumericWidget<TYPE>::SetMaxValue(const TAttribute<TOptional<TYPE>>& InMaxValue);             \
	template<>                                                                                                                            \
	void SImNumericWidget<TYPE>::SetMaxSliderValue(const TAttribute<TOptional<TYPE>>& InMaxSliderValue);                             \
	extern template IMSLATE_API void SImNumericWidget<TYPE>::SetMaxSliderValue(const TAttribute<TOptional<TYPE>>& InMaxSliderValue); \
	template<>                                                                                                                            \
	void SImNumericWidget<TYPE>::SetMinSliderValue(const TAttribute<TOptional<TYPE>>& InMinSliderValue);                             \
	extern template IMSLATE_API void SImNumericWidget<TYPE>::SetMinSliderValue(const TAttribute<TOptional<TYPE>>& InMinSliderValue); \
	template<>                                                                                                                            \
	void SImNumericWidget<TYPE>::SetDelta(const TYPE& InDeltaValue);                                                                 \
	extern template IMSLATE_API void SImNumericWidget<TYPE>::SetDelta(const TYPE& InDeltaValue);                                     \
	template<>                                                                                                                            \
	void SImNumericWidget<TYPE>::SetSliderExponent(const TAttribute<float>& InSliderExponent);                                       \
	extern template IMSLATE_API void SImNumericWidget<TYPE>::SetSliderExponent(const TAttribute<float>& InSliderExponent);

Z_IMSLATE_DECLARE_NUMERIC_WIDGET(float, SImNumericFloatWidget)
Z_IMSLATE_DECLARE_NUMERIC_WIDGET(double, SImNumericDoubleWidget)
Z_IMSLATE_DECLARE_NUMERIC_WIDGET(int32, SImNumericInt32Widget)
Z_IMSLATE_DECLARE_NUMERIC_WIDGET(int32, SImNumericUInt32Widget)
Z_IMSLATE_DECLARE_NUMERIC_WIDGET(int64, SImNumericInt64Widget)
Z_IMSLATE_DECLARE_NUMERIC_WIDGET(int64, SImNumericUInt64Widget)
}


UCLASS(BlueprintType, Abstract)
class IMSLATE_API UImSlateNumericWidget
	: public UWidget
{
	GENERATED_BODY()

protected:
	template<typename T>
	TSharedRef<ImSlate::SImNumericWidget<T>> ConstructImWidget() const;

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties();

protected:
	IM_SLATE_PALETTECATEGORY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText LabelText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSlateBrush LabelBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FMargin LabelPadding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSlateColor BorderForegroundColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSlateColor BorderBackgroundColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FEditableTextBoxStyle EditableTextBoxStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSlateFontInfo Font;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSpinBoxStyle SpinBoxStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSlateBrush BorderImageNormal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSlateBrush BorderImageHovered;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSlateBrush BorderImageFocused;

private:
	TSharedPtr<SWidget> MyNumericWidget;
};


UCLASS(BlueprintType)
class IMSLATE_API UImNumericFloatWidget
	: public UImSlateNumericWidget
	, public TImFactory<ImSlate::SImNumericWidget<float>>
{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImNumericWidget<float>> ConstructImWidget() const;
};
UCLASS(BlueprintType)
class IMSLATE_API UImNumericDoubleWidget
	: public UImSlateNumericWidget
	, public TImFactory<ImSlate::SImNumericWidget<double>>
{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImNumericWidget<double>> ConstructImWidget() const;
};

UCLASS(BlueprintType)
class IMSLATE_API UImNumericInt32Widget
	: public UImSlateNumericWidget
	, public TImFactory<ImSlate::SImNumericWidget<int32>>
{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImNumericWidget<int32>> ConstructImWidget() const;
};
UCLASS(BlueprintType)
class IMSLATE_API UImNumericUInt32Widget
	: public UImSlateNumericWidget
	, public TImFactory<ImSlate::SImNumericWidget<uint32>>
{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImNumericWidget<uint32>> ConstructImWidget() const;
};
UCLASS(BlueprintType)
class IMSLATE_API UImNumericInt64Widget
	: public UImSlateNumericWidget
	, public TImFactory<ImSlate::SImNumericWidget<int64>>
{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImNumericWidget<int64>> ConstructImWidget() const;
};
UCLASS(BlueprintType)
class IMSLATE_API UImNumericUInt64Widget
	: public UImSlateNumericWidget
	, public TImFactory<ImSlate::SImNumericWidget<uint64>>
{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImNumericWidget<uint64>> ConstructImWidget() const;
};
