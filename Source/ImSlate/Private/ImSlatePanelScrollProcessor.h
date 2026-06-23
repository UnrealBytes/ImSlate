// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

namespace ImSlate
{
class SImSlatePanel;

// App-level input preprocessor that lets a vertical drag STARTED ON A CHILD WIDGET (checkbox,
// button, text box inside a scrollable panel) scroll the panel — even though the child captured the
// pointer on down and thus swallows the panel's own OnMouseMove.
//
// Why a preprocessor: FSlateApplication::ProcessMouseMoveEvent calls InputPreProcessors first, BEFORE
// routing the move to whoever holds capture. So we see every move regardless of capture. Touch goes
// through ProcessTouchMovedEvent -> ProcessMouseMoveEvent, so the same handlers cover mouse + touch.
//
// On a confirmed vertical drag we steal capture (ReleaseMouseCapture) and drive the panel's existing
// ExternalPanMove/ExternalPanEnd (which run the same scroll + inertia path as a normal panel drag).
//
// Mode is controlled by CVar imslate.ScrollTakeoverMode:
//   0 = off (legacy behaviour; only fold-header PressBehavior handles drag-scroll)
//   1 = take over children WITHOUT their own release logic (checkbox / plain button / text box);
//       SKIP fold-header SImButton so its existing PressBehavior keeps working
//   2 = take over EVERYTHING (including fold-header); fold-header PressBehavior becomes redundant
class FImSlatePanelScrollProcessor : public IInputProcessor
{
public:
	virtual ~FImSlatePanelScrollProcessor() override = default;

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;

	virtual const TCHAR* GetDebugName() const override { return TEXT("ImSlatePanelScroll"); }

private:
	void ResetArm();
	// Forward a drag to whichever container we armed (panel or virtual list). Defined in the .cpp.
	void DriveExternalPanMove(const FVector2D& PressPos, const FVector2D& CurPos);
	void DriveExternalPanEnd(const FVector2D& CurPos);

	// Armed scrollable ancestor under the press — either a SImSlatePanel or a SImSlateVirtualList
	// (both expose ExternalPanMove/ExternalPanEnd/CanScroll). Stored as SWidget; dispatched by bArmedIsList.
	TWeakPtr<SWidget> ArmedTarget;
	bool bArmedIsList = false;              // true → SImSlateVirtualList, false → SImSlatePanel
	bool bArmedAxisHorizontal = false;     // the armed container's scroll axis (list can be horizontal)
	int32 ArmedPointerIndex = INDEX_NONE;  // which finger / pointer armed the candidate
	FVector2D PressScreenPos = FVector2D::ZeroVector;
	bool bTookOver = false;                // crossed the threshold → we're driving the scroll
};

}  // namespace ImSlate
