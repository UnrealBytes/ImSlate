// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImVirtualKey.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace ImSlate
{

static FSlateBrush& GetKeyBrush(bool bPressed)
{
	static FSlateBrush NormalBrush;
	static FSlateBrush PressedBrush;
	static bool bInit = false;
	if (!bInit)
	{
		auto InitBrush = [](FSlateBrush& Brush, const FLinearColor& Color) {
			Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Brush.TintColor = Color;
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			Brush.OutlineSettings.CornerRadii = FVector4(6, 6, 6, 6);
		};
		InitBrush(NormalBrush, FLinearColor(0.2f, 0.2f, 0.2f, 0.95f));
		InitBrush(PressedBrush, FLinearColor(0.4f, 0.4f, 0.4f, 1.f));
		bInit = true;
	}
	return bPressed ? PressedBrush : NormalBrush;
}

// ==================== SImSlateKey ====================

void SImSlateKey::Construct(const FArguments& InArgs)
{
	KeyDef = InArgs._KeyDef;
	bShiftActive = InArgs._bShiftActive;
	bShiftSingleShot = InArgs._bShiftSingleShot;
	OnKeyInput = InArgs._OnKeyInput;
	OnKeyAction = InArgs._OnKeyAction;
	OnLongPress = InArgs._OnLongPress;
	OnLongPressMove = InArgs._OnLongPressMove;
	OnLongPressEnd = InArgs._OnLongPressEnd;
	OnPressVisual = InArgs._OnPressVisual;
	OnMoveVisual = InArgs._OnMoveVisual;
	OnReleaseVisual = InArgs._OnReleaseVisual;
	OnSpaceCursorZone = InArgs._OnSpaceCursorZone;
}

void SImSlateKey::UpdateKeyDef(const FVirtualKeyDef* InKeyDef)
{
	KeyDef = InKeyDef;
}

void SImSlateKey::SetLongPressPopupInfo(float PopupCenterAbsX, float InCellWidth, int32 InCharCount)
{
	// NOTE: do NOT override LongPressAnchorPos here. Step-drag selection uses the finger's
	// own trigger position (set in TryTriggerLongPress) as the anchor, like Space/Del. Using
	// the popup center instead broke right-side keys: the popup gets clamped left of the
	// finger, so XOffset started positive and left-drag jumped rightward.
	LongPressCellWidth = InCellWidth;
	LongPressCharCount = InCharCount;
}

FVector2D SImSlateKey::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// Use the KEYBOARD scale (= effective × imslate.KeyboardScale), the SAME scale the keyboard
	// layout/spacing use. Previously this used GetImSlateEffectiveScale() (without KeyboardScale),
	// so each key's desired size was smaller than the slot the layout reserved for it — keys looked
	// undersized / row heights inconsistent. Now keys and their slots scale together.
	float Scale = GetImSlateKeyboardScale();
	float H = 32.f * Scale;  // base key height in logical px (× keyboard scale)
	float W = H * 0.85f;
	if (KeyDef)
		W *= KeyDef->WidthMultiplier;
	return FVector2D(W, H);
}

