// Copyright ImSlate, Inc. All Rights Reserved.
#include "SImSlateWindow.h"

#include "SImSlatePanel.h"
#include "Widgets/Layout/SDPIScaler.h"
#if defined(IMSLATE3D_API)
#include "SImSlate3DTransformBox.h"   // X-b: slot2 wraps ChildPanel for SetNextWindow3DTransform
#endif



#include "SImSlateViewport.h"
#include "SImViewportGame.h"
#include "SImViewportHost.h"
#include "UObject/StrongObjectPtr.h"

//
#include "AttributeCompatibility.h"
#include "Application/SlateApplicationBase.h"  // GetSafeZoneSize for maximize safe-area inset
#include "Brushes/SlateColorBrush.h"
#include "Engine/Texture2D.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "HAL/PlatformApplicationMisc.h"  // FPlatformApplicationMisc::GetPhysicalScreenDensity ([TitleSizeDbg])
#include "ImSlate.h"
#include "ImSlateStyleSetting.h"
#include "Misc/ScopeExit.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Application/ThrottleManager.h"
#include "XConsoleManager.h"
#if ENGINE_MAJOR_VERSION >= 5
#include "Layout/ChildrenBase.h"
#endif

namespace ImSlate
{
extern SImViewportGame* GetGameViewportImpl();

// Multi-window (ImViewportHost = separate native SWindow) is DESKTOP-ONLY. iOS allows only
// one UI window — dragging an ImSlate window out of the game viewport used to switch to the
// Host path and create a second SWindow, crashing with "Only one UI window may be created on
// iOS". On mobile, windows must always stay inline in the game viewport.
bool bSupportImViewportHost = PLATFORM_DESKTOP;
#if PLATFORM_DESKTOP
FXConsoleVariableRef CVar_SupportImViewportHost(TEXT("imslate.multiview"), bSupportImViewportHost, TEXT(""));
#endif

class FViewportPopupHolder
{
public:
	FViewportPopupHolder(SImViewportHost* In, SImSlateWindow* InWindow, bool bActive)
		: Host(In)
	{
		if (ensure(Host && InWindow))
		{
			Host->MakePopupWindow(InWindow, bActive);
		}
	}
	~FViewportPopupHolder()
	{
		if (Host)
		{
			Host->DestroyPopupWindow();
		}
	}

protected:
	SImViewportHost* Host;
};

static SImViewportHost* SelectViewportHost(SImSlateWindow* InWindow, bool bActive)
{
	auto& g = *GImSlate;
	TSharedPtr<SImViewportHost> Host;
	for (auto& Viewport : g.Viewports)
	{
		if (!Viewport->Window && !Viewport->IsGameViewport())
		{
			Host = StaticCastSharedRef<SImViewportHost>(Viewport);
			ensure(Host->GetChildren()->Num() == 0);
			break;
		}
	}

	if (!Host)
	{
		auto Index = g.Viewports.Add(SAssignNew(Host, SImViewportHost));
		Host->ID = Index;
	}
	SImViewportHost* Ptr = Host.Get();
	g.HostById.Emplace(Host.ToSharedRef(), MakeShared<FViewportPopupHolder>(Ptr, InWindow, bActive));
	return Ptr;
}
static void ReleaseViewportHost(const TSharedRef<SImSlateWindow>& InWindow, SImSlateViewport* InViewport)
{
	check(InWindow->Viewport == InViewport);
	auto Shared = StaticCastSharedRef<SImSlateViewport>(InViewport->AsShared());
	InViewport->RemoveWindow(InWindow);
	GImSlate->Viewports.Remove(Shared);
}

class FImSlateDragOperation : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FImSlateDragOperation, FDragDropOperation)

	static TSharedRef<FImSlateDragOperation> New(const TSharedRef<SImSlateWindow>& InWindowToBeDragged, const FVector2D& InGrabOffset, int32 InOwnerPointerIndex)
	{
		const TSharedRef<FImSlateDragOperation> Operation = MakeShareable(new FImSlateDragOperation(InWindowToBeDragged, InGrabOffset, InOwnerPointerIndex));
		return Operation;
	}

protected:
	TSharedRef<SImSlateWindow> ImSlateWindow;
	FVector2D AbsGrabOffset;
	int32 FrameKeepInGameView = 0;
	int32 OwnerPointerIndex = -1;  // the pointer (finger) that started the drag; OnDragged ignores all others
	FImSlateDragOperation(const TSharedRef<SImSlateWindow>& InWindowToBeDragged, const FVector2D& InGrabOffset, int32 InOwnerPointerIndex)
		: ImSlateWindow(InWindowToBeDragged)
		, AbsGrabOffset(InGrabOffset)
		, OwnerPointerIndex(InOwnerPointerIndex)
	{
	}

	virtual void OnDragged(const FDragDropEvent& DragDropEvent) override
	{
		// Ignore drag updates from any pointer OTHER than the finger/cursor that started this drag. On mobile a
		// phantom pointer (observed PtrIdx=10, ScreenPos=(0,0)) interleaves with the real finger's events; acting
		// on it sets the window to ~origin, so the panel flickers between the correct spot and (0,0). Filtering
		// by the originating pointer index drops those bogus (0,0) frames. (OwnerPointerIndex == -1 → no filter.)
		if (OwnerPointerIndex != -1 && DragDropEvent.GetPointerIndex() != OwnerPointerIndex)
			return;

		bool bIsHostGameViewport = ImSlateWindow->IsViewportGame();

		if (bSupportImViewportHost && bIsHostGameViewport && !ImSlateWindow->IsAreaInGameViewport())
		{
			auto Viewport = ImSlateWindow->SelectViewport();
			// gameview to hostview
			ImSlateWindow->MoveToViewport(Viewport);
			bIsHostGameViewport = false;
			FrameKeepInGameView = 6;
		}

		FVector2D AbsPos = DragDropEvent.GetScreenSpacePosition() - AbsGrabOffset;
		if (bIsHostGameViewport)
		{
			ImSlateWindow->Viewport->SetWindowPos(&ImSlateWindow.Get(), ImSlateWindow->Viewport->AbsoluteToLocal(AbsPos));
		}
		else
		{
			ImSlateWindow->Viewport->SetWindowPos(&ImSlateWindow.Get(), AbsPos);
			if (FrameKeepInGameView > 0)
			{
				FrameKeepInGameView--;
				auto GameViewport = ImSlateWindow->GetViewportGame();
				GameViewport->AddWindow(ImSlateWindow);
				GameViewport->SetWindowPos(&ImSlateWindow.Get(), GameViewport->AbsoluteToLocal(AbsPos));
			}
		}
#if 0
		if (auto GameViewportClient = ImSlateWindow->GetViewportGame()->GameViewportClient.Get())
			GameViewportClient->RedrawRequested(GameViewportClient->Viewport);
#endif
	}

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
	{
		if (ImSlateWindow->IsAreaInGameViewport())
		{
			// hostview to gameview
			auto GameViewport = ImSlateWindow->GetViewportGame();
			ImSlateWindow->MoveToViewport(GameViewport);
			FVector2D TargetPosition = MouseEvent.GetScreenSpacePosition() - AbsGrabOffset;
			TargetPosition = ImSlateWindow->Viewport->AbsoluteToLocal(TargetPosition);
			ImSlateWindow->Viewport->SetWindowPos(&ImSlateWindow.Get(), TargetPosition);
			ImSlateWindow->BringWindowToFront();
		}
		else if (bSupportImViewportHost)
		{
			ImSlateWindow->BringWindowToFront();
		}
	}
};

