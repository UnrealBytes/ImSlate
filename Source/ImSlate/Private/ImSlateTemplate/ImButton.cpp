// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImButton.h"

//
#include "Components/ButtonSlot.h"
#include "Widgets/Input/SButton.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "ImSlateTemplate/ImMultiStateButton.h"
#include "Rendering/DrawElements.h"
#include "Slate/UMGDragDropOp.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Text/STextBlock.h"
#include "PrivateFieldAccessor.h"

#if ENGINE_MAJOR_VERSION >= 5
namespace XButtonNS
{
GS_PRIVATEACCESS_MEMBER(SButton, Style, const FButtonStyle*);
}
#endif

namespace ImSlate
{

static FName SButtonTypeName("SImButton");

SImButton::SImButton()
{
}

/**
 * Construct this widget
 *
 * @param	InArgs	The declaration data for this widget
 */
void SImButton::Construct(const FArguments& InArgs, UImMultiStateButton* InButtonObject)
{
	SButton::Construct(InArgs);

	// Only do this if we're exactly an SXButton
	if (GetType() == SButtonTypeName)
	{
		SetCanTick(false);
	}

	ButtonObject = InButtonObject;

	// InButtonObject may be null for lightweight uses (e.g. a fold header that only needs the
	// base button behaviour + SetMousePressBehavior). Only pull the extra style when present.
	if (InButtonObject)
		SetButtonExtraStyle(&InButtonObject->ExtraStyle);
}

void SImButton::SetButtonExtraStyle(const FImButtonExtraStyle* ButtonStyle)
{
	FocusedImage = &ButtonStyle->Focused;
	DraggedImage = &ButtonStyle->Dragged;

	Invalidate(EInvalidateWidget::Layout);
}

bool SImButton::IsFocused() const
{
	return HasKeyboardFocus();
}

bool SImButton::IsDragging() const
{
	return bInDragDrop;
}

void SImButton::EnterCustomState(FImCustomWidgetState* State)
{
	if (!State)
	{
		return;
	}
	CustomState = *State;
	bUseCustomState = true;
}

void SImButton::QuitCustomState()
{
	bUseCustomState = false;
}


FReply SImButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const bool bWasPress = bPressActive;
	const bool bWasDrag = bDragScrolling;
	// Single teardown (ends the pan if any, clears press flags + visual) — same path as capture-lost/drag-leave.
	EndPressTracking();
	if (bWasDrag)
		// This press was a drag, not a click — consume the up and don't fire OnClicked.
		return FReply::Handled().ReleaseMouseCapture();
	// Bubble-on-press path: down didn't capture (it bubbled to the window for the move). A tap (no drag)
	// must still fire — do the click manually, since SButton never saw a captured press.
	if (bWasPress && PressBehavior.IsSet() && PressBehavior.ShouldBubbleOnPress && PressBehavior.ShouldBubbleOnPress())
	{
		const float TapTol = 6.f * ImSlate::GetImSlateEffectiveScale();
		if ((MouseEvent.GetScreenSpacePosition() - PressScreenPos).Size() < TapTol)
			return ExecuteOnClick();   // tap → click (e.g. fold toggle)
		return FReply::Unhandled();    // was a drag → the ancestor handled the move
	}
	if (IsDragging())
	{
		Drop();
	}
	return SButton::OnMouseButtonUp(MyGeometry, MouseEvent);
}

void SImButton::OnDropOperation()
{
	if (IsDragging())
	{
		Drop();
	}
}

void SImButton::Drop()
{
	bInDragDrop = false;
	if (ButtonObject.IsValid())
		ButtonObject->OnDragEnd.Broadcast();
}