int32 SImSlateKey::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!KeyDef) return LayerId;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const bool bShift = bShiftActive.Get(false);
	// Disabled keys (e.g. 8/9 while in OCT base): draw dimmed; press is ignored in HandlePress.
	const float Dim = KeyDef->bDisabled ? 0.3f : 1.f;

	// Background
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		&GetKeyBrush(bIsPressed),
		ESlateDrawEffect::None,
		GetKeyBrush(bIsPressed).TintColor.GetSpecifiedColor() * FLinearColor(1.f, 1.f, 1.f, Dim));

	TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FVector2D Center = LocalSize * 0.5f;
	float IconSize = FMath::Min(LocalSize.X, LocalSize.Y) * 0.28f;  // self-drawn icons (⌫ ⇪ ✥ …) a bit smaller
	FLinearColor IconColor = FLinearColor(1.f, 1.f, 1.f, Dim);

	bool bDrawnIcon = false;
	if (KeyDef->Action == EVirtualKeyAction::Backspace)
	{
		// Backspace: a tag/arrow outline (point on the left) with an "×" inside the right portion.
		float W = IconSize * 0.9f, H = IconSize * 0.62f;
		TArray<FVector2D> Points;
		Points.Add(Center + FVector2D(-W, 0));
		Points.Add(Center + FVector2D(-W * 0.3f, -H));
		Points.Add(Center + FVector2D(W, -H));
		Points.Add(Center + FVector2D(W, H));
		Points.Add(Center + FVector2D(-W * 0.3f, H));
		Points.Add(Center + FVector2D(-W, 0));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, IconColor, true, 1.5f);

		// The "×" inside the box's right half (clear of the left point). Two crossing strokes.
		const float Cx = Center.X + W * 0.35f;    // × center x: right portion of the tag
		const float R = H * 0.5f;                  // × half-extent (stays inside top/bottom edges)
		TArray<FVector2D> D1 = { FVector2D(Cx - R, Center.Y - R), FVector2D(Cx + R, Center.Y + R) };
		TArray<FVector2D> D2 = { FVector2D(Cx - R, Center.Y + R), FVector2D(Cx + R, Center.Y - R) };
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), D1, ESlateDrawEffect::None, IconColor, true, 1.5f);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), D2, ESlateDrawEffect::None, IconColor, true, 1.5f);
		bDrawnIcon = true;
	}
	else if (KeyDef->Action == EVirtualKeyAction::Shift)
	{
		float S = IconSize;
		bool bSingleShot = bShiftSingleShot.Get(false);
		FVector2D ArrowCenter = bSingleShot ? FVector2D(Center.X, Center.Y - S * 0.15f) : Center;

		TArray<FVector2D> Points;
		Points.Add(ArrowCenter + FVector2D(0, -S));
		Points.Add(ArrowCenter + FVector2D(S * 0.8f, S * 0.2f));
		Points.Add(ArrowCenter + FVector2D(S * 0.3f, S * 0.2f));
		Points.Add(ArrowCenter + FVector2D(S * 0.3f, S * 0.7f));
		Points.Add(ArrowCenter + FVector2D(-S * 0.3f, S * 0.7f));
		Points.Add(ArrowCenter + FVector2D(-S * 0.3f, S * 0.2f));
		Points.Add(ArrowCenter + FVector2D(-S * 0.8f, S * 0.2f));
		Points.Add(ArrowCenter + FVector2D(0, -S));

		if (bShift)
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, IconColor, true, 3.f);
		else
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, FLinearColor(0.6f, 0.6f, 0.6f), true, 1.5f);

		if (bSingleShot)
		{
			float UnderY = ArrowCenter.Y + S * 0.9f;
			TArray<FVector2D> Line;
			Line.Add(FVector2D(Center.X - S * 0.6f, UnderY));
			Line.Add(FVector2D(Center.X + S * 0.6f, UnderY));
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, IconColor, false, 2.f);
		}
		bDrawnIcon = true;
	}
	else if (KeyDef->Action == EVirtualKeyAction::Left)
	{
		// ◀ ▶ two triangles
		float S = IconSize * 0.5f, Gap = IconSize * 0.6f;
		TArray<FVector2D> L;
		L.Add(Center + FVector2D(-Gap - S, 0));
		L.Add(Center + FVector2D(-Gap + S, -S));
		L.Add(Center + FVector2D(-Gap + S, S));
		L.Add(Center + FVector2D(-Gap - S, 0));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), L, ESlateDrawEffect::None, IconColor, true, 1.5f);
		TArray<FVector2D> R;
		R.Add(Center + FVector2D(Gap + S, 0));
		R.Add(Center + FVector2D(Gap - S, -S));
		R.Add(Center + FVector2D(Gap - S, S));
		R.Add(Center + FVector2D(Gap + S, 0));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), R, ESlateDrawEffect::None, IconColor, true, 1.5f);
		bDrawnIcon = true;
	}

	// Whether a top hint will be drawn below — used to push the main label toward the LOWER half so the
	// hint (top) and label (lower-center) don't crowd each other (matches system keyboards).
	const bool bHasHint = !KeyDef->Swipe.Up.Label.IsEmpty() || (!KeyDef->Value.IsEmpty() && KeyDef->Value != KeyDef->GetDisplayLabel(bShift));

	if (!bDrawnIcon)
	{
		// Main label text
		FSlateFontInfo MainFont = GetImSlateDefaultFont(10);
		FString DisplayLabel = KeyDef->GetDisplayLabel(bShift);
		FVector2D TextSize = (FVector2D)FontMeasure->Measure(DisplayLabel, MainFont);
		// Centered horizontally; vertically centered when there's no hint, else nudged into the lower
		// half so the hint sits clearly above it.
		FVector2D TextPos((LocalSize.X - TextSize.X) * 0.5f,
			bHasHint ? (LocalSize.Y * 0.58f - TextSize.Y * 0.5f) : (LocalSize.Y - TextSize.Y) * 0.5f);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(TextPos))),
			DisplayLabel, MainFont, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Dim));
	}

	// Top hint: T9 keys show number (Value), T26 keys show swipe-up char. Centered along the TOP edge
	// (was a tiny top-right corner) and a bit larger, so it's readable.
	{
		// Prefer the swipe-up symbol as the hint (T26 punctuation like ',' over the '.' key, etc.);
		// fall back to Value (the T9 number) when there's no swipe-up. (Width-based detection was wrong:
		// a WIDE punctuation key like the bottom '.' is also >=2 wide but should show its swipe-up ','.)
		// T9 keys (wide, WidthMultiplier>=2): show the NUMBER (Value) top-right ONLY — their Swipe.Up is a
		// letter (e.g. 'a'), which must NOT appear as a centered hint. Other keys (narrow): show the
		// swipe-up symbol (e.g. ',' over the '.' key) top-centered.
		const bool bIsT9 = (KeyDef->WidthMultiplier >= 2.f);
		FString HintText;
		bool bHintIsNumber = false;          // true → T9 number (Value), shown top-RIGHT
		if (bIsT9)
		{
			HintText = KeyDef->Value;
			bHintIsNumber = !HintText.IsEmpty();
		}
		else
		{
			HintText = KeyDef->Swipe.Up.Label;
		}

		// Skip the hint when it duplicates the main label (e.g. the T9 "0" key, whose label AND number
		// are both "0" → don't draw a second 0 stacked on top).
		if (HintText == KeyDef->GetDisplayLabel(bShift))
			HintText.Empty();

		if (!HintText.IsEmpty())
		{
			FSlateFontInfo HintFont = GetImSlateDefaultFont(7);
			FLinearColor HintColor(0.72f, 0.72f, 0.72f, 0.95f * Dim);  // brighter so thin glyphs like ',' read
			FVector2D HintSize = (FVector2D)FontMeasure->Measure(HintText, HintFont);
			const float Margin = 3.f;
			// T9 number hint → top-RIGHT corner (per request). Swipe-up symbol hint → top-centered.
			FVector2D HintPos = bHintIsNumber
				? FVector2D(LocalSize.X - HintSize.X - Margin, Margin)
				: FVector2D((LocalSize.X - HintSize.X) * 0.5f, 1.f);
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(HintSize, FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(HintPos))),
				HintText, HintFont, ESlateDrawEffect::None, HintColor);
		}
	}

	return LayerId + 1;
}

// ==================== Input Handling ====================

