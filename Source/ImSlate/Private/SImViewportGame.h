// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "SImSlateViewport.h"

class ILevelEditor;
class SOverlay;  // engine-global; keep WeakKeyboardOverlay bound to ::SOverlay, not ImSlate::SOverlay
namespace ImSlate
{
class IMSLATE_API SImViewportGame final : public SImSlateViewport
{
public:
	using FArguments = SImSlateViewport::FArguments;
	void Construct(const FArguments& InArgs, int32 InZOrder = 1024);
	virtual ~SImViewportGame();  // removes the virtual keyboard from the viewport overlay

public:
	virtual void BringWindowToFront(const TSharedRef<SImSlateWindow>& InWindow) override;
	virtual void AddWindow(const TSharedRef<SImSlateWindow>& InWindow) override;
	virtual int32 RemoveWindow(const TSharedRef<SImSlateWindow>& InWidget) override;
	virtual bool IsGameViewport() const override { return true; }

	virtual bool Contains(const FGeometry& WindowGeometry) const;
	virtual void SetWindowPos(SImSlateWindow* InWindow, TOptional<FVector2D> InPos) override;
	virtual void SetWindowSize(SImSlateWindow* InWindow, TOptional<FVector2D> InSize) override;
	virtual FVector2D GetWindowPos(const SImSlateWindow* InWindow) const override;
	virtual FVector2D GetWindowSize(const SImSlateWindow* InWindow) const override;

	TSharedPtr<class SImSlateVirtualKeyboard> GetOrCreateVirtualKeyboard();
	// Non-creating accessor: returns the keyboard only if it already exists (unlike Get() /
	// GetOrCreateVirtualKeyboard, which would lazily create one). Used by the app-lifecycle
	// (suspend/background) handler to hide an already-open keyboard without side-effects.
	TSharedPtr<class SImSlateVirtualKeyboard> GetExistingVirtualKeyboard() const { return VirtualKeyboard; }
	void EnsureKeyboardInViewport();
	void RemoveKeyboard();  // detach the virtual keyboard from the engine viewport overlay

protected:
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	TSharedPtr<class SImSlateVirtualKeyboard> VirtualKeyboard;

	// Overlay the keyboard was added to (game path), so the destructor can remove it.
	TWeakPtr<::SOverlay> WeakKeyboardOverlay;

#if WITH_EDITOR
	TWeakPtr<ILevelEditor> WeakLevelEditor;
	static TWeakPtr<ILevelEditor> CurrentWeakLevelEditor;
#endif
};

}  // namespace ImSlate
