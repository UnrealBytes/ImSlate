// Copyright ImSlate, Inc. All Rights Reserved.
#include "SImViewportGame.h"

#include "Application/SlateApplicationBase.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ImSlate.h"
#include "Kismet/KismetMathLibrary.h"
#include "ProtectFieldAccessor.h"
#include "SImSlateWindow.h"
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "Widgets/Layout/SConstraintCanvas.h"

#if WITH_EDITOR
#include "Editor/LevelEditor/Public/ILevelEditor.h"
#include "Editor/LevelEditor/Public/SLevelViewport.h"
#endif

namespace ImSlate
{
// Virtual keyboard must render above ImSlate panels (viewport Z=12000) and ImGui (Z=10000).
static constexpr int32 IMSLATE_VIRTUALKEYBOARD_Z_ORDER = 14000;

#if WITH_EDITOR
TWeakPtr<ILevelEditor> SImViewportGame::CurrentWeakLevelEditor;
#endif
void SImViewportGame::Construct(const FArguments& InArgs, int32 InZOrder)
{
	SImSlateViewport::Construct(InArgs);
	if (auto InClient = InArgs._GameViewportClient)
	{
		InClient->AddViewportWidgetContent(ToSharedRefWithDPI(), InZOrder);
	}
#if WITH_EDITOR
	else if(TSharedPtr<ILevelEditor> LevelEditor = InArgs._LevelEditor.Pin())
	{
		auto LevelViewport = LevelEditor->GetActiveViewportInterface();
		TSharedPtr<SOverlay>& ViewportOverlay = GS_ACCESS_PROTECT(LevelViewport.Get(), SLevelViewport, ViewportOverlay)->ViewportOverlay;
		ViewportOverlay
		->AddSlot(InZOrder)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			ToSharedRefWithDPI()
		];
		WeakLevelEditor = LevelEditor;

		if (!CurrentWeakLevelEditor.IsValid())
			CurrentWeakLevelEditor = LevelEditor;
	}
#endif
}

void SImViewportGame::BringWindowToFront(const TSharedRef<SImSlateWindow>& Widget)
{
	if (Widget->Flags & ImSlateWindowFlags_NoBringToFront)
		return;

	auto CurChildIndex = Children.Find(Widget);
	if (CurChildIndex >= 0 && CurChildIndex != Children.Num() - 1)
	{
		Children.Move(CurChildIndex, Children.Num() - 1);
	}

	for (int32 i = 0; i < Children.Num(); ++i)
	{
		if (Children[i]->DisplayOrder != INT_MAX)
			Children[i]->DisplayOrder = i;
		else
			Children.Move(i, Children.Num() - 1);
	}
}

void SImViewportGame::AddWindow(const TSharedRef<SImSlateWindow>& InWindow)
{
	int32 NewInsertIndex = 0;
	for (; NewInsertIndex < NewChildren.Num(); ++NewInsertIndex)
	{
		if (NewChildren[NewInsertIndex]->GetOrder() > InWindow->GetOrder())
		{
			break;
		}
	}
	//UE_LOG(LogTemp, Log, TEXT("Add Window title:%s, display order:%d, add index:%d"), *InWindow->GetTitleText().ToString(), InWindow->DisplayOrder, NewInsertIndex);
	this->NewChildren.Insert(InWindow, NewInsertIndex);
}

int32 SImViewportGame::RemoveWindow(const TSharedRef<SImSlateWindow>& SlotWidget)
{
	Window = nullptr;
	return Children.Remove(SlotWidget);
}

bool SImViewportGame::Contains(const FGeometry& WindowGeometry) const
{
	do
	{
		auto& CachedGeometry = GetCachedGeometry();
		FVector2D ErrorVec{1.f, 1.f};
		auto TopLeft = WindowGeometry.GetAbsolutePosition();
		auto ViewTopLeft = GetCachedGeometry().GetAbsolutePosition();
		if (!((TopLeft + ErrorVec).ComponentwiseAllGreaterOrEqual(ViewTopLeft)))
			break;

		auto RightBottom = TopLeft + WindowGeometry.GetAbsoluteSize();
		auto ViewRightBottom = ViewTopLeft + GetCachedGeometry().GetAbsoluteSize();
		if (!((RightBottom - ErrorVec).ComponentwiseAllLessOrEqual(ViewRightBottom)))
			break;
		return true;
	} while (false);
	return false;
}

void SImViewportGame::SetWindowPos(SImSlateWindow* InWindow, TOptional<FVector2D> InPos)
{
	if (InPos.IsSet())
	{
		InWindow->ActualPos = InPos.GetValue();
		// 		if (TSharedPtr<SConstraintCanvas> FullScreenCanvas = WeakScreenCanvas.Pin())
		// 		{
		// 			InWindow->PositionAttr = FullScreenCanvas->GetCachedGeometry().LocalToRoundedLocal(InWindow->PositionAttr);
		// 		}
	}
}
void SImViewportGame::SetWindowSize(SImSlateWindow* InWindow, TOptional<FVector2D> InSize)
{
	if (InSize.IsSet())
		InWindow->ActualSize = InSize.GetValue();
}