static int32 DisableThrottleCnt = 0;
template<typename Base = SCompoundWidget>
class SImSlateDragingBase : public Base
{
public:
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!DragParameters.IsSet() &&  MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			DragParameters = FDragParameters(Target->GetCachedGeometry().GetAbsolutePosition(), MouseEvent.GetScreenSpacePosition(), MouseEvent.GetPointerIndex());
			return FReply::Handled().CaptureMouse(this->AsShared());
		}
		return FReply::Handled();
	}
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (DragParameters.IsSet() && DragParameters->PointerIndex == MouseEvent.GetPointerIndex() && this->HasMouseCapture())
		{
			auto ScreenSpaceDelta = MouseEvent.GetScreenSpacePosition() - DragParameters->AbsDragStart;
			FVector2D AbsPos = DragParameters->AbsWindowPos + ScreenSpaceDelta;
			if (Target && AbsPos.RoundToVector() != DragParameters->AbsWindowPos.RoundToVector())
			{
				OnDragingVal(Target, DragParameters.GetValue(), MouseEvent.GetScreenSpacePosition());
			}
		}
		return FReply::Handled();
	}
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if(DragParameters->PointerIndex == MouseEvent.GetPointerIndex() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			DragParameters.Reset();
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Handled();
	}

	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!DragParameters.IsSet())
		{
			DragParameters = FDragParameters(Target->GetCachedGeometry().GetAbsolutePosition(), MouseEvent.GetScreenSpacePosition(), MouseEvent.GetPointerIndex());
			if (DisableThrottleCnt++ == 0)
			{
				FSlateThrottleManager::Get().DisableThrottle(true);
			}
			// MUST capture (mirrors the mouse path's OnMouseButtonDown): without it, touch OnTouchMoved is not
			// routed here, so the drag never continues — the resize/move only fired once on press (looked like
			// "tap = one fixed size change, no drag"). Capture is per-PointerIndex so it's valid for touch.
			return FReply::Handled().CaptureMouse(this->AsShared());
		}
		return FReply::Handled();
	}
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (DragParameters.IsSet() && DragParameters->PointerIndex == MouseEvent.GetPointerIndex() && this->HasMouseCapture())
		{
			auto ScreenSpaceDelta = MouseEvent.GetScreenSpacePosition() - DragParameters->AbsDragStart;
			FVector2D AbsPos = DragParameters->AbsWindowPos + ScreenSpaceDelta;
			if (Target && AbsPos.RoundToVector() != DragParameters->AbsWindowPos.RoundToVector())
			{
				OnDragingVal(Target, DragParameters.GetValue(), MouseEvent.GetScreenSpacePosition());
			}
		}
		return FReply::Handled();
	}
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (DragParameters.IsSet() && DragParameters->PointerIndex == MouseEvent.GetPointerIndex())
		{
			DragParameters.Reset();
			if (--DisableThrottleCnt == 0)
			{
				FSlateThrottleManager::Get().DisableThrottle(false);
			}
			// Release the capture taken in OnTouchStarted (symmetry; mirrors the mouse OnMouseButtonUp path).
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Handled();
	}
	virtual void CacheDesiredSize(float LayoutScaleMultiplier) override { SWidget::CacheDesiredSize(LayoutScaleMultiplier); }

protected:
	struct FDragParameters
	{
		FDragParameters(FVector2D InAbsOriginal, FVector2D InAbsDragStart, uint32 InPointIdx = -1)
			: AbsWindowPos(InAbsOriginal)
			, AbsDragStart(InAbsDragStart)
			, PointerIndex(InPointIdx)
		{
		}

		FVector2D AbsWindowPos;
		FVector2D AbsDragStart;
		uint32 PointerIndex;
	};
	virtual void OnDragingVal(SImSlateWindow* InTarget, const FDragParameters& DragParam, FVector2D ScreenSpacePos) {}
	TOptional<FDragParameters> DragParameters;
	SImSlateWindow* Target = nullptr;
};

class SResizeArea final : public SImSlateDragingBase<>
{
public:
	SLATE_BEGIN_ARGS(SResizeArea) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, SImSlateWindow* InTarget)
	{
		SetClipping(InTarget->GetClipping());
		Target = InTarget;
		ChildSlot
		[
			SNew(SBox)
			.HeightOverride(5.f)
		];
	}
	FVector2D OrignalSize = FVector2D::ZeroVector;
	virtual void OnDragingVal(SImSlateWindow* InTarget, const FDragParameters& DragParam, FVector2D ScreenSpacePos) override
	{
		FVector2D Delta = ScreenSpacePos - DragParam.AbsDragStart;
		InTarget->DragingWindowSize(OrignalSize + Delta);
	}
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
	{
		if (IsInArea(MyGeometry, CursorEvent))
			return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
		else
			return SWidget::OnCursorQuery(MyGeometry, CursorEvent);
	}
	// Resize is disabled when NoResize is set (e.g. while maximized). Guard explicitly: when the
	// handle isn't arranged its geometry is zero and IsInArea() would read as always-true, so a
	// drag could still resize. Checking the flag prevents that.
	bool IsResizeAllowed() const { return Target && !(Target->Flags & ImSlateWindowFlags_NoResize); }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (IsResizeAllowed() && IsInArea(MyGeometry, MouseEvent))
		{
			OrignalSize = Target->IsViewportGame() ? Target->GetCachedGeometry().GetLocalSize() : Target->GetCachedGeometry().GetAbsoluteSize();
			return SImSlateDragingBase<>::OnMouseButtonDown(MyGeometry, MouseEvent);
		}
		else
		{
			return SWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
		}
	}
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (IsResizeAllowed() && IsInArea(MyGeometry, MouseEvent))
		{
			OrignalSize = Target->IsViewportGame() ? Target->GetCachedGeometry().GetLocalSize() : Target->GetCachedGeometry().GetAbsoluteSize();
			return SImSlateDragingBase<>::OnTouchStarted(MyGeometry, MouseEvent);
		}
		else
		{
			return SWidget::OnTouchStarted(MyGeometry, MouseEvent);
		}
	}
	bool IsInArea(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
	{
		auto LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		return (LocalPos.X + LocalPos.Y >= MyGeometry.GetLocalSize().X);
	}

	virtual int32 OnPaint(const FPaintArgs& Args,
						  const FGeometry& AllottedGeometry,
						  const FSlateRect& MyCullingRect,  //
						  FSlateWindowElementList& OutDrawElements,
						  int32 LayerId,
						  const FWidgetStyle& InWidgetStyle,
						  bool bParentEnabled) const override
	{
		int32 ContentLayerId = LayerId + 1;
		int32 MaxLayerId = ContentLayerId;

		FSlateColorBrush ColorBrush(FLinearColor::White);
		const auto Width = AllottedGeometry.GetLocalSize().X;
		TArray<FVector2D> Pair{{0.f, Width}, {Width, Width}, {Width, 0}};
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Pair, ESlateDrawEffect::None, FLinearColor::Gray);

		Pair.SetNum(2);
		const int32 TotalCnt = 4;
		for (int32 Idx = TotalCnt; Idx >= 0; --Idx)
		{
			auto Side = Width * Idx / TotalCnt;
			Pair[0] = FVector2D{Width, Side};
			Pair[1] = FVector2D{Side, Width};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Pair, ESlateDrawEffect::None, FLinearColor::Gray);
		}

		return MaxLayerId;
	}
};

// Header buttons drawn with FSlateDrawElement::MakeLines (no glyph/texture), so the icon scales
// crisply with the title bar height. Both subclass SButton to reuse its click + hover handling,
// hide the default button background, and self-draw the icon on top. Sized square (width = title
// height) and VAlign_Fill in the header so the icon area fills the bar vertically.
class SImIconButton : public SButton
{
public:
	// Icon lines are drawn inset by this fraction of the smaller side, leaving a margin.
	static constexpr float kInsetFrac = 0.30f;

	SLATE_BEGIN_ARGS(SImIconButton) {}
		SLATE_EVENT(FOnClicked, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SButton::Construct(SButton::FArguments()
			.ButtonStyle(&NoBgStyle())
			.ContentPadding(FMargin(0.f))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.OnClicked(InArgs._OnClicked));
	}

