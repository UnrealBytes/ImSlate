// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImEditableText.h"
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "SImSlateViewport.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/TextLayout.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Widgets/Text/SlateEditableTextLayout.h"
#include "UObject/GarbageCollection.h"  // GIsGarbageCollecting
#include "HAL/PlatformApplicationMisc.h"  // FPlatformApplicationMisc::RequiresVirtualKeyboard
#include "PrivateFieldAccessor.h"

// The caret's pixel position comes from FTextLayout::GetLocationAt (public), but the
// FTextLayout instance lives in FSlateEditableTextLayout::TextLayout which is private.
// GenericStorages' PrivateFieldAccessor exposes it legally (friend injection idiom).
GS_PRIVATEACCESS_MEMBER(FSlateEditableTextLayout, TextLayout, TSharedPtr<FSlateTextLayout>)

namespace ImSlate
{

// SImEditableText
int32 SImEditableText::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
	const
{
	const FSlateBrush* BrushResource = BorderImage.Get();
	if (BrushResource && BrushResource->DrawAs != ESlateBrushDrawType::NoDrawType)
	{
		FSlateDrawElement::MakeBox(OutDrawElements,
								   LayerId,
								   AllottedGeometry.ToPaintGeometry(),
								   BrushResource,
								   ESlateDrawEffect::None,
								   BrushResource->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint() /* * BorderBackgroundColor.Get().GetColor(InWidgetStyle)*/);
	}

	// Preview digit-scrub highlight: draw the selected digit's background BELOW the text (on LayerId, so
	// the glyph stays readable on top). Bounds come from the engine text layout (DPI-safe, same path as
	// the caret self-draw below).
	if (bPreviewDisplayMode && HighlightDigitIndex != INDEX_NONE && EditableTextLayout)
	{
		float HL = 0.f, HR = 0.f;
		if (GetDigitCellBounds(HighlightDigitIndex, HL, HR) && HR > HL)
		{
			const float LocalSizeY = (float)AllottedGeometry.GetLocalSize().Y;
			static FSlateBrush HighlightBrush;
			HighlightBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
			HighlightBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			HighlightBrush.OutlineSettings.CornerRadii = FVector4(3, 3, 3, 3);
			FSlateDrawElement::MakeBox(
				OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(HR - HL, LocalSizeY),
					FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(FVector2D(HL, 0.f)))),
				&HighlightBrush, ESlateDrawEffect::None, FLinearColor(0.30f, 0.80f, 1.00f, 0.35f));
		}
	}

	int32 MaxLayerId = SEditableText::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// Preview display mode: the engine only draws the native caret when focused
	// (SlateEditableTextLayout.cpp: `if (bHasKeyboardFocus && !bIsReadOnly)`), and we
	// deliberately keep this widget unfocused. So self-draw the caret using the engine's
	// own coordinate API, mirroring FCursorLineHighlighter::OnPaint exactly.
	if (bPreviewDisplayMode && bPreviewCaretVisible && EditableTextLayout)
	{
		const TSharedPtr<FSlateTextLayout>& TextLayout = PrivateAccess::TextLayout(*EditableTextLayout);
		if (TextLayout.IsValid())
		{
			const FTextLocation CaretLoc = EditableTextLayout->GetCursorLocation();
			// GetLocationAt returns a pre-scaled location already including -ScrollOffset.
			const FVector2D Location = TextLayout->GetLocationAt(CaretLoc, /*bPerformInclusiveBoundsCheck*/ false);

			// Horizontal: take the engine-computed X (already includes justify offset & scroll),
			// converted to local space. Vertical: GetLocationAt's Y can land outside the widget
			// geometry, so place the caret vertically centered within the widget's own height
			// (single-line SEditableText renders its text vertically centered too).
			const float Scale = TextLayout->GetScale();
			const float LocalSizeY = (float)AllottedGeometry.GetLocalSize().Y;

			const FSlateFontInfo FontInfo = Font.Get();
			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const float FontMaxCharHeight = (float)FontMeasure->GetMaxCharacterHeight(FontInfo);
			const float CaretWidth = FMath::Clamp(0.08f * FontMaxCharHeight, 1.f, 2.f);
			const float CaretHeight = FMath::Min(FontMaxCharHeight, LocalSizeY);

			const float CaretX = Location.X / Scale;                          // local X
			const float CaretY = FMath::Max(0.f, (LocalSizeY - CaretHeight) * 0.5f);  // vertically centered
			const FVector2D CaretLocalPos(CaretX, CaretY);

			static FSlateBrush CaretBrush;
			CaretBrush.DrawAs = ESlateBrushDrawType::Image;
			CaretBrush.TintColor = FLinearColor::White;

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				MaxLayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(CaretWidth, CaretHeight),
					FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(CaretLocalPos))),
				&CaretBrush,
				ESlateDrawEffect::None,
				FLinearColor::White);

			return MaxLayerId + 1;
		}
	}

	return MaxLayerId;
}