FReply SImSlateKey::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (KeyDef && KeyDef->bDisabled)
		return FReply::Unhandled();
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		HandlePress(MyGeometry, MouseEvent.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

// On rapid repeated taps Slate routes the 2nd press as a double-click instead of a
// regular down. Without this override that press would fall through to the keyboard
// (which swallows double-clicks) and its paired up would arrive with bIsPressed=false
// and be dropped — losing every other tap. Treat a double-click as a normal press.
FReply SImSlateKey::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SImSlateKey::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && HasMouseCapture())
	{
		HandleRelease(MyGeometry, MouseEvent.GetScreenSpacePosition());
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SImSlateKey::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsPressed)
	{
		HandleMove(MyGeometry, MouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SImSlateKey::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (KeyDef && KeyDef->bDisabled)
		return FReply::Unhandled();
	// During a preview scrub, don't consume/capture this touch — bubble it UP so the keyboard can register it
	// as a relay finger (a 2nd finger landing on a key continues the scrub instead of typing). HandlePress also
	// no-ops mid-scrub, but returning Unhandled here is what lets the event reach the parent keyboard.
	if (TSharedPtr<SImSlateVirtualKeyboard> Kb = SImSlateVirtualKeyboard::Get())
		if (Kb->IsPreviewDragging())
			return FReply::Unhandled();
	HandlePress(MyGeometry, InTouchEvent.GetScreenSpacePosition());
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SImSlateKey::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (bIsPressed)
		HandleMove(MyGeometry, InTouchEvent.GetScreenSpacePosition());
	return FReply::Handled();
}

FReply SImSlateKey::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	HandleRelease(MyGeometry, InTouchEvent.GetScreenSpacePosition());
	return FReply::Handled().ReleaseMouseCapture();
}

void SImSlateKey::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Capture went away without a button-up (Alt+Tab / app deactivate / focus steal). Tear down any
	// floating swipe/step popup and reset the gesture state so it doesn't linger or mis-fire next time.
	if (LongPressTimer.IsValid()) { UnRegisterActiveTimer(LongPressTimer.ToSharedRef()); LongPressTimer.Reset(); }
	if (bIsPressed || bSwipeVisualShown || bLongPressHandled || bStepDragActive)
		OnReleaseVisual.ExecuteIfBound();  // dismiss the popup (same hook HandleRelease uses)
	bIsPressed = false;
	bSwipeDetected = false;
	bSwipeActive = false;
	bLongPressHandled = false;
	bSwipeVisualShown = false;
	bStepDragActive = false;
	ActiveSwipeDir = ESwipeDirection::None;
	// (SWidget::OnMouseCaptureLost is a no-op base; nothing to forward.)
}

// ==================== Gesture Logic ====================

void SImSlateKey::HandlePress(const FGeometry& MyGeometry, const FVector2D& ScreenPos)
{
	// Disabled keys (e.g. 8/9 in OCT base) are inert: painted dimmed, swallow no gesture.
	if (KeyDef && KeyDef->bDisabled)
		return;

	// While the preview value is being scrub-dragged, keys are inert: a finger drifting onto a key (or a relay
	// finger landing on one) must not type. HandlePress is the single press entry for ALL keys, so one check
	// here covers every digit/function key. (Keyboard already exists — this key is its child — so Get() won't
	// create one.)
	if (TSharedPtr<SImSlateVirtualKeyboard> Kb = SImSlateVirtualKeyboard::Get())
		if (Kb->IsPreviewDragging())
			return;

	bIsPressed = true;
	bSwipeDetected = false;
	bSwipeActive = false;
	bLongPressHandled = false;
	bWasInOuterZone = false;
	bSwipeVisualShown = false;
	bStepDragActive = false;
	StepDragLastDirX = 0;  // fresh press → no stale reverse-debounce direction on any step-drag channel
	StepDragLastDirY = 0;
	LongPressLastDir = 0;
	StepAnchorPos = ScreenPos;
	ActiveSwipeDir = ESwipeDirection::None;
	LastCursorZone = 0;
	PressStartPos = ScreenPos;
	PressStartTime = FPlatformTime::Seconds();

	// Auto long-press: fire the popup after LongPressThreshold even if the finger never moves
	// (HandleMove only runs on movement, so a still hold would otherwise never trigger).
	if (KeyDef && KeyDef->LongPressChars.Num() > 0)
	{
		LongPressTimer = RegisterActiveTimer(LongPressThreshold,
			FWidgetActiveTimerDelegate::CreateLambda([this](double, float) -> EActiveTimerReturnType {
				LongPressTimer.Reset();
				if (bIsPressed)
					TryTriggerLongPress(GetCachedGeometry(), PressStartPos);  // finger still at start
				return EActiveTimerReturnType::Stop;
			}));
	}
}

bool SImSlateKey::TryTriggerLongPress(const FGeometry& Geometry, const FVector2D& ScreenPos)
{
	if (bLongPressHandled || bSwipeVisualShown || !KeyDef || KeyDef->LongPressChars.Num() == 0)
		return false;
	bLongPressHandled = true;
	LongPressAnchorPos = ScreenPos;
	LongPressSelIndex = KeyDef->LongPressChars.Num() / 2;  // start at the middle entry
	OnLongPress.ExecuteIfBound(*KeyDef, Geometry);
	return true;
}

