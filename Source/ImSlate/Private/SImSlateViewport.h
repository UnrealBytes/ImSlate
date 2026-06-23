// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "Widgets/SPanel.h"
//
#include "ImSlateInternal.h"
#include "SImSlateLayout.h"
#include "SImSlateWindow.h"

class SConstraintCanvas;
class UGameViewportClient;
class ILevelEditor;
namespace ImSlate
{
class IMSLATE_API SImSlateViewport : public SPanel
{
public:
	SImSlateViewport();
	SLATE_BEGIN_ARGS(SImSlateViewport) {}
	SLATE_ARGUMENT_DEFAULT(EVisibility, Visibility){EVisibility::Visible};
	SLATE_ARGUMENT_DEFAULT(UGameViewportClient*, GameViewportClient){nullptr};
#if WITH_EDITOR
	SLATE_ARGUMENT_DEFAULT(TWeakPtr<ILevelEditor>, LevelEditor){nullptr};
#endif
	SLATE_END_ARGS()
	~SImSlateViewport();

	void Construct(const FArguments& InArgs);

	ImSlateId ID;                // Unique identifier for the viewport
	ImSlateViewportFlags Flags;  // See ImSlateViewportFlags_
	ImSlateWindow* Window = nullptr;  // Set when the viewport is owned by a window (and ImSlateViewportFlags_CanHostOtherWindows is NOT set)

	// Helpers
	TSharedRef<SWidget> ToSharedRefWithDPI(TAttribute<float> InDPIScale = 1.f);

	ImVec2 GetWorkCenter() const;
	ImVec2 GetViewportCenter() const;
	static float StaticGetDPIScaleFactorAtPoint(FVector2D AbsPos = FVector2D::ZeroVector);

public:
	virtual bool IsGameViewport() const = 0;
	virtual void BringWindowToFront(const TSharedRef<SImSlateWindow>& InWindow) = 0;

	virtual void AddWindow(const TSharedRef<SImSlateWindow>& InWindow) = 0;
	virtual int32 RemoveWindow(const TSharedRef<SImSlateWindow>& InWidget) = 0;

	virtual void SetWindowPos(SImSlateWindow* InWindow, TOptional<FVector2D> InPos) = 0;
	virtual void SetWindowSize(SImSlateWindow* InWindow, TOptional<FVector2D> InSize) = 0;
	virtual FVector2D GetWindowPos(const SImSlateWindow* InWindow) const = 0;
	virtual FVector2D GetWindowSize(const SImSlateWindow* InWindow) const = 0;
	FVector2D AbsoluteToLocal(const FVector2D& InPos) { return GetCachedGeometry().AbsoluteToLocal(InPos); }
	virtual bool Contains(const FGeometry& WindowGeometry) const { return false; }

	virtual void OnClose(SImSlateWindow* InWindow);

	UGameViewportClient* GetGameViewportClient() const { return WeakGameViewportClient.Get(); }

public:
	int32 FindWindowIndex(const SImSlateWindow* InWindow) const;
	void RemoveAllWindow();
	void ClearChildren();
	virtual FChildren* GetChildren() override;

protected:
	TWeakObjectPtr<UGameViewportClient> WeakGameViewportClient;
	virtual bool CustomPrepass(float LayoutScaleMultiplier) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
		const override;
	virtual FVector2D ComputeDesiredSize(float) const override;

	TImSlateChildren<SImSlateWindow> Children;
	TImSlateChildren<SImSlateWindow> NewChildren;

	TMap<uint32, TSharedRef<SImSlateWindow>> CachedWindows;
};
}  // namespace ImSlate
