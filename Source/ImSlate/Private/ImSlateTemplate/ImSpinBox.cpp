// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImSpinBox.h"

#include <limits>
#include "PrivateFieldAccessor.h"
#include "Widgets/Text/SlateEditableTextLayout.h"
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "Framework/Application/SlateApplication.h"

namespace ImSlate
{
// SSpinBox<float> that pops the self-rendered ImSlate numeric keypad instead of the OS/IME keyboard
// when you click into its text field — while keeping all native SSpinBox behaviour (slider drag,
// arrows, delta). A click (not a drag) makes the base SSpinBox EnterTextMode() + focus its internal
// SEditableText (SSpinBox.cpp:351-354); we detect that, immediately exit the base text mode (so the
// engine edit doesn't also take input), and drive editing through our keyboard. Commit writes the
// parsed value back via SetValue. The keypad is shaped from the spinbox's own Min/Max/Delta.
class SImSlateSpinBox : public SSpinBox<float>
{
public:
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		FReply Reply = SSpinBox<float>::OnMouseButtonUp(MyGeometry, MouseEvent);

		// IsInTextMode() (protected base helper) is true when the click just entered text-editing mode.
		// Only redirect to our keyboard when it's the in-viewport (overlay) input path.
		if (IsInTextMode() && SImSlateVirtualKeyboard::ShouldUseVirtualKeyboard(this))
		{
			if (auto Keyboard = SImSlateVirtualKeyboard::Get())
			{
				// Back out of the base text mode — our keyboard owns the edit now (avoids dual input
				// and the engine edit grabbing focus / popping the OS keyboard).
				ExitTextMode();

				TWeakPtr<SImSlateSpinBox> WeakSelf = StaticCastSharedRef<SImSlateSpinBox>(AsShared());
				FVirtualKeyboardShowParams Params;
				Params.InitialText = GetValueAsString();
				Params.Owner = AsShared();
				Params.InitialKeyboardType = EImKeyboardType::Number;
				Params.Numeric.bAllowDecimal = true;      // float
				// Only forward a clamp when the spinbox actually HAS one. SSpinBox::GetMinValue/GetMaxValue
				// return numeric_limits lowest()/max() when unset (SSpinBox.cpp:711/722), NOT TOptional —
				// forwarding those as Min/Max would (a) wrongly mark the field as range-limited and (b)
				// e.g. make the keypad think it can't go negative. Treat the sentinel extremes as "unset".
				const float SpinMin = GetMinValue();
				const float SpinMax = GetMaxValue();
				if (SpinMin > std::numeric_limits<float>::lowest())
					Params.Numeric.Min = (double)SpinMin;
				if (SpinMax < std::numeric_limits<float>::max())
					Params.Numeric.Max = (double)SpinMax;
				// Allow negatives unless an explicit min clamps the range to >= 0.
				Params.Numeric.bAllowNegative = !(Params.Numeric.Min.IsSet() && Params.Numeric.Min.GetValue() >= 0.0);
				if (GetDelta() != 0.f)
					Params.Numeric.Step = (double)GetDelta();
				Params.CommitCallback = [WeakSelf](const FString& Text, ETextCommit::Type Type)
				{
					if (auto Self = WeakSelf.Pin())
					{
						if (Type != ETextCommit::OnCleared)
							Self->SetValue(FCString::Atof(*Text), /*bShouldCommit=*/ true);
					}
				};
				Keyboard->Show(Params);
			}
		}
		return Reply;
	}
};
}  // namespace ImSlate

//
namespace ImSlateSpinBox
{
GS_PRIVATEACCESS_MEMBER(FSlateEditableTextLayout, Margin, TAttribute<FMargin>);
using ImSpinBox = SSpinBox<float>;
GS_PRIVATEACCESS_MEMBER(ImSpinBox, EditableText, TSharedPtr<SEditableText>);
GS_PRIVATEACCESS_MEMBER(ImSpinBox, TextBlock, TSharedPtr<STextBlock>);

FMargin GetLayoutMargin(FSlateEditableTextLayout* InLayout)
{
	return PrivateAccess::Margin(*InLayout).Get();
}
void ApplyMargin(TSharedRef<SSpinBox<float>>& MyStealSpinBox)
{
	auto Margin = GS_ACCESS_PROTECT(PrivateAccess::EditableText(MyStealSpinBox.Get()), SEditableText, EditableTextLayout)->EditableTextLayout.Get();
	PrivateAccess::TextBlock(MyStealSpinBox.Get())->SetMargin(GetLayoutMargin(Margin));
}
}  // namespace ImSlateSpinBox

TSharedRef<SSpinBox<float>> UImSpinBox::ConstructImWidget() const
{
	auto MyStealSpinBox = SNew(ImSlate::SImSlateSpinBox)  // SSpinBox<float> that pops the ImSlate numeric keypad
							.Style(&GetWidgetStyle())
							.Font(ImSlate::ScaleImSlateFont(GetFont()))
							.ClearKeyboardFocusOnCommit(GetClearKeyboardFocusOnCommit())
							.SelectAllTextOnCommit(GetSelectAllTextOnCommit())
							.Justification(GetJustification())
	//.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged))
	//.OnValueCommitted(BIND_UOBJECT_DELEGATE(FOnFloatValueCommitted, HandleOnValueCommitted))
	//.OnBeginSliderMovement(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnBeginSliderMovement))
	//.OnEndSliderMovement(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnEndSliderMovement));
		;
#if 0
	ImSlateSpinBox::ApplyMargin(MyStealSpinBox);
#endif
	// UWidget

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealSpinBox->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealSpinBox->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealSpinBox->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealSpinBox->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealSpinBox->SetClipping(GetClipping());

	MyStealSpinBox->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealSpinBox->ForceVolatile(bIsVolatile);
	MyStealSpinBox->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealSpinBox->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealSpinBox->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealSpinBox->SetRenderTransformPivot(GetRenderTransformPivot());

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

	MyStealSpinBox->SetDelta(GetDelta());
	MyStealSpinBox->SetSliderExponent(GetSliderExponent());
	MyStealSpinBox->SetMinDesiredWidth(GetMinDesiredWidth());

	MyStealSpinBox->SetForegroundColor(GetForegroundColor());

	MyStealSpinBox->SetMinFractionalDigits(GetMinFractionalDigits());
	MyStealSpinBox->SetMaxFractionalDigits(GetMaxFractionalDigits());
	MyStealSpinBox->SetAlwaysUsesDeltaSnap(GetAlwaysUsesDeltaSnap());

	// Set optional values
#if 0
	bOverride_MinValue ? SetMinValue(GetMinValue()) : ClearMinValue();
	bOverride_MaxValue ? SetMaxValue(GetMaxValue()) : ClearMaxValue();
	bOverride_MinSliderValue ? SetMinSliderValue(GetMinSliderValue()) : ClearMinSliderValue();
	bOverride_MaxSliderValue ? SetMaxSliderValue(GetMaxSliderValue()) : ClearMaxSliderValue();
#endif

	// Always set the value last so that the max/min values are taken into account.
	// TAttribute<float> ValueBinding = PROPERTY_BINDING(float, Value);
	MyStealSpinBox->SetValue(GetValue());

	return MyStealSpinBox;
}