void SImSlateKey::HandleRelease(const FGeometry& MyGeometry, const FVector2D& ScreenPos)
{
	if (LongPressTimer.IsValid()) { UnRegisterActiveTimer(LongPressTimer.ToSharedRef()); LongPressTimer.Reset(); }

	if (!bIsPressed || !KeyDef)
	{
		bIsPressed = false;
		return;
	}

	double PressDuration = FPlatformTime::Seconds() - PressStartTime;
	float SwipeThreshold = 8.f * GetImSlateEffectiveScale();
	bIsPressed = false;
	OnReleaseVisual.ExecuteIfBound();
	if (LastCursorZone != 0)
		OnSpaceCursorZone.ExecuteIfBound(0);

	// Four-way step-drag already fired its OnStep calls continuously during the move. On release we
	// only finish: fire the single-shot Callback for the final direction IF that direction has one
	// (orthogonal channel — most step directions don't), then stop. No FireAction fallback (which
	// would e.g. spuriously move the cursor on the cursor key).
	if (bStepDragActive)
	{
		FVector2D FinalDelta = ScreenPos - PressStartPos;
		if (FinalDelta.Size() >= SwipeThreshold)
		{
			const FVirtualKeySwipeEntry& E = KeyDef->Swipe.Entry(DetectSwipe(FinalDelta));
			if (!E.bStep && E.Callback.IsBound())  // single-shot only where step isn't owning it
				E.Callback.Execute();
		}
		bStepDragActive = false;
		return;
	}

	if (bSwipeActive)
	{
		FVector2D FinalDelta = ScreenPos - PressStartPos;

		// If finger returned to center, treat as tap (input Value/default)
		if (FinalDelta.Size() < SwipeThreshold * 1.5f)
		{
			if (KeyDef->Action != EVirtualKeyAction::Char)
				FireAction(KeyDef->Action);
			else
				FireInput(KeyDef->GetInputValue(bShiftActive.Get(false)));
			return;
		}

		ESwipeDirection FinalDir = DetectSwipe(FinalDelta);
		const FVirtualKeySwipeEntry* Entry = nullptr;
		switch (FinalDir)
		{
		case ESwipeDirection::Up:    Entry = &KeyDef->Swipe.Up; break;
		case ESwipeDirection::Down:  Entry = &KeyDef->Swipe.Down; break;
		case ESwipeDirection::Left:  Entry = &KeyDef->Swipe.Left; break;
		case ESwipeDirection::Right: Entry = &KeyDef->Swipe.Right; break;
		default: break;
		}

		if (Entry && Entry->IsSet())
		{
			if (Entry->Callback.IsBound())
				Entry->Callback.Execute();
			else
				FireInput(Entry->Label);
		}
		else
		{
			// Fallback: no swipe configured for this direction → input default Value
			if (KeyDef->Action != EVirtualKeyAction::Char)
				FireAction(KeyDef->Action);
			else
				FireInput(KeyDef->GetInputValue(bShiftActive.Get(false)));
		}
		return;
	}

	if (bSwipeDetected)
		return;

	if (bLongPressHandled)
	{
		if (LongPressCharCount > 0)
		{
			// Use the step-drag accumulated selection (matches the highlight shown during move).
			OnLongPressEnd.ExecuteIfBound(LongPressSelIndex);
		}
		return;
	}

	if (KeyDef->Action != EVirtualKeyAction::Char)
	{
		FireAction(KeyDef->Action);
	}
	else
	{
		FireInput(KeyDef->GetInputValue(bShiftActive.Get(false)));
	}
}

