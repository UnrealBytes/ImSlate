// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "ImSlatePrivate.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"

namespace ImSlate
{
class SImSlateViewport;
class SImViewportGame;
#if defined(IMSLATE3D_API)
class SImSlate3DTransformBox;   // X-b slot2 wrapper (held by TSharedPtr; full def in SImSlate3DTransformBox.h)
#endif
using SImSlateWindowBase = SWidget;
class SImSlateWindow final : public SImSlateWindowBase
{
public:
	SLATE_BEGIN_ARGS(SImSlateWindow) {}
	SLATE_ARGUMENT(uint32, Flags)
	SLATE_ARGUMENT(FText, Title)
	SLATE_END_ARGS()
	SImSlateWindow();

	void Construct(const FArguments& InArgs, uint32 ImSlateId);

public:
	ImSlateId WindowId;
	ImSlateIdStack IDStack;

	ImSlateWindowFlags Flags;
	ImSlateWindowFlags FlagsPreviousFrame;  // See enum ImSlateWindowFlags_

	ImVec2 ActualPos;
	ImVec2 ActualSize;
	union
	{
		uint32 FlagBits;
		struct
		{
			uint8 Cond : 4;
			uint8 HAlignment : 4;
			uint8 VAlignment : 4;
		};
	};

	ImSlateId PopupId;

	int32 DisplayOrder = INT_MAX - 1;

	bool SkipItems = false;
	bool Appearing = true;

	bool WriteAccessed = true;
	bool Collapsed = false;
	bool Active = false;
	bool Hidden = false;

	bool ViewportOwned = false;
	ImSlateId ViewportId = 0;
	SImSlateViewport* Viewport = nullptr;

	ImSlateCond SetWindowPosAllowFlags : 8;        // store acceptable condition flags for SetNextWindowPos() use.
	ImSlateCond SetWindowSizeAllowFlags : 8;       // store acceptable condition flags for SetNextWindowSize() use.
	ImSlateCond SetWindowCollapsedAllowFlags : 8;  // store acceptable condition flags for SetNextWindowCollapsed() use.
	ImSlateCond SetWindowByAlphaAllowFlags : 8;    // store acceptable condition flags for SetNextWindowCollapsed() use.
	ImSlateCond SetWindowDockAllowFlags : 8;       // store acceptable condition flags for SetNextWindowDock() use.
	ImSlateCond SetWindowTitleAllowFlags : 8;      // store acceptable condition flags for SetNextWindowTitle() use.
	ImSlateCond SetWindowTopmostAllowFlags : 8;    // store acceptable condition flags for SetNextWindowTitle() use.

	// store window pivot for positioning. ImVec2(0, 0) when positioning from top-left corner; ImVec2(0.5f, 0.5f) for centering; ImVec2(1, 1) for bottom right.
	TOptional<ImVec2> SetWindowPosPivot;
	// store window position when using a non-zero Pivot (position set needs to be processed when we know the window size)
	ImVec2 SetWindowPosVal;

	int32 BeginCount = 0;
	int32 LastFrameActive;       // Last frame number the window was Active.
	int32 LastFrameJustFocused;  // Last frame number the window was made Focused.
	float LastTimeActive;        // Last timestamp the window was Active (using float as we don't need high precision there)
	int32 LastFrameEnded;        // Last frame number the window was Active.

	ImSlateWindow* ParentWindowInBeginStack;

	TOptional<FTransform> Transform;

	TWeakPtr<SWidget> ParentAnchor;

	SImSlateWindow* ParentWindow = nullptr;

	int8 HiddenFramesCanSkipItems;     // Hide the window for N frames
	int8 HiddenFramesCannotSkipItems;  // Hide the window for N frames while allowing items to be submitted so we can measure their size

	FVector2D MinSize = FVector2D::ZeroVector;
	TAttribute<FVector2D> MaxSize;

	void SetContentCollapsed(bool bCollapsed);
	bool IsContentCollapsed() const;

	void SetShowTitleBar(bool bShowTitleBar);
	bool IsShowTitleBar() const;

	void SetTitleText(FText InName);
	FText GetTitleText() const;

	//mutable TArray<FSlateVertex> VertexBuffer;
	//mutable TArray<SlateIndex> IndexBuffer;

	void SetScrollTarget(FVector2D In);
	// True when the content panel has room to scroll. Used by the fold-header drag behaviour to decide
	// scroll-vs-move-window at press time.
	bool CanScrollContent() const;
	// Forward a capturing child's scroll drag (e.g. fold header) to the content panel's external pan.
	void PanContentMove(FVector2D PressPos, FVector2D CurPos);
	void PanContentEnd(FVector2D CurPos);
	void CheckCloseButton(bool* bIn);