FReply SImButton::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// No backing UObject (e.g. fold header) → no UMG-style drag-drop; nothing to do.
	if (!ButtonObject.IsValid())
		return FReply::Unhandled();
	ButtonObject->OnDragQuery.Broadcast(MyGeometry, MouseEvent);
	if (IsValid(ButtonObject->DragDropOp))
	{
		bInDragDrop = true;
		ButtonObject->OnDragBegin.Broadcast();
		FVector2D ScreenCursorPos = MouseEvent.GetScreenSpacePosition();
		FVector2D ScreenDrageePosition = (FVector2D)MyGeometry.AbsolutePosition;
		float DPIScale = UWidgetLayoutLibrary::GetViewportScale(ButtonObject.Get());
		FOnButtonDropOperation OnDrop;
		OnDrop.BindSP(SharedThis(this), &SImButton::OnDropOperation);
		TSharedRef<FImButtonDragDropOp> Op = FImButtonDragDropOp::New(ButtonObject->DragDropOp, MouseEvent.GetPointerIndex(), ScreenCursorPos, ScreenDrageePosition, DPIScale, ButtonObject->GetWorld(), OnDrop);
		return FReply::Handled().BeginDragDrop(Op);
	}
	return FReply::Unhandled();
}

FReply SImButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (PressBehavior.IsSet())
	{
		bPressActive = true;
		bDragScrolling = false;
		PressScreenPos = MouseEvent.GetScreenSpacePosition();
		LastDragPos = PressScreenPos;
		// Behaviour asks to bubble (e.g. content can't scroll) → DON'T capture; let the press bubble up so
		// an ancestor (the window) does DetectDrag(self) → OnDragDetected → move. This is the titlebar path
		// (a child capturing or detecting the window did not carry content). A tap (no drag) is turned into
		// a click in OnMouseButtonUp.
		if (PressBehavior.ShouldBubbleOnPress && PressBehavior.ShouldBubbleOnPress())
			return FReply::Unhandled();
	}
	return SButton::OnMouseButtonDown(MyGeometry, MouseEvent);  // capture (drag + click)
}

FReply SImButton::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Buttons with a press behaviour (e.g. fold headers): once a press becomes a drag, forward it to the
	// behaviour so the parent panel's scroll / the window's move takes over (instead of clicking).
	if (bPressActive && HasMouseCapture() && PressBehavior.IsSet())
	{
		const FVector2D Cur = MouseEvent.GetScreenSpacePosition();
		const float Threshold = 6.f * ImSlate::GetImSlateEffectiveScale();
		if (!bDragScrolling && (Cur - PressScreenPos).Size() > Threshold)
		{
			bDragScrolling = true;
			LastDragPos = Cur;
		}
		if (bDragScrolling)
		{
			if (PressBehavior.OnDragMove)
			{
				// Forward (press, current); behaviour returns Handled (it took over — keep capture &
				// keep forwarding) or Unhandled (declined). The click is cancelled in OnMouseButtonUp.
				LastDragPos = Cur;
				FReply R = PressBehavior.OnDragMove(PressScreenPos, Cur);
				if (R.IsEventHandled())
					return R;
			}
			// Behaviour declined — release capture so an ancestor can take over (old fallback). Teardown here
			// (idempotent: the ReleaseMouseCapture below also fires OnMouseCaptureLost → EndPressTracking,
			// which finds the flags already cleared and does nothing more).
			EndPressTracking();
			return FReply::Handled().ReleaseMouseCapture();
		}
	}

	auto Reply = SButton::OnMouseMove(MyGeometry, MouseEvent);
	// pressed & moving
	if (IsPressed())
	{
		if (!IsDragging())
		{
			return OnDragDetected(MyGeometry, MouseEvent);
		}
	}
	return Reply;
}