void SImEditableText::SetBorderImage(const TAttribute<const FSlateBrush*>& InBrushAttribute)
{
	BorderImage.SetImage(*this, InBrushAttribute);
}

// Local-space X bounds of the char at CharIndex: [GetLocationAt(i), GetLocationAt(i+1)] / Scale.
// GetLocationAt already includes scale/scroll/justify; dividing by Scale yields widget-local X.
bool SImEditableText::GetDigitCellBounds(int32 CharIndex, float& OutL, float& OutR) const
{
	if (!EditableTextLayout || CharIndex < 0)
		return false;
	const TSharedPtr<FSlateTextLayout>& TextLayout = PrivateAccess::TextLayout(*EditableTextLayout);
	if (!TextLayout.IsValid())
		return false;
	const FString Text = GetText().ToString();
	if (CharIndex >= Text.Len())
		return false;
	const float Scale = TextLayout->GetScale();
	if (Scale <= 0.f)
		return false;
	const float LX = (float)TextLayout->GetLocationAt(FTextLocation(0, CharIndex), false).X / Scale;
	const float RX = (float)TextLayout->GetLocationAt(FTextLocation(0, CharIndex + 1), false).X / Scale;
	OutL = FMath::Min(LX, RX);
	OutR = FMath::Max(LX, RX);
	return true;
}

// Local X of the caret at an arbitrary index — the EXACT same path the self-drawn caret uses
// (GetLocationAt / Scale), so overlays align pixel-perfectly to where the caret renders.
bool SImEditableText::GetLocalXAt(int32 CharIndex, float& OutX) const
{
	if (!EditableTextLayout || CharIndex < 0)
		return false;
	const TSharedPtr<FSlateTextLayout>& TextLayout = PrivateAccess::TextLayout(*EditableTextLayout);
	if (!TextLayout.IsValid())
		return false;
	const float Scale = TextLayout->GetScale();
	if (Scale <= 0.f)
		return false;
	OutX = (float)TextLayout->GetLocationAt(FTextLocation(0, CharIndex), false).X / Scale;
	return true;
}

// Local X of the LIVE caret (blinking caret position) — same source as the caret self-draw.
bool SImEditableText::GetCaretLocalX(float& OutX) const
{
	if (!EditableTextLayout)
		return false;
	return GetLocalXAt(EditableTextLayout->GetCursorLocation().GetOffset(), OutX);
}

// Map an absolute screen position to the nearest DIGIT char index (skips '-' '.' and any non-hex-digit).
// Returns INDEX_NONE when the text holds no digit.
int32 SImEditableText::HitTestDigitIndex(const FVector2D& AbsScreenPos) const
{
	if (!EditableTextLayout)
		return INDEX_NONE;
	const FVector2D Local = GetCachedGeometry().AbsoluteToLocal(AbsScreenPos);
	const FString Text = GetText().ToString();
	int32 Best = INDEX_NONE;
	float BestDist = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Text.Len(); ++i)
	{
		if (!FChar::IsHexDigit(Text[i]))
			continue;  // skip '-', '.', etc.
		float L = 0.f, R = 0.f;
		if (!GetDigitCellBounds(i, L, R))
			continue;
		if ((float)Local.X >= L && (float)Local.X <= R)
			return i;  // direct hit
		const float Center = (L + R) * 0.5f;
		const float Dist = FMath::Abs((float)Local.X - Center);
		if (Dist < BestDist) { BestDist = Dist; Best = i; }
	}
	return Best;
}