void SImSlateKey::HandleMove(const FGeometry& MyGeometry, const FVector2D& ScreenPos)
{
	if (bSwipeDetected || !KeyDef) return;

	FVector2D Delta = ScreenPos - PressStartPos;
	float SwipeThreshold = 8.f * GetImSlateEffectiveScale();
	// Keys whose horizontal drag does a repeating "step-drag" (every StepW of motion fires once):
	// Space (cursor), Backspace (delete/undo), and the numeric spin keys StepUp/StepDown (+/- value).
	const bool bIsStep = (KeyDef->Action == EVirtualKeyAction::StepUp || KeyDef->Action == EVirtualKeyAction::StepDown);
	const bool bHorizontal = FMath::Abs(Delta.X) > FMath::Abs(Delta.Y);
	// Done (Enter) AND Del (Backspace): HORIZONTAL drag = step-drag (Del=delete/undo, Done=delete/undo);
	// VERTICAL swipe = four-way (Del up=Clr / Done up=Clr,down=Esc). So a vertical drag must NOT enter
	// step-drag — it has to fall through to the swipe-visual path so the up/down Callback fires.
	// (Previously Del entered step-drag on ANY direction, which swallowed its up-swipe Clr.)
	const bool bIsDone = (KeyDef->Action == EVirtualKeyAction::Enter);
	// Only the spin keys (StepUp/StepDown) still use the legacy any-direction step-drag. Space and Del
	// moved to the new four-way bStep+OnStep path (in-popup scrolling ruler), so they must NOT enter the
	// legacy path here — they're handled by the swipe-visual + step block below.
	const bool bAnyDirStepDrag   = bIsStep;
	// Done: HORIZONTAL drag = legacy step-drag (delete/undo bar); VERTICAL = four-way popup (Clr/Esc).
	// Decided by the dominant axis so the two are mutually exclusive.
	const bool bHorizOnlyStepDrag = bIsDone && bHorizontal;

	// Step-drag past threshold (Space/spin any direction; Del/Done horizontal only).
	if (!bLongPressHandled && !bSwipeVisualShown && (bAnyDirStepDrag || bHorizOnlyStepDrag) && Delta.Size() >= SwipeThreshold)
	{
		bLongPressHandled = true;
		LongPressAnchorPos = PressStartPos;
		// Show the horizontal step-drag floating hint (◀ Del | Undo ▶). Done now shows it too, so its
		// horizontal drag has its OWN popup distinct from its vertical four-way (Clr/Esc) — and since
		// only one of the two paths can arm per gesture (dominant axis), the two popups are mutually exclusive.
		OnPressVisual.ExecuteIfBound(*KeyDef, MyGeometry, /*bForceStepDrag*/ true);
	}

	// Long-press check (finger near start, for keys with LongPressChars).
	// The auto timer started in HandlePress is the primary trigger; this is the fallback when
	// the finger is moving slightly. Skip if a swipe popup is already active (sliding back must
	// not flip swipe → long-press).
	if (!bLongPressHandled && !bSwipeVisualShown && KeyDef->LongPressChars.Num() > 0 && Delta.Size() < SwipeThreshold
		&& FPlatformTime::Seconds() - PressStartTime >= LongPressThreshold)
	{
		TryTriggerLongPress(MyGeometry, ScreenPos);
	}

	// Swipe popup trigger: finger moved past the swipe threshold → show four-way visual.
	// Use distance from the press point (not "left the key bounds"), so edge keys — which
	// can't be dragged outside toward the screen edge — still pop their four-way popup.
	if (!bLongPressHandled && !bSwipeVisualShown && KeyDef->Swipe.HasAny())
	{
		if (Delta.Size() >= SwipeThreshold)
		{
			bSwipeVisualShown = true;
			OnPressVisual.ExecuteIfBound(*KeyDef, MyGeometry, /*bForceStepDrag*/ false);  // four-way popup
			if (KeyDef->Swipe.HasStep())  // arm four-way step-drag from here
			{
				bStepDragActive = true;
				StepAnchorPos = PressStartPos;
			}
		}
	}

	// Four-way step-drag: each bStep direction fires the key-level OnStep once per StepW of travel,
	// on BOTH axes independently (drag up/down adjusts, left/right moves cursor — they can run at
	// once). Mirrors Del's left/right step-drag but generalized to four directions with one callback.
	// Uses its own StepAnchorPos so it never collides with Space/Del's LongPressAnchorPos path.
	if (bStepDragActive && KeyDef->Swipe.OnStep)
	{
		const float StepW = 12.f * GetImSlateEffectiveScale();
		const float Scale = GetImSlateEffectiveScale();
		// Horizontal (right = +X). Feed the offset to the shared helper (reverse-debounce), then advance the
		// anchor by however much it consumed. Vertical mirrors it (screen Y down → up = -Y).
		float XOff = ScreenPos.X - StepAnchorPos.X;
		ImSlateStepAccumulate(XOff, StepDragLastDirX, StepW, Scale,
			[this] { if (KeyDef->Swipe.Right.bStep) KeyDef->Swipe.OnStep(EImSwipeDir::Right); },
			[this] { if (KeyDef->Swipe.Left.bStep)  KeyDef->Swipe.OnStep(EImSwipeDir::Left); });
		StepAnchorPos.X = ScreenPos.X - XOff;
		float YOff = ScreenPos.Y - StepAnchorPos.Y;
		ImSlateStepAccumulate(YOff, StepDragLastDirY, StepW, Scale,
			[this] { if (KeyDef->Swipe.Down.bStep) KeyDef->Swipe.OnStep(EImSwipeDir::Down); },  // +Y = down
			[this] { if (KeyDef->Swipe.Up.bStep)   KeyDef->Swipe.OnStep(EImSwipeDir::Up); });   // -Y = up
		StepAnchorPos.Y = ScreenPos.Y - YOff;
		// Drive the ruler visual (absolute drag delta; the visual itself does the fmod wrap).
		OnMoveVisual.ExecuteIfBound(FVector2D(ScreenPos.X - PressStartPos.X, ScreenPos.Y - PressStartPos.Y), true);
		return;  // step-drag owns this move; skip the single-shot swipe-visual direction logic below
	}

	// Step-drag active: Space = cursor, Del = delete/undo, LongPress = popup
	if (bLongPressHandled)
	{
		float XOffset = ScreenPos.X - LongPressAnchorPos.X;
		const float StepW = 12.f * GetImSlateEffectiveScale();
		const float Scale = GetImSlateEffectiveScale();
		OnMoveVisual.ExecuteIfBound(FVector2D(XOffset, 0.f), true);

		// All long-press step-drags are single horizontal-axis and mutually exclusive (only one branch runs),
		// so they share LongPressLastDir for the shared reverse-debounce helper. Each branch supplies its
		// Pos (drag-right) / Neg (drag-left) action; the anchor is then advanced by however much was consumed.
		if (KeyDef->Action == EVirtualKeyAction::Space)
		{
			ImSlateStepAccumulate(XOffset, LongPressLastDir, StepW, Scale,
				[this] { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::Right); },
				[this] { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::Left); });
		}
		else if (bIsStep)
		{
			// Numeric spin keys: horizontal drag continuously adjusts the value (right = +, left = −). A plain
			// tap (no drag) still fires the key's own StepUp/StepDown once via HandleRelease.
			ImSlateStepAccumulate(XOffset, LongPressLastDir, StepW, Scale,
				[this] { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::StepUp); },
				[this] { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::StepDown); });
		}
		else if (KeyDef->Action == EVirtualKeyAction::Backspace || KeyDef->Action == EVirtualKeyAction::Enter)
		{
			// Del / Done horizontal drag: right = Undo, left = Delete.
			ImSlateStepAccumulate(XOffset, LongPressLastDir, StepW, Scale,
				[this] { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::UndoBackspace); },
				[this] { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::Backspace); });
		}
		else if (LongPressCharCount > 0)
		{
			// Step-drag selection: each StepW of travel advances the selection by one (accumulating scan, not
			// an absolute finger-position mapping).
			const int32 OldIndex = LongPressSelIndex;
			ImSlateStepAccumulate(XOffset, LongPressLastDir, StepW, Scale,
				[this] { LongPressSelIndex = FMath::Min(LongPressSelIndex + 1, LongPressCharCount - 1); },
				[this] { LongPressSelIndex = FMath::Max(LongPressSelIndex - 1, 0); });
			if (LongPressSelIndex != OldIndex)
				OnLongPressMove.ExecuteIfBound(LongPressSelIndex);
		}
		LongPressAnchorPos.X = ScreenPos.X - XOffset;  // advance anchor by the consumed amount
		return;
	}

	// Swipe visual: a direction activates when the finger crosses, in that direction, the
	// smaller of (a) the standard "leave the key bounds" distance and (b) the space actually
	// available before the finger hits the screen edge. So a normal key keeps the standard
	// bounds behavior, while an EDGE key — which can't be dragged past its border toward the
	// screen edge — gets a reduced threshold on its squeezed side so that direction is still
	// reachable. Only the squeezed side is relaxed; the others stay standard.
	if (bSwipeVisualShown)
	{
		const FVector2D KeySize = MyGeometry.GetLocalSize();
		const float GeoScale = MyGeometry.GetAccumulatedLayoutTransform().GetScale();
		// Standard distance to "leave" the key from its center, in absolute screen px.
		const float StdX = (KeySize.X * 0.5f) * GeoScale;
		const float StdY = (KeySize.Y * 0.5f) * GeoScale;
		const float Margin = 6.f * GeoScale;  // keep a little room from the very edge

		// Absolute key rect and the window (screen) rect the finger can move within.
		const FVector2D AbsTL = MyGeometry.LocalToAbsolute(FVector2D::ZeroVector);
		const FVector2D AbsBR = MyGeometry.LocalToAbsolute(KeySize);
		FVector2D ScreenMin(0.f, 0.f), ScreenMax(FLT_MAX, FLT_MAX);
		if (TSharedPtr<SWindow> Win = FSlateApplication::Get().FindWidgetWindow(SharedThis(this)))
		{
			const FGeometry& WG = Win->GetTickSpaceGeometry();
			ScreenMin = WG.LocalToAbsolute(FVector2D::ZeroVector);
			ScreenMax = WG.LocalToAbsolute(WG.GetLocalSize());
		}
		// Reachable travel before hitting the screen edge, per direction (absolute px).
		const float ReachL = FMath::Max(0.f, (AbsTL.X - ScreenMin.X) - Margin);
		const float ReachR = FMath::Max(0.f, (ScreenMax.X - AbsBR.X) - Margin);
		const float ReachU = FMath::Max(0.f, (AbsTL.Y - ScreenMin.Y) - Margin);
		const float ReachD = FMath::Max(0.f, (ScreenMax.Y - AbsBR.Y) - Margin);

		// Per-direction threshold: standard, but capped to what's reachable on a squeezed side.
		const float MinThresh = 8.f * GetImSlateEffectiveScale();
		const float ThreshL = FMath::Max(MinThresh, FMath::Min(StdX, ReachL));
		const float ThreshR = FMath::Max(MinThresh, FMath::Min(StdX, ReachR));
		const float ThreshU = FMath::Max(MinThresh, FMath::Min(StdY, ReachU));
		const float ThreshD = FMath::Max(MinThresh, FMath::Min(StdY, ReachD));

		bool bDirActive = false;
		if (FMath::Abs(Delta.X) > FMath::Abs(Delta.Y))
			bDirActive = (Delta.X > 0) ? (Delta.X >= ThreshR) : (-Delta.X >= ThreshL);
		else
			bDirActive = (Delta.Y < 0) ? (-Delta.Y >= ThreshU) : (Delta.Y >= ThreshD);

		OnMoveVisual.ExecuteIfBound(Delta, bDirActive);

		if (bDirActive)
		{
			ActiveSwipeDir = DetectSwipe(Delta);
			bSwipeActive = true;
		}
		else
		{
			bSwipeActive = false;
			ActiveSwipeDir = ESwipeDirection::None;
		}
	}
}