	// A button style with no visible background in any state (we draw the icon ourselves; hover is
	// indicated by a faint overlay we paint, not by the style).
	static const FButtonStyle& NoBgStyle()
	{
		static FButtonStyle Style = []() {
			FButtonStyle S = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
			FSlateColorBrush Transparent(FLinearColor::Transparent);
			S.SetNormal(Transparent);
			S.SetHovered(Transparent);
			S.SetPressed(Transparent);
			S.SetNormalPadding(FMargin(0.f));
			S.SetPressedPadding(FMargin(0.f));
			return S;
		}();
		return Style;
	}

protected:
	// Background highlight on hover, in the given colour (subclasses pass their own: white for
	// maximize, red for close). Returns the layer to draw the icon on (above the highlight).
	int32 PaintHoverBg(const FGeometry& G, FSlateWindowElementList& Out, int32 LayerId, const FLinearColor& HoverColor) const
	{
		if (IsHovered())
		{
			static FSlateColorBrush HoverBrush(FLinearColor::White);
			FSlateDrawElement::MakeBox(Out, LayerId, G.ToPaintGeometry(), &HoverBrush,
				ESlateDrawEffect::None, HoverColor);
		}
		return LayerId + 1;
	}

	// Inset square (in local space) the icon is drawn within.
	FSlateRect IconRect(const FGeometry& G) const
	{
		const FVector2D Sz = G.GetLocalSize();
		const float S = FMath::Min(Sz.X, Sz.Y);
		const float Inset = S * kInsetFrac;
		const FVector2D C = Sz * 0.5f;
		const float H = S * 0.5f - Inset;
		return FSlateRect(C.X - H, C.Y - H, C.X + H, C.Y + H);
	}

	// Always clearly visible (not only on hover): light-white at rest, pure white on hover.
	FLinearColor IconColor() const { return IsHovered() ? FLinearColor::White : FLinearColor(0.85f, 0.85f, 0.85f, 1.f); }
};

// Close button: draws an "X" (two diagonal lines).
class SImCloseButton final : public SImIconButton
{
public:
	SLATE_BEGIN_ARGS(SImCloseButton) {}
		SLATE_EVENT(FOnClicked, OnClicked)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs)
	{
		SImIconButton::Construct(SImIconButton::FArguments().OnClicked(InArgs._OnClicked));
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull,
		FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InStyle, bool bEnabled) const override
	{
		int32 Layer = SButton::OnPaint(Args, G, Cull, Out, LayerId, InStyle, bEnabled);
		// Close: hover fills RED (a common close-button affordance).
		Layer = PaintHoverBg(G, Out, Layer, FLinearColor(0.85f, 0.15f, 0.15f, 0.9f));
		const FSlateRect R = IconRect(G);
		// On the red hover fill, draw the X white for contrast.
		const FLinearColor Col = IsHovered() ? FLinearColor::White : IconColor();
		const auto PG = G.ToPaintGeometry();
		FSlateDrawElement::MakeLines(Out, Layer, PG, {FVector2D(R.Left, R.Top), FVector2D(R.Right, R.Bottom)}, ESlateDrawEffect::None, Col, true, 1.5f);
		FSlateDrawElement::MakeLines(Out, Layer, PG, {FVector2D(R.Right, R.Top), FVector2D(R.Left, R.Bottom)}, ESlateDrawEffect::None, Col, true, 1.5f);
		return Layer + 1;
	}
};

// Maximize / restore button: a single square when normal (→ maximize), or two overlapping
// squares when maximized (→ restore). State is queried from the owning window each paint.
class SImMaximizeButton final : public SImIconButton
{
public:
	SLATE_BEGIN_ARGS(SImMaximizeButton) {}
		SLATE_EVENT(FOnClicked, OnClicked)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs)
	{
		SImIconButton::Construct(SImIconButton::FArguments().OnClicked(InArgs._OnClicked));
	}

	TFunction<bool()> IsMaximized;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull,
		FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InStyle, bool bEnabled) const override
	{
		int32 Layer = SButton::OnPaint(Args, G, Cull, Out, LayerId, InStyle, bEnabled);
		// Maximize/restore: hover fills a faint white.
		Layer = PaintHoverBg(G, Out, Layer, FLinearColor(1.f, 1.f, 1.f, 0.25f));
		const FSlateRect R = IconRect(G);
		const FLinearColor Col = IconColor();
		const auto PG = G.ToPaintGeometry();
		auto Box = [&](const FSlateRect& B) {
			TArray<FVector2D> Pts{
				FVector2D(B.Left, B.Top), FVector2D(B.Right, B.Top),
				FVector2D(B.Right, B.Bottom), FVector2D(B.Left, B.Bottom), FVector2D(B.Left, B.Top)};
			FSlateDrawElement::MakeLines(Out, Layer, PG, Pts, ESlateDrawEffect::None, Col, true, 1.5f);
		};
		const bool bMax = IsMaximized && IsMaximized();
		if (bMax)
		{
			// Restore icon: two overlapping squares (a back one shifted up-right, a front one).
			const float D = (R.Right - R.Left) * 0.25f;
			Box(FSlateRect(R.Left + D, R.Top, R.Right, R.Bottom - D));        // back
			Box(FSlateRect(R.Left, R.Top + D, R.Right - D, R.Bottom));        // front
		}
		else
		{
			Box(R);  // maximize icon: a single square
		}
		return Layer + 1;
	}
};

class SImHeaderArea final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImHeaderArea) {}
	SLATE_ATTRIBUTE(FVector2D, MinSize)
	SLATE_END_ARGS()
	SImSlateWindow* Target = nullptr;

	void Construct(const FArguments& InArgs, SImSlateWindow* InWindow, const FText& InTitle = FText::GetEmpty())
	{
		SetClipping(InWindow->GetClipping());
		Target = InWindow;
		Title = InTitle;
		MinSizeAttr = InArgs._MinSize;

		TitleBrush.TintColor = GetDefault<UXImSlateStyleSetting>()->WindowHeaderColor;

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Fill)
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(ComputeDesiredSize(0.f).Y)
				[
					SNew(SBorder)
					// Give the title bar a SOLID (opaque) fill. Previously BorderImage was commented
					// out, so the SBorder used the default themed brush which is mostly transparent —
					// inside a viewport (no host SWindow backing it) the bar showed through to the
					// half-transparent content background and the game scene, making the title text
					// and the maximize/close buttons hard to read. A flat opaque box fixes that.
					.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
					.BorderBackgroundColor(TAttribute<FSlateColor>::CreateSP(this, &SImHeaderArea::GetHeaderFillColor))
					.Visibility(EVisibility::SelfHitTestInvisible)
					.Content()
					[
						SNew(SHorizontalBox)
						//加上simage
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Center)
						.AutoWidth()
						.Padding(8.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SImage)
							.Image(HeaderIconBrush())
							// Icon loading is currently disabled (HeaderIconBrush returns an empty brush),
							// and an empty image brush renders as a "◇?" missing-resource placeholder.
							// Collapse the image while there's no real icon texture so the placeholder
							// doesn't show next to the title.
							.Visibility_Lambda([this]() { return IconBrush.GetResourceObject() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
						]
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(TAttribute<FText>::CreateSP(Target, &SImSlateWindow::GetTitleText))
							.Font(ImSlate::GetImSlateDefaultFont(12))
							.ColorAndOpacity(FLinearColor::White)
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							SNew(SSpacer)
						]
						// Maximize / restore button (square, fills the bar vertically).
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Fill)
						.AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(TAttribute<FOptionalSize>::CreateSP(this, &SImHeaderArea::GetTitleHeightSize))
							[
								SAssignNew(MaximizeButton, SImMaximizeButton)
								.OnClicked(this, &SImHeaderArea::OnMaximizeBtnClick)
							]
						]
						// Close button (square, fills the bar vertically). Drawn as an "X" in OnPaint.
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Fill)
						.AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(TAttribute<FOptionalSize>::CreateSP(this, &SImHeaderArea::GetTitleHeightSize))
							.Visibility(TAttribute<EVisibility>::CreateLambda([this]() {
								return this->Target->ShouldShowCloseButton() ? EVisibility::Visible : EVisibility::Collapsed;
							}))
							[
								SNew(SImCloseButton)
								.OnClicked(this, &SImHeaderArea::OnCloseBtnClick)
							]
						]
						// Right safe-area gutter: when maximized, push the button group left by
						// safe.Right so close / maximize don't sit under the screen's rounded corner /
						// notch. Zero width otherwise (and on desktop, where safe = 0).
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Fill)
						.AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([this]() -> FOptionalSize {
								if (!Target || !Target->IsMaximized())
									return FOptionalSize(0.f);
								FMargin Safe(0.f);
								if (FSlateApplication::IsInitialized())
									FSlateApplicationBase::Get().GetSafeZoneSize(Safe, FVector2f::ZeroVector);
								return FOptionalSize(Safe.Right);
							}))
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SNew(SSpacer)
			]
		];

		// Maximize button draws "restore" vs "maximize" based on the window's live state.
		if (MaximizeButton.IsValid())
		{
			SImSlateWindow* W = Target;
			MaximizeButton->IsMaximized = [W]() { return W && W->IsMaximized(); };
		}
	}

	FVector2D GetMinSize() const { return MinSizeAttr.Get(FVector2D{200.f, 40.f}); }