void SImEditableText::SetHighlightDigit(int32 InCharIndex)
{
	if (HighlightDigitIndex != InCharIndex)
	{
		HighlightDigitIndex = InCharIndex;
		Invalidate(EInvalidateWidgetReason::Paint);  // self-drawn highlight; repaint on change
	}
}

FReply SImEditableText::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	// Preview display mode: text is driven by the virtual keyboard's keys, not by this
	// widget. Swallow char input so it never edits the displayed text directly.
	if (bPreviewDisplayMode)
		return FReply::Handled();

	FReply Reply = SEditableText::OnKeyChar(MyGeometry, InCharacterEvent);
	if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
	{
		if (Keyboard->IsShowing())
		{
			const int32 Caret = EditableTextLayout ? EditableTextLayout->GetCursorLocation().GetOffset() : -1;
			Keyboard->SyncFromEditor(GetText().ToString(), Caret);  // sync caret too (single-line offset)
		}
	}
	return Reply;
}

FReply SImEditableText::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Preview display mode: forward Enter/Escape to the keyboard, swallow the rest.
	// Never call SEditableText::OnKeyDown (would edit text / move caret independently).
	if (bPreviewDisplayMode)
	{
		if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
		{
			if (Keyboard->IsShowing())
			{
				const FKey Key = InKeyEvent.GetKey();
				if (Key == EKeys::Enter)  { Keyboard->Hide(true);  return FReply::Handled(); }
				if (Key == EKeys::Escape) { Keyboard->Hide(false); return FReply::Handled(); }
			}
		}
		return FReply::Handled();
	}

	if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
	{
		if (Keyboard->IsShowing())
		{
			const FKey Key = InKeyEvent.GetKey();
			if (Key == EKeys::Enter)  { Keyboard->Hide(true);  return FReply::Handled(); }
			if (Key == EKeys::Escape) { Keyboard->Hide(false); return FReply::Handled(); }

			// Ctrl+V paste: SEditableText's paste goes through UICommandList which doesn't route
			// reliably here (modal keyboard / focus), so Ctrl+V was a no-op. Call paste directly.
			if (Key == EKeys::V && InKeyEvent.IsControlDown() && EditableTextLayout && !IsTextReadOnly())
			{
				EditableTextLayout->PasteTextFromClipboard();
				const int32 PasteCaret = EditableTextLayout->GetCursorLocation().GetOffset();
				Keyboard->SyncFromEditor(GetText().ToString(), PasteCaret);
				return FReply::Handled();
			}

			// While the virtual keyboard is open, DON'T allow Shift+arrow/Home/End to make a
			// selection. Downgrade to a plain caret move by re-issuing the same key without the
			// Shift modifier, so the cursor still moves but no range is selected.
			const bool bIsNavKey = (Key == EKeys::Left || Key == EKeys::Right
				|| Key == EKeys::Up || Key == EKeys::Down
				|| Key == EKeys::Home || Key == EKeys::End);
			if (bIsNavKey && InKeyEvent.IsShiftDown())
			{
				FModifierKeysState NoShift(
					/*bIsLeftShift*/ false, /*bIsRightShift*/ false,
					InKeyEvent.IsLeftControlDown(), InKeyEvent.IsRightControlDown(),
					InKeyEvent.IsLeftAltDown(), InKeyEvent.IsRightAltDown(),
					InKeyEvent.IsLeftCommandDown(), InKeyEvent.IsRightCommandDown(),
					InKeyEvent.AreCapsLocked());
				FKeyEvent PlainKey(Key, NoShift, InKeyEvent.GetUserIndex(),
					InKeyEvent.IsRepeat(), InKeyEvent.GetCharacter(), InKeyEvent.GetKeyCode());
				FReply NavReply = SEditableText::OnKeyDown(MyGeometry, PlainKey);
				const int32 NavCaret = EditableTextLayout ? EditableTextLayout->GetCursorLocation().GetOffset() : -1;
				Keyboard->SyncFromEditor(GetText().ToString(), NavCaret);
				return NavReply;
			}
		}
	}
	FReply Reply = SEditableText::OnKeyDown(MyGeometry, InKeyEvent);
	if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
	{
		if (Keyboard->IsShowing())
		{
			const int32 Caret = EditableTextLayout ? EditableTextLayout->GetCursorLocation().GetOffset() : -1;
			Keyboard->SyncFromEditor(GetText().ToString(), Caret);  // sync caret too (arrow keys / edits)
		}
	}
	return Reply;
}

