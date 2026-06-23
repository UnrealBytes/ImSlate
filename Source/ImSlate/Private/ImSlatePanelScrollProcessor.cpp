// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlatePanelScrollProcessor.h"

#include "SImSlatePanel.h"
#include "ImSlateVirtualList.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"  // FSlateUser::SetPointerCaptor / ReleaseCapture
#include "Layout/WidgetPath.h"
#include "HAL/IConsoleManager.h"

namespace ImSlate
{
// 0 = off, 1 = children without own release logic (skip fold-header SImButton), 2 = everything.
int32 GImSlateScrollTakeoverMode = 1;
static FAutoConsoleVariableRef CVar_ScrollTakeoverMode(
	TEXT("imslate.ScrollTakeoverMode"),
	GImSlateScrollTakeoverMode,
	TEXT("Drag-scroll takeover via input preprocessor: 0=off, 1=children only (keep fold-header PressBehavior), 2=all widgets."));

// Walk a hit-test path leaf → root; return the nearest scrollable container (SImSlatePanel OR
// SImSlateVirtualList), whether it's a list, its scroll axis, and whether a fold-header SImButton
// sits between the press and the container (so mode 1 can skip it).
static TSharedPtr<SWidget> FindScrollContainerInPath(const FWidgetPath& Path, bool& bOutIsList, bool& bOutAxisHorizontal, bool& bOutHasButtonAbove)
{
	bOutIsList = false;
	bOutAxisHorizontal = false;
	bOutHasButtonAbove = false;
	if (!Path.IsValid())
		return nullptr;

	bool bSawButton = false;
	for (int32 i = Path.Widgets.Num() - 1; i >= 0; --i)
	{
		const TSharedRef<SWidget>& W = Path.Widgets[i].Widget;
		const FName Type = W->GetType();
		// Press on the scrollbar itself → it drives its own drag-scroll; don't take over.
		if (Type == TEXT("SScrollBar") || Type == TEXT("SScrollBarTrack"))
			return nullptr;
		if (Type == TEXT("SImButton"))
			bSawButton = true;  // fold-header (and any other SImButton) sits between press and container

		if (Type == TEXT("SImSlatePanel"))
		{
			TSharedRef<SImSlatePanel> Panel = StaticCastSharedRef<SImSlatePanel>(W);
			if (Panel->CanScroll() && Panel->IsPanEnabled())
			{
				bOutIsList = false;
				bOutAxisHorizontal = false;  // panel is always vertical
				bOutHasButtonAbove = bSawButton;
				return W;
			}
			return nullptr;
		}
		if (Type == TEXT("SImSlateVirtualList"))
		{
			TSharedRef<SImSlateVirtualList> List = StaticCastSharedRef<SImSlateVirtualList>(W);
			if (List->CanScroll() && List->IsPanEnabled())
			{
				bOutIsList = true;
				bOutAxisHorizontal = (List->GetOrientation() == Orient_Horizontal);
				bOutHasButtonAbove = bSawButton;
				return W;
			}
			return nullptr;
		}
	}
	return nullptr;
}

void FImSlatePanelScrollProcessor::DriveExternalPanMove(const FVector2D& PressPos, const FVector2D& CurPos)
{
	TSharedPtr<SWidget> Target = ArmedTarget.Pin();
	if (!Target.IsValid())
		return;
	if (bArmedIsList)
		StaticCastSharedPtr<SImSlateVirtualList>(Target)->ExternalPanMove(PressPos, CurPos);
	else
		StaticCastSharedPtr<SImSlatePanel>(Target)->ExternalPanMove(PressPos, CurPos);
}

void FImSlatePanelScrollProcessor::DriveExternalPanEnd(const FVector2D& CurPos)
{
	TSharedPtr<SWidget> Target = ArmedTarget.Pin();
	if (!Target.IsValid())
		return;
	if (bArmedIsList)
		StaticCastSharedPtr<SImSlateVirtualList>(Target)->ExternalPanEnd(CurPos);
	else
		StaticCastSharedPtr<SImSlatePanel>(Target)->ExternalPanEnd(CurPos);
}

void FImSlatePanelScrollProcessor::ResetArm()
{
	ArmedTarget.Reset();
	bArmedIsList = false;
	bArmedAxisHorizontal = false;
	ArmedPointerIndex = INDEX_NONE;
	bTookOver = false;
}

bool FImSlatePanelScrollProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	// Multi-touch guard: if we're already actively scrolling with one finger, ignore a second
	// finger's press — don't reset and re-arm to it (that would hijack the live scroll). The active
	// finger's own up/cancel ends the scroll normally.
	if (bTookOver && ArmedTarget.IsValid())
		return false;

	ResetArm();
	if (GImSlateScrollTakeoverMode <= 0)
		return false;

	FWidgetPath Path = SlateApp.LocateWindowUnderMouse(MouseEvent.GetScreenSpacePosition(), SlateApp.GetInteractiveTopLevelWindows());
	bool bIsList = false, bAxisH = false, bHasButtonAbove = false;
	TSharedPtr<SWidget> Target = FindScrollContainerInPath(Path, bIsList, bAxisH, bHasButtonAbove);
	if (!Target.IsValid())
		return false;