SImSlateKey::ESwipeDirection SImSlateKey::DetectSwipe(const FVector2D& Delta) const
{
	if (FMath::Abs(Delta.X) > FMath::Abs(Delta.Y))
		return Delta.X > 0 ? ESwipeDirection::Right : ESwipeDirection::Left;
	else
		return Delta.Y < 0 ? ESwipeDirection::Up : ESwipeDirection::Down;
}

void SImSlateKey::FireInput(const FString& InputValue)
{
	if (!InputValue.IsEmpty() && KeyDef)
		OnKeyInput.ExecuteIfBound(*KeyDef, InputValue);
}

void SImSlateKey::FireAction(EVirtualKeyAction Action)
{
	OnKeyAction.ExecuteIfBound(Action);
}

// ==================== SImSlateKeyPopup ====================

// Declared in ImVirtualKey.h (shared with ImVirtualKeyboard.cpp); not a TU-local static.
FSlateBrush& GetPopupBgBrush()
{
	static FSlateBrush Brush;
	static bool bInit = false;
	if (!bInit)
	{
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FLinearColor(0.12f, 0.12f, 0.12f, 0.98f);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(8, 8, 8, 8);
		Brush.OutlineSettings.Color = FLinearColor(0.3f, 0.3f, 0.3f, 1.f);
		Brush.OutlineSettings.Width = 1.f;
		bInit = true;
	}
	return Brush;
}

// ==================== SImSwitchKey ====================

void SImSwitchKey::Construct(const FArguments& InArgs)
{
	CurrentLabel = InArgs._CurrentLabel;
	TargetLabel = InArgs._TargetLabel;
	OnClicked = InArgs._OnClicked;
}

FVector2D SImSwitchKey::ComputeDesiredSize(float) const
{
	const float Scale = GetImSlateKeyboardScale();
	return FVector2D(32.f * Scale * 1.4f, 32.f * Scale);  // a touch wider than a square key for two labels
}