SImEditableText::~SImEditableText()
{
	// Keyboard follows this editable's lifecycle: when the bound editable is destroyed, dismiss
	// the keyboard (no-op if rebound to another one).
	// GUARD: during GC / world teardown / engine exit, the current UWorld is null and calling
	// SImSlateVirtualKeyboard::Get() → GImSlate would try to CREATE a FWorldContextRoot for a
	// null world, dereferencing it (GetPackage) and crashing. In that state the keyboard is being
	// destroyed too, so notifying is pointless — skip it.
	if (GIsGarbageCollecting || IsEngineExitRequested())
		return;
	if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
		Keyboard->NotifyOwnerDestroyed(this);
}

FReply SImEditableText::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	// Preview display mode: never grab focus / pop the keyboard / activate IME.
	if (bPreviewDisplayMode)
		return FReply::Handled();

	// The on-screen keyboard is popped on click-RELEASE (OnMouseButtonUp/OnTouchEnded with no drag),
	// NOT on focus — so a press-then-drag scrolls the panel instead of popping the keyboard. Here we
	// only suppress the engine focus handling (no IME / OS keyboard) when the virtual keyboard is the
	// active input path; the actual Show happens in TryShowVirtualKeyboardOnRelease().
	if (ImSlate::SImSlateVirtualKeyboard::ShouldUseVirtualKeyboard(this))
		return FReply::Handled();  // swallow focus → no IME; release handler will Show the keyboard

	return SEditableText::OnFocusReceived(MyGeometry, InFocusEvent);
}

void SImEditableText::ShowVirtualKeyboardForSelf()
{
	if (bPreviewDisplayMode)
		return;
	if (ImSlate::SImSlateVirtualKeyboard::ShouldUseVirtualKeyboard(this))
	{
		if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
		{
			auto WeakSelf = TWeakPtr<SImEditableText>(StaticCastSharedRef<SImEditableText>(AsShared()));
			ImSlate::FVirtualKeyboardShowParams Params;
			Params.InitialText = GetText().ToString();
			Params.OnTextChanged = [WeakSelf](const FString& Text) {
				if (auto This = WeakSelf.Pin())
					This->SetText(FText::FromString(Text));
			};
			Params.CommitCallback = [WeakSelf](const FString& Text, ETextCommit::Type Type) {
				if (auto This = WeakSelf.Pin())
				{
					// Always apply Text: on OnEnter it's the edited text; on OnCleared (Esc) it's
					// the original text captured at open time, so this RESTORES the field (the live
					// OnTextChanged sync had been updating it during editing).
					const FText NewText = FText::FromString(Text);
					This->SetText(NewText);
					// SetText only updates the value — it does NOT fire SEditableText's OnTextChanged/
					// OnTextCommitted delegates. Downstream business logic (XConsole command, search
					// filters, dependent UI) listens on those delegates, so without this it wouldn't
					// react to a keyboard Done/Cancel. Broadcast both explicitly (these protected
					// overrides call OnTextChangedCallback/OnTextCommittedCallback ExecuteIfBound).
					This->OnTextChanged(NewText);
					This->OnTextCommitted(NewText, Type);
					if (This->VKCommitCallback)
						This->VKCommitCallback(Text, Type);
				}
			};
			Params.SuggestionProvider = VKSuggestionProvider;
			Params.Owner = AsShared();
			// Map the edit's virtual-keyboard type to the on-screen layout: Number → numeric pad.
			// (GetVirtualKeyboardType returns the engine's ::EKeyboardType, set via the UMG
			// VirtualKeyboardType property / SetVirtualKeyboardType.)
			if (GetVirtualKeyboardType() == Keyboard_Number)
			{
				Params.InitialKeyboardType = ImSlate::EImKeyboardType::Number;
				Params.Numeric.bAllowDecimal  = bNumAllowDecimal;
				Params.Numeric.bAllowNegative = bNumAllowNegative;
				Params.Numeric.bHex           = bNumHex;
				Params.Numeric.BitWidth       = NumBitWidth;
				Params.Numeric.Min            = NumMin;
				Params.Numeric.Max            = NumMax;
				Params.Numeric.Step           = NumStep;
			}
			// Input history (persisted per key, shown as recent suggestions).
			Params.HistoryKey = HistoryKey;
			Params.HistoryFilter = HistoryFilter;
			Keyboard->Show(Params);
			// The keyboard's Show() moves Slate user focus onto its own preview, so this original
			// edit doesn't keep focus / pop IME.
		}
	}
}

