// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "ImSlateFwd.h"
#include "SImSlateLayout.h"
#include "SlotBase.h"
#include "Framework/Layout/InertialScrollManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/SPanel.h"

namespace ImSlate
{
class SImSlateWindow;
class SImSlatePanel final : public SPanel
{
public:
	class FSlot
		: public FItemSlotPod
		, public TSlotBase<FSlot>
	{
	public:
		FSlot(uint32 InKey)
			: FItemSlotPod(InKey)
			, TSlotBase<FSlot>()

		{
		}

		FSlot& Padding(FMargin InPadding)
		{
			SlotPadding = InPadding;
			return *this;
		}

		FSlot& Padding(float Left, float Top, float Right, float Bottom)
		{
			SlotPadding = FMargin(Left, Top, Right, Bottom);
			return *this;
		}

		FSlot& Padding(float Horizontal, float Vertical)
		{
			SlotPadding = FMargin(Horizontal, Vertical);
			return *this;
		}

		FSlot& Padding(float Uniform)
		{
			SlotPadding = FMargin(Uniform);
			return *this;
		}

		FSlot& AutoWidth()
		{
			bFillWidth = 0;
			return *this;
		}

		FSlot& FillWidth(float StretchCoefficient)
		{
			bFillWidth = 1;
			StretchValue = StretchCoefficient;
			return *this;
		}

		FSlot& StretchToColumn(int32 InStretchToCol)
		{
			bAlignCol = 1;
			StretchToCol = InStretchToCol;
			return *this;
		}

		FSlot& NewRow()
		{
			bNewRow = 1;
			return *this;
		}

		FSlot& BreakLine()
		{
			bBreakLine = 1;
			return *this;
		}

		FSlot& Apply(const FItemSlotPod& Other)
		{
			static_cast<FItemSlotPod&>(*this) = Other;
			Invalidate(EInvalidateWidgetReason::Layout);
			return *this;
		}

		FSlot& operator=(const FItemSlotPod& Other) { return Apply(Other); }

		friend bool operator==(const FSlot& Lhs, const FSlot& Rhs) { return Lhs.Hash == Rhs.Hash && Lhs.GetWidget() == Rhs.GetWidget() && Lhs.FlagBits == Rhs.FlagBits && Lhs.StretchToCol == Rhs.StretchToCol; }

		EHorizontalAlignment GetHorizontalAlignment() const { return (EHorizontalAlignment)HAlignment; }

		EVerticalAlignment GetVerticalAlignment() const { return (EVerticalAlignment)VAlignment; }
	};

	static FSlot& Slot(uint32 InKey) { return *(new FSlot(InKey)); }

	FSlot& AddSlot(uint32 InKey)
	{
		FSlot& NewSlot = Slot(InKey);
		int32 Index = this->Children.Num();
		this->Children.Add(&NewSlot);
		if (bFrameStarted)
			FrameOrder.Add(Index);
		// Always Invalidate — Slate must know about new children (like SlateIM's Slot.SetContent)
		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
		return NewSlot;
	}

	void AddSlot(FSlot* InSlot)
	{
		int32 Index = this->Children.Num();
		this->Children.Add(InSlot);
		if (bFrameStarted)
			FrameOrder.Add(Index);
		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
	}

	// EventDrived mode: add directly without frame tracking
	FSlot& AddCurrentSlot(uint32 InKey)
	{
		FSlot& NewSlot = Slot(InKey);
		this->Children.Add(&NewSlot);
		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
		return NewSlot;
	}

	void AddCurrentSlot(FSlot* InSlot)
	{
		this->Children.Add(InSlot);
		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
	}

	// Frame lifecycle: non-EventDrived mode
	void BeginItemFrame()
	{
		FrameOrder.Reset();
		TouchedSlots.Reset();
		TouchedSlots.Init(false, Children.Num());
		bFrameStarted = true;
		bLayoutDirty = false;
		bFrameOrderIsSequential = true;
		NextExpectedTouchIndex = 0;
		FrameBaseChildCount = Children.Num();
	}

	void MarkLayoutDirty() { bLayoutDirty = true; }

	// Returns true if the slot was successfully touched (first time this frame).
	// Returns false if already touched (duplicate ID) — caller should create a new slot instead.
	bool TouchSlot(int32 ChildIndex)
	{
		if (TouchedSlots.IsValidIndex(ChildIndex) && TouchedSlots[ChildIndex])
			return false;  // already touched this frame — duplicate ID
		if (TouchedSlots.IsValidIndex(ChildIndex))
			TouchedSlots[ChildIndex] = true;
		FrameOrder.Add(ChildIndex);
		// Track if touched indices match the expected non-collapsed sequence
		if (bFrameOrderIsSequential)
		{
			// Skip over collapsed indices to find next expected active index
			while (NextExpectedTouchIndex < Children.Num()
				&& IsInCollapsedSubtree(Children[NextExpectedTouchIndex].Hash))
			{
				++NextExpectedTouchIndex;
			}
			if (ChildIndex != NextExpectedTouchIndex)
				bFrameOrderIsSequential = false;
			++NextExpectedTouchIndex;
		}
		return true;
	}

