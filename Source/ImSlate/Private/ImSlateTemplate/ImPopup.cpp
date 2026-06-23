// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImPopup.h"
#include "Framework/Application/SlateApplication.h"
#include "ProtectFieldAccessor.h"

GS_PRIVATEACCESS_MEMBER(SWindow, WindowActivationPolicy, EWindowActivationPolicy);

namespace ImSlate
{

void SetWindowActivationPolicyNever(SWindow& InWindow)
{
	PrivateAccess::WindowActivationPolicy(InWindow) = EWindowActivationPolicy::Never;
}

void SImSlatePopup::Construct(const FArguments& InArgs)
{
	Placement = InArgs._Placement;
	bFocusOnOpen = InArgs._bFocusOnOpen;
	DismissPolicy = InArgs._DismissPolicy;
	OnGetMenuContent = InArgs._OnGetMenuContent;
	OnOpenChanged = InArgs._OnOpenChanged;

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SImSlatePopup::SetMenuContent(TSharedRef<SWidget> InContent)
{
	MenuContentWidget = InContent;
	if (PopupWindow.IsValid())
		PopupWindow->SetContent(InContent);
}

bool SImSlatePopup::ShouldOpenDueToClick() const
{
	return (FPlatformTime::Seconds() - LastDismissTime) > 0.2;
}

bool SImSlatePopup::IsPointInPopupOrAnchor(const FVector2D& AbsPos) const
{
	if (GetCachedGeometry().IsUnderLocation(AbsPos))
		return true;
	if (PopupWindow.IsValid() && PopupWindow->IsVisible())
	{
		FVector2D WinPos = PopupWindow->GetPositionInScreen();
		FVector2D WinSize = PopupWindow->GetSizeInScreen();
		if (FSlateRect(WinPos, WinPos + WinSize).ContainsPoint(AbsPos))
			return true;
	}
	return false;
}

void SImSlatePopup::SetIsOpen(bool bInIsOpen)
{
	SetIsOpen(bInIsOpen, bFocusOnOpen);
}

void SImSlatePopup::SetIsOpen(bool bInIsOpen, bool bFocusMenu, int32 FocusUserIndex)
{
	if (bInIsOpen)
	{
		TSharedPtr<SWidget> Content;
		if (OnGetMenuContent.IsBound())
			Content = OnGetMenuContent.Execute();
		else
			Content = MenuContentWidget;
		if (!Content.IsValid()) return;

		if (bPopupVisible && PopupWindow.IsValid())
		{
			PopupWindow->MoveWindowTo(ComputePopupPosition());
			return;
		}

		if (!PopupWindow.IsValid())
		{
			TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
			if (!ParentWindow.IsValid()) return;

			PopupWindow = SNew(SWindow)
				.Type(EWindowType::Menu)
				.IsPopupWindow(true)
				.SizingRule(ESizingRule::Autosized)
				.AutoCenter(EAutoCenter::None)
				.SupportsTransparency(EWindowTransparency::PerWindow)
				.FocusWhenFirstShown(true)
				.ActivationPolicy(EWindowActivationPolicy::Always)
				.HasCloseButton(false)
				.SupportsMinimize(false)
				.SupportsMaximize(false)
				.CreateTitleBar(false)
				[
					Content.ToSharedRef()
				];

			FSlateApplication::Get().AddWindowAsNativeChild(PopupWindow.ToSharedRef(), ParentWindow.ToSharedRef(), true);

			// Window is now in the input routing. Switch to Never so it won't steal focus again.
			SetWindowActivationPolicyNever(*PopupWindow);
		}
		else
		{
			PopupWindow->SetContent(Content.ToSharedRef());
		}

		PopupWindow->MoveWindowTo(ComputePopupPosition());
		PopupWindow->ShowWindow();
		bPopupVisible = true;

		if (!bFocusMenu)
		{
			TSharedRef<SWidget> AnchorContent = ChildSlot.GetWidget();
			if (AnchorContent != SNullWidget::NullWidget)
				FSlateApplication::Get().SetKeyboardFocus(AnchorContent);
		}

		BindDismissPolicy();
		OnOpenChanged.ExecuteIfBound(true);
	}
	else
	{
		DismissInternal();
	}
}

FVector2D SImSlatePopup::ComputePopupPosition() const
{
	FVector2D AnchorAbsPos = GetCachedGeometry().GetAbsolutePosition();
	FVector2D AnchorSize = GetCachedGeometry().GetAbsoluteSize();
	EMenuPlacement P = Placement.Get(MenuPlacement_BelowAnchor);

	switch (P)
	{
	case MenuPlacement_AboveAnchor:
	case MenuPlacement_AboveRightAnchor:
		return AnchorAbsPos;
	case MenuPlacement_RightLeftCenter:
	case MenuPlacement_MenuRight:
		return AnchorAbsPos + FVector2D(AnchorSize.X, 0);
	case MenuPlacement_MenuLeft:
		return AnchorAbsPos - FVector2D(AnchorSize.X, 0);
	default:
		return AnchorAbsPos + FVector2D(0, AnchorSize.Y);
	}
}

void SImSlatePopup::DismissInternal()
{
	UnbindDismissPolicy();
	if (bPopupVisible)
	{
		bPopupVisible = false;
		if (PopupWindow.IsValid())
			PopupWindow->HideWindow();
		LastDismissTime = FPlatformTime::Seconds();
		OnOpenChanged.ExecuteIfBound(false);
	}
}

void SImSlatePopup::BindDismissPolicy()
{
	UnbindDismissPolicy();

	if (DismissPolicy == EImPopupDismiss::Manual)
		return;

	if (EnumHasAnyFlags(DismissPolicy, EImPopupDismiss::ClickOutside) && PopupWindow.IsValid())
	{
		ClickOutsideHandle = FSlateApplication::Get().GetPopupSupport().RegisterClickNotification(
			PopupWindow.ToSharedRef(),
			FOnClickedOutside::CreateSP(this, &SImSlatePopup::DismissInternal));
	}

	if (EnumHasAnyFlags(DismissPolicy, EImPopupDismiss::PressOutside | EImPopupDismiss::ReleaseOutside | EImPopupDismiss::MoveOutside))
	{
		bool bWasPressed = false;
		DismissTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
			[this, bWasPressed](double, float) mutable -> EActiveTimerReturnType
			{
				if (!IsOpen()) { DismissTimer.Reset(); return EActiveTimerReturnType::Stop; }

				FSlateApplication& App = FSlateApplication::Get();
				FVector2D CursorPos = App.GetCursorPos();
				bool bInside = IsPointInPopupOrAnchor(CursorPos);
				bool bPressed = App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);

				if (EnumHasAnyFlags(DismissPolicy, EImPopupDismiss::PressOutside) && bPressed && !bWasPressed && !bInside)
				{ DismissInternal(); return EActiveTimerReturnType::Stop; }
				if (EnumHasAnyFlags(DismissPolicy, EImPopupDismiss::ReleaseOutside) && !bPressed && bWasPressed && !bInside)
				{ DismissInternal(); return EActiveTimerReturnType::Stop; }
				if (EnumHasAnyFlags(DismissPolicy, EImPopupDismiss::MoveOutside) && !bInside)
				{ DismissInternal(); return EActiveTimerReturnType::Stop; }

				bWasPressed = bPressed;
				return EActiveTimerReturnType::Continue;
			}));
	}
}

void SImSlatePopup::UnbindDismissPolicy()
{
	if (ClickOutsideHandle.IsValid())
	{
		FSlateApplication::Get().GetPopupSupport().UnregisterClickNotification(ClickOutsideHandle);
		ClickOutsideHandle.Reset();
	}
	if (DismissTimer.IsValid())
	{
		auto Timer = DismissTimer;
		DismissTimer.Reset();
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
}

}  // namespace ImSlate