// --- Pop the keyboard on click-RELEASE (no drag), so a press-then-drag scrolls instead. ---
// Mouse and touch share the same logic; PreviewDisplayMode edits never pop a keyboard.

FReply SImEditableText::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	VKPressScreenPos = MouseEvent.GetScreenSpacePosition();
	bVKPressTracking = !bPreviewDisplayMode;
	return SEditableText::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SImEditableText::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = SEditableText::OnMouseButtonUp(MyGeometry, MouseEvent);
	// A click (down→up with no meaningful movement) pops the keyboard; a drag does not (it was a
	// scroll / selection). Threshold = Slate's drag trigger distance.
	if (bVKPressTracking)
	{
		bVKPressTracking = false;
		const float Moved = FVector2D(MouseEvent.GetScreenSpacePosition() - VKPressScreenPos).Size();
		if (Moved <= FSlateApplication::Get().GetDragTriggerDistance())
			ShowVirtualKeyboardForSelf();
	}
	return Reply;
}

FReply SImEditableText::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	VKPressScreenPos = InTouchEvent.GetScreenSpacePosition();
	bVKPressTracking = !bPreviewDisplayMode;
	return SEditableText::OnTouchStarted(MyGeometry, InTouchEvent);
}

FReply SImEditableText::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	FReply Reply = SEditableText::OnTouchEnded(MyGeometry, InTouchEvent);
	if (bVKPressTracking)
	{
		bVKPressTracking = false;
		const float Moved = FVector2D(InTouchEvent.GetScreenSpacePosition() - VKPressScreenPos).Size();
		if (Moved <= FSlateApplication::Get().GetDragTriggerDistance())
			ShowVirtualKeyboardForSelf();
	}
	return Reply;
}

/**
* Interface for tool tips.
*/
class FDelegateToolTip : public IToolTip
{
public:
	/**
	* Gets the widget that this tool tip represents.
	*
	* @return The tool tip widget.
	*/
	virtual TSharedRef<SWidget> AsWidget() override { return GetContentWidget(); }

	/**
	* Gets the tool tip's content widget.
	*
	* @return The content widget.
	*/
	virtual TSharedRef<SWidget> GetContentWidget() override
	{
		if (CachedToolTip.IsValid())
		{
			return CachedToolTip.ToSharedRef();
		}

		UWidget* Widget = ToolTipWidgetDelegate.Execute();
		if (Widget)
		{
			CachedToolTip = Widget->TakeWidget();
			return CachedToolTip.ToSharedRef();
		}

		return SNullWidget::NullWidget;
	}

	/**
	* Sets the tool tip's content widget.
	*
	* @param InContentWidget The new content widget to set.
	*/
	virtual void SetContentWidget(const TSharedRef<SWidget>& InContentWidget) override { CachedToolTip = InContentWidget; }

	/**
	* Checks whether this tool tip has no content to display right now.
	*
	* @return true if the tool tip has no content to display, false otherwise.
	*/
	virtual bool IsEmpty() const override { return !ToolTipWidgetDelegate.IsBound(); }

	/**
	* Checks whether this tool tip can be made interactive by the user (by holding Ctrl).
	*
	* @return true if it is an interactive tool tip, false otherwise.
	*/
	virtual bool IsInteractive() const override { return false; }

	virtual void OnClosed() override
	{
		//TODO Notify interface implementing widget of closure

		CachedToolTip.Reset();
	}

	virtual void OnOpening() override
	{
		//TODO Notify interface implementing widget of opening
	}

public:
	UWidget::FGetWidget ToolTipWidgetDelegate;

private:
	TSharedPtr<SWidget> CachedToolTip;
};
}  // namespace ImSlate