	// Maximize / restore: fill the whole viewport, then restore the saved pos/size. While
	// maximized, moving and resizing are disabled (NoMove/NoResize forced on; restored on exit).
	void ToggleMaximize();
	bool IsMaximized() const { return bMaximized; }

	// Whether a window-move drag may start. Normally = user did not request NoMove. While maximized
	// the NoMove flag is forced on internally, but we still allow a drag IF the user's original
	// intent was movable (bSavedNoMove == false) — that drag triggers a restore-then-follow (see
	// OnDragDetected). So a genuinely user-pinned (NoMove) window stays unmovable even maximized.
	bool CanStartMoveDrag() const
	{
#if PLATFORM_IOS || PLATFORM_ANDROID
		// Mobile: a maximized window fills the screen — there's nowhere to move it, and the desktop
		// "drag to restore-then-follow" gesture is unwanted on touch. Block move entirely while maximized.
		if (bMaximized)
			return false;
#endif
		return bMaximized ? !bSavedNoMove : !(Flags & ImSlateWindowFlags_NoMove);
	}

	SImViewportGame* IsAreaInGameViewport() const;
	bool IsViewportGame() const;

	bool IsHidden() const { return Hidden; }
	bool IsActive() const { return Active; }
	bool IsCollapsed() const { return Collapsed; }

	SImSlateViewport* SelectViewport();
	void UpdateViewport();
	void MoveToViewport(SImSlateViewport* InViewport);
	int32 GetOrder() const { return DisplayOrder; }

	FVector2D CalcSize(FVector2D InSize) const;

#if WITH_EDITOR
	int32 PIEInstanceID = -1;
#endif

public:
	TSharedRef<SImSlateWindow> ToSharedRef() { return SharedThis<SImSlateWindow>(this); }
	const FVector2D GetContentScale() const { return ContentScale.Get(); }
	void SetContentScale(const TAttribute<FVector2D>& InContentScale);
	FLinearColor GetColorAndOpacity() const { return ColorAndOpacity.Get(); }
	void SetColorAndOpacity(const TAttribute<FLinearColor>& InColorAndOpacity) { SetAttribute(ColorAndOpacity, InColorAndOpacity, EInvalidateWidgetReason::Paint); }
	void SetForegroundColor(const TAttribute<FSlateColor>& InForegroundColor) { SetAttribute(ForegroundColor, InForegroundColor, EInvalidateWidgetReason::Paint); }
	bool ShouldShowCloseButton() const;

	void SetBgAlpha(float InAlpha);
	float GetBgAlpha() const { return BackgroudAlpha; }
	void SetBgColor(const FLinearColor& InColor);
	FLinearColor GetBgColor() const { return ContentBackgroundBrush.TintColor.GetSpecifiedColor(); }
	const TArray<TSharedRef<SImSlateWindow>>& GetChildWindows() const { return ChildWindows; }
	TArray<TSharedRef<SImSlateWindow>>& GetChildWindows() { return ChildWindows; }

	void SetWindowSize(FVector2D InSize);
	void SetWindowContentSize(FVector2D InSize);
	void SetWindowTitleHeight(float InHeight);
	FVector2D GetContentSize() const;

	void DragingWindowSize(FVector2D InSize);
	void DragingWindowPos(FVector2D InPos, FVector2D InAbsolutePos);
	FVector2D GetWindowSize() const;
	FVector2D GetWindowPos() const;

	void BringWindowToFront();

	void BeginItemFrame();
	void CommitItemFrame();
	void MarkPanelLayoutDirty();
	void SetItemParent(ImSlateId ItemId);
	void PushFoldContext(ImSlateId FoldId);
	void PopFoldContext();
	void SetFoldCollapsed(ImSlateId FoldId, bool bCollapsed);
	FItemSlotPod* FindItem(ImSlateId InId, SWidget** OutWidget = nullptr, int32* OutChildIndex = nullptr);
	FItemSlotPod& AddItem(ImSlateId InId, const TSharedRef<SWidget>& Item);
	bool ReuseItem(ImSlateId InId, int32 ExistingChildIndex);
	void AddExistItem(ImSlateId InId, FItemSlotPod* InExistSlot);

	SImViewportGame* GetViewportGame() const { return ViewportGame; }

	ImSlateResizeCallback ResizeCallback;
	void InvokeResizeCallback(FVector2D NewSize);
	void AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObjects(ReferencedObjects); }
	void AddReferencedObject(const UObject* InObj) { ReferencedObjects.Add(InObj); }

protected:
	virtual bool CustomPrepass(float LayoutScaleMultiplier) override;
	TArray<TSharedRef<SImSlateWindow>> ChildWindows;