public:
	FText Title;
	FSlateBrush TitleBrush;

	FSlateBrush IconBrush;
	TStrongObjectPtr<UTexture2D> IconTexture;

	TAttribute<FVector2D> MinSizeAttr;
	TOptional<float> TitleHeight;
	float GetTitleHeight() const
	{
		// Logical value × effectiveScale (already physically consistent). The previous mobile max(H,44) over-
		// inflated the bar on high-PPI / density-fallback devices (square buttons are bar-height wide → didn't
		// fit). Reverted to match SImSlateWindow::GetTitleHeight.
		return TitleHeight.Get(24.f) * ImSlate::GetImSlateEffectiveScale();
	}

public:
	const FSlateBrush* HeaderBrush() { return &TitleBrush; }

	const FSlateBrush* HeaderIconBrush()
	{
		if (!IconTexture.Get())
		{
			//IconTexture.Reset(LoadObject<UTexture2D>(nullptr, TEXT("Logo")));
			//IconBrush = FSlateImageBrush((UObject*)(IconTexture.Get()), FVector2D(16.f, 16.f));
		}
		return &IconBrush;
	}

	void SetHeaderBrushBgAlpha(float InAlpha)
	{
		auto DefaultColor = GetDefault<UXImSlateStyleSetting>()->WindowHeaderColor;
		DefaultColor.A = InAlpha;
		TitleBrush.TintColor = DefaultColor;
	}

	// Opaque title-bar fill: the themed header color but forced to full alpha, so the bar (and its
	// title text + maximize/close buttons) stay readable regardless of the window's content alpha.
	FSlateColor GetHeaderFillColor() const
	{
		FLinearColor C = GetDefault<UXImSlateStyleSetting>()->WindowHeaderColor;
		C.A = 1.f;
		return FSlateColor(C);
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!(Target->Flags & ImSlateWindowFlags_NoMove))
			return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
		return FReply::Handled();
	}
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!(Target->Flags & ImSlateWindowFlags_NoMove))
			return SCompoundWidget::OnTouchStarted(MyGeometry, MouseEvent);
		return FReply::Handled();
	}
	void SetTitle(const FText& InTitle) { Title = InTitle; }
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		if (Target && Target->Viewport)
			return {Target->GetWindowSize().X, GetTitleHeight()};
		return {100.f, GetTitleHeight()};
	}

	FReply OnCloseBtnClick()
	{
		if (Target)
		{
			Target->Viewport->OnClose(Target);
		}
		return FReply::Handled();
	}
	FReply OnMaximizeBtnClick()
	{
		if (Target)
			Target->ToggleMaximize();
		return FReply::Handled();
	}
	// Square button width = title bar height, so the icon area is a square that fills the bar.
	FOptionalSize GetTitleHeightSize() const { return FOptionalSize(GetTitleHeight()); }

	TSharedPtr<SImMaximizeButton> MaximizeButton;
	virtual void CacheDesiredSize(float LayoutScaleMultiplier) override { SWidget::CacheDesiredSize(LayoutScaleMultiplier); }
};

//////////////////////////////////////////////////////////////////////////
SImSlateWindow::SImSlateWindow()
	: Children(this)
{
	bHasCustomPrepass = true;
	LastFrameActive = -1;
	LastFrameJustFocused = -1;
	LastTimeActive = -1.0f;
	Clipping = EWidgetClipping::ClipToBounds;
	HAlignment = EHorizontalAlignment::HAlign_Fill;
	VAlignment = EVerticalAlignment::VAlign_Fill;
	ViewportGame = GetGameViewportImpl();
}

void SImSlateWindow::Construct(const FArguments& InArgs, uint32 ImSlateId)
{
	UE_LOG(LogTemp, Log, TEXT("SImSlateWindow Construct: %p, id: %d"), this, ImSlateId);
	// [TitleSizeDbg] Full scale diagnostics (one line per window create) to tell apart the two possible causes
	// of oversized UI on this device: (A) double-scaling (viewport already DPI-scaled, then ×effectiveScale) vs
	// (B) the density source over-reporting. Compare:
	//   effectiveScale (ImSlate, = ScreenDensity/96) vs engineDPIScale (what UE's own UI uses).
	//   ScreenDensity + Accuracy (0=Unknown/1=Approx/2=Truth) = the raw physical density.
	//   viewportLogicalSize = the coordinate space ImSlate lays out in (if this is already small/DPI-scaled,
	//   ×effectiveScale double-counts → that's cause A).
	{
		int32 ScreenDensity = 0;
		const EScreenPhysicalAccuracy Acc = FPlatformApplicationMisc::GetPhysicalScreenDensity(ScreenDensity);
		const float EngineDPI = SImSlateViewport::StaticGetDPIScaleFactorAtPoint(FVector2D::ZeroVector);
		FVector2D VpSize(0, 0);
		if (ViewportGame) VpSize = ViewportGame->GetCachedGeometry().GetLocalSize();
		UE_LOG(LogTemp, Warning, TEXT("[TitleSizeDbg] effectiveScale=%.3f engineDPIScale=%.3f ScreenDensity=%d Accuracy=%d viewportLogicalSize=%.0fx%.0f TitleHeight=%.1f → titleH=%.1f"),
			ImSlate::GetImSlateEffectiveScale(), EngineDPI, ScreenDensity, (int32)Acc, VpSize.X, VpSize.Y,
			TitleHeight, TitleHeight * ImSlate::GetImSlateEffectiveScale());
	}

	WindowId = ImSlateId;
	Flags = InArgs._Flags;

	ImSlateCond InitCond = ImSlateCond_Always | ImSlateCond_Once | ImSlateCond_Appearing;
	SetWindowByAlphaAllowFlags = InitCond;
	SetWindowPosAllowFlags = InitCond;
	SetWindowSizeAllowFlags = InitCond;
	SetWindowCollapsedAllowFlags = InitCond;
	SetWindowDockAllowFlags = InitCond;

	bShowResizeHandle = !(Flags & (ImSlateWindowFlags_NoResize));

	ContentBackgroundBrush.TintColor = GetDefault<UXImSlateStyleSetting>()->WindowContentColor;

	Children.Add(SAssignNew(ResizeArea, SResizeArea, this));
	Children.Add(SAssignNew(HeaderHandle, SImHeaderArea, this, InArgs._Title));
#if defined(IMSLATE3D_API)
	// slot2 = a 3D box wrapping the item panel. No placement → pure 2D passthrough (identical behaviour);
	// SetNextWindow3DTransform installs a placement and the panel projects onto a 3D world plane.
	Children.Add(SAssignNew(Content3DBox, ImSlate::SImSlate3DTransformBox)
	[
		SAssignNew(ChildPanel, SImSlatePanel, this)
	]);
#else
	Children.Add(SAssignNew(ChildPanel, SImSlatePanel, this));
#endif
}