// Touch path for drag-to-scroll buttons (fold headers). SButton doesn't handle touch itself, and the
// engine's touch→mouse fallback isn't relied on here: capturing the touch and handling tap/drag
// ourselves keeps both behaviours working. Only buttons that opted into drag-scroll take this path;
// others fall through to SButton (which lets touch fall back to mouse as before).
FReply SImButton::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (PressBehavior.IsSet())
	{
		bPressActive = true;
		bDragScrolling = false;
		PressScreenPos = InTouchEvent.GetScreenSpacePosition();
		LastDragPos = PressScreenPos;
		Press();  // pressed visual; cleared on drag or end
		return FReply::Handled().CaptureMouse(AsShared());
	}
	return SButton::OnTouchStarted(MyGeometry, InTouchEvent);
}

FReply SImButton::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (bPressActive && PressBehavior.IsSet())
	{
		const FVector2D Cur = InTouchEvent.GetScreenSpacePosition();
		const float Threshold = 6.f * ImSlate::GetImSlateEffectiveScale();
		if (!bDragScrolling && (Cur - PressScreenPos).Size() > Threshold)
		{
			bDragScrolling = true;
			LastDragPos = Cur;
			Release();  // became a drag, not a press
		}
		if (bDragScrolling)
		{
			if (PressBehavior.OnDragMove)
			{
				LastDragPos = Cur;
				FReply R = PressBehavior.OnDragMove(PressScreenPos, Cur);  // scroll / move-window
				if (R.IsEventHandled())
					return R;
			}
			// Behaviour declined — release capture so an ancestor can take over (mirror the mouse path).
			// Without this the touch capture leaked: the button kept eating moves it had nothing to do
			// with, so the panel never got to scroll. Teardown is idempotent (the ReleaseMouseCapture also
			// fires OnMouseCaptureLost → EndPressTracking, which then finds the flags already cleared).
			EndPressTracking();
			return FReply::Handled().ReleaseMouseCapture();
		}
	}
	return SButton::OnTouchMoved(MyGeometry, InTouchEvent);
}

FReply SImButton::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (bPressActive || bDragScrolling)
	{
		const bool bWasDrag = bDragScrolling;
		EndPressTracking();  // single teardown (ends pan if any, clears flags + visual)
		if (bWasDrag)
			return FReply::Handled().ReleaseMouseCapture();  // was a drag, not a tap → no click
		// A tap: fire the click (e.g. fold toggle), since SButton's own touch path is a no-op.
		return ExecuteOnClick().ReleaseMouseCapture();
	}
	return SButton::OnTouchEnded(MyGeometry, InTouchEvent);
}

void SImButton::EndPressTracking()
{
	// End an in-progress scroll pan (best-effort end pos = last seen during the drag), then clear all
	// press/drag state and the pressed visual. Idempotent: every lifecycle-end signal (up / capture-lost /
	// drag-leave) routes through here, and whichever fires first leaves nothing for the others to leak.
	if (bDragScrolling)
	{
		bDragScrolling = false;
		if (PressBehavior.OnDragEnd)
			PressBehavior.OnDragEnd(LastDragPos);
	}
	bPressActive = false;
	Release();  // clear pressed visual if any
}

void SImButton::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Drag-scroll capture went away (typically: dragged outside the panel/window and released there, so
	// the up never reached us). Without this the drag-scroll state would linger — capture lost but
	// bDragScrolling/bPressActive still true — and moving the pointer back over the button would resume
	// scrolling (OnMouseMove sees bPressActive && ... ). End the pan and clear all press/drag state.
	EndPressTracking();
	SButton::OnMouseCaptureLost(CaptureLostEvent);
}

void SImButton::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	// Bubble-on-press path closure: when a press bubbled to the window (non-scrollable panel) and the window
	// turned it into a window-move via BeginDragDrop, this button never held capture, so neither
	// OnMouseButtonUp nor OnMouseCaptureLost runs. Slate sends OnDragLeave to the widgets on the drag-detect
	// path the moment the drag-drop starts — that's the lifecycle signal that closes this path, so clear the
	// press tracking here. Without it bPressActive stays true and a later stray up fires a phantom click
	// (the "fold toggles on press / can't be dragged" symptom).
	EndPressTracking();
	SButton::OnDragLeave(DragDropEvent);
}

