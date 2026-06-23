// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "ImSlateFactory.h"
#include "ImSlateFwd.h"
#include "Templates/UniqueObj.h"
#include "UnrealCompatibility.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBar;
DECLARE_DELEGATE_OneParam(FOnVirtualPosChanged, float);
namespace ImSlate
{
class IImSlateListData;
struct FVirtualListSlots;
class FVirtualListSlot;

class IMSLATE_API SImSlateVirtualList
	: public SCompoundWidget
	, public IScrollableWidget
	, public IImSlateCtxBase
{
public:
	using Super = SCompoundWidget;
	SLATE_BEGIN_ARGS(SImSlateVirtualList) {}
	SLATE_ARGUMENT_DEFAULT(UObject*, WorldCtxPtr){nullptr};
	SLATE_ARGUMENT(TSharedPtr<SScrollBar>, ExternalScrollbar)
	SLATE_ARGUMENT_DEFAULT(EOrientation, Orientation){Orient_Vertical};
	/** Style used to draw this list's scrollbar */
	SLATE_STYLE_ARGUMENT(FScrollBarStyle, ScrollBarStyle)
	/** The direction that children will be stacked, and also the direction the box will scroll. */
	SLATE_ARGUMENT_DEFAULT(EVisibility, ScrollBarVisibility){EVisibility::Collapsed};
	SLATE_ARGUMENT_DEFAULT(bool, ScrollBarAlwaysVisible){false};
	SLATE_ARGUMENT_DEFAULT(EFocusCause, ScrollBarDragFocusCause){EFocusCause::Mouse};
	SLATE_ARGUMENT_DEFAULT(UE::Slate::FDeprecateVector2DParameter, ScrollBarThickness){FVector2f(0.f)};
	/** This accounts for total internal scroll bar padding; default 2.0f padding from the scroll bar itself is removed */
	SLATE_ARGUMENT_DEFAULT(FMargin, ScrollBarPadding){0.f};
	SLATE_ARGUMENT_DEFAULT(EAllowOverscroll, AllowOverscroll){EAllowOverscroll::Yes};
	SLATE_ARGUMENT_DEFAULT(bool, BackPadScrolling){false};
	SLATE_ARGUMENT_DEFAULT(bool, FrontPadScrolling){false};
	SLATE_ARGUMENT_DEFAULT(bool, AnimateWheelScrolling){true};
	SLATE_ARGUMENT_DEFAULT(float, WheelScrollMultiplier){1.f};
	SLATE_ARGUMENT_DEFAULT(EDescendantScrollDestination, NavigationDestination){EDescendantScrollDestination::IntoView};
	/**
	* The amount of padding to ensure exists between the item being navigated to, at the edge of the list.
	* Use this if you want to ensure there's a preview of the next item the user could scroll to.
	*/
	SLATE_ARGUMENT_DEFAULT(float, NavigationScrollPadding){0.f};
	SLATE_ARGUMENT_DEFAULT(EScrollWhenFocusChanges, ScrollWhenFocusChanges){EScrollWhenFocusChanges::NoScroll};
	/** Called when the button is clicked */
	SLATE_EVENT(FOnUserScrolled, OnUserScrolled)
	/** Fired when scroll bar visibility changed */
	SLATE_EVENT(FOnScrollBarVisibilityChanged, OnScrollBarVisibilityChanged)
	SLATE_ARGUMENT_DEFAULT(EConsumeMouseWheel, ConsumeMouseWheel){EConsumeMouseWheel::WhenScrollingPossible};
	/** Called when the button is clicked */
	SLATE_EVENT(FOnVirtualPosChanged, OnVirtualPosChanged)

	SLATE_END_ARGS()

	SImSlateVirtualList();
	~SImSlateVirtualList();

	void Construct(const FArguments& InArgs);

public:
	// Set buffer rows (a few more rows outside the visible area)
	void SetOverCountRowNum(int32 InNum);

	// Set Tile mode and Item width
	void SetTileOrthVal(float InOrth);

	// Bind data source
	void SetData(TSharedPtr<IImSlateListData> InDataBinding, float InVirtualPos = -0.f, float InTileAxis = 0.f);

	bool ScrollToPos(float InVirtualPos, bool bItemAlign = false);

	// make sure item is at least be visible
	void ScrollToItem(int32 InDataIndex, bool bCenterAlign = false);

	// Update specified data, default is to update all visible data
	void Update(int32 InDataIndex = -1, bool bReConstruct = false);

	void SetBackgroundContent(TSharedRef<SWidget> InWidget);

	using FOnItemInputEvent = TDelegate<bool(SImSlateVirtualList*, const FGeometry&, const FKeyEvent&)>;
	void SetItemEventOnInputKey(FOnItemInputEvent Delegate) { OnItemKeyEvent = MoveTemp(Delegate); }

	void SetScrollbarUserVisibility(TAttribute<EVisibility> InUserVisibility);

	TSharedPtr<SScrollBar> GetScrollBar() const { return ScrollBar; }

	// Set Cull Widget
	void EnableWidgetClipping(bool bCullWidget);

public:
	float GetVirtualPos() const;
	int32 DataIndexFromOffset(float InOffset, int32 OutRangeIndex = INDEX_NONE) const;
	int32 UpperDataIndex(float InOffset) const;
	bool RowIndexRange(int32 InIdx, int32& Lower, int32& Upper) const;
	void RowIndexRange(int32 InIdx, const TFunctionRef<void(int32)>& Func) const;
	float GetDataOffset(int32 InIndex) const;
	float GetDataAxis(int32 InIndex) const;
	float GetCachedTotalAxis(bool bFullItems = false) const;
	int32 GetDataCount() const;
	float GetItemAxis(int32 InIndex = 0) const;
	bool IsShowScrollbar() const;

	void SetAnimateScrollDuration(float AnimDuration);

	// InRelativePos : -0.f for any pixel
	bool IsItemOffsetVisible(int32 InIndex, float InRelativePos = -0.f) const;

	virtual ImVec2 GetVisibleSize(bool bEnsureExist = true) const;
	float GetVisibleAxis(bool bEnsureExist = true) const;
	float GetVisibleOrth(bool bEnsureExist = true) const;
	template<typename Vec2Type>
	auto GetOrientationAxis(const Vec2Type& In) const
	{
		return Orientation == EOrientation::Orient_Vertical ? In.Y : In.X;
	}
	template<typename Vec2Type>
	auto GetOrientationOrth(const Vec2Type& In) const
	{
		return Orientation == EOrientation::Orient_Vertical ? In.X : In.Y;
	}
	template<typename Vec2Type>
	auto GetOrientationAxisVec(const Vec2Type& In) const
	{
		return Orientation == EOrientation::Orient_Vertical ? Vec2Type(0, In.Y) : Vec2Type(In.X, 0);
	}
	template<typename Vec2Type>
	auto GetOrientationOrthVec(const Vec2Type& In) const
	{
		return Orientation == EOrientation::Orient_Vertical ? Vec2Type(In.X, 0) : Vec2Type(0, In.Y);
	}

	void SetItemPadding(FMargin InPadding);

protected:
	float AnimateDuration = 0.f;
	mutable float LeftAnimateDuration = 0.f;
	mutable float AnimationStartPos = 0.f;

	// Begin IImSlateListData Wrapper
	void OnPosChanged(float InVirtualPos);
	void OnSetData(int32 Index, TSharedRef<SWidget> Widget);
	void GenerateDataWidget(int32 InIndex, TSharedRef<SWidget>& InOutWidget);
	void ReleaseDataWidget(int32 InIndex, TSharedRef<SWidget>& ReleasedWidget);
	bool IsHeterogeneous() const;
	bool NeedPrepassItem() const;
	void RefreshVisibleWidget();

	// End IImSlateListData Wrapper
	FOnVirtualPosChanged OnVirtualPosChanged;

	void ReloadToItem(int32 InIndex, bool bCenterAlign = false);
	void DelayScrollToItem(int32 InIndex, bool bCenterAlign = false);

	void ReloadToPos(float InVirtualPos = -0.f, bool bItemAlign = false);
	bool InnerReloadToPos(float InVirtualPos = -0.f, bool bItemAlign = false);
	bool InnerScrollToPos(float InVirtualPos, float InListAxis, bool bItemAlign = false);
	void DelayScrollToPos(float InVirtualPos, bool bItemAlign = false);

	bool UpdateRange(float InListAxis, float InVirtualPos, float InOverscrollAmount, bool bForce = false);
	bool UpdateRangeIndex(float InListAxis, float& InOutVirtualPos, float InOverscrollAmount, bool bForce = false) const;

	struct FIndexRange
	{
		int32 StartIndex;
		int32 EndIndex;
		int32 OldStartIndex;
		int32 OldEndIndex;
		bool bChanged;

		FIndexRange() { Reset(); }
		void Reset(bool bInChanged = false)
		{
			StartIndex = -1;
			OldStartIndex = -1;
			EndIndex = 0;
			OldEndIndex = 0;
			SetChanged(bInChanged);
		}

		bool HasChanged() const { return bChanged; }
		void SetChanged(bool bInChanged) { bChanged = bInChanged; }
		void AppendChanged(bool bInChanged) { bChanged = bChanged || bInChanged; }

		bool HasValidRange() const { return StartIndex >= 0 && EndIndex >= StartIndex; }
		bool SetRange(int32 NewStart, int32 NewEnd)
		{
			AppendChanged(NewStart != StartIndex || NewEnd != EndIndex);

			StartIndex = NewStart;
			EndIndex = NewEnd;
			return HasChanged();
		}
		void StoreRange()
		{
			OldStartIndex = StartIndex;
			OldEndIndex = EndIndex;
			SetChanged(false);
		}

		bool IsInRange(int32 InIdx) const { return StartIndex <= InIdx && InIdx <= EndIndex; }
	};

	mutable FIndexRange IndexRange;
	float ActualVirtualPos = 0.f;

	void ResetAxises(int32 FromIdx = 0);
	bool UpdateData(int32 InDataIndex, bool bReConstruct);

	int32 OverCountRowNum = 2;

	float TileOrthValue = -1.f;
	float GetTileOrth() const { return TileOrthValue; }

private:
	SImSlateVirtualList(const SImSlateVirtualList&) = delete;
	SImSlateVirtualList& operator=(const SImSlateVirtualList&) = delete;
	SImSlateVirtualList(SImSlateVirtualList&&) = delete;
	SImSlateVirtualList& operator=(SImSlateVirtualList&&) = delete;
	friend class IImSlateListData;
#if WITH_EDITOR
	mutable TArray<TPair<TWeakPtr<SWidget>, int32>> WidgetIndexes;
#endif
	bool EnsureDataAxises(int32 InIndex) const;
	bool EnsureDataAxises(float InOffset) const;
	void AppendDataAxis(int32 InDataIndex);
	bool UpdateSlotedAxis(const FVirtualListSlot* Slot, bool bPrepass);
	bool UpdateSlotedAxisDelta(const FVirtualListSlot* Slot, float Delta);

	TArray<float, TInlineAllocator<4>> DataOffsets;
	TArray<float, TInlineAllocator<4>> DataAxises;
	TBitArray<TInlineAllocator<4>> CachedWidgets;
	float LastRowOffset = 0.f;
	float LastRowAxis = 0.f;
	int32 TotalCol = 0;
	int32 GetTotalCol() const { return TotalCol; }
	bool HasValidCol() const { return TotalCol > 0; }
	TUniqueObj<FVirtualListSlots> SlotCollection;
	virtual FChildren* GetChildren() override;

protected:
	bool IsUserScrolling() const;
	FOnContextMenuOpening OnContextMenuOpening;
	virtual void OnRightMouseButtonDown(const FPointerEvent& MouseEvent);
	virtual void OnRightMouseButtonUp(const FPointerEvent& MouseEvent);
	virtual void NotifyItemScrolledIntoView() {}

protected:
	SImSlateVirtualList* MutableThis() const
	{
		auto This = this;
		return const_cast<SImSlateVirtualList*>(This);
	}
	TSharedRef<SImSlateVirtualList> ToSharedRef() const { return StaticCastSharedRef<SImSlateVirtualList>(MutableThis()->AsShared()); }

	// Begin IScrollableWidget interface
	virtual FVector2D GetScrollDistance() override;
	virtual FVector2D GetScrollDistanceRemaining() override;
	virtual TSharedRef<SWidget> GetScrollWidget() override;
	// End IScrollableWidget interface

	FMargin ItemPadding;

private:
	mutable TOptional<float> OverrideAxis;
	void UpdateScrollState() const;
	bool GenerateWidgetIfNeeded();
	TSharedPtr<IImSlateListData> DataBinding;
	TMap<FName, TSharedRef<SWidget>> WidgetCache;
	FOnItemInputEvent OnItemKeyEvent;
	TSharedPtr<SBox> BackgroundBox;

protected:
	FVector2f PressedScreenSpacePosition;
	FVector2f LastMouseMovedScreenSpacePosition;
	float PressedVirtualPos = 0.f;

protected:
	bool IsAllowOverScroll() const { return AllowOverscroll == EAllowOverscroll::Yes; }
	// Begin SWidget overrides.
	virtual bool CustomPrepass(float LayoutScaleMultiplier) override;
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual void CacheDesiredSize(float LayoutScaleMultiplier) override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool ComputeVolatility() const override;
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual void OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent) override;

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override;

	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchFirstMove(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual FReply OnTouchForceChanged(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	// End SWidget overrides.

protected:
	/** @return Returns true if the user is currently interactively scrolling the view by holding
		        the right mouse button and dragging. */
	bool IsRightClickScrolling() const;

	EAllowOverscroll GetAllowOverscroll() const;
	void SetAllowOverscroll(EAllowOverscroll NewAllowOverscroll);
	void SetAnimateWheelScrolling(bool bInAnimateWheelScrolling);
	void SetWheelScrollMultiplier(float NewWheelScrollMultiplier);
	void SetScrollWhenFocusChanges(EScrollWhenFocusChanges NewScrollWhenFocusChanges);
	float GetScrollOffset() const;
	float GetViewFraction() const;
	float GetViewOffsetFraction() const;
	/** Gets the scroll offset of the bottom of the list in Slate Units. */
	float GetScrollOffsetOfEnd() const;
	void SetScrollOffset(float NewScrollOffset);
	void ScrollToStart();
	void ScrollToEnd();
	void EndInertialScrolling();
	/** 
	 * Attempt to scroll a widget into view, will safely handle non-descendant widgets 
	 *
	 * @param WidgetToFind The widget whose geometry we wish to discover.
	 * @param InAnimateScroll	Whether or not to animate the scroll
	 * @param InDestination		Where we want the child widget to stop.
	 */
	void ScrollDescendantIntoView(const TSharedPtr<SWidget>& WidgetToFind, bool InAnimateScroll = true, EDescendantScrollDestination InDestination = EDescendantScrollDestination::IntoView, float Padding = 0);

public:
	// --- Public API consumed by FImSlatePanelScrollProcessor (drag-scroll takeover). ---
	EOrientation GetOrientation();

	// External pan: driven by FImSlatePanelScrollProcessor when a drag started on a child widget
	// (which holds capture) should scroll this list. Mirrors SImSlatePanel::ExternalPanMove/End but
	// uses the list's Orientation (GetOrientationAxis) so it works for vertical AND horizontal lists.
	// Pass press + current SCREEN positions; first call seeds (no scroll), later calls apply the delta.
	void ExternalPanMove(FVector2D PressPos, FVector2D CurPos);
	void ExternalPanEnd(FVector2D CurPos);
	bool CanScroll() const { return GetScrollOffsetOfEnd() > 0.f; }
	bool IsPanEnabled() const { return true; }

protected:
	void SetNavigationDestination(const EDescendantScrollDestination NewNavigationDestination);
	void SetConsumeMouseWheel(EConsumeMouseWheel NewConsumeMouseWheel);
	void SetOrientation(EOrientation InOrientation);

	void SetScrollBarVisibility(EVisibility InVisibility);

	void SetScrollBarAlwaysVisible(bool InAlwaysVisible);

	void SetScrollBarTrackAlwaysVisible(bool InAlwaysVisible);

	void SetScrollBarThickness(UE::Slate::FDeprecateVector2DParameter InThickness);

	void SetScrollBarPadding(const FMargin& InPadding);

	void SetScrollBarRightClickDragAllowed(bool bIsAllowed);

	void SetScrollBarStyle(const FScrollBarStyle* InBarStyle);
	void InvalidateScrollBarStyle();

	/** Builds a default Scrollbar */
	TSharedPtr<SScrollBar> ConstructScrollBar();

	/** Constructs internal layout widgets for scrolling vertically using the existing ScrollPanel and ScrollBar. */
	void ConstructVerticalLayout();

	/** Constructs internal layout widgets for scrolling horizontally using the existing ScrollPanel and ScrollBar. */
	void ConstructHorizontalLayout();

	/** Gets the component of a vector in the direction of scrolling based on the Orientation property. */
	FORCEINLINE float GetScrollComponentFromVector(FVector2f Vector) const { return float(Orientation == Orient_Vertical ? Vector.Y : Vector.X); }

	/** Sets the component of a vector in the direction of scrolling based on the Orientation property. */
	inline void SetScrollComponentOnVector(FVector2f& InVector, float Value) const
	{
		if (Orientation == Orient_Vertical)
		{
			InVector.Y = Value;
		}
		else
		{
			InVector.X = Value;
		}
	}

	/** Scroll offset that the user asked for. We will clamp it before actually scrolling there. */
	float DesiredScrollOffset;

	/**
	 * Scroll the view by ScrollAmount given its currently AllottedGeometry.
	 *
	 * @param AllottedGeometry  The geometry allotted for this list by the parent
	 * @param ScrollAmount      
	 * @param InAnimateScroll	Whether or not to animate the scroll
	 * @return Whether or not the scroll was fully handled
	 */
	bool ScrollBy(const FGeometry& AllottedGeometry, float LocalScrollAmount, EAllowOverscroll Overscroll, bool InAnimateScroll);

	/** Invoked when the user scroll via the scrollbar */
	void ScrollBar_OnUserScrolled(float InScrollOffsetFraction);

	void ScrollBar_OnScrollBarVisibilityChanged(EVisibility NewVisibility);

	/** Does the user need a hint that they can scroll to the start of the list? */
	FSlateColor GetStartShadowOpacity() const;

	/** Does the user need a hint that they can scroll to the end of the list? */
	FSlateColor GetEndShadowOpacity() const;

	/** Active timer to update inertial scrolling as needed */
	EActiveTimerReturnType UpdateInertialScroll(double InCurrentTime, float InDeltaTime);

	/** Check whether the current state of the table warrants inertial scroll by the specified amount */
	bool CanUseInertialScroll(float ScrollAmount) const;

	void BeginInertialScrolling();

	/** Padding to the list */
	FMargin ScrollBarSlotPadding;

	union
	{
		// vertical scroll bar is stored in horizontal box and vice versa
		SHorizontalBox::FSlot* VerticalScrollBarSlot;  // valid when Orientation == Orient_Vertical
		SVerticalBox::FSlot* HorizontalScrollBarSlot;  // valid when Orientation == Orient_Horizontal
	};

	/** Scrolls or begins scrolling a widget into view, only valid to call when we have layout geometry. */
	bool InternalScrollDescendantIntoView(const FGeometry& MyGeometry,
										  const TSharedPtr<SWidget>& WidgetToFind,
										  bool InAnimateScroll = true,
										  EDescendantScrollDestination InDestination = EDescendantScrollDestination::IntoView,
										  float Padding = 0);

	/** returns widget that can receive keyboard focus or nullprt **/
	TSharedPtr<SWidget> GetKeyboardFocusableWidget(TSharedPtr<SWidget> InWidget);

	/** The scrollbar which controls scrolling for the list. */
	TSharedPtr<SScrollBar> ScrollBar = nullptr;

	/** The amount we have scrolled this tick cycle */
	float TickScrollDelta = 0.f;

	/** Did the user start an interaction in this list? */
	TOptional<int32> bFingerOwningTouchInteraction;

	/** How much we scrolled while the rmb has been held */
	float AmountScrolledWhileRightMouseDown;

	/** The current deviation we've accumulated on scrol, once it passes the trigger amount, we're going to begin scrolling. */
	float PendingScrollTriggerAmount;

	/** Helper object to manage inertial scrolling */
	FInertialScrollManager InertialScrollManager;

	/** The overscroll state management structure. */
	FOverscroll Overscroll;

	/** Whether to permit overscroll on this scroll box */
	EAllowOverscroll AllowOverscroll = EAllowOverscroll::No;

	/**
	 * The amount of padding to ensure exists between the item being navigated to, at the edge of the list.
	 * Use this if you want to ensure there's a preview of the next item the user could scroll to.
	 */
	float NavigationScrollPadding;

	/** Sets where to scroll a widget to when using explicit navigation or if ScrollWhenFocusChanges is enabled */
	EDescendantScrollDestination NavigationDestination;

	/** Scroll behavior when user focus is given to a child widget */
	EScrollWhenFocusChanges ScrollWhenFocusChanges;

	/**	The current position of the software cursor */
	FVector2f SoftwareCursorPosition{ForceInitToZero};

	/** Fired when the user scrolls the list */
	FOnUserScrolled OnUserScrolled;

	/** Fired when scroll bar visibility changed */
	FOnScrollBarVisibilityChanged OnScrollBarVisibilityChanged;

	/** The scrolling and stacking orientation. */
	EOrientation Orientation = EOrientation::Orient_Vertical;

	/** External-pan state (FImSlatePanelScrollProcessor drives scroll while a child holds capture). */
	bool bExternalPanActive = false;
	FVector2D ExternalPanLastScreenPos = FVector2D::ZeroVector;

	/** Style resource for the scrollbar */
	const FScrollBarStyle* ScrollBarStyle;

	/** How we should handle scrolling with the mouse wheel */
	EConsumeMouseWheel ConsumeMouseWheel = EConsumeMouseWheel::WhenScrollingPossible;

	/** Cached geometry for use with the active timer */
	FGeometry CachedGeometry;
	/** Scroll into view request. */
	TFunction<void(const FGeometry&)> ScrollIntoViewRequest;

	TSharedPtr<FActiveTimerHandle> UpdateInertialScrollHandle;

	double LastScrollTime = 0.0;

	/** Multiplier applied to each click of the scroll wheel (applied alongside the global scroll amount) */
	float WheelScrollMultiplier = 1.f;

	/** Whether to back pad this scroll box, allowing user to scroll backward until child contents are no longer visible */
	bool bBackPadScrolling : 1;
	/** Whether to front pad this scroll box, allowing user to scroll forward until child contents are no longer visible */
	bool bFrontPadScrolling : 1;

	/** Whether to animate wheel scrolling */
	bool bAnimateWheelScrolling : 1;
	/**	Whether the software cursor should be drawn in the viewport */
	bool bShowSoftwareCursor : 1;
	/** Whether or not the user supplied an external scrollbar to control scrolling. */
	bool bScrollBarIsExternal : 1;
	/** Are we actively scrolling right now */
	bool bIsScrolling : 1;
	/** Should the current scrolling be animated or immediately jump to the desired scroll offer */
	bool bAnimateScroll : 1;
	/** If true, will scroll to the end next Tick */
	bool bScrollToEnd : 1;
	/** Whether the active timer to update the inertial scroll is registered */
	bool bIsScrollingActiveTimerRegistered : 1;
	bool bAllowsRightClickDragScrolling : 1;
	bool bTouchPanningCapture : 1;

	bool bStartedTouchInteraction : 1;
	bool bAllowOverBoundDraw : 1;
	bool bSameContentLayerId : 1;
};

}  // namespace ImSlate