int32 SImSwitchKey::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();

	// Background (same rounded brush as the regular keys; brighter when pressed).
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		&GetKeyBrush(bIsPressed),
		ESlateDrawEffect::None,
		GetKeyBrush(bIsPressed).TintColor.GetSpecifiedColor());

	TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FString TopText = CurrentLabel.Get().ToString();  // current state
	const FString BotText = TargetLabel.Get().ToString();   // switch target

	// Diagonal slash from bottom-left to top-right (the 中/英 separator).
	TArray<FVector2D> Slash = {
		FVector2D(LocalSize.X * 0.18f, LocalSize.Y * 0.82f),
		FVector2D(LocalSize.X * 0.82f, LocalSize.Y * 0.18f)
	};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Slash,
		ESlateDrawEffect::None, FLinearColor(0.5f, 0.5f, 0.5f, 0.7f), true, 1.f);

	// Pick the largest font (≤ a preferred logical size) whose rendered width fits MaxWidthPx, so a 3-char
	// label like "DEC" shrinks on a narrow key instead of overflowing (the old fixed 11pt was too big there).
	auto FitFont = [&](const FString& Str, int32 PreferredSize, float MaxWidthPx, int32 MinSize) -> FSlateFontInfo
	{
		FSlateFontInfo Font = GetImSlateDefaultFont(PreferredSize);
		const float W = (float)FontMeasure->Measure(Str, Font).X;
		if (W > MaxWidthPx && W > 0.f)
		{
			const int32 Fitted = FMath::Clamp(FMath::FloorToInt(PreferredSize * (MaxWidthPx / W)), MinSize, PreferredSize);
			Font = GetImSlateDefaultFont(Fitted);
		}
		return Font;
	};

	// Current state: big, top-left (above the slash). Switch target: small, bottom-right (below it).
	if (!TopText.IsEmpty())
	{
		// Available width = from the left inset (0.10) to roughly the slash mid-line; clamp so it never spills.
		FSlateFontInfo TopFont = FitFont(TopText, 11, LocalSize.X * 0.62f, 6);
		const FVector2D TopSize = (FVector2D)FontMeasure->Measure(TopText, TopFont);
		const FVector2D TopPos(LocalSize.X * 0.10f, LocalSize.Y * 0.12f);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(TopSize, FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(TopPos))),
			TopText, TopFont, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 1.f));
	}
	if (!BotText.IsEmpty())
	{
		FSlateFontInfo BotFont = FitFont(BotText, 7, LocalSize.X * 0.55f, 5);
		const FVector2D BotSize = (FVector2D)FontMeasure->Measure(BotText, BotFont);
		const FVector2D BotPos(LocalSize.X * 0.90f - BotSize.X, LocalSize.Y * 0.88f - BotSize.Y);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(BotSize, FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(BotPos))),
			BotText, BotFont, ESlateDrawEffect::None, FLinearColor(0.62f, 0.62f, 0.62f, 0.95f));
	}
	return LayerId + 1;
}

FReply SImSwitchKey::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsPressed = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SImSwitchKey::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && HasMouseCapture())
	{
		bIsPressed = false;
		// Fire only if released inside the key (standard button behaviour).
		if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
			OnClicked.ExecuteIfBound();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SImSwitchKey::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	bIsPressed = true;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SImSwitchKey::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (HasMouseCapture())
	{
		bIsPressed = false;
		if (MyGeometry.IsUnderLocation(InTouchEvent.GetScreenSpacePosition()))
			OnClicked.ExecuteIfBound();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void SImSlateKeyPopup::Construct(const FArguments& InArgs)
{
	Chars = InArgs._Chars;
	HighlightIndex = -1;
	float Scale = GetImSlateEffectiveScale();
	CellWidth = 44.f * Scale;

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 i = 0; i < Chars.Num(); ++i)
	{
		TSharedPtr<SBorder> CellBorder;
		// Each cell is forced to a fixed width (CellWidth) so the on-screen layout matches
		// the index-mapping math in SImSlateKey::HandleMove (which steps by CellWidth).
		// Previously cells were AutoWidth → actual width ≠ CellWidth → highlight drifted
		// from the finger position.
		Row->AddSlot()
		.AutoWidth()
		.Padding(1.f)
		[
			SNew(SBox)
			.WidthOverride(CellWidth)
			[
				SAssignNew(CellBorder, SBorder)
				.BorderImage(&GetKeyBrush(false))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 2.f * Scale))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Chars[i]))
					.Font(GetImSlateDefaultFont(16))
					.ColorAndOpacity(FLinearColor::White)
				]
			]
		];
		CellBorders.Add(CellBorder);
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(&GetPopupBgBrush())
		.Padding(4.f * Scale)
		[
			Row
		]
	];
}

void SImSlateKeyPopup::SetHighlightIndex(int32 Index)
{
	if (HighlightIndex == Index) return;
	if (CellBorders.IsValidIndex(HighlightIndex))
		CellBorders[HighlightIndex]->SetBorderImage(&GetKeyBrush(false));
	HighlightIndex = Index;
	if (CellBorders.IsValidIndex(HighlightIndex))
		CellBorders[HighlightIndex]->SetBorderImage(&GetKeyBrush(true));
}

// ==================== SImStepRuler ====================

void SImStepRuler::Construct(const FArguments& InArgs)
{
	Axis  = InArgs._Axis;
	StepW = FMath::Max(1.f, InArgs._StepW);
}

FVector2D SImStepRuler::ComputeDesiredSize(float) const
{
	// Transparent overlay: contributes NO desired size, so it never enlarges the popup. It simply
	// fills (HAlign/VAlign_Fill) whatever area the popup's Grid defines and paints ticks over it.
	return FVector2D::ZeroVector;
}

