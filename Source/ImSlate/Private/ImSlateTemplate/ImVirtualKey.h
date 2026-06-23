// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Types/SlateEnums.h"  // EOrientation (SImStepRuler axis)
#include "Widgets/SLeafWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "ImSlateFactory.h"
#include "ImSlateTemplate/ImVirtualKeyboard.h"

namespace ImSlate
{

DECLARE_DELEGATE_TwoParams(FOnVirtualKeyPressed, const FVirtualKeyDef& /*KeyDef*/, const FString& /*InputValue*/);
DECLARE_DELEGATE_OneParam(FOnVirtualKeyAction, EVirtualKeyAction /*Action*/);
DECLARE_DELEGATE_TwoParams(FOnVirtualKeyLongPress, const FVirtualKeyDef& /*KeyDef*/, const FGeometry& /*KeyGeometry*/);
DECLARE_DELEGATE_OneParam(FOnVirtualKeyLongPressMove, int32 /*HighlightIndex*/);
DECLARE_DELEGATE_OneParam(FOnVirtualKeyLongPressEnd, int32 /*SelectedIndex*/);
DECLARE_DELEGATE_ThreeParams(FOnVirtualKeyPressVisual, const FVirtualKeyDef& /*KeyDef*/, const FGeometry& /*KeyGeometry*/, bool /*bForceStepDrag*/);
DECLARE_DELEGATE_TwoParams(FOnVirtualKeyMoveVisual, const FVector2D& /*Delta from press*/, bool /*bSwipeReady*/);
DECLARE_DELEGATE(FOnVirtualKeyReleaseVisual);

// -1 = auto-scroll left, 0 = stop, 1 = auto-scroll right
DECLARE_DELEGATE_OneParam(FOnSpaceCursorZone, int32 /*Direction*/);

class SImSlateKey : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlateKey)
		: _KeyDef()
		, _bShiftActive(false)
		, _bShiftSingleShot(false)
	{}
		SLATE_ARGUMENT(const FVirtualKeyDef*, KeyDef)
		SLATE_ATTRIBUTE(bool, bShiftActive)
		SLATE_ATTRIBUTE(bool, bShiftSingleShot)
		SLATE_EVENT(FOnVirtualKeyPressed, OnKeyInput)
		SLATE_EVENT(FOnVirtualKeyAction, OnKeyAction)
		SLATE_EVENT(FOnVirtualKeyLongPress, OnLongPress)
		SLATE_EVENT(FOnVirtualKeyLongPressMove, OnLongPressMove)
		SLATE_EVENT(FOnVirtualKeyLongPressEnd, OnLongPressEnd)
		SLATE_EVENT(FOnVirtualKeyPressVisual, OnPressVisual)
		SLATE_EVENT(FOnVirtualKeyMoveVisual, OnMoveVisual)
		SLATE_EVENT(FOnVirtualKeyReleaseVisual, OnReleaseVisual)
		SLATE_EVENT(FOnSpaceCursorZone, OnSpaceCursorZone)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void UpdateKeyDef(const FVirtualKeyDef* InKeyDef);
	void SetLongPressPopupInfo(float PopupCenterAbsX, float CellWidth, int32 CharCount);
	void SetKeyboardWidthGetter(TAttribute<float> InGetter) { KeyboardWidthGetter = MoveTemp(InGetter); }

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	// Capture-loss safety net: if capture is yanked away mid-gesture (Alt+Tab, app deactivate, focus
	// steal) no button-up reaches us, so the swipe/step popup would linger. End the gesture + clear the
	// floating popup here (the engine always fires this when capture goes away).
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	const FVirtualKeyDef* KeyDef = nullptr;
	TAttribute<bool> bShiftActive;
	TAttribute<bool> bShiftSingleShot;
	FOnVirtualKeyPressed OnKeyInput;
	FOnVirtualKeyAction OnKeyAction;
	FOnVirtualKeyLongPress OnLongPress;
	FOnVirtualKeyLongPressMove OnLongPressMove;
	FOnVirtualKeyLongPressEnd OnLongPressEnd;
	FOnVirtualKeyPressVisual OnPressVisual;
	FOnVirtualKeyMoveVisual OnMoveVisual;
	FOnVirtualKeyReleaseVisual OnReleaseVisual;
	FOnSpaceCursorZone OnSpaceCursorZone;
	int32 LastCursorZone = 0;
	bool bWasInOuterZone = false;
	TAttribute<float> KeyboardWidthGetter;

	FVector2D LongPressAnchorPos = FVector2D::ZeroVector;
	float LongPressCellWidth = 0.f;
	int32 LongPressCharCount = 0;
	int32 LongPressSelIndex = 0;  // current selection, advanced by step-drag (not absolute mapping)

	// Four-way step-drag anchors (continuous OnStep). Independent per axis, separate from
	// LongPressAnchorPos (Space/Del's single-axis step-drag) so the two never interfere.
	FVector2D StepAnchorPos = FVector2D::ZeroVector;
	bool bStepDragActive = false;
	// Last-fired step direction per step-drag channel, for ImSlateStepAccumulate's reverse-debounce. Four-way
	// has independent X/Y; the long-press family (Space cursor / spin / Del / Done / selection) is single-axis
	// and mutually exclusive so one suffices. Reset on press/release.
	int32 StepDragLastDirX = 0;
	int32 StepDragLastDirY = 0;
	int32 LongPressLastDir = 0;

	using ESwipeDirection = EImSwipeDir;  // public enum; alias keeps existing references compiling

	bool bIsPressed = false;
	bool bSwipeDetected = false;
	bool bSwipeActive = false;
	bool bLongPressHandled = false;
	bool bSwipeVisualShown = false;
	ESwipeDirection ActiveSwipeDir = ESwipeDirection::None;
	FVector2D PressStartPos = FVector2D::ZeroVector;
	double PressStartTime = 0.0;
	TSharedPtr<FActiveTimerHandle> LongPressTimer;

	static constexpr float LongPressThreshold = 0.5f;

	void HandlePress(const FGeometry& MyGeometry, const FVector2D& ScreenPos);
	void HandleRelease(const FGeometry& MyGeometry, const FVector2D& ScreenPos);
	void HandleMove(const FGeometry& MyGeometry, const FVector2D& ScreenPos);
	// Fire the long-press popup (finger near start, key has LongPressChars). Called by both
	// the auto timer (finger held still) and HandleMove. Returns true if it triggered.
	bool TryTriggerLongPress(const FGeometry& Geometry, const FVector2D& ScreenPos);

	void FireInput(const FString& InputValue);
	void FireAction(EVirtualKeyAction Action);
	ESwipeDirection DetectSwipe(const FVector2D& Delta) const;
};