void SImSlateWindow::SetContentCollapsed(bool bInCollapsed)
{
	ChildPanel->SetVisibility(bInCollapsed ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
}

#if defined(IMSLATE3D_API)
TSharedRef<SWidget> SImSlateWindow::GetContentSlotWidget() const
{
	// slot2 layout/clip root: the box (which forwards to ChildPanel) when present, else the bare panel.
	if (Content3DBox.IsValid())
	{
		return Content3DBox.ToSharedRef();
	}
	return ChildPanel.ToSharedRef();
}

void SImSlateWindow::Apply3DTransform(const ImSlate::FImWorldPlacement& Placement)
{
	if (!Content3DBox.IsValid())
	{
		return;
	}
	Content3DBox->SetWorldPlacement(Placement);
	if (!bIs3D)
	{
		// First frame entering 3D: hide the 2D chrome (title bar / resize handle). A 3D window is moved by
		// a gizmo (phase 2), not by dragging the 2D title bar; its hit region would fall outside the
		// projected trapezoid anyway. Remember the prior state to restore on exit.
		bSaved3DShowTitleBar = IsShowTitleBar();
		bSaved3DShowResize   = bShowResizeHandle;
		SetShowTitleBar(false);
		bShowResizeHandle = false;
		bIs3D = true;
	}
}

void SImSlateWindow::Clear3DTransform()
{
	if (!bIs3D)
	{
		return;  // already 2D — nothing to undo (avoids touching chrome every non-3D frame)
	}
	if (Content3DBox.IsValid())
	{
		Content3DBox->ClearWorldPlacement();
	}
	SetShowTitleBar(bSaved3DShowTitleBar);
	bShowResizeHandle = bSaved3DShowResize;
	bIs3D = false;
}
#endif

bool SImSlateWindow::IsContentCollapsed() const
{
	return ChildPanel->GetVisibility() != EVisibility::Collapsed;
}

void SImSlateWindow::SetShowTitleBar(bool bInShowTitleBar)
{
	HeaderHandle->SetVisibility(bInShowTitleBar ? EVisibility::Visible : EVisibility::Collapsed);
}

bool SImSlateWindow::IsShowTitleBar() const
{
	return HeaderHandle->GetVisibility() != EVisibility::Collapsed;
}

void SImSlateWindow::SetScrollTarget(FVector2D In)
{
	ChildPanel->SetScrollTarget(In);
}

bool SImSlateWindow::CanScrollContent() const
{
	return ChildPanel && ChildPanel->CanScroll();
}

void SImSlateWindow::PanContentMove(FVector2D PressPos, FVector2D CurPos)
{
	if (ChildPanel)
		ChildPanel->ExternalPanMove(PressPos, CurPos);
}

void SImSlateWindow::PanContentEnd(FVector2D CurPos)
{
	if (ChildPanel)
		ChildPanel->ExternalPanEnd(CurPos);
}

void SImSlateWindow::CheckCloseButton(bool* bIn)
{
	if (bIn)
	{
		*bIn = !Hidden;
	}
}

void SImSlateWindow::SetTitleText(FText InTitle)
{
	HeaderHandle->Title = InTitle;
}

FText SImSlateWindow::GetTitleText() const
{
	return HeaderHandle->Title;
}

bool SImSlateWindow::CustomPrepass(float LayoutScaleMultiplier)
{
	return true;
}

FReply SImSlateWindow::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	auto Reply = SImSlateWindowBase::OnFocusReceived(MyGeometry, InFocusEvent);
	if (Reply.IsEventHandled() && Viewport)
	{
		Viewport->BringWindowToFront(ToSharedRef());
	}
	return Reply;
}

SImViewportGame* SImSlateWindow::IsAreaInGameViewport() const
{
	do
	{
		if (!Viewport)
			break;
#if WITH_EDITOR
		if (GIsEditor)
		{
			auto& g = *GImSlate;
			for (auto Gameviewport : g.Viewports)
			{
				if (!Gameviewport->Contains(GetCachedGeometry()))
					continue;
				if (!Gameviewport->IsGameViewport())
					continue;
				return (SImViewportGame*)(&Gameviewport.Get());
			}
		}
		else
#endif
		{
			auto Gameviewport = GetViewportGame();
			if (!Gameviewport)
				break;
			if (!Gameviewport->Contains(GetCachedGeometry()))
				continue;
			if (!Gameviewport->IsGameViewport())
				continue;
			return (SImViewportGame*)Gameviewport;
		}
	} while (false);
	return nullptr;
}

bool SImSlateWindow::IsViewportGame() const
{
	return Viewport == GetViewportGame();
}

SImSlateViewport* SImSlateWindow::SelectViewport()
{
	auto GameViewport = IsAreaInGameViewport();
	if (ViewportOwned && GameViewport)
	{
		return GameViewport;
	}

	if (!GameViewport && !ViewportOwned && bSupportImViewportHost)
	{
		return SelectViewportHost(this, false);
	}

	return Viewport;
}

void SImSlateWindow::UpdateViewport()
{
	if (!ensure(Viewport))
	{
		Viewport = GetViewportGame();
		ViewportOwned = false;
	}
	Viewport->AddWindow(ToSharedRef());
}

void SImSlateWindow::MoveToViewport(SImSlateViewport* InViewport)
{
	InViewport = InViewport ? InViewport : GetViewportGame();
	check(InViewport);
	if (Viewport != InViewport)
	{
		const bool bTargetGameview = InViewport->IsGameViewport();
		if (bTargetGameview)
		{
			// hostview to gameview
			if ((ViewportOwned && ensure(Viewport)))
			{
				// ReleaseViewportHost(ToSharedRef(), Viewport);
			}
		}
		else
		{
			// gameview to hostview
		}

		Viewport = InViewport;
		ViewportOwned = !bTargetGameview;
		// Prevent Appearing re-trigger on viewport switch (keep window "active")
		LastFrameActive = GImSlate->FrameCount;
	}
}

FVector2D SImSlateWindow::CalcSize(FVector2D InSize) const
{
	InSize = FVector2D::Max(MinSize, InSize);
	InSize = FVector2D::Min(MaxSize.Get(InSize), InSize);
	return InSize;
}

void SImSlateWindow::SetContentScale(const TAttribute<FVector2D>& InContentScale)
{
	if (SetAttribute(ContentScale, InContentScale, EInvalidateWidgetReason::Layout))
	{
		Invalidate(EInvalidateWidgetReason::Layout);
#if UE_5_00_OR_LATER
		MarkPrepassAsDirty();
#else
		// Invalidate(EInvalidateWidgetReason::Prepass);
		InvalidatePrepass();
#endif
	}
}

bool SImSlateWindow::ShouldShowCloseButton() const
{
	return true;
}

void SImSlateWindow::SetBgAlpha(float InAlpha)
{
	BackgroudAlpha = InAlpha;
	// SetRenderOpacity(BackgroudAlpha);
}

void SImSlateWindow::SetBgColor(const FLinearColor& InColor)
{
	ContentBackgroundBrush.TintColor = InColor;
}

void SImSlateWindow::SetWindowSize(FVector2D InSize)
{
	RequiredSize = InSize;
	if (ensure(Viewport))
	{
		Viewport->SetWindowSize(this, RequiredSize);
	}
}

void SImSlateWindow::SetWindowContentSize(FVector2D InSize)
{
	if (ensure(Viewport))
	{
		RequiredSize = InSize + FVector2D(PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Horizontal>(), PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Vertical>()) + FVector2D(0.f, GetTitleHeight());
		Viewport->SetWindowSize(this, RequiredSize);
	}
}