	TOptional<FVector2D> RequiredSize;

	TArray<float, TInlineAllocator<4>> ColumnOffset;

	TAttribute<bool> bAlwaysShowScrollbar;

	SImViewportGame* ViewportGame = nullptr;

	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
		const override;
	virtual void OnClippingChanged() override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void CacheDesiredSize(float LayoutScaleMultiplier) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;  // touch mirror of OnMouseButtonDown (mobile window move + titlebar buttons)
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FPopupMethodReply OnQueryPopupMethod() const override { return FPopupMethodReply::UseMethod(EPopupMethod::CreateNewWindow).SetShouldThrottle(EShouldThrottle::No); }
	ImVec2 TotalDesiredSizes;

	TSlotlessChildren<SWidget> Children;
	virtual FChildren* GetChildren() override { return &Children; }

	TSharedPtr<class SImHeaderArea> HeaderHandle;
	TSharedPtr<class SImSlatePanel> ChildPanel;
	TSharedPtr<SWidget> ResizeArea;

#if defined(IMSLATE3D_API)
	// X-b: slot2 is a SImSlate3DTransformBox that wraps ChildPanel. With no placement it's a pure 2D
	// passthrough (window behaves exactly as before); SetNextWindow3DTransform → Begin() gives it a
	// placement → the whole content panel projects onto a 3D world plane. ChildPanel is still the item
	// container (all AddSlot/BeginItemFrame go straight to it); only the slot2 layout/clip ROOT is the box.
	TSharedPtr<SImSlate3DTransformBox> Content3DBox;
	bool bIs3D = false;                  // currently projecting (placement set on the box)
	bool bSaved3DShowTitleBar = true;    // title-bar state to restore when leaving 3D
	bool bSaved3DShowResize = false;     // resize-handle state to restore when leaving 3D

	// The slot2 layout/clip root: the box when 3D-capable, else the bare ChildPanel. Used by
	// OnArrangeChildren / OnPaint / OnClippingChanged (the only places that treat slot2 as a layout root).
	TSharedRef<SWidget> GetContentSlotWidget() const;

public:
	// Begin() calls these when consuming the _Has3DTransform flag (set/clear placement + toggle chrome).
	void Apply3DTransform(const ImSlate::FImWorldPlacement& Placement);
	void Clear3DTransform();

	// The 3D content box (slot2 wrapper). Immediate-mode 3D items (e.g. GizmoArrow) wire themselves to it so
	// they can borrow its projector / cursor map. Null when the 3D module isn't built. Valid only after Construct.
	TSharedPtr<ImSlate::SImSlate3DTransformBox> GetContent3DBox() const { return Content3DBox; }
private:
#endif
	const FMargin PanelMargin{2.f, 2.f, 2.f, 2.f};
	bool bShowResizeHandle = false;
	bool bShowDockingCross = false;

	// Maximize state. While maximized the window fills the viewport; SavedPos/SavedSize hold the
	// pre-maximize geometry to restore, and SavedFlags holds the move/resize flag bits we forced.
	bool bMaximized = false;
	FVector2D SavedPos = FVector2D::ZeroVector;
	FVector2D SavedSize = FVector2D::ZeroVector;
	bool bSavedShowResizeHandle = false;  // bShowResizeHandle before maximize, to restore exactly
	bool bSavedNoMove = false;            // whether NoMove was already set before maximize
	bool bSavedNoResize = false;          // whether NoResize was already set before maximize
	bool bWasInHostBeforeMaximize = false;// popped into a host SWindow before maximize → restore there

public:
	// When restoring a maximized window back to a host popup, the host's real on-screen geometry
	// (absolute) to apply. host pos/size live in the real SWindow, not ActualPos. Read & consumed by
	// SImViewportHost::MakePopupWindow so it positions/sizes the popup correctly REGARDLESS of when
	// the SWindow is actually created (avoids the "set before SWindow exists → no-op" race).
	TOptional<FVector2D> PendingHostPos;
	TOptional<FVector2D> PendingHostSize;
protected:

	TAttribute<FVector2D> ContentScale;
	TAttribute<FLinearColor> ColorAndOpacity;
	TAttribute<FSlateColor> ForegroundColor;

	FSlateBrush ContentBackgroundBrush;
	float BackgroudAlpha = 1.f;
	float ResizeHandleWidth = 24.f;
	float TitleHeight = 24.f;
	float GetTitleHeight() const;

	TArray<TObjectPtr<const UObject>> ReferencedObjects;
};
}  // namespace ImSlate
