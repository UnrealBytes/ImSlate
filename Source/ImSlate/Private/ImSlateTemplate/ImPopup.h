// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"

namespace ImSlate
{

DECLARE_DELEGATE_OneParam(FOnImPopupOpenChanged, bool);

IMSLATE_API void SetWindowActivationPolicyNever(SWindow& InWindow);

enum class EImPopupDismiss : uint8
{
	Manual           = 0,         // 仅手动 Close()
	ClickOutside     = 1 << 0,    // 点击 popup 外部关闭（FPopupSupport 机制）
	ReleaseOutside   = 1 << 1,    // 鼠标/触摸释放在 popup 外部时关闭
	PressOutside     = 1 << 2,    // 鼠标/触摸按下在 popup 外部时关闭
	MoveOutside      = 1 << 3,    // 鼠标/触摸移出 popup+anchor 区域时关闭
};
ENUM_CLASS_FLAGS(EImPopupDismiss);

class IMSLATE_API SImSlatePopup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlatePopup)
		: _Placement(MenuPlacement_BelowAnchor)
		, _bFocusOnOpen(true)
		, _DismissPolicy(EImPopupDismiss::ClickOutside)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ATTRIBUTE(EMenuPlacement, Placement)
		SLATE_ARGUMENT(bool, bFocusOnOpen)
		SLATE_ARGUMENT(EImPopupDismiss, DismissPolicy)
		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
		SLATE_EVENT(FOnImPopupOpenChanged, OnOpenChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetMenuContent(TSharedRef<SWidget> InContent);
	void SetIsOpen(bool bInIsOpen);
	void SetIsOpen(bool bInIsOpen, bool bFocusMenu, int32 FocusUserIndex = 0);
	bool IsOpen() const { return bPopupVisible; }
	void SetMenuPlacement(TAttribute<EMenuPlacement> InPlacement) { Placement = MoveTemp(InPlacement); }
	void SetFocusOnOpen(bool bFocus) { bFocusOnOpen = bFocus; }
	void SetDismissPolicy(EImPopupDismiss InPolicy) { DismissPolicy = InPolicy; }
	bool ShouldOpenDueToClick() const;
	TSharedPtr<SWindow> GetPopupWindow() const { return PopupWindow; }

	bool IsPointInPopupOrAnchor(const FVector2D& AbsPos) const;

private:
	TAttribute<EMenuPlacement> Placement;
	bool bFocusOnOpen = true;
	EImPopupDismiss DismissPolicy = EImPopupDismiss::ClickOutside;

	FOnGetContent OnGetMenuContent;
	FOnImPopupOpenChanged OnOpenChanged;

	TSharedPtr<SWidget> MenuContentWidget;
	TSharedPtr<SWindow> PopupWindow;
	bool bPopupVisible = false;
	double LastDismissTime = 0.0;

	FDelegateHandle ClickOutsideHandle;
	TSharedPtr<FActiveTimerHandle> DismissTimer;

	FVector2D ComputePopupPosition() const;
	void DismissInternal();
	void BindDismissPolicy();
	void UnbindDismissPolicy();
};

}  // namespace ImSlate