void SImSlateWindow::ToggleMaximize()
{
	if (!ensure(Viewport))
		return;

	if (!bMaximized)
	{
		// Save current geometry + the exact move/resize state, then fill the viewport and disable
		// move/resize. Saving the prior NoMove/NoResize bits means restore is exact even for windows
		// that were already non-movable / non-resizable.
		SavedPos = GetWindowPos();
		SavedSize = GetWindowSize();
		bSavedShowResizeHandle = bShowResizeHandle;
		bSavedNoMove = (Flags & ImSlateWindowFlags_NoMove) != 0;
		bSavedNoResize = (Flags & ImSlateWindowFlags_NoResize) != 0;

		// If the window had been popped out into a host SWindow, maximizing means "go fullscreen
		// in the viewport": pull it back into the game viewport first, so fullscreen always == fill
		// the viewport (consistent), and remember to restore it to a host on un-maximize.
		bWasInHostBeforeMaximize = !IsViewportGame();
		if (bWasInHostBeforeMaximize)
		{
			// Save the host popup's REAL on-screen geometry now (it lives in the SWindow, not in
			// ActualPos). Stash it as the pending host geometry so the restored popup is recreated
			// at the same place & size (consumed in SImViewportHost::MakePopupWindow).
			PendingHostPos = GetCachedGeometry().GetAbsolutePosition();
			PendingHostSize = GetCachedGeometry().GetAbsoluteSize();
			MoveToViewport(GetViewportGame());
		}

		const FVector2D ViewportSize = Viewport->GetCachedGeometry().GetLocalSize();

		bMaximized = true;
		Flags |= (ImSlateWindowFlags_NoMove | ImSlateWindowFlags_NoResize);
		bShowResizeHandle = false;

		// Maximize fills the whole viewport (corners may fall under the screen's rounded corners /
		// notch — that's fine, nothing interactive lives there). The titlebar buttons are kept out
		// of the unsafe corner separately: the header insets its right-side button group by
		// safe.Right when maximized (see SImHeaderArea), so close / maximize stay tappable.
		DragingWindowPos(FVector2D::ZeroVector, Viewport->GetCachedGeometry().GetAbsolutePosition());
		if (ViewportSize.X > 0 && ViewportSize.Y > 0)
			SetWindowSize(ViewportSize);
	}
	else
	{
		// Restore: put back exactly the flags/geometry from before maximize.
		bMaximized = false;
		if (!bSavedNoMove)   Flags &= ~ImSlateWindowFlags_NoMove;
		if (!bSavedNoResize) Flags &= ~ImSlateWindowFlags_NoResize;
		bShowResizeHandle = bSavedShowResizeHandle;

		if (bWasInHostBeforeMaximize && bSupportImViewportHost)
		{
			// SPECIAL CASE (host popup restore): the popup's real pos/size live in the SWindow, not
			// in ActualPos. Recreate the host with PendingHostPos/Size (consumed in
			// SImViewportHost::MakePopupWindow) — do NOT use SetWindowSize/DragingWindowPos here
			// (SavedPos is the stale host ActualPos, and it would fight the pending geometry).
			// SelectViewportHost (not SelectViewport): the still-fullscreen window would otherwise
			// be judged as belonging to the game viewport.
			bWasInHostBeforeMaximize = false;
			// Capture the host size BEFORE MoveToViewport — it triggers MakePopupWindow which
			// consumes (Resets) PendingHostSize.
			const TOptional<FVector2D> HostSize = PendingHostSize;
			MoveToViewport(SelectViewportHost(this, false));

			// IMPORTANT: maximizing set ActualSize to the (game) fullscreen size and MakePopupWindow
			// only resizes the real SWindow — it does NOT write ActualSize back. Restore ActualSize
			// to the host's real size here, otherwise a later dock back to the game viewport would
			// arrange the window using the stale fullscreen ActualSize.
			if (HostSize.IsSet())
				ActualSize = HostSize.GetValue();
		}
		else
		{
			// ORIGINAL in-viewport restore (unchanged): put back the saved local pos/size.
			if (!SavedSize.IsZero())
				SetWindowSize(SavedSize);
			DragingWindowPos(SavedPos, SavedPos);
		}
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SImSlateWindow::SetWindowTitleHeight(float InHeight)
{
	if (TitleHeight != InHeight)
	{
		auto Delta = InHeight - TitleHeight;
		ActualSize.Y += Delta;
		TitleHeight = InHeight;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

FVector2D SImSlateWindow::GetContentSize() const
{
	return GetCachedGeometry().GetLocalSize() - FVector2D(0.f, GetTitleHeight());
}

void SImSlateWindow::DragingWindowSize(FVector2D InSize)
{
	InvokeResizeCallback(InSize);
}

void SImSlateWindow::InvokeResizeCallback(FVector2D InSize)
{
	if (ensure(Viewport))
	{
		// Minimum window size floor (resize can't shrink below this). On mobile, scale it by the effective UI
		// scale so the floor stays a sensible PHYSICAL size on high-PPI screens (the raw logical 200×40 would
		// shrink too small). Desktop uses the raw logical value.
		FVector2D MinHeaderSize = HeaderHandle->GetMinSize() * FVector2D(1.f, 2.f);
#if !PLATFORM_DESKTOP
		MinHeaderSize *= ImSlate::GetImSlateEffectiveScale();
#endif
		if (ResizeCallback)
		{
			ImSlateSizeCallbackData Data;
			Data.Window = this;
			Data.Pos = GetWindowPos();
			Data.ContentSize = InSize - FVector2D(0.f, GetTitleHeight());
			Data.DesiredSize = Data.ContentSize;
			ResizeCallback(Data);
			if (Data.ContentSize != Data.DesiredSize)
			{
				SetWindowContentSize(FVector2D::Max(Data.DesiredSize, MinHeaderSize));
			}
		}
		else
		{
			Viewport->SetWindowSize(this, FVector2D::Max(InSize, MinHeaderSize));
		}
	}
}

void SImSlateWindow::DragingWindowPos(FVector2D InPos, FVector2D InAbsolutePos)
{
	if (ensure(Viewport))
	{
		Viewport->SetWindowPos(this, IsViewportGame() ? InPos : InAbsolutePos);
	}
}

FVector2D SImSlateWindow::GetWindowSize() const
{
	if (ensure(Viewport))
	{
		return Viewport->GetWindowSize(this);
	}
	return FVector2D::ZeroVector;
}

FVector2D SImSlateWindow::GetWindowPos() const
{
	if (ensure(Viewport))
	{
		return Viewport->GetWindowPos(this);
	}
	return FVector2D::ZeroVector;
}

void SImSlateWindow::BringWindowToFront()
{
	if (ensure(Viewport))
	{
		Viewport->BringWindowToFront(ToSharedRef());
	}
}

void SImSlateWindow::BeginItemFrame()
{
	if (!(Flags & ImSlateWindowFlags_EventDrived) && ChildPanel)
	{
		ChildPanel->BeginItemFrame();
	}
}

void SImSlateWindow::CommitItemFrame()
{
	if (ChildPanel)
	{
		ChildPanel->CommitItemFrame();
	}
}

void SImSlateWindow::SetItemParent(ImSlateId ItemId)
{
	if (ChildPanel) ChildPanel->SetItemParent(ItemId);
}

void SImSlateWindow::PushFoldContext(ImSlateId FoldId)
{
	if (ChildPanel) ChildPanel->PushFoldContext(FoldId);
}

void SImSlateWindow::PopFoldContext()
{
	if (ChildPanel) ChildPanel->PopFoldContext();
}

void SImSlateWindow::SetFoldCollapsed(ImSlateId FoldId, bool bCollapsed)
{
	if (ChildPanel) ChildPanel->SetFoldCollapsed(FoldId, bCollapsed);
}

void SImSlateWindow::MarkPanelLayoutDirty()
{
	if (ChildPanel)
	{
		ChildPanel->MarkLayoutDirty();
	}
}

FItemSlotPod* SImSlateWindow::FindItem(ImSlateId InId, SWidget** OutWidget, int32* OutChildIndex)
{
	auto FindIdx = ChildPanel->CachedItems.FindIndexById(InId);
	if (FindIdx != INDEX_NONE)
	{
		if (!ChildPanel->Children.IsValidIndex(FindIdx))
		{
			ChildPanel->CachedItems.DeleteByID(InId);
			return nullptr;
		}

		// During frame build, only find items from previous frame (not same-frame additions)
		if (ChildPanel->bFrameStarted && FindIdx >= ChildPanel->GetFrameBaseChildCount())
		{
			return nullptr;
		}

		auto Slot = &ChildPanel->Children[FindIdx];
		if (OutWidget)
			*OutWidget = &Slot->GetWidget().Get();
		if (OutChildIndex)
			*OutChildIndex = FindIdx;
		return Slot;
	}
	return nullptr;
}

FItemSlotPod& SImSlateWindow::AddItem(ImSlateId InId, const TSharedRef<SWidget>& Item)
{
	if (!(Flags & ImSlateWindowFlags_EventDrived))
	{
		int32 NewIndex = ChildPanel->Children.Num();
		ChildPanel->CachedItems.Add(InId, NewIndex);
	}
	return !!(Flags & ImSlateWindowFlags_EventDrived) ? ChildPanel->AddCurrentSlot(InId)[Item] : ChildPanel->AddSlot(InId)[Item];
}

bool SImSlateWindow::ReuseItem(ImSlateId InId, int32 ExistingChildIndex)
{
	if (!(Flags & ImSlateWindowFlags_EventDrived))
	{
		return ChildPanel->TouchSlot(ExistingChildIndex);
	}
	return true;
}

void SImSlateWindow::AddExistItem(ImSlateId InId, FItemSlotPod* InExistSlot)
{
	if (!(Flags & ImSlateWindowFlags_EventDrived))
	{
		int32 NewIndex = ChildPanel->Children.Num();
		ChildPanel->CachedItems.Add(InId, NewIndex);
	}
	!!(Flags & ImSlateWindowFlags_EventDrived) ? ChildPanel->AddCurrentSlot(static_cast<SImSlatePanel::FSlot*>(InExistSlot)) : ChildPanel->AddSlot(static_cast<SImSlatePanel::FSlot*>(InExistSlot));
}

FVector2D SImSlateWindow::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return (FVector2D)TotalDesiredSizes;
}

void SImSlateWindow::CacheDesiredSize(float LayoutScaleMultiplier)
{
	auto TmpTotalDesiredSizes = FVector2D::ZeroVector;
	if (RequiredSize.IsSet())
	{
		// always compute desired size
		if (RequiredSize.GetValue().IsZero())
		{
			if (bool bShowHeader = !(Flags & ImSlateWindowFlags_NoTitleBar))
			{
				HeaderHandle->CacheDesiredSize(LayoutScaleMultiplier);
				TmpTotalDesiredSizes.X = FMath::Max(TmpTotalDesiredSizes.X, HeaderHandle->GetDesiredSize().X);
				TmpTotalDesiredSizes.Y += TitleHeight;
			}
			if (bool bShowContent = (!Collapsed && EVisibility::Collapsed != ChildPanel->GetVisibility()))
			{
				ChildPanel->CacheDesiredSize(LayoutScaleMultiplier);
				TmpTotalDesiredSizes.X = FMath::Max(TmpTotalDesiredSizes.X, ChildPanel->GetDesiredWidth() + PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Horizontal>());
				TmpTotalDesiredSizes.Y += (ChildPanel->GetDesiredHeight() + PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Vertical>());
			}
		}
		else
		{
			TmpTotalDesiredSizes = RequiredSize.GetValue();
		}
		RequiredSize.Reset();

		TotalDesiredSizes = CalcSize(TmpTotalDesiredSizes);
		ActualSize = TotalDesiredSizes;
	}
	else if (Flags & (ImSlateWindowFlags_AlwaysAutoResize) || (SetWindowSizeAllowFlags & ImSlateCond_FirstClearMask))
	{
		if (bool bShowHeader = !(Flags & ImSlateWindowFlags_NoTitleBar))
		{
			HeaderHandle->CacheDesiredSize(LayoutScaleMultiplier);
			TmpTotalDesiredSizes.X = FMath::Max(TmpTotalDesiredSizes.X, HeaderHandle->GetDesiredSize().X);
			TmpTotalDesiredSizes.Y += TitleHeight;
		}
		if (bool bShowContent = (!Collapsed && EVisibility::Collapsed != ChildPanel->GetVisibility()))
		{
			ChildPanel->CacheDesiredSize(LayoutScaleMultiplier);
			TmpTotalDesiredSizes.X = FMath::Max(TmpTotalDesiredSizes.X, ChildPanel->GetDesiredWidth() + PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Horizontal>());
			TmpTotalDesiredSizes.Y += (ChildPanel->GetDesiredHeight() + PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Vertical>());
		}
		TotalDesiredSizes = CalcSize(TmpTotalDesiredSizes);
		ActualSize = TotalDesiredSizes;
	}

	ensure(ActualSize.HasValidSize() && TotalDesiredSizes.HasValidSize());
	if (!ViewportOwned && SetWindowPosPivot.IsSet())
	{
		ActualPos -= SetWindowPosPivot.GetValue() * ActualSize;
		SetWindowPosPivot.Reset();
	}

	if (Viewport && !ViewportOwned)
	{
		FVector2D ViewportSize = Viewport->GetCachedGeometry().GetLocalSize();
		if (ViewportSize.X > 0 && ViewportSize.Y > 0)
		{
			if (!bSupportImViewportHost)
			{
				// Non-multiview (mobile, or imslate.multiview off): the window can't escape to a
				// host SWindow, so it must stay fully inside the viewport on ALL four edges — both
				// position AND size. Without this the old clamp (below) let the window hang off the
				// left/right by MinGrabWidth and off the bottom (clamped only by title height), so a
				// drag past an edge wrote an out-of-bounds target every frame while the partial clamp
				// rewrote it back — the position flickered between the two each frame.
				// Clamp size first (a window bigger than the viewport could never satisfy a 4-edge
				// position clamp), then clamp the top-left so the whole rect fits.
				ActualSize.X = FMath::Min(ActualSize.X, ViewportSize.X);
				ActualSize.Y = FMath::Min(ActualSize.Y, ViewportSize.Y);
				ActualPos.X = FMath::Clamp(ActualPos.X, 0.f, FMath::Max(0.f, ViewportSize.X - ActualSize.X));
				ActualPos.Y = FMath::Clamp(ActualPos.Y, 0.f, FMath::Max(0.f, ViewportSize.Y - ActualSize.Y));
			}
			else
			{
				// Multiview (desktop): a window may be dragged partly off-viewport to then pop out
				// into a host SWindow, so keep the looser clamp that only guarantees a grabbable
				// strip stays on screen.
				if (SetWindowSizeAllowFlags & ImSlateCond_FirstClearMask)
				{
					ActualSize.X = FMath::Min(ActualSize.X, ViewportSize.X);
					ActualSize.Y = FMath::Min(ActualSize.Y, ViewportSize.Y);
				}
				if (!(Flags & ImSlateWindowFlags_NoTitleBar))
				{
					constexpr float MinGrabWidth = 50.f;
					float TitleH = GetTitleHeight();
					ActualPos.X = FMath::Clamp(ActualPos.X, -(ActualSize.X - MinGrabWidth), ViewportSize.X - MinGrabWidth);
					ActualPos.Y = FMath::Clamp(ActualPos.Y, 0.f, FMath::Max(0.f, ViewportSize.Y - TitleH));
				}
			}
		}
	}

#if 0
	if (Flags & ImSlateWindowFlags_AlwaysAutoResize)
		Flags |= ImSlateWindowFlags_NoResize;
#endif

	SetWindowSizeAllowFlags &= ~ImSlateCond_FirstClearMask;
	SImSlateWindowBase::CacheDesiredSize(LayoutScaleMultiplier);
}

FReply SImSlateWindow::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ((Flags & ImSlateWindowFlags_NoMouseInputs) == 0)
	{
		BringWindowToFront();
	}
	return SImSlateWindowBase::OnPreviewMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SImSlateWindow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Slate routes events leaf→root. If we reach here, no child widget handled the click.
	// Safe to start drag detection for window movement. (CanStartMoveDrag also permits a drag while
	// maximized → it restores + follows the finger; see OnDragDetected.)
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && CanStartMoveDrag())
	{
		return FReply::Handled().DetectDrag(this->AsShared(), EKeys::LeftMouseButton).PreventThrottling();
	}
	return FReply::Unhandled();
}

FReply SImSlateWindow::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	// Touch mirror of OnMouseButtonDown. On mobile the window receives TOUCH events, not mouse, so
	// without this: (1) window-move drag never started, and (2) the touch that bubbled up from the
	// header / empty area was left half-handled, which broke the press→release pairing of the
	// titlebar buttons (close / maximize) — they highlighted on touch-down but never fired OnClicked.
	// As with the mouse path we only reach here if no child (a button, or a scrollable panel) consumed
	// the touch first, so it's safe to start drag detection. DetectDrag works for touch too:
	// ProcessPointerMoveEvent routes OnDragDetected once the finger passes the drag threshold (the
	// same code path mouse uses). Use the touch's pointer index so the correct finger is tracked.
	if (CanStartMoveDrag())
	{
		// DetectDrag matches on FKey: a touch's effecting button maps to LeftMouseButton in Slate
		// (FSlateUser::DetectDrag compares DragState->TriggerButton == event.GetEffectingButton()),
		// so use LeftMouseButton here too — same as the mouse path. (DetectDrag has no pointer-index
		// overload; the finger is tracked via the pointer event's index internally.)
		return FReply::Handled().DetectDrag(this->AsShared(), EKeys::LeftMouseButton).PreventThrottling();
	}
	return FReply::Unhandled();
}