	// Mode 1: leave fold-header SImButton to its own PressBehavior drag-scroll. Mode 2: arm regardless.
	if (GImSlateScrollTakeoverMode == 1 && bHasButtonAbove)
		return false;

	ArmedTarget = Target;
	bArmedIsList = bIsList;
	bArmedAxisHorizontal = bAxisH;
	ArmedPointerIndex = MouseEvent.GetPointerIndex();
	PressScreenPos = MouseEvent.GetScreenSpacePosition();
	bTookOver = false;
	return false;  // never consume the press — the child still gets it (click / focus / toggle)
}

bool FImSlatePanelScrollProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!ArmedTarget.IsValid() || MouseEvent.GetPointerIndex() != ArmedPointerIndex)
		return false;

	const FVector2D Cur = MouseEvent.GetScreenSpacePosition();

	if (!bTookOver)
	{
		const FVector2D Delta = Cur - PressScreenPos;
		const float Trigger = SlateApp.GetDragTriggerDistance();
		// Take over when the drag along the CONTAINER'S scroll axis passes the threshold and that axis
		// dominates. Panel/vertical list → Y; horizontal list → X.
		const float Along = bArmedAxisHorizontal ? Delta.X : Delta.Y;
		const float Across = bArmedAxisHorizontal ? Delta.Y : Delta.X;
		if (FMath::Abs(Along) > Trigger && FMath::Abs(Along) >= FMath::Abs(Across))
		{
			bTookOver = true;
			// Move pointer capture to the PANEL itself (the thing actually scrolling). This:
			//  - makes the child release its capture → it gets OnMouseCaptureLost → pressed visual resets;
			//  - guarantees the button-up is still delivered even if the cursor leaves the panel/viewport
			//    (capture means the OS routes the outside-window up back here) → no leaked scroll state.
			// We still drive the scroll ourselves below (the panel doesn't run its own move handler while
			// we absorb the moves via return-true), and we get the up first as a preprocessor.
			if (TSharedPtr<FSlateUser> User = SlateApp.GetUser(MouseEvent))
			{
				if (TSharedPtr<SWidget> Target = ArmedTarget.Pin())
				{
					FWidgetPath PanelPath;
					if (SlateApp.FindPathToWidget(Target.ToSharedRef(), PanelPath))
						User->SetPointerCaptor(MouseEvent.GetPointerIndex(), Target.ToSharedRef(), PanelPath);
				}
			}
			DriveExternalPanMove(PressScreenPos, Cur);  // first call seeds; no delta
			return true;
		}
		return false;  // cross-axis or below threshold: let the child keep the interaction
	}

	DriveExternalPanMove(PressScreenPos, Cur);
	return true;
}

bool FImSlatePanelScrollProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetPointerIndex() != ArmedPointerIndex)
		return false;

	const bool bWasScrolling = bTookOver && ArmedTarget.IsValid();
	if (bWasScrolling)
	{
		DriveExternalPanEnd(MouseEvent.GetScreenSpacePosition());  // end + start inertia
		// We took capture onto the panel on takeover and we consume this up (return true) so the
		// panel never runs its own OnMouseButtonUp → release the capture ourselves here.
		if (TSharedPtr<FSlateUser> User = SlateApp.GetUser(MouseEvent))
			User->ReleaseCapture(MouseEvent.GetPointerIndex());
	}

	ResetArm();
	return bWasScrolling;  // consume the up only if it ended a scroll (so it isn't treated as a click)
}

void FImSlatePanelScrollProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	// Safety net for a missed button-up. IInputProcessor has no cancel / capture-lost hook, so if the
	// up event never reaches us (capture stolen elsewhere, PIE end, app deactivate, focus change) the
	// armed/took-over state would otherwise leak and the next move would wrongly take over. Each tick:
	//  - target gone (panel/list destroyed) → clear.
	//  - we took over but the left mouse button is no longer pressed → end the scroll & clear.
	// (Touch up is reliable; this primarily covers mouse / editor teardown. The mouse-button check is
	//  skipped while a real touch drives the scroll so a touch scroll isn't ended prematurely.)
	if (ArmedTarget.IsValid())
	{
		if (bTookOver && !SlateApp.IsFakingTouchEvents())
		{
			// End if the left button is no longer pressed. On takeover we move capture to the panel,
			// so a normal up is delivered (even outside the window) and handled in HandleMouseButtonUp;
			// this is only the backstop for a genuinely missed up (PIE end / focus steal). Release the
			// panel capture here too so it doesn't linger.
			const bool bLeftDown = SlateApp.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
			if (!bLeftDown)
			{
				DriveExternalPanEnd(PressScreenPos);  // best-effort end (no fresh pos available here)
				if (ArmedPointerIndex != INDEX_NONE)
					if (TSharedPtr<FSlateUser> User = SlateApp.GetCursorUser())
						User->ReleaseCapture(ArmedPointerIndex);
				ResetArm();
			}
		}
	}
	else if (ArmedPointerIndex != INDEX_NONE || bTookOver)
	{
		// Target died mid-interaction → drop stale state.
		ResetArm();
	}
}

}  // namespace ImSlate