int32 SImStepRuler::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	// NO background, NO center baseline — just tick lines over the popup. Grey ticks vanished against
	// the grey popup, so use the popup's accent blue (long brighter, short dimmer) for contrast.
	const FLinearColor LongTick (0.30f, 0.80f, 1.00f, 0.95f);
	const FLinearColor ShortTick(0.30f, 0.80f, 1.00f, 0.45f);

	const FPaintGeometry PG = AllottedGeometry.ToPaintGeometry();
	const int32 SubDiv = 5;                 // 1 long + 4 short per StepW
	const float SubW = StepW / SubDiv;      // spacing of the fine (short) ticks

	// Ticks GROW FROM THE EDGE (not centered). Length & thickness scale by proportion: long ticks
	// (step points) reach deeper and are thicker; short ticks (subdivisions) are shallow and thin.
	// Vertical ticks (cursor axis) grow DOWN from the TOP edge; horizontal ticks (value axis) grow
	// LEFT from the RIGHT edge.
	const float LongThick = 1.8f;
	const float ShortThick = 1.0f;
	auto DrawTick = [&](float P, bool bLong)
	{
		const FLinearColor C = bLong ? LongTick : ShortTick;
		const float Thick = bLong ? LongThick : ShortThick;
		const float Frac  = bLong ? LongFrac : ShortFrac;  // member fractions (preview overrides to 1.0 / 0.5)
		if (Axis == EOrientation::Orient_Vertical)
		{
			// vertical tick at X=P, grows down from the top edge (y=0).
			const float Len = Frac * Size.Y;
			TArray<FVector2D> L = { FVector2D(P, 0.f), FVector2D(P, Len) };
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PG, L, ESlateDrawEffect::None, C, true, Thick);
		}
		else
		{
			// horizontal tick at Y=P, grows left from the right edge (x=Size.X).
			const float Len = Frac * Size.X;
			TArray<FVector2D> L = { FVector2D(Size.X, P), FVector2D(Size.X - Len, P) };
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PG, L, ESlateDrawEffect::None, C, true, Thick);
		}
	};

	const float AxisLen = (Axis == EOrientation::Orient_Vertical) ? Size.X : Size.Y;
	const float Center  = AxisLen * 0.5f;
	// Long ticks sit at Center + (Offset mod StepW) + n*StepW (track the finger). Between each pair of
	// long ticks are 4 evenly-spaced short ticks. We iterate sub-tick units across the whole axis and
	// flag every SubDiv-th one (aligned to the long-tick phase) as long.
	float LongPhase = FMath::Fmod(Offset, StepW);
	if (LongPhase < 0.f) LongPhase += StepW;
	// j counts sub-tick steps from the long-tick phase; j%SubDiv==0 → long tick.
	// Start j at the most-negative index whose position is still >= 0, then sweep up to AxisLen.
	const int32 jStart = (int32)FMath::CeilToFloat((0.f - (Center + LongPhase)) / SubW);
	const int32 jEnd   = (int32)FMath::FloorToFloat((AxisLen - (Center + LongPhase)) / SubW);
	for (int32 j = jStart; j <= jEnd; ++j)
	{
		const float P = Center + LongPhase + j * SubW;
		const bool bLong = (((j % SubDiv) + SubDiv) % SubDiv) == 0;
		DrawTick(P, bLong);
	}

	return LayerId + 1;
}

// ==================== SImSlateCursorSlider ====================

void SImSlateCursorSlider::Construct(const FArguments& InArgs)
{
	OnCursorMove = InArgs._OnCursorMove;
	StepThreshold = 20.f * GetImSlateEffectiveScale();
}

FVector2D SImSlateCursorSlider::ComputeDesiredSize(float) const
{
	float Scale = GetImSlateEffectiveScale();
	return FVector2D(0.f, 32.f * Scale);
}

int32 SImSlateCursorSlider::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FVector2D Size = AllottedGeometry.GetLocalSize();
	float Scale = GetImSlateEffectiveScale();

	static FSlateBrush TrackBrush;
	static bool bInit = false;
	if (!bInit)
	{
		TrackBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		TrackBrush.TintColor = FLinearColor(0.15f, 0.15f, 0.15f, 0.9f);
		TrackBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		TrackBrush.OutlineSettings.CornerRadii = FVector4(4, 4, 4, 4);
		bInit = true;
	}

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &TrackBrush);

	// Draw arrows ◀ ▶ and center grip ═══
	FSlateFontInfo Font = GetImSlateDefaultFont(12);
	TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FString Label = TEXT("\x25C0 \x2550\x2550\x2550 \x25B6");
	FVector2D TextSize = (FVector2D)FM->Measure(Label, Font);
	FVector2D TextPos = (Size - TextSize) * 0.5f;
	FLinearColor Color = bDragging ? FLinearColor(0.8f, 0.8f, 0.8f, 1.f) : FLinearColor(0.5f, 0.5f, 0.5f, 0.8f);

	FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
		AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(TextPos))),
		Label, Font, ESlateDrawEffect::None, Color);

	return LayerId + 1;
}

FReply SImSlateCursorSlider::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = true;
		DragAccumulator = 0.f;
		LastDragPos = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SImSlateCursorSlider::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bDragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SImSlateCursorSlider::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging)
	{
		float DeltaX = MouseEvent.GetScreenSpacePosition().X - LastDragPos.X;
		LastDragPos = MouseEvent.GetScreenSpacePosition();
		DragAccumulator += DeltaX;
		while (DragAccumulator > StepThreshold)  { OnCursorMove.ExecuteIfBound(EVirtualKeyAction::Right); DragAccumulator -= StepThreshold; }
		while (DragAccumulator < -StepThreshold) { OnCursorMove.ExecuteIfBound(EVirtualKeyAction::Left);  DragAccumulator += StepThreshold; }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SImSlateCursorSlider::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	bDragging = true;
	DragAccumulator = 0.f;
	LastDragPos = InTouchEvent.GetScreenSpacePosition();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SImSlateCursorSlider::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (bDragging)
	{
		float DeltaX = InTouchEvent.GetScreenSpacePosition().X - LastDragPos.X;
		LastDragPos = InTouchEvent.GetScreenSpacePosition();
		DragAccumulator += DeltaX;
		while (DragAccumulator > StepThreshold)  { OnCursorMove.ExecuteIfBound(EVirtualKeyAction::Right); DragAccumulator -= StepThreshold; }
		while (DragAccumulator < -StepThreshold) { OnCursorMove.ExecuteIfBound(EVirtualKeyAction::Left);  DragAccumulator += StepThreshold; }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SImSlateCursorSlider::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	bDragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}

}  // namespace ImSlate