FReply SImSlateWindow::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return ChildPanel->OnMouseWheel(ChildPanel->GetCachedGeometry(), MouseEvent);
}

float SImSlateWindow::GetTitleHeight() const
{
	if (Flags & ImSlateWindowFlags_NoTitleBar)
		return 0.f;
	// TitleHeight is a LOGICAL value; ×effectiveScale already makes it physically consistent (scale =
	// ScreenDensity/ReferencePPI). The previous mobile `max(H,44)` over-inflated the bar on high-PPI /
	// density-fallback devices (the square close/maximize buttons are bar-height wide, so the bar grew until
	// they no longer fit). Reverted to the plain logical value.
	return TitleHeight * ImSlate::GetImSlateEffectiveScale();
}

FReply SImSlateWindow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (CanStartMoveDrag())
	{
		// Grab offset = finger position relative to the window's top-left (so the window keeps the
		// same point under the finger while dragging).
		FVector2D AbsGrabOffset = MouseEvent.GetScreenSpacePosition() - MyGeometry.GetAbsolutePosition();

		// Dragging a MAXIMIZED window restores it first, then follows the finger (desktop-style).
		// ToggleMaximize() restores the pre-maximize size; the window then shrinks, so the grab
		// offset (computed against the full-screen geometry) must be rescaled by the width ratio,
		// otherwise the restored window would jump out from under the finger.
		if (bMaximized)
		{
			const float MaxW = (float)MyGeometry.GetLocalSize().X;
			const float RatioX = MaxW > 0.f ? (float)(AbsGrabOffset.X / MaxW) : 0.f;
			const float TitleH = GetTitleHeight();

			ToggleMaximize();  // restores SavedSize and clears NoMove (if it wasn't user-set)

			const float RestoredW = (float)GetWindowSize().X;
			AbsGrabOffset.X = RatioX * RestoredW;          // keep the finger at the same relative X on the titlebar
			AbsGrabOffset.Y = FMath::Min((float)AbsGrabOffset.Y, TitleH * 0.5f);  // clamp into the titlebar
		}

		TSharedRef<FImSlateDragOperation> DragDropOperation = FImSlateDragOperation::New(ToSharedRef(), AbsGrabOffset, MouseEvent.GetPointerIndex());
		return FReply::Handled().BeginDragDrop(DragDropOperation).PreventThrottling();
	}
	return SImSlateWindowBase::OnDragDetected(MyGeometry, MouseEvent);
}