// A two-state "switch" key, like the system 中/英 key: the CURRENT state is drawn big in the top-left,
// the state a tap switches TO is drawn small in the bottom-right, with a diagonal slash between them. A
// plain tap fires OnClicked. Self-drawn leaf — independent of SImSlateKey (no swipe/long-press), used for
// the keyboard's radix (DEC/HEX) and type (T26/T9) toggles.
class SImSwitchKey : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SImSwitchKey) {}
		SLATE_ATTRIBUTE(FText, CurrentLabel)   // current state, big, top-left
		SLATE_ATTRIBUTE(FText, TargetLabel)    // switch target, small, bottom-right
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override { bIsPressed = false; }

private:
	TAttribute<FText> CurrentLabel;
	TAttribute<FText> TargetLabel;
	FSimpleDelegate OnClicked;
	bool bIsPressed = false;
};

class SImSlateKeyPopup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlateKeyPopup) {}
		SLATE_ARGUMENT(TArray<FString>, Chars)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetHighlightIndex(int32 Index);
	int32 GetHighlightIndex() const { return HighlightIndex; }
	int32 GetCharCount() const { return Chars.Num(); }
	FString GetCharAt(int32 Index) const { return Chars.IsValidIndex(Index) ? Chars[Index] : FString(); }
	float GetCellWidth() const { return CellWidth; }

private:
	TArray<FString> Chars;
	int32 HighlightIndex = -1;
	float CellWidth = 0.f;
	TArray<TSharedPtr<SBorder>> CellBorders;
};

// Scrolling ruler shown during a four-way step-drag. Evenly-spaced tick lines that slide with the
// finger and wrap (looks like an infinite ruler), with a bright-blue baseline fixed at the center;
// each tick crossing the baseline corresponds to one step trigger. Orientation::Vertical draws
// VERTICAL ticks that move left/right (cursor axis); Horizontal draws HORIZONTAL ticks that move
// up/down (value axis). Self-drawn leaf; the keyboard feeds it the live drag offset.
class SImStepRuler : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SImStepRuler)
		: _Axis(EOrientation::Orient_Vertical)
		, _StepW(12.f)
	{}
		SLATE_ARGUMENT(EOrientation, Axis)   // Vertical ticks (cursor/horizontal drag) vs Horizontal ticks (value/vertical drag)
		SLATE_ARGUMENT(float, StepW)         // distance between adjacent tick lines (= one step)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetOffset(float InOffset) { Offset = InOffset; Invalidate(EInvalidateWidgetReason::Paint); }
	float GetOffset() const { return Offset; }
	// Reconfigure for reuse (pooled): switch axis / step spacing without recreating the widget.
	void SetAxis(EOrientation InAxis) { Axis = InAxis; Invalidate(EInvalidateWidgetReason::Paint); }
	void SetStepW(float InStepW) { StepW = FMath::Max(1.f, InStepW); Invalidate(EInvalidateWidgetReason::Paint); }
	// Tick LENGTH as a fraction of the cross-axis extent (long = step ticks, short = subdivisions). Default
	// matches the four-way step popups; the preview ruler overrides them (long = full width = 1 char, short
	// = half).
	void SetTickFractions(float InLong, float InShort) { LongFrac = InLong; ShortFrac = InShort; Invalidate(EInvalidateWidgetReason::Paint); }

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	EOrientation Axis = EOrientation::Orient_Vertical;
	float StepW = 12.f;     // tick spacing in local px
	float Offset = 0.f;     // current drag offset along the axis (wrapped by StepW at paint time)
	float LongFrac = 0.31f; // long-tick length as fraction of cross-axis extent (default = four-way popups)
	float ShortFrac = 0.15f;// short-tick length fraction
};

// Draggable cursor slider — hold and drag left/right to move text cursor
class SImSlateCursorSlider : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlateCursorSlider) {}
		SLATE_EVENT(FOnVirtualKeyAction, OnCursorMove)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;

private:
	FOnVirtualKeyAction OnCursorMove;
	bool bDragging = false;
	float DragAccumulator = 0.f;
	FVector2D LastDragPos = FVector2D::ZeroVector;
	float StepThreshold = 0.f;
};

// Shared rounded dark popup background brush. Defined in ImVirtualKey.cpp; also used by
// ImVirtualKeyboard.cpp's step-drag overlay — declared here so it isn't a TU-local static.
FSlateBrush& GetPopupBgBrush();

}  // namespace ImSlate