void SImButton::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	if (ButtonObject.IsValid())
		ButtonObject->OnUnFocused.Broadcast();
	SButton::OnFocusLost(InFocusEvent);
}

FReply SImButton::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (ButtonObject.IsValid())
		ButtonObject->OnFocused.Broadcast();
	return SWidget::OnFocusReceived(MyGeometry, InFocusEvent);
}

/** @return An image that represents this button's border*/
const FSlateBrush* SImButton::GetBorder() const
{
	if (bUseCustomState)
	{
		return &CustomState.ActivatedBrush;
	}
#if ENGINE_MAJOR_VERSION >= 5
	auto CurrentStyle = XButtonNS::PrivateAccess::Style(*this);
	if (!GetShowDisabledEffect() && !IsEnabled())
	{
		return &CurrentStyle->Disabled;
	}
	else if (IsDragging() && DraggedImage)
	{
		return DraggedImage;
	}
	else if (IsPressed())
	{
		return &CurrentStyle->Pressed;
	}
	else if (IsHovered())
	{
		return &CurrentStyle->Hovered;
	}
	else if (IsFocused() && FocusedImage)
	{
		return FocusedImage;
	}
	else
	{
		return &CurrentStyle->Normal;
	}
#else
	if (!GetShowDisabledEffect() && !IsEnabled())
	{
		return DisabledImage;
	}
	else if (IsDragging())
	{
		return DraggedImage;
	}
	else if (IsPressed())
	{
		return PressedImage;
	}
	else if (IsHovered())
	{
		return HoverImage;
	}
	else if (IsFocused())
	{
		return FocusedImage;
	}
	else
	{
		return NormalImage;
	}
#endif
}
}  // namespace ImSlate

TSharedRef<SButton> UImButton::ConstructImWidget() const
{
	TSharedPtr<SButton> MyStealButton = SNew(SButton)
										//.OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, SlateHandleClicked))
										//.OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandlePressed))
										//.OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandleReleased))
										//.OnHovered_UObject(this, &ThisClass::SlateHandleHovered)
										//.OnUnhovered_UObject(this, &ThisClass::SlateHandleUnhovered)
										.ButtonStyle(&GetStyle())
										.ClickMethod(GetClickMethod())
										.TouchMethod(GetTouchMethod())
										.PressMethod(GetPressMethod())
										.IsFocusable(GetIsFocusable());

	if (GetChildrenCount() > 0)
	{
		Cast<UButtonSlot>(GetContentSlot())->BuildSlot(MyStealButton.ToSharedRef());
	}

	// UWidget

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealButton->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealButton->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealButton->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealButton->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealButton->SetClipping(GetClipping());

	MyStealButton->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealButton->ForceVolatile(bIsVolatile);
	MyStealButton->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealButton->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealButton->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealButton->SetRenderTransformPivot(GetRenderTransformPivot());

	// 	if (ToolTipWidgetDelegate.IsBound() && !IsDesignTime())
	// 	{
	// 		TSharedRef<ImSlate::FDelegateToolTip> ToolTip = MakeShared<ImSlate::FDelegateToolTip>();
	// 		ToolTip->ToolTipWidgetDelegate = ToolTipWidgetDelegate;
	// 		MyStealEditableText->SetToolTip(ToolTip);
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
	// 		MyStealEditableText->SetToolTip(ToolTip);
	// 	}
	// 	else if (!ToolTipText.IsEmpty() || ToolTipTextDelegate.IsBound())
	// 	{
	// 		MyStealEditableText->SetToolTipText(PROPERTY_BINDING(FText, ToolTipText));
	// 	}

	// UWidget End

	MyStealButton->SetColorAndOpacity(GetColorAndOpacity());
	MyStealButton->SetBorderBackgroundColor(GetBackgroundColor());

	return MyStealButton.ToSharedRef();
}