	// Items added before this count are from previous frame (visible to FindItem)
	// Items at or after this count were added during current frame (invisible to FindItem)
	int32 GetFrameBaseChildCount() const { return FrameBaseChildCount; }

	bool CommitItemFrame();

	FSlot& InsertSlot(uint32 InKey, int32 Index = INDEX_NONE)
	{
		if (Index == INDEX_NONE)
		{
			return AddSlot(InKey);
		}

		FSlot& NewSlot = Slot(InKey);
		this->Children.Insert(&NewSlot, Index);

		Invalidate(EInvalidateWidget::Layout);

		return NewSlot;
	}

	int32 NumSlots() const { return this->Children.Num(); }

	int32 RemoveSlot(const TSharedRef<SWidget>& SlotWidget);
	int32 RemoveSlot(uint32 InKey);

	void ClearChildren();

	SImSlatePanel();
	~SImSlatePanel();

	SLATE_BEGIN_ARGS(SImSlatePanel) {}
#if UE_5_00_OR_LATER
	SLATE_SLOT_ARGUMENT(FSlot, Slots)
#else
	SLATE_SUPPORTS_SLOT(FSlot)
#endif
	SLATE_ARGUMENT_DEFAULT(bool, HideScollBar) = false;
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, SImSlateWindow* InParent);
	auto ToSharedRef() { return SharedThis<SImSlatePanel>(this); }

protected:
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual FChildren* GetChildren() override;

	mutable TChildrenLayout<FSlot> Children;
	TArray<int32, TInlineAllocator<64>> FrameOrder;
	TBitArray<> TouchedSlots;
	bool bFrameStarted = false;
	bool bLayoutDirty = false;
	bool bFrameOrderIsSequential = true;
	int32 NextExpectedTouchIndex = 0;
	int32 FrameBaseChildCount = 0;

	void OnScrollFraction(float InFraction);
	bool OnScrollOffset(float InOffset);
	void SetScrollTarget(FVector2D In) { OnScrollOffset(In.Y); }

protected:
#if !UE_5_00_OR_LATER
	void MarkPrepassAsDirty() { InvalidatePrepass(); }
#endif

	struct FCachedItem
	{
		TMap<uint32, int32> CachedID2Indexes;
		TMap<int32, uint32> CachedIndex2IDs;

		void Add(uint32 InId, int32 InIndex)
		{
			DeleteByIndex(InIndex);
			CachedID2Indexes.FindOrAdd(InId) = InIndex;
			CachedIndex2IDs.FindOrAdd(InIndex) = InId;
		}

		void DeleteByID(uint32 InId)
		{
			if (auto* Find = CachedID2Indexes.Find(InId))
			{
				CachedIndex2IDs.Remove(*Find);
				CachedID2Indexes.Remove(InId);
			}
		}

		void DeleteByIndex(int32 InIndex)
		{
			if (auto* Find = CachedIndex2IDs.Find(InIndex))
			{
				CachedID2Indexes.Remove(*Find);
				CachedIndex2IDs.Remove(InIndex);
			}
		}

		int32 FindIndexById(uint32 InId)
		{
			if (auto* Find = CachedID2Indexes.Find(InId))
				return *Find;
			return INDEX_NONE;
		}
	};

	FCachedItem CachedItems;

	// Tree structure: each item knows its parent fold via ImSlateId
	TMap<ImSlateId, ImSlateId> ItemParentMap;           // ItemId → ParentFoldId (0 = root)
	TSet<ImSlateId> CollapsedFoldIds;                   // folds that are currently collapsed
	TArray<ImSlateId, TInlineAllocator<4>> FoldContextStack;  // current fold nesting during frame build

	bool IsInCollapsedSubtree(ImSlateId ItemId) const;

public:
	// Drag-to-scroll: shift content by a vertical delta (clamped inside OnScrollOffset). Public so the
	// owning SImSlateWindow can drive it from draggable content widgets.
	void ScrollByDelta(float DeltaY) { OnScrollOffset(ScrollOffset + DeltaY); }
	// True when content exceeds the visible height (there is room to scroll).
	bool CanScroll() const { return TotalActualSize.Y > GetCachedGeometry().GetLocalSize().Y; }

	// External pan: a child that holds capture (fold-header SButton) OR an app-level input
	// preprocessor (FImSlatePanelScrollProcessor) forwards its drag here so the SAME scroll logic
	// runs without the panel itself holding capture. Pass press + current screen positions; the
	// caller is responsible for its own drag threshold before calling. Public so the preprocessor
	// can drive it. IsPanEnabled() so callers can pre-check.
	void ExternalPanMove(FVector2D PressPos, FVector2D CurPos);
	void ExternalPanEnd(FVector2D CurPos);
	bool IsPanEnabled() const;