void SImSlateWindow::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	float OffsetHeight = 0;
	auto HeaderHandleVis = HeaderHandle->GetVisibility();
	if (bool bShowHeaderHandle = !(Flags & ImSlateWindowFlags_NoTitleBar) && ArrangedChildren.Accepts(HeaderHandleVis))
	{
		FVector2D HeaderSize = AllottedGeometry.GetLocalSize();
		OffsetHeight = GetTitleHeight();
		HeaderSize.Y = OffsetHeight;
		ArrangedChildren.AddWidget(HeaderHandleVis, AllottedGeometry.MakeChild(HeaderHandle.ToSharedRef(), FVector2D::ZeroVector, HeaderSize));
	}

	auto ChildPanelVis = ChildPanel->GetVisibility();
	if (bool bShowContent = !Collapsed && ArrangedChildren.Accepts(ChildPanelVis))
	{
		const FVector2D PanelSize{AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y - OffsetHeight};
		// Arrange the slot2 ROOT (the 3D box wrapping ChildPanel when present; the bare panel otherwise).
		// The box is a 2D passthrough until a placement is set, so layout is unchanged in the 2D case.
#if defined(IMSLATE3D_API)
		const TSharedRef<SWidget> ContentRoot = GetContentSlotWidget();
#else
		const TSharedRef<SWidget> ContentRoot = ChildPanel.ToSharedRef();
#endif
		ArrangedChildren.AddWidget(ChildPanelVis, AllottedGeometry.MakeChild(ContentRoot, FVector2D(0.f, OffsetHeight) + PanelMargin.GetTopLeft(), PanelSize - PanelMargin.GetDesiredSize()));
	}

	if (bShowResizeHandle)
	{
		// Mobile: enlarge the bottom-right resize hit area to a finger-friendly size (the desktop 24 is too
		// small to grab reliably). The handle is the arranged rect itself, so a bigger rect = bigger hit zone.
		float HandleW = ResizeHandleWidth;
#if PLATFORM_IOS || PLATFORM_ANDROID
		HandleW = FMath::Max(HandleW, 44.f);
#endif
		ArrangedChildren.AddWidget(EVisibility::Visible, AllottedGeometry.MakeChild(ResizeArea.ToSharedRef(), AllottedGeometry.GetLocalSize() - FVector2D{HandleW}, FVector2D{HandleW}));
	}
}

/*
	╭-------------------------------------------------╮
	|                                                 | <-- HeaderHandle
	├-------------------------------------------------┤
	|                                                 │
	|                                                 │
	|  ChildPanel                                     | 
	|                                                 |
	|                                                 │
	|                                               ┌─┤
	╰-----------------------------------------------┴-╯ <-- ResizeHandle
*/
int32 SImSlateWindow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FArrangedChildren ArrangedChildren(EVisibility::Visible);
	this->ArrangeChildren(AllottedGeometry, ArrangedChildren);

	int32 ContentLayerId = LayerId + 1;
	int32 MaxLayerId = ContentLayerId;
	if (ArrangedChildren.Num() > 0)
	{
		FSlateRect MyNewCullingRect(AllottedGeometry.GetRenderBoundingRect(CullingBoundsExtension));
		FSlateClippingZone ClippingZone(MyCullingRect.IntersectionWith(MyNewCullingRect));
		OutDrawElements.PushClip(ClippingZone);
		ON_SCOPE_EXIT { OutDrawElements.PopClip(); };

		// Because we paint multiple children, we must track the maximum layer id that they produced in case one of our parents wants to an overlay for all of its contents.
		const FPaintArgs NewArgs = Args.WithNewParent(this);
		const bool bShouldBeEnabled = ShouldBeEnabled(bParentEnabled);

		for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
		{
			const FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];

			if (!IsChildWidgetCulled(MyNewCullingRect, CurWidget))
			{
				ContentLayerId = CurWidget.Widget->Paint(NewArgs, CurWidget.Geometry, MyNewCullingRect, OutDrawElements, ContentLayerId, InWidgetStyle, bShouldBeEnabled);
				MaxLayerId = FMath::Max(MaxLayerId, ContentLayerId);
			}
			else
			{
				//SlateGI - RemoveContent
			}
		}

		// Content background is now drawn by SImSlatePanel::PaintContentBackground (inside the 3D box's capture
		// subtree, so it perspective-projects with the content). SImSlateWindow is the box's PARENT — anything
		// painted here is outside the projection boundary, so the old MakeCustomVerts background was un-projectable
		// (showed as a flat 2D rect in 3D mode). No special-case: one paint path for 2D and 3D.

		//DockingCross
		if (bShowDockingCross)
		{
		}
	}
	return MaxLayerId;
}

void SImSlateWindow::OnClippingChanged()
{
	ChildPanel->SetClipping(GetClipping());
}

}  // namespace ImSlate
