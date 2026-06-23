// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "SImSlateViewport.h"

class SWindow;  // engine-global; keep host-window members bound to ::SWindow, not ImSlate::SWindow

namespace ImSlate
{
class IMSLATE_API SImViewportHost final : public SImSlateViewport
{
public:
	using FArguments = SImSlateViewport::FArguments;
	void Construct(const FArguments& InArgs);
	~SImViewportHost();

	TSharedPtr<::SWindow> GetHostWindow() const { return WeakOwner.Pin(); }
	TSharedPtr<SWindow> MakePopupWindow(SImSlateWindow* InWindow, bool bAutoActive);
	void DestroyPopupWindow();

protected:
	virtual void BringWindowToFront(const TSharedRef<SImSlateWindow>& InWindow) override;
	virtual void AddWindow(const TSharedRef<SImSlateWindow>& InWindow) override;
	virtual int32 RemoveWindow(const TSharedRef<SImSlateWindow>& InWidget) override;
	virtual bool IsGameViewport() const override { return false; }

	virtual void SetWindowPos(SImSlateWindow* InWindow, TOptional<FVector2D> InAbsolutePos) override;
	virtual void SetWindowSize(SImSlateWindow* InWindow, TOptional<FVector2D> InSize) override;
	virtual FVector2D GetWindowPos(const SImSlateWindow* InWindow) const { return InWindow->ActualPos; }
	virtual FVector2D GetWindowSize(const SImSlateWindow* InWindow) const { return InWindow->ActualSize; }

	virtual void OnClose(SImSlateWindow* InWindow) override;

protected:
	auto AsSharedRef() { return SharedThis<SImViewportHost>(this); }
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	TWeakPtr<::SWindow> WeakOwner;
	virtual EWindowZone::Type GetWindowZoneOverride() const override;
};
}  // namespace ImSlate