	void SetItemParent(ImSlateId ItemId);               // assign current fold context as parent
	void PushFoldContext(ImSlateId FoldId);
	void PopFoldContext();
	void SetFoldCollapsed(ImSlateId FoldId, bool bCollapsed);

protected:
	ImSlateWindow* Window = nullptr;
	bool bHideScrollBar = false;
	friend class SImSlateWindow;
	virtual float GetDesiredWidth() const;
	virtual float GetDesiredHeight() const;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void CacheDesiredSize(float LayoutScaleMultiplier) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// Drag/touch-to-scroll (and drag-move when no scroll room). SScrollBox-style: a preview marks a drag
	// candidate without consuming the press; a move past the drag threshold captures and takes over.
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
		const override;
	ImVec2 TotalActualSize;

	TArray<float, TInlineAllocator<4>> ColumnAlignOff;

	TSharedPtr<SScrollBar> ScrollHandle;
	float ScrollOffset = 0.f;
	float ScrollBarWidth = 12.f;
	mutable TArray<float, TInlineAllocator<4>> RowHeights;

	// --- Drag/touch pan state (SScrollBox-style: preview marks a candidate, over-threshold capture) ---
	enum class EPanMode : uint8 { None, Scroll, MoveWindow };
	TOptional<int32> FingerOwningInteraction;            // pointer index of the active drag candidate
	FVector2D PressScreenPos = FVector2D::ZeroVector;
	FVector2D LastMoveScreenPos = FVector2D::ZeroVector;
	float PendingDragTrigger = 0.f;                      // accumulated movement before the threshold
	bool bPanningCapture = false;                        // crossed threshold & captured
	EPanMode PanMode = EPanMode::None;                   // locked once at the threshold (no mid-drag switch)
	FVector2D AbsGrabOffset = FVector2D::ZeroVector;     // MoveWindow only: press pos - window abs pos
	// MoveWindow only: the window's position + the finger's screen pos at the moment the move started, so each
	// frame sets window = MoveWindowStartWindowPos + (currentScreenPos - MoveWindowStartScreenPos)/scale.
	FVector2D MoveWindowStartWindowPos = FVector2D::ZeroVector;
	FVector2D MoveWindowStartScreenPos = FVector2D::ZeroVector;

	void BeginPanCandidate(const FPointerEvent& PointerEvent);
	// Returns true (with OutReply set) when a pan starts this move; false to keep bubbling.
	bool TryStartPan(const FGeometry& MyGeometry, const FPointerEvent& PointerEvent, FReply& OutReply);
	void ApplyPanDelta(const FGeometry& MyGeometry, FVector2D CurAbsScreenPos, FVector2D ScreenDelta);
	void EndPan();

	// --- Inertial scrolling (flick-to-coast), same mechanism as SImSlateVirtualList ---
	// FInertialScrollManager samples drag velocity; on release we coast via an active timer until the
	// velocity decays to zero or we hit a scroll edge. The timer self-unregisters when it stops, so there
	// is ZERO per-frame cost when not coasting (SetCanTick stays false).
	FInertialScrollManager InertialScrollManager;
	TWeakPtr<FActiveTimerHandle> InertialTimerHandle;
	bool bIsInertialScrolling = false;
	double LastScrollSampleTime = 0.0;
	void BeginInertialScrolling();                                       // register the coast timer
	void StopInertialScrolling();                                        // unregister + clear velocity
	EActiveTimerReturnType UpdateInertialScroll(double InCurrentTime, float InDeltaTime);
	// Tick runs ONLY while a scroll drag is active (toggled via SetCanTick at pan start/end — otherwise the
	// panel stays SetCanTick(false), zero cost). It mirrors SScrollBox::Tick: if the finger pauses past the
	// sample timeout, clear the residual velocity so a release after a pause produces no inertia.
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	void SetScrollTickActive(bool bActive);

	// BeginGroup/EndGroup: per-arrange merged background rects keyed by GroupId.
	// Filled during OnArrangeChildren (item rects already include -ScrollOffset), drawn in OnPaint below children.
	struct FGroupRect
	{
		FSlateRect Rect = FSlateRect(0.f, 0.f, 0.f, 0.f);
		FLinearColor Color = FLinearColor::Transparent;
		bool bInit = false;
	};
	mutable TMap<uint32, FGroupRect> CachedGroupRects;
	// Window content background, drawn inside the panel so it rides the 3D projection (was SImSlateWindow's job).
	void PaintContentBackground(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	void PaintGroupBackgrounds(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
};
}  // namespace ImSlate