FVector2D SImViewportGame::GetWindowPos(const SImSlateWindow* InWindow) const
{
	return InWindow->ActualPos;
}

FVector2D SImViewportGame::GetWindowSize(const SImSlateWindow* InWindow) const
{
	return InWindow->ActualSize;
}

void SImViewportGame::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	if (Children.Num() > 0)
	{
		TArray<FArrangedWidget> ArrangedWidgets;
		ArrangedWidgets.Reserve(Children.Num());
		for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
		{
			const TSharedRef<SImSlateWindow>& CurChild = Children[ChildIndex];
			if (CurChild->IsHidden())
				continue;

			const FVector2D ChildSize = CurChild->CalcSize(CurChild->ActualSize);

			//Handle HAlignment
			FVector2D Offset(0.0f, 0.0f);
			switch (CurChild->HAlignment)
			{
				case HAlign_Center:
					Offset.X = -ChildSize.X / 2.0f;
					break;
				case HAlign_Right:
					Offset.X = -ChildSize.X;
					break;
				case HAlign_Fill:
				case HAlign_Left:
					break;
			}

			//handle VAlignment
			switch (CurChild->VAlignment)
			{
				case VAlign_Bottom:
					Offset.Y = -ChildSize.Y;
					break;
				case VAlign_Center:
					Offset.Y = -ChildSize.Y / 2.0f;
					break;
				case VAlign_Top:
				case VAlign_Fill:
					break;
			}
			ArrangedWidgets.Add(AllottedGeometry.MakeChild(CurChild, (CurChild->ActualPos + Offset), ChildSize));

			if (CurChild->DisplayOrder != INT_MAX)
				CurChild->DisplayOrder = ChildIndex;
		}

		// Add the information about this child to the output list (ArrangedChildren)
		for (auto& ArrangedWidget : ArrangedWidgets)
			ArrangedChildren.AddWidget(ArrangedWidget);
	}
}

TSharedPtr<SImSlateVirtualKeyboard> SImViewportGame::GetOrCreateVirtualKeyboard()
{
	if (!VirtualKeyboard.IsValid())
		VirtualKeyboard = SNew(SImSlateVirtualKeyboard);
	return VirtualKeyboard;
}

void SImViewportGame::EnsureKeyboardInViewport()
{
	if (!VirtualKeyboard.IsValid()) return;
	if (VirtualKeyboard->GetParentWidget().IsValid()) return;

	if (auto Client = GetGameViewportClient())
	{
		Client->AddViewportWidgetContent(VirtualKeyboard.ToSharedRef(), IMSLATE_VIRTUALKEYBOARD_Z_ORDER);
	}
#if WITH_EDITOR
	else if (TSharedPtr<ILevelEditor> LevelEditor = WeakLevelEditor.Pin())
	{
		auto LevelViewport = LevelEditor->GetActiveViewportInterface();
		TSharedPtr<SOverlay>& ViewportOverlay = GS_ACCESS_PROTECT(LevelViewport.Get(), SLevelViewport, ViewportOverlay)->ViewportOverlay;
		WeakKeyboardOverlay = ViewportOverlay;  // remember for removal in the destructor
		// VAlign_Fill (not Bottom) so the keyboard widget fills the whole viewport and its
		// origin is fixed. VAlign_Bottom made the widget only as tall as its content, so its
		// top drifted when the suggestion row count changed — dragging the popups along. The
		// keyboard pins its visible content to the bottom internally via a top FillHeight spacer.
		// This matches the game path (AddViewportWidgetContent → default VAlign_Fill).
		ViewportOverlay
		->AddSlot(IMSLATE_VIRTUALKEYBOARD_Z_ORDER)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			VirtualKeyboard.ToSharedRef()
		];
	}
#endif
}

void SImViewportGame::RemoveKeyboard()
{
	// The virtual keyboard is parented into the engine's viewport overlay (independent of this
	// ImSlate world context). When the world is torn down (e.g. editor map switch) the overlay
	// still holds a ref to the keyboard → it lingers on screen after ImSlate disappears. Remove
	// it from BOTH possible parents (game-client overlay and editor ViewportOverlay), and drop
	// our own ref, so it goes away with ImSlate. Called from ~FWorldContextRoot (guaranteed
	// teardown point, while refs are still valid) and from the destructor as a backstop.
	if (!VirtualKeyboard.IsValid())
		return;
	if (auto Client = GetGameViewportClient())
		Client->RemoveViewportWidgetContent(VirtualKeyboard.ToSharedRef());
	if (TSharedPtr<SOverlay> Overlay = WeakKeyboardOverlay.Pin())
		Overlay->RemoveSlot(VirtualKeyboard.ToSharedRef());
	VirtualKeyboard.Reset();
}

SImViewportGame::~SImViewportGame()
{
	RemoveKeyboard();
}

}  // namespace ImSlate
