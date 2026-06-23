// Copyright ImSlate, Inc. All Rights Reserved.
#include "SImViewportHost.h"

#include "Application/SlateApplicationBase.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ImSlate.h"
#include "ImSlateInternal.h"
#include "Kismet/KismetMathLibrary.h"
#include "ProtectFieldAccessor.h"
#include "SImSlateWindow.h"
#include "SImViewportGame.h"
#include "Widgets/Layout/SConstraintCanvas.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace ImSlate
{
void SImViewportHost::Construct(const FArguments& InArgs)
{
	SImSlateViewport::Construct(InArgs);
}

SImViewportHost::~SImViewportHost()
{
	DestroyPopupWindow();
}

void SImViewportHost::DestroyPopupWindow()
{
	if (auto CurWindow = WeakOwner.Pin())
	{
		FSlateApplication::Get().DestroyWindowImmediately(CurWindow.ToSharedRef());
		ClearChildren();
		WeakOwner = nullptr;
		Window = nullptr;
	}
}

void SImViewportHost::SetWindowPos(SImSlateWindow* InWindow, TOptional<FVector2D> InAbsolutePos)
{
	if (!InAbsolutePos.IsSet())
		return;

	if (auto OwnerWindow = WeakOwner.Pin())
	{
		OwnerWindow->MoveWindowTo(InAbsolutePos.GetValue());
	}
}

void SImViewportHost::BringWindowToFront(const TSharedRef<SImSlateWindow>& InWindow)
{
	if (auto OwnerWindow = WeakOwner.Pin())
	{
		OwnerWindow->BringToFront(true);
	}
}

void SImViewportHost::AddWindow(const TSharedRef<SImSlateWindow>& InWindow)
{
	if (Window)
		this->NewChildren.Add(InWindow);

	//	auto SharedRef = GetPopupWindow(&InWindow.Get(), false).ToSharedRef();
	// FSlateApplication::Get().FindWidgetWindow(GetGameViewport())->AddChildWindow(SharedRef);
}

void SImViewportHost::SetWindowSize(SImSlateWindow* InWindow, TOptional<FVector2D> InSize)
{
	if (!InSize.IsSet())
		return;

	InWindow->ActualSize = InSize.GetValue();
	if (auto OwnerWindow = WeakOwner.Pin())
	{
		OwnerWindow->Resize(InSize.GetValue());
	}
}

int32 SImViewportHost::RemoveWindow(const TSharedRef<SImSlateWindow>& InWindow)
{
	Window = nullptr;
	auto Ret = Children.Remove(InWindow);
	if (auto OwnerWindow = WeakOwner.Pin())
	{
		// FSlateApplication::Get().FindWidgetWindow(GetGameViewport())->RemoveDescendantWindow(OwnerWindow.ToSharedRef());
		OwnerWindow->HideWindow();
	}
	InWindow->Viewport = nullptr;
	InWindow->ViewportOwned = false;
	return Ret;
}

void SImViewportHost::OnClose(SImSlateWindow* InWindow)
{
	InWindow->Hidden = true;
	DestroyPopupWindow();
}

void SImViewportHost::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	if (Children.Num() > 0)
	{
		if (!ensure(Window && FindWindowIndex(Window) == 0))
		{
			UE_LOG(LogImSlate, Log, TEXT("SImViewportHost Children Error!"));
		}
		auto Child = Children[0];
		auto ChildVis = Child->GetVisibility();
		if (ArrangedChildren.Accepts(ChildVis))
		{
			ArrangedChildren.AddWidget(ChildVis, AllottedGeometry.MakeChild(Child, FVector2D::ZeroVector, AllottedGeometry.GetLocalSize()));
		}
	}
}

EWindowZone::Type SImViewportHost::GetWindowZoneOverride() const
{
	return SImSlateViewport::GetWindowZoneOverride();
}