TSharedRef<SEditableText> UImEditableText::ConstructImWidget() const
{
	auto MyStealEditableText = SNew(ImSlate::SImEditableText)
								.Style(&WidgetStyle)
								.MinDesiredWidth(GetMinimumDesiredWidth())
								.IsCaretMovedWhenGainFocus(GetIsCaretMovedWhenGainFocus())
								.SelectAllTextWhenFocused(GetSelectAllTextWhenFocused())
								.RevertTextOnEscape(GetRevertTextOnEscape())
								.ClearKeyboardFocusOnCommit(GetClearKeyboardFocusOnCommit())
								.SelectAllTextOnCommit(GetSelectAllTextOnCommit())
								//.OnTextChanged(BIND_UOBJECT_DELEGATE(FOnTextChanged, HandleOnTextChanged))
								//.OnTextCommitted(BIND_UOBJECT_DELEGATE(FOnTextCommitted, HandleOnTextCommitted))
								.VirtualKeyboardType(EVirtualKeyboardType::AsKeyboardType(KeyboardType.GetValue()))
								.VirtualKeyboardOptions(VirtualKeyboardOptions)
								.VirtualKeyboardTrigger(VirtualKeyboardTrigger)
								.VirtualKeyboardDismissAction(VirtualKeyboardDismissAction)
								.Justification(GetJustification());

	MyStealEditableText->SetBorderImage(&BackgroundImage);

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealEditableText->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealEditableText->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealEditableText->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealEditableText->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealEditableText->SetClipping(GetClipping());

	MyStealEditableText->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealEditableText->ForceVolatile(bIsVolatile);
	MyStealEditableText->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealEditableText->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealEditableText->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealEditableText->SetRenderTransformPivot(GetRenderTransformPivot());

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

	//TAttribute<FText> TextBinding = PROPERTY_BINDING(FText, Text);
	//TAttribute<FText> HintTextBinding = PROPERTY_BINDING(FText, HintText);

	MyStealEditableText->SetFont(ImSlate::GetImSlateDefaultFont());

	MyStealEditableText->SetText(GetText());
	MyStealEditableText->SetHintText(GetHintText());
	MyStealEditableText->SetIsReadOnly(GetIsReadOnly());
	MyStealEditableText->SetIsPassword(GetIsPassword());
	MyStealEditableText->SetAllowContextMenu(AllowContextMenu);
	MyStealEditableText->SetVirtualKeyboardDismissAction(VirtualKeyboardDismissAction);
	MyStealEditableText->SetJustification(GetJustification());
	// TODO UMG Complete making all properties settable on SEditableText

	((FShapedTextOptions&)ShapedTextOptions).SynchronizeShapedTextProperties(*MyStealEditableText);

	return MyStealEditableText;
}

TSharedRef<SWidget> UImEditableText::RebuildWidget()
{
	MyEditableText = SNew(ImSlate::SImEditableText)
						.Style(&WidgetStyle)
						.MinDesiredWidth(GetMinimumDesiredWidth())
						.IsCaretMovedWhenGainFocus(GetIsCaretMovedWhenGainFocus())
						.SelectAllTextWhenFocused(GetSelectAllTextWhenFocused())
						.RevertTextOnEscape(GetRevertTextOnEscape())
						.ClearKeyboardFocusOnCommit(GetClearKeyboardFocusOnCommit())
						.SelectAllTextOnCommit(GetSelectAllTextOnCommit())
						.OnTextChanged(BIND_UOBJECT_DELEGATE(FOnTextChanged, HandleOnTextChanged))
						.OnTextCommitted(BIND_UOBJECT_DELEGATE(FOnTextCommitted, HandleOnTextCommitted))
						.VirtualKeyboardType(EVirtualKeyboardType::AsKeyboardType(KeyboardType.GetValue()))
						.VirtualKeyboardOptions(VirtualKeyboardOptions)
						.VirtualKeyboardTrigger(VirtualKeyboardTrigger)
						.VirtualKeyboardDismissAction(VirtualKeyboardDismissAction)
						.Justification(GetJustification());

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyEditableText->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	StaticCastSharedPtr<ImSlate::SImEditableText>(MyEditableText)->SetBorderImage(&BackgroundImage);
	return MyEditableText.ToSharedRef();
}
