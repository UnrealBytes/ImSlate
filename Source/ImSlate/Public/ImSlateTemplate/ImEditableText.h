// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "SlateCore.h"

#include "Components/EditableText.h"
#include "ImSlateFactory.h"

//
#include "ImEditableText.generated.h"

namespace ImSlate
{

class IMSLATE_API SImEditableText : public SEditableText
{
protected:
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
		const override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	// Keyboard pops on click-RELEASE (no drag) — these track press position and Show on a clean tap.
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	void ShowVirtualKeyboardForSelf();  // build ShowParams + Show the on-screen keyboard for this edit

public:
	virtual ~SImEditableText();

	void SetBorderImage(const TAttribute<const FSlateBrush*>& InBrushAttribute);
	// Request a virtual-keyboard layout on focus (Keyboard_Number → numeric pad). Drives both the
	// self-rendered ImSlate keyboard (mapped in OnFocusReceived) and the OS keyboard.
	// VirtualKeyboardType is a protected TAttribute on the SEditableText base.
	void SetVirtualKeyboardType(EKeyboardType InType) { VirtualKeyboardType = InType; }
	// Numeric-keypad shaping forwarded into FVirtualKeyboardShowParams::Numeric on focus. Stored as
	// plain fields (not FImNumericKeyboardParams) to avoid pulling the virtual-keyboard UObject header
	// into this one. Only consumed when VirtualKeyboardType == Keyboard_Number.
	void SetNumericParams(bool bAllowDecimal, bool bAllowNegative, bool bHex,
		TOptional<double> Min, TOptional<double> Max, TOptional<double> Step, int32 BitWidth = 0)
	{
		bNumAllowDecimal = bAllowDecimal; bNumHex = bHex; NumBitWidth = BitWidth;
		// The ImSlate numeric APIs use FLT_MIN / FLT_MAX as "no lower / no upper bound" sentinels
		// (see InputFloat/NumericFloat defaults). FLT_MIN is the smallest POSITIVE float (~1.17e-38),
		// NOT the lowest — if it leaks through as a real clamp the value can't go at/below ~0 (e.g. a
		// float "won't go negative"). Strip the sentinels here so the keypad treats them as unbounded.
		auto StripSentinel = [](TOptional<double>& V, double Sentinel)
		{ if (V.IsSet() && FMath::IsNearlyEqual((float)V.GetValue(), (float)Sentinel)) V.Reset(); };
		StripSentinel(Min, FLT_MIN);
		StripSentinel(Max, FLT_MAX);
		NumMin = Min; NumMax = Max; NumStep = Step;
		// Allow negatives unless an explicit (real) min clamps the range to >= 0.
		bNumAllowNegative = bAllowNegative && !(NumMin.IsSet() && NumMin.GetValue() >= 0.0);
	}
	// Input history: non-empty key → committed entries are persisted per key and shown as recent
	// suggestions on focus. Filter (optional): only entries passing it are recorded.
	void SetHistoryKey(const FString& InKey) { HistoryKey = InKey; }
	void SetHistoryFilter(TFunction<bool(const FString&)> InFilter) { HistoryFilter = MoveTemp(InFilter); }
	void SetVirtualKeyboardCommitCallback(TFunction<void(const FString&, ETextCommit::Type)> InCallback) { VKCommitCallback = MoveTemp(InCallback); }
	void SetVirtualKeyboardSuggestionProvider(TFunction<void(const FString&, TArray<FString>&)> InProvider) { VKSuggestionProvider = MoveTemp(InProvider); }

	// Preview display mode: used by the virtual keyboard's preview text row.
	// In this mode the widget only *displays* text (driven externally via SetText/GoTo);
	// it never grabs focus or pops the keyboard, and it self-draws the caret (engine
	// only draws the native caret when focused, which we deliberately avoid here).
	void SetPreviewDisplayMode(bool bEnable) { bPreviewDisplayMode = bEnable; }
	// Caret blink toggle, driven by the keyboard's blink timer (OnPaint is const, only reads it).
	void SetPreviewCaretVisible(bool bVisible) { bPreviewCaretVisible = bVisible; }

	// ---- Preview digit-scrubbing support (numeric keypad's drag-to-edit on the preview row) ----
	// Map an absolute screen position to the index of the DIGIT char under it (skips '-' '.'), choosing
	// the nearest digit cell when between/outside. Returns INDEX_NONE if there is no digit. Used to pick
	// the initially-selected digit on press.
	int32 HitTestDigitIndex(const FVector2D& AbsScreenPos) const;
	// Local-space X bounds [OutL,OutR] of the char at CharIndex (already /Scale). false if unavailable.
	bool  GetDigitCellBounds(int32 CharIndex, float& OutL, float& OutR) const;
	// Local-space X of the SELF-DRAWN caret (same source as the blinking caret: GetCursorLocation +
	// GetLocationAt / Scale). false if unavailable. Lets overlays (the scrub ruler) align exactly to the caret.
	bool  GetCaretLocalX(float& OutX) const;
	// Local-space X of the caret at an ARBITRARY text index (not necessarily the live cursor). Used to bound
	// the ruler to a digit cell with the EXACT same coordinate path as the caret. false if unavailable.
	bool  GetLocalXAt(int32 CharIndex, float& OutX) const;
	// Highlight the digit char at this index (INDEX_NONE = none). Repaints when it changes.
	void  SetHighlightDigit(int32 InCharIndex);

private:
	FInvalidatableBrushAttribute BorderImage;
	TFunction<void(const FString&, ETextCommit::Type)> VKCommitCallback;
	TFunction<void(const FString&, TArray<FString>&)> VKSuggestionProvider;
	bool bPreviewDisplayMode = false;
	bool bPreviewCaretVisible = true;
	int32 HighlightDigitIndex = INDEX_NONE;  // preview: digit char to highlight (drag-to-edit selection)
	// Numeric-keypad shaping (see SetNumericParams).
	bool bNumAllowDecimal = true;
	bool bNumAllowNegative = true;
	bool bNumHex = false;
	int32 NumBitWidth = 0;   // integer bit width 8/16/32/64 (0 = float/unknown); → ShowParams.Numeric.BitWidth
	TOptional<double> NumMin, NumMax, NumStep;
	// Input history (see SetHistoryKey/SetHistoryFilter), forwarded into ShowParams on focus.
	FString HistoryKey;
	TFunction<bool(const FString&)> HistoryFilter;
	// Click-release keyboard pop: press position + whether this press is a keyboard-pop candidate.
	FVector2D VKPressScreenPos = FVector2D::ZeroVector;
	bool bVKPressTracking = false;
};

}  // namespace ImSlate

UCLASS(BlueprintType)
class IMSLATE_API UImEditableText
	: public UEditableText
	, public TImFactory<SEditableText>
{
	GENERATED_BODY()
public:
	TSharedRef<SEditableText> ConstructImWidget() const;

protected:
	//~ Begin UWidget Interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Appearance, meta = (DisplayName = "BackgroudBrush"))
	FSlateBrush BackgroundImage;

protected:
	IM_SLATE_PALETTECATEGORY()
};