TSharedPtr<SWindow> SImViewportHost::MakePopupWindow(SImSlateWindow* InWindow, bool bAutoActive)
{
	check(InWindow);
	Window = InWindow;
	// Prefer an explicitly requested host geometry (set when restoring a maximized popup) over the
	// current cached geometry — at restore time the window is still fullscreen in the game viewport,
	// so its cached geometry would be the fullscreen rect, not the original popup. Consume it once.
	FVector2D WindowPosition = InWindow->PendingHostPos.IsSet()
		? InWindow->PendingHostPos.GetValue()
		: InWindow->GetCachedGeometry().GetAbsolutePosition();
	FVector2D WindowSize = InWindow->PendingHostSize.IsSet()
		? InWindow->PendingHostSize.GetValue()
		: InWindow->GetCachedGeometry().GetAbsoluteSize();
	InWindow->PendingHostPos.Reset();
	InWindow->PendingHostSize.Reset();
	bool bGotWindowSize = !WindowSize.IsNearlyZero();
	auto OwnerWindow = WeakOwner.Pin();
	if (!OwnerWindow)
	{
		struct SImPopupWindow : public SWindow
		{
			using FArguments = SWindow::FArguments;
			void Construct(const FArguments& InArgs, SImViewportHost* InViewport)
			{
				SWindow::Construct(InArgs);
				HostViewport = InViewport->AsSharedRef();
			}
			TSharedPtr<SImViewportHost> HostViewport;
			virtual FPopupMethodReply OnQueryPopupMethod() const override { return FPopupMethodReply::UseMethod(EPopupMethod::CreateNewWindow).SetShouldThrottle(EShouldThrottle::No); }
			virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override
			{
				auto ChildVis = ChildSlot.GetWidget()->GetVisibility();
				if (ArrangedChildren.Accepts(ChildVis))
				{
					ArrangedChildren.AddWidget(ChildVis, AllottedGeometry.MakeChild(ChildSlot.GetWidget(), FVector2D::ZeroVector, AllottedGeometry.GetLocalSize()));
				}
			}
			void Show(bool bAutoActive, bool bInPrepass)
			{
				if (!bAutoActive)
					ShowWindow();
				if (bInPrepass)
					SlatePrepass(FSlateApplicationBase::Get().GetApplicationScale());
			}
		};

		TSharedPtr<SWindow> GameWindow;
		bool bHasGameWindow = false;
		if(Window->GetViewportGame())
		{
			GameWindow = FSlateApplication::Get().FindWidgetWindow(Window->GetViewportGame()->AsShared());
			bHasGameWindow = ensure(GameWindow.IsValid());
		}

		auto SlateWindow = SNew(SImPopupWindow, this)
							.AdjustInitialSizeAndPositionForDPIScale(false)
							.SaneWindowPlacement(true)
							.IsPopupWindow(true)
							.IsTopmostWindow(!bHasGameWindow)
							.CreateTitleBar(false)
							.HasCloseButton(false)
							.SupportsMaximize(false)
							.SupportsMinimize(false)
							.ScreenPosition(WindowPosition)
							.ClientSize(WindowSize)
							.FocusWhenFirstShown(bAutoActive)
							.UserResizeBorder(0.f)
							.LayoutBorder(0.f)
							.ActivationPolicy(bAutoActive ? EWindowActivationPolicy::Always : EWindowActivationPolicy::Never)
							.SupportsTransparency(EWindowTransparency::PerWindow)
							.AutoCenter(!bGotWindowSize ? EAutoCenter::PreferredWorkArea : EAutoCenter::None)
							.SizingRule(!bGotWindowSize ? ESizingRule::Autosized : ESizingRule::FixedSize);

		if (bHasGameWindow)
		{
			FSlateApplication::Get().AddWindowAsNativeChild(SlateWindow, GameWindow.ToSharedRef(), false);
		}
		else
		{
			FSlateApplication::Get().AddWindow(SlateWindow, false);
		}

		OwnerWindow = SlateWindow;
		WeakOwner = OwnerWindow;
		SlateWindow->SetOpacity(FMath::Clamp(InWindow->GetBgAlpha() + 0.2f, 0.f, 1.f));
		OwnerWindow->SetContent(ToSharedRefWithDPI());
		SlateWindow->Show(bAutoActive, bHasGameWindow);
	}
	else
	{
		OwnerWindow->MoveWindowTo(WindowPosition);
		if (bGotWindowSize)
		{
			OwnerWindow->Resize(WindowSize);
		}
		OwnerWindow->SetContent(ToSharedRefWithDPI());
	}

#if WITH_EDITOR
	if (GIsEditor && Window->GetViewportGame() && Window->GetViewportGame()->GetGameViewportClient())
	{
		auto OnSwitchWorldHack = FOnSwitchWorldHack::CreateLambda([PIEInstanceID{InWindow->PIEInstanceID}](int32 In) {  // fix for PIE
			return GS_ACCESS_PROTECT(GEditor, UEditorEngine, OnSwitchWorldForSlatePieWindow)->OnSwitchWorldForSlatePieWindow(In, PIEInstanceID);
		});
		OwnerWindow->SetOnWorldSwitchHack(OnSwitchWorldHack);
	}
#endif
	return OwnerWindow;
}

}  // namespace ImSlate
