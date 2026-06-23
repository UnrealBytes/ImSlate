// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlate.h"
#include "ImSlatePrivate.h"

#include "Slate.h"

#include "AttributeCompatibility.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/SlateDelegates.h"
#include "ImSlateInternal.h"
#include "ImSlateStyleSetting.h"
#include "ImSlateTemplates.h"
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "ImSlateTemplate/ImButton.h"  // SImButton::SetMousePressBehavior (fold header drag)
#include "Internationalization/Text.h"
#include "Kismet/KismetMathLibrary.h"
#include "PrivateFieldAccessor.h"
#include "ProtectFieldAccessor.h"
#include "SImSlatePanel.h"
#include "SImSlateViewport.h"
#include "SImSlateWindow.h"
#include "SImViewportGame.h"
#include "SImViewportHost.h"
#include "UnrealCompatibility.h"

DEFINE_LOG_CATEGORY(LogImSlate);

extern float GImSlateLayoutScale;
extern int32 GImSlateDragScrollContent;

namespace ImSlate
{
namespace ImSlateInternal
{
	GS_PRIVATEACCESS_FUNCTION(SWidget, Prepass_Internal, void(float))
	GS_PRIVATEACCESS_FUNCTION(SWidget, SetDesiredSize, void(const FVector2D&))

}  // namespace ImSlateInternal

void PrepassInternal(const TSharedRef<SWidget>& InWidget, float LayoutScaleMultiplier)
{
	ImSlateInternal::PrivateAccess::Prepass_Internal(InWidget.Get(), LayoutScaleMultiplier);
}
void SetDesiredSize(SWidget* InWidget, const ImVec2& InSize)
{
	if (InSize.HasValidSize())
		ImSlateInternal::PrivateAccess::SetDesiredSize(*InWidget, InSize);
}

// Save current stack sizes for later compare
void ImSlateStackSizes::SetToCurrentState()
{
	ImSlateContext& g = *GImSlate;
	ImSlateWindow* MyWindow = g.CurrentWindow;
	SizeOfBeginPopupStack = (short)g.BeginPopupStack.Num();
	// SizeOfIDStack = (short)MyWindow->IDStack.Num();
}

// Compare to detect usage errors
void ImSlateStackSizes::CompareWithCurrentState()
{
	ImSlateContext& g = *GImSlate;
	ImSlateWindow* MyWindow = g.CurrentWindow;
	check(MyWindow);

	// Window stacks
	// NOT checking: DC.ItemWidth, DC.TextWrapPos (per MyWindow) to allow user to conveniently push once and not pop (they are cleared on Begin)
	// checkf(SizeOfIDStack == MyWindow->IDStack.Num(), TEXT("PushID/PopID or TreeNode/TreePop Mismatch!"));

	// Global stacks
	// For color, style and font stacks there is an incentive to use Push/Begin/Pop/.../End patterns, so we relax our checks a little to allow them.
	checkf(SizeOfBeginPopupStack == g.BeginPopupStack.Num(), TEXT("BeginPopup/EndPopup or BeginMenu/EndMenu Mismatch!"));
}

static void SetWindowConditionAllowFlags(ImSlateWindow* MyWindow, ImSlateCond Cond, bool bEnabled)
{
	MyWindow->SetWindowPosAllowFlags = bEnabled ? (MyWindow->SetWindowPosAllowFlags | Cond) : (MyWindow->SetWindowPosAllowFlags & ~Cond);
	MyWindow->SetWindowSizeAllowFlags = bEnabled ? (MyWindow->SetWindowSizeAllowFlags | Cond) : (MyWindow->SetWindowSizeAllowFlags & ~Cond);
	MyWindow->SetWindowCollapsedAllowFlags = bEnabled ? (MyWindow->SetWindowCollapsedAllowFlags | Cond) : (MyWindow->SetWindowCollapsedAllowFlags & ~Cond);
	MyWindow->SetWindowDockAllowFlags = bEnabled ? (MyWindow->SetWindowDockAllowFlags | Cond) : (MyWindow->SetWindowDockAllowFlags & ~Cond);
	MyWindow->SetWindowTitleAllowFlags = bEnabled ? (MyWindow->SetWindowTitleAllowFlags | Cond) : (MyWindow->SetWindowTitleAllowFlags & ~Cond);
}

void SetWindowPos(ImSlateWindow* MyWindow, const ImVec2& Pos, ImSlateCond Cond)
{
	// Test condition (NB: bit 0 is always true) and clear flags for next time
	if (Cond && (MyWindow->SetWindowPosAllowFlags & Cond) == 0)
		return;

	check(Cond == 0 || FMath::IsPowerOfTwo(Cond));  // Make sure the user doesn't attempt to combine multiple condition flags.
	MyWindow->SetWindowPosAllowFlags &= ~ImSlateCond_FirstClearMask;
	MyWindow->SetWindowPosVal = ImVec2(FLT_MAX, FLT_MAX);

	MyWindow->DragingWindowPos(Pos, Pos);
}

void SetWindowSize(ImSlateWindow* MyWindow, const ImVec2& Size, ImSlateCond Cond)
{
	// Test condition (NB: bit 0 is always true) and clear flags for next time
	if (Cond && (MyWindow->SetWindowSizeAllowFlags & Cond) == 0)
		return;

	check(Cond == 0 || FMath::IsPowerOfTwo(Cond));  // Make sure the user doesn't attempt to combine multiple condition flags.
	MyWindow->SetWindowSizeAllowFlags &= ~ImSlateCond_FirstClearMask;

	// Set
	MyWindow->SetWindowSize(Size);
}
void SetWindowContentSize(ImSlateWindow* MyWindow, const ImVec2& Size, ImSlateCond Cond)
{
	// Test condition (NB: bit 0 is always true) and clear flags for next time
	if (Cond && (MyWindow->SetWindowSizeAllowFlags & Cond) == 0)
		return;

	check(Cond == 0 || FMath::IsPowerOfTwo(Cond));  // Make sure the user doesn't attempt to combine multiple condition flags.
	MyWindow->SetWindowSizeAllowFlags &= ~ImSlateCond_FirstClearMask;

	// Set
	MyWindow->SetWindowContentSize(Size);
}
void SetWindowCollapsed(ImSlateWindow* MyWindow, bool bCollapsed, ImSlateCond Cond)
{
	// Test condition (NB: bit 0 is always true) and clear flags for next time
	if (Cond && (MyWindow->SetWindowCollapsedAllowFlags & Cond) == 0)
		return;
	MyWindow->SetWindowCollapsedAllowFlags &= ~ImSlateCond_FirstClearMask;

	// Set
	MyWindow->Collapsed = bCollapsed;
}

void SetWindowTopmost(ImSlateWindow* MyWindow, bool bTopmost, ImSlateCond Cond)
{
	// Test condition (NB: bit 0 is always true) and clear flags for next time
	if (Cond && (MyWindow->SetWindowTopmostAllowFlags & Cond) == 0)
		return;
	MyWindow->SetWindowTopmostAllowFlags &= ~ImSlateCond_FirstClearMask;

	// Set
	if (bTopmost)
		MyWindow->DisplayOrder = INT_MAX;
	else if (MyWindow->DisplayOrder == INT_MAX)
		MyWindow->DisplayOrder = INT_MAX - 1;
}

void SetWindowBgAlpha(ImSlateWindow* MyWindow, float Alpha, ImSlateCond Cond)
{
	// Test condition (NB: bit 0 is always true) and clear flags for next time
	if (Cond && (MyWindow->SetWindowByAlphaAllowFlags & Cond) == 0)
		return;
	MyWindow->SetWindowByAlphaAllowFlags &= ~ImSlateCond_FirstClearMask;

	// Set
	MyWindow->SetBgAlpha(Alpha);
}

void SetWindowBgColor(ImSlateWindow* MyWindow, const FLinearColor& InColor, ImSlateCond Cond)
{
	if (Cond && (MyWindow->SetWindowByAlphaAllowFlags & Cond) == 0)
		return;
	MyWindow->SetBgColor(InColor);
}

void SetWindowTitle(ImSlateWindow* MyWindow, const FText& InTitle, ImSlateCond Cond)
{
	// Test condition (NB: bit 0 is always true) and clear flags for next time
	if (Cond && (MyWindow->SetWindowTitleAllowFlags & Cond) == 0)
		return;
	MyWindow->SetWindowTitleAllowFlags &= ~ImSlateCond_FirstClearMask;

	// Set
	MyWindow->SetTitleText(InTitle);
}

void SetWindowFocus(ImSlateWindow* MyWindow, ImSlateCond Cond = 0)
{
	if (MyWindow)
		FSlateApplication::Get().SetAllUserFocus(MyWindow->ToSharedRef());
}

void UpdateWindowParentAndRootLinks(ImSlateWindow* MyWindow, ImSlateWindowFlags InFlags, ImSlateWindow* parent_window)
{
	// MyWindow->ParentWindow = parent_window;
	// 	MyWindow->RootWindow = MyWindow->RootWindowPopupTree = MyWindow->RootWindowDockTree = MyWindow->RootWindowForTitleBarHighlight = MyWindow->RootWindowForNav = MyWindow;
	// 	if (parent_window && (flags & ImSlateWindowFlags_Popup))
	// 		MyWindow->RootWindowPopupTree = parent_window->RootWindowPopupTree;
	// 	while (MyWindow->RootWindowForNav->Flags & ImSlateWindowFlags_NavFlattened)
	// 	{
	// 		IM_ASSERT(MyWindow->RootWindowForNav->ParentWindow != NULL);
	// 		MyWindow->RootWindowForNav = MyWindow->RootWindowForNav->ParentWindow;
	// 	}
}

template<typename T = SImSlateViewport>
static T* GetViewportImpl()
{
	auto& g = *GImSlate;
#if WITH_EDITOR
	for (auto& Viewport : g.Viewports)
	{
		if constexpr (std::is_same<T, SImSlateViewport>::value)
		{
			return &Viewport.Get();
		}
		else if constexpr (std::is_same<T, SImViewportGame>::value)
		{
			if (Viewport->IsGameViewport())
				return static_cast<T*>(&Viewport.Get());
		}
		else
		{
			if (!Viewport->IsGameViewport())
				return static_cast<T*>(&Viewport.Get());
		}
	}
	return nullptr;
#else
	check(g.Viewports.Num() && (std::is_same<T, SImViewportGame>::value || g.Viewports[0]->IsGameViewport()));
	return static_cast<T*>(&g.Viewports[0].Get());
#endif
}

static void WindowSelectViewport(ImSlateWindow* MyWindow)
{
	auto Viewport = MyWindow->Viewport;
#if WITH_EDITOR && 0
	if (Viewport == nullptr)
	{
		Viewport = GetViewportImpl();
		Viewport = MyWindow->SelectViewport();
	}
#endif
	MyWindow->MoveToViewport(Viewport);
	MyWindow->UpdateViewport();
}

SImViewportGame* GetGameViewportImpl()
{
	return GetViewportImpl<SImViewportGame>();
}

SImSlateViewport* GetWindowViewport()
{
	ImSlateContext& g = *GImSlate;
	check(g.CurrentViewport != nullptr && g.CurrentViewport == g.CurrentWindow->Viewport);
	return g.CurrentViewport;
}

ImVec2 GetMainViewportSize()
{
	// Local size of the main (game) viewport, used to position a window relative to the screen (e.g. center it)
	// before Begin(). Avoid GetGameViewportImpl()'s check() — return (0,0) when no viewport exists or it hasn't
	// been laid out yet, so callers can fall back gracefully on the first frame.
	if (!GImSlate)
		return ImVec2::ZeroVector;
	for (auto& Viewport : GImSlate->Viewports)
	{
		if (Viewport->IsGameViewport())
		{
			const FVector2f Sz = (FVector2f)Viewport->GetCachedGeometry().GetLocalSize();
			return ImVec2(Sz.X, Sz.Y);
		}
	}
	return ImVec2::ZeroVector;
}

void BeginFrameImpl(float DeltaTime, ImSlateContext* Ptr)
{
	ImSlateContext& g = *Ptr;
	ensure(!g.bIsFrameStarted);
	if (!g.bIsFrameStarted && g.bIsFrameEnded)
	{
		g.Time += DeltaTime;
		g.WithinFrameScope = true;
		g.FrameCount = GFrameNumber;
		// g.WithinFrameScopeWithImplicitWindow = true;

		//g.CurrentWindowStack.Reset(0);
		//g.BeginPopupStack.Reset(0);

		g.NextWindowData.ClearFlags();
		g.NextItemData.ClearFlags();

		g.bIsFrameEnded = false;
		g.bIsFrameStarted = true;
	}
}
void EndFrameImpl(ImSlateContext* Ptr)
{
	ImSlateContext& g = *Ptr;
	ensure(!g.bIsFrameEnded);
	if (g.bIsFrameStarted && !g.bIsFrameEnded)
	{
		ensureMsgf(g.IDStack.StackInfos.Num() == 1, TEXT("Begin/End mismatch!!!"));
		// g.WithinFrameScopeWithImplicitWindow = false;
		if (g.CurrentWindow && !g.CurrentWindow->WriteAccessed)
			g.CurrentWindow->Active = false;

		// End frame
		g.ReferencedObjects.SetNum(0, EAllowShrinking::No);
		g.WithinFrameScope = false;
		g.FrameCountEnded = g.FrameCount;

		g.LastWindowsById = MoveTemp(g.WindowsById);
		g.LastHostById = MoveTemp(g.HostById);

		g.bIsFrameStarted = false;
		g.bIsFrameEnded = true;
	}
}

IMSLATE_API void BeginFrame(float DeltaTime)
{
	BeginFrameImpl(DeltaTime, GImSlate);
}
IMSLATE_API void EndFrame()
{
	EndFrameImpl(GImSlate);
}

static SImSlateWindow* FindWindowById(uint32 Id)
{
	ImSlateContext& g = *GImSlate;
	if (auto Find = g.WindowsById.Find(Id))
	{
		return &Find->Get();
	}
	if (auto Find = g.LastWindowsById.Find(Id))
	{
		return &Find->Get();
	}

	return nullptr;
}

SImSlateWindow* FindWindowByName(ImStr Name)
{
	uint32 id = ImSlateIdStack::FullHash(Name.GetData(), Name.Len());
	return FindWindowById(id);
}

SImSlateWindow* FindWindowByStackName(ImStr Name)
{
	ImSlateContext& g = *GImSlate;
	uint32 id = g.IDStack.GetId(Name.GetData(), Name.Len());
	return FindWindowById(id);
}

static void SetCurrentWindow(ImSlateWindow* MyWindow)
{
	ImSlateContext& g = *GImSlate;
	g.CurrentWindow = MyWindow;
}

static SImSlateWindow* CreateNewWindowImpl(uint32 InWindowID, ImStr Name, ImWindowFlags Flags)
{
	ImSlateContext& g = *GImSlate;
	auto SlateWindowPtr = SNew(SImSlateWindow, InWindowID)
							.Flags(Flags)
							.Title(Name.StartsWith("##") ? FText::GetEmpty() : FText::FromString(Name.GetData()));
	SImSlateWindow* Window = &SlateWindowPtr.Get();
#if WITH_EDITOR
	Window->PIEInstanceID = g.PIEInstanceID;
#endif
	g.WindowsById.Add(Window->WindowId, SlateWindowPtr);
	return Window;
}

static SImSlateWindow* CreateNewWindow(ImStr Name, ImWindowFlags Flags)
{
	ImSlateContext& g = *GImSlate;
	auto WindowId = g.IDStack.GetId(Name.GetData(), Name.Len());
	return CreateNewWindowImpl(WindowId, Name, Flags);
}

static bool FindOrCreateWindow(ImStr StackName, ImWindowFlags Flags, SImSlateWindow*& OutWindow)
{
	ImSlateContext& g = *GImSlate;
	auto WindowId = g.IDStack.Push(StackName.GetData(), StackName.Len());
	SImSlateWindow* MyWindow = FindWindowById(WindowId);
	if (!MyWindow)
	{
		UE_LOG(LogImSlate, Log, TEXT("FindOrCreateWindow Not Find: WindowId: %d"), WindowId);
	}
	OutWindow = (!MyWindow) ? CreateNewWindowImpl(WindowId, StackName, Flags) : &g.WindowsById.Add(MyWindow->WindowId, MyWindow->ToSharedRef()).Get();
	return !MyWindow;
}

void PushId(ImStr StrId)
{
	ImSlateContext& g = *GImSlate;
	g.IDStack.Push(StrId);
}

void PushId(const void* PtrId)
{
	ImSlateContext& g = *GImSlate;
	char Tmp[256];
	auto Cnt = snprintf(Tmp, 255, "%p", PtrId);
	g.IDStack.Push(Tmp, Cnt);
}

void PushId(int32 IntId)
{
	ImSlateContext& g = *GImSlate;
	char Tmp[32];
	auto Cnt = snprintf(Tmp, 31, "%d", IntId);
	g.IDStack.Push(Tmp, Cnt);
}

void PopId()
{
	ImSlateContext& g = *GImSlate;
	g.IDStack.Pop();
}

uint32 GetId(ImStr StrId)
{
	ImSlateContext& g = *GImSlate;
	return g.IDStack.GetId(StrId);
}

uint32 GetId(const void* ptr_id)
{
	ImSlateContext& g = *GImSlate;
	char tmp[256];
	auto cnt = snprintf(tmp, 255, "%p", ptr_id);
	return g.IDStack.GetId(tmp, cnt);
}
template<typename T>
uint32 TestId(const T& Val)
{
	return GetId(Val);
}

struct ImSlateScopedId
{
	template<typename... TArgs>
	ImSlateScopedId(const TArgs&... Args)
	{
		PushId(Args...);
	}
	~ImSlateScopedId() { PopId(); }
	uint32 GetId() const { return GImSlate->IDStack.GetId(); }
	operator uint32() const { return GetId(); }
};

static void SetWindowViewport(ImSlateWindow* MyWindow, ImSlateViewport*& InViewport)
{
	check(MyWindow && InViewport);

	ImSlateContext& g = *GImSlate;
	g.CurrentViewport = InViewport;

	if (!InViewport->IsGameViewport())
	{
		TSharedPtr<FViewportPopupHolder> Holder;
		auto Shared = StaticCastSharedRef<SImSlateViewport>(InViewport->AsShared());
		if (g.LastHostById.RemoveAndCopyValue(Shared, Holder))
		{
			// keep host lifecycle
			g.HostById.Emplace(Shared, MoveTemp(Holder));
		}
	}

	if (MyWindow->ViewportOwned && MyWindow->Viewport != InViewport)
	{
		check(MyWindow->Viewport->Window == MyWindow);
		MyWindow->Viewport->RemoveWindow(MyWindow->ToSharedRef());
	}

	MyWindow->Viewport = InViewport;
	MyWindow->ViewportId = InViewport->ID;
	MyWindow->ViewportOwned = (InViewport->Window == MyWindow);
}

void SetCurrentViewport(SImSlateWindow* CurrentWindow, SImSlateViewport* InViewport)
{
#if 1
	check(CurrentWindow && InViewport);

	ImSlateContext& g = *GImSlate;
	g.CurrentViewport = InViewport;

	if (!InViewport->IsGameViewport())
	{
		TSharedPtr<FViewportPopupHolder> Holder;
		auto Shared = StaticCastSharedRef<SImSlateViewport>(InViewport->AsShared());
		if (g.LastHostById.RemoveAndCopyValue(Shared, Holder))
		{
			// keep host lifecycle
			g.HostById.Emplace(Shared, MoveTemp(Holder));
		}
	}
#endif
}

ImSlateViewport* FindViewportByID(ImSlateId InId)
{
	ImSlateContext& g = *GImSlate;
	for (int n = 0; n < g.Viewports.Num(); ++n)
	{
		if (g.Viewports[n]->ID == InId)
			return &g.Viewports[n].Get();
	}
	return nullptr;
}

//////////////////////////////////////////////////////////////////////////
void SetWindowPos(ImStr Name, const ImVec2& Pos, ImSlateCond Cond)
{
	if (auto MyWindow = FindWindowByName(Name))
		SetWindowPos(MyWindow, Pos, Cond);
}
void SetWindowSize(ImStr Name, const ImVec2& Size, ImSlateCond Cond)
{
	if (auto MyWindow = FindWindowByName(Name))
		SetWindowSize(MyWindow, Size, Cond);
}
void SetWindowCollapsed(ImStr Name, bool bCollapsed, ImSlateCond Cond)
{
	if (auto MyWindow = FindWindowByName(Name))
		SetWindowCollapsed(MyWindow, bCollapsed, Cond);
}
void SetWindowTopmost(ImStr Name, bool bTopmost, ImSlateCond Cond)
{
	if (auto MyWindow = FindWindowByName(Name))
		SetWindowTopmost(MyWindow, bTopmost, Cond);
}
void SetWindowBgAlpha(ImStr Name, float Alpha, ImSlateCond Cond)
{
	if (auto MyWindow = FindWindowByName(Name))
		SetWindowBgAlpha(MyWindow, Alpha, Cond);
}
void SetWindowTitle(ImStr Name, const FText& InTitle, ImSlateCond Cond)
{
	if (auto MyWindow = FindWindowByName(Name))
		SetWindowTitle(MyWindow, InTitle, Cond);
}

void SetCurrentWindowTitle(TFunctionRef<FText()> InTitle, ImSlateCond Cond)
{
	if (auto MyWindow = GetCurrentWindow())
	{
		// Test condition (NB: bit 0 is always true) and clear flags for next time
		if (Cond && (MyWindow->SetWindowTitleAllowFlags & Cond) == 0)
			return;

		MyWindow->SetWindowTitleAllowFlags &= ~ImSlateCond_FirstClearMask;
		// Set
		MyWindow->SetTitleText(InTitle());
	}
}

void SetWindowFocus(ImStr name, ImSlateCond Cond)
{
	if (auto MyWindow = FindWindowByName(name))
		SetWindowFocus(MyWindow, Cond);
}

//////////////////////////////////////////////////////////////////////////

bool Begin(ImStr Name, bool* bIsOpen, ImWindowFlags Flags, int32 Id)
{
	// Automatically disable manual moving/resizing when NoInputs is set
	if ((Flags & ImSlateWindowFlags_NoInputs) == ImSlateWindowFlags_NoInputs)
		Flags |= ImSlateWindowFlags_NoMove | ImSlateWindowFlags_NoResize;

	if (Flags & ImSlateWindowFlags_NavFlattened)
		check(Flags & ImSlateWindowFlags_ChildWindow);

	if (bIsOpen)
		Flags |= ImSlateWindowFlags_ShowCloseButton;

	ImSlateContext& g = *GImSlate;

	// Find or create
	SImSlateWindow* CurWindow = nullptr;
	const bool bWindowJustCreated = FindOrCreateWindow(Name, Flags, CurWindow);
	g.WindowStack.Push(CurWindow);

	// Save and reset indent for this window
	g.IndentStack.Push(g.CurrentIndent);
	g.CurrentIndent = 0.f;

	const auto CurFrame = g.FrameCount;
	const bool bFirstBeginOfTheFrame = (CurWindow->LastFrameActive != CurFrame);
	bool bWindowJustActivatedByUser = (CurWindow->LastFrameActive < CurFrame - 1);
	if (Flags & ImSlateWindowFlags_Popup)
	{
		ImSlatePopupData& PopupRef = g.OpenPopupStack.Last();
		bWindowJustActivatedByUser |= (CurWindow->PopupId != PopupRef.PopupId);  // We recycle popups so treat MyWindow as activated if popup id changed
		bWindowJustActivatedByUser |= (CurWindow != PopupRef.Window);
	}

	// Update Flags, LastFrameActive fields
	const bool bWindowWasAppearing = CurWindow->Appearing;
	if (bFirstBeginOfTheFrame)
	{
		CurWindow->Appearing = bWindowJustActivatedByUser;
		if (CurWindow->Appearing)
			SetWindowConditionAllowFlags(CurWindow, ImSlateCond_Appearing, true);

		CurWindow->FlagsPreviousFrame = CurWindow->Flags;
		CurWindow->Flags = (ImSlateWindowFlags)Flags;
		// While maximized the window must stay non-movable / non-resizable. Flags are driven by the
		// caller's Begin() every frame (immediate mode), so re-apply NoMove|NoResize here — otherwise
		// the per-frame Flags assignment above would wipe out what ToggleMaximize set and the window
		// would become draggable/resizable again next frame.
		if (CurWindow->IsMaximized())
			CurWindow->Flags |= (ImSlateWindowFlags_NoMove | ImSlateWindowFlags_NoResize);
		CurWindow->LastFrameActive = CurFrame;
	}
	else
	{
		Flags = CurWindow->Flags;
	}

	// Add to stack
	// We intentionally set g.CurrentWindow to NULL to prevent usage until when the viewport is set, then will call SetCurrentWindow()
	g.CurrentWindow = CurWindow;
	ImSlateWindowStackData WindowStackData;
	WindowStackData.Window = CurWindow;
	WindowStackData.ParentLastItemDataBackup = g.LastItemData;
	WindowStackData.StackSizesOnBegin.SetToCurrentState();
	g.CurrentWindowStack.Push(WindowStackData);

	g.CurrentWindow = nullptr;
	if (Flags & ImSlateWindowFlags_Popup)
	{
		ImSlatePopupData& PopupRef = g.OpenPopupStack.Last();
		PopupRef.Window = CurWindow;
		g.BeginPopupStack.Push(PopupRef);
		CurWindow->PopupId = PopupRef.PopupId;
	}

	// Code explicitly request a viewport
	const bool bLockViewport = g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasViewport;
	if (bLockViewport)
	{
		CurWindow->Viewport = FindViewportByID(g.NextWindowData.ViewportId);
		CurWindow->ViewportId = g.NextWindowData.ViewportId;
	}

	// Init
	if (bFirstBeginOfTheFrame)
	{
		// Begin in-place frame tracking for non-EventDrived panels
		CurWindow->BeginItemFrame();

		// Initialize
		CurWindow->Active = true;
		WindowSelectViewport(CurWindow);
		SetWindowViewport(CurWindow, CurWindow->Viewport);
		Flags = CurWindow->Flags;
	}
	else
	{
		SetWindowViewport(CurWindow, CurWindow->Viewport);
	}
	CurWindow->CheckCloseButton(bIsOpen);

	// Process SetNextWindow***() calls
	// (FIXME: Consider splitting the HasXXX flags into X/Y components
	bool bWindowPosSetByApi = false;
	bool bWindowSizeXSetByApi = false;
	bool bWindowSizeYSetByApi = false;

	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasSize)
	{
		bWindowSizeXSetByApi = (CurWindow->SetWindowSizeAllowFlags & g.NextWindowData.SizeCond) != 0 && (g.NextWindowData.SizeVal.X > 0.0f);
		bWindowSizeYSetByApi = (CurWindow->SetWindowSizeAllowFlags & g.NextWindowData.SizeCond) != 0 && (g.NextWindowData.SizeVal.Y > 0.0f);
		SetWindowSize(CurWindow, g.NextWindowData.SizeVal, g.NextWindowData.SizeCond);
	}

	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasContentSize)
	{
		bWindowSizeXSetByApi = (CurWindow->SetWindowSizeAllowFlags & g.NextWindowData.ContentSizeCond) != 0 && (g.NextWindowData.ContentSizeVal.X > 0.0f);
		bWindowSizeYSetByApi = (CurWindow->SetWindowSizeAllowFlags & g.NextWindowData.ContentSizeCond) != 0 && (g.NextWindowData.ContentSizeVal.Y > 0.0f);
		SetWindowContentSize(CurWindow, g.NextWindowData.ContentSizeVal, g.NextWindowData.ContentSizeCond);
	}

	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasResizeCallback && g.NextWindowData.ResizeCallback)
	{
		CurWindow->ResizeCallback = MoveTemp(g.NextWindowData.ResizeCallback);
	}

	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasPos)
	{
		bWindowPosSetByApi = (CurWindow->SetWindowPosAllowFlags & g.NextWindowData.PosCond) != 0;
		if (bWindowPosSetByApi && g.NextWindowData.PosPivotVal.HasValidSize())
		{
			// May be processed on the next frame if this is our first frame and we are measuring size
			// FIXME: Look into removing the branch so everything can go through this same code path for consistency.
			CurWindow->SetWindowPosVal = g.NextWindowData.PosVal;
			CurWindow->SetWindowPosPivot = g.NextWindowData.PosPivotVal;
			// CurWindow->SetWindowPosAllowFlags &= ~ImSlateCond_FirstClearMask;
		}
		SetWindowPos(CurWindow, g.NextWindowData.PosVal, g.NextWindowData.PosCond);
	}

	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasTitle)
		SetWindowTitle(CurWindow, g.NextWindowData.TitleVal, g.NextWindowData.TitleCond);
	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasScroll)
		CurWindow->SetScrollTarget(g.NextWindowData.ScrollVal);
	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasBgAlpha)
		SetWindowBgAlpha(CurWindow, g.NextWindowData.BgAlphaVal, g.NextWindowData.BgAlphaCond);
	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasBgColor)
		SetWindowBgColor(CurWindow, g.NextWindowData.BgColorVal, g.NextWindowData.BgColorCond);
	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasCollapsed)
		SetWindowCollapsed(CurWindow, g.NextWindowData.CollapsedVal, g.NextWindowData.CollapsedCond);
	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasTopmost)
		SetWindowTopmost(CurWindow, g.NextWindowData.TopmostVal, g.NextWindowData.TopmostCond);

	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_HasFocus)
		SetWindowFocus(CurWindow);

#if defined(IMSLATE3D_API)
	// SetNextWindow3DTransform: drive the window's 3D state every frame. Flag set → project onto the world
	// plane; flag absent → revert to 2D (only does work on the transition, so non-3D windows pay nothing).
	if (g.NextWindowData.Flags & ImSlateNextWindowDataFlags_Has3DTransform)
		CurWindow->Apply3DTransform(g.NextWindowData.Placement3DVal);
	else
		CurWindow->Clear3DTransform();
#endif

	if (!bWindowJustCreated)
		SetWindowConditionAllowFlags(CurWindow, ImSlateCond_Appearing, false);

	g.CurrentWindow = CurWindow;
	// Clear 'accessed' flag last thing (After PushClipRect which will set the flag.
	// We want the flag to stay false when the default "Debug" MyWindow is unused)
	CurWindow->BeginCount++;
	g.NextWindowData.ClearFlags();

	// Update visibility
	if (bFirstBeginOfTheFrame)
	{
#if WITH_EDITOR
		if (CurWindow->SkipItems && !CurWindow->Appearing)
		{
			check(CurWindow->Appearing == false);
		}
#endif
	}

	return !CurWindow->SkipItems;
}

void End()
{
	ImSlateContext& g = *GImSlate;
	ImSlateWindow* MyWindow = g.CurrentWindow;
	check(MyWindow);

	// Restore indent from before this window's Begin
	if (g.IndentStack.Num() > 0)
		g.CurrentIndent = g.IndentStack.Pop();

	g.NextItemData.ClearFlags();

	// Error checking: verify that user hasn't called End() too many times!
	if (g.CurrentWindowStack.Num() <= 1 && g.WithinFrameScopeWithImplicitWindow)
	{
		checkf(g.CurrentWindowStack.Num() > 1, TEXT("Calling End() too many times!"));
		return;
	}
	check(g.CurrentWindowStack.Num() > 0);

	checkSlow(g.CurrentWindow && g.WindowStack.Last() == g.CurrentWindow);
	g.WindowStack.Pop();

	// Error checking: verify that user doesn't directly call End() on a child MyWindow.
	if ((MyWindow->Flags & ImSlateWindowFlags_ChildWindow) && !(MyWindow->Flags & ImSlateWindowFlags_DockNodeHost))
		checkf(g.WithinEndChild, TEXT("Must call EndChild() and not End()!"));

	// Pop from MyWindow stack
	g.LastItemData = g.CurrentWindowStack.Last().ParentLastItemDataBackup;
	if (MyWindow->Flags & ImSlateWindowFlags_Popup)
		g.BeginPopupStack.Pop(EAllowShrinking::No);
	g.CurrentWindowStack.Last().StackSizesOnBegin.CompareWithCurrentState();
	g.CurrentWindowStack.Pop(EAllowShrinking::No);
	SetCurrentWindow(g.CurrentWindowStack.Num() == 0 ? nullptr : g.CurrentWindowStack.Last().Window);
	if (g.CurrentWindow)
		SetWindowViewport(g.CurrentWindow, g.CurrentWindow->Viewport);
	// Commit frame in End() — like SlateIM's EndRoot(), all changes finalized during PreTick
	if (!(MyWindow->Flags & ImSlateWindowFlags_EventDrived))
	{
		MyWindow->CommitItemFrame();
	}

	MyWindow->LastFrameEnded = g.FrameCount;
	g.IDStack.Pop();
}

ImSlateWindow* GetCurrentWindowRead()
{
	ImSlateContext& g = *GImSlate;
	return g.CurrentWindow;
}

ImSlateWindow* GetCurrentWindow()
{
	ImSlateContext& g = *GImSlate;
	return g.CurrentWindow;
}

void SetNextWindowPos(const ImVec2& Pos, ImSlateCond Cond, const ImVec2& Pivot)
{
	ImSlateContext& g = *GImSlate;
	check(Cond == 0 || FMath::IsPowerOfTwo(Cond));
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasPos;
	g.NextWindowData.PosVal = Pos;
	g.NextWindowData.PosPivotVal = Pivot;
	g.NextWindowData.PosCond = Cond ? Cond : ImSlateCond_Always;
	g.NextWindowData.PosUndock = true;
}

void SetNextWindowSize(const ImVec2& Size, ImSlateCond Cond)
{
	ImSlateContext& g = *GImSlate;
	check(Cond == 0 || FMath::IsPowerOfTwo(Cond));
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasSize;
	g.NextWindowData.SizeVal = Size;
	g.NextWindowData.SizeCond = Cond ? Cond : ImSlateCond_Always;
}

void SetNextContentSize(const ImVec2& Size, ImSlateCond Cond)
{
	ImSlateContext& g = *GImSlate;
	check(Cond == 0 || FMath::IsPowerOfTwo(Cond));
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasContentSize;
	g.NextWindowData.ContentSizeVal = Size;
	g.NextWindowData.ContentSizeCond = Cond ? Cond : ImSlateCond_Always;
}

void SetNextWindowContentSize(const ImVec2& Size)
{
	ImSlateContext& g = *GImSlate;
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasContentSize;
	g.NextWindowData.ContentSizeVal = Size;
}

void SetNextWindowCollapsed(bool bCollapsed, ImSlateCond Cond)
{
	ImSlateContext& g = *GImSlate;
	check(Cond == 0 || FMath::IsPowerOfTwo(Cond));
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasCollapsed;
	g.NextWindowData.CollapsedVal = bCollapsed;
	g.NextWindowData.CollapsedCond = Cond ? Cond : ImSlateCond_Always;
}

void SetNextWindowTopmost(bool bTopmost, ImSlateCond Cond)
{
	ImSlateContext& g = *GImSlate;
	check(Cond == 0 || FMath::IsPowerOfTwo(Cond));
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasTopmost;
	g.NextWindowData.TopmostVal = bTopmost;
	g.NextWindowData.TopmostCond = Cond ? Cond : ImSlateCond_Always;
}

void SetNextWindowFocus()
{
	ImSlateContext& g = *GImSlate;
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasFocus;
}

void SetNextWindowBgAlpha(float Alpha, ImSlateCond Cond)
{
	ImSlateContext& g = *GImSlate;
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasBgAlpha;
	g.NextWindowData.BgAlphaVal = Alpha;
	g.NextWindowData.BgAlphaCond = Cond ? Cond : ImSlateCond_Always;
}

void SetNextWindowBgColor(const FLinearColor& InColor, ImSlateCond Cond)
{
	ImSlateContext& g = *GImSlate;
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasBgColor;
	g.NextWindowData.BgColorVal = InColor;
	g.NextWindowData.BgColorCond = Cond ? Cond : ImSlateCond_Always;
}

void SetCurrentWindowColorAndOpacity(const FLinearColor& InColor)
{
	if (auto* MyWindow = GetCurrentWindow())
		MyWindow->SetColorAndOpacity(InColor);
}

void SetCurrentWindowForegroundColor(const FSlateColor& InColor)
{
	if (auto* MyWindow = GetCurrentWindow())
		MyWindow->SetForegroundColor(InColor);
}

void SetCurrentWindowContentScale(const FVector2D& InScale)
{
	if (auto* MyWindow = GetCurrentWindow())
		MyWindow->SetContentScale(InScale);
}

void SetNextWindowTitle(const FText& InTitle, ImSlateCond Cond)
{
	ImSlateContext& g = *GImSlate;
	check(Cond == 0 || FMath::IsPowerOfTwo(Cond));
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasTitle;
	g.NextWindowData.TitleVal = InTitle;
	g.NextWindowData.TitleCond = Cond ? Cond : ImSlateCond_Always;
}

#if defined(IMSLATE3D_API)
void SetNextWindow3DTransform(const ImSlate::FImWorldPlacement& Placement)
{
	ImSlateContext& g = *GImSlate;
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_Has3DTransform;
	g.NextWindowData.Placement3DVal = Placement;
}

void SetNextWindow3DTransform(const FVector& WorldLocation, const FRotator& WorldRotation, float WorldScale)
{
	// Common-params overload → build the placement and delegate to the full one (DRY).
	ImSlate::FImWorldPlacement P;
	P.WorldLocation = WorldLocation;
	P.WorldRotation = WorldRotation;
	P.WorldScale = WorldScale;
	SetNextWindow3DTransform(P);
}

void SetNextWindow3DTransform(const FVector& WorldLocation, const FVector2f& Pivot,
	const FRotator& WorldRotation)
{
	ImSlate::FImWorldPlacement P;
	P.WorldLocation = WorldLocation;
	P.Pivot = Pivot;
	P.WorldRotation = WorldRotation;
	SetNextWindow3DTransform(P);
}
#endif

void SetNextWindowResizeCallback(ImSlateResizeCallback CustomCallback)
{
	ImSlateContext& g = *GImSlate;
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasResizeCallback;
	g.NextWindowData.ResizeCallback = MoveTemp(CustomCallback);
}

void SetNextWindowViewport(ImSlateId InViewportId, ImSlateCond Cond)
{
	ImSlateContext& g = *GImSlate;
	check(Cond == 0 || FMath::IsPowerOfTwo(Cond));
	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasViewport;
	g.NextWindowData.ViewportId = InViewportId;
	g.NextWindowData.ViewportCond = Cond ? Cond : ImSlateCond_Always;
}

// void SetNextWindowViewport(uint32 viewport_id)
// {
// 	ImSlateContext& g = *GImSlate;
// 	g.NextWindowData.Flags |= ImSlateNextWindowDataFlags_HasViewport;
// 	g.NextWindowData.ViewportId = viewport_id;
// }

bool IsWindowValid(ImStr Name)
{
	ImSlateWindow* WindowPtr = FindWindowByName(Name);
	if (WindowPtr)
	{
		return true;
	}
	return false;
}

bool IsWindowAppearing()
{
	ImSlateWindow* MyWindow = GetCurrentWindowRead();
	return MyWindow->Appearing;
}

bool IsWindowCollapsed()
{
	ImSlateWindow* MyWindow = GetCurrentWindowRead();
	return MyWindow->Collapsed;
}

bool IsWindowFocused(ImSlateFocusedFlags InFlags)
{
	ImSlateWindow* MyWindow = GetCurrentWindowRead();
	return FSlateApplication::Get().GetCursorUser()->HasFocusedDescendants(MyWindow->ToSharedRef());
}

bool IsWindowHovered(ImSlateFocusedFlags InFlags)
{
	check((InFlags & (ImSlateHoveredFlags_AllowWhenOverlapped | ImSlateHoveredFlags_AllowWhenDisabled)) == 0);
	ImSlateWindow* MyWindow = GetCurrentWindowRead();
	return FSlateApplication::Get().GetCursorUser()->IsWidgetUnderCursor(MyWindow->ToSharedRef());
}

float GetWindowDpiScale()
{
	float DpiScale = FSlateApplication::Get().GetApplicationScale();
	if (auto Window = GetCurrentWindowRead())
	{
		auto AbsPos = Window->GetCachedGeometry().GetAbsolutePosition();
		DpiScale = SImSlateViewport::StaticGetDPIScaleFactorAtPoint(AbsPos);
	}
	return DpiScale;
}

ImVec2 GetWindowPos()
{
	ImSlateWindow* MyWindow = GetCurrentWindowRead();
	return (ImVec2)MyWindow->GetCachedGeometry().GetAbsolutePosition();
}

ImVec2 GetWindowSize()
{
	ImSlateWindow* MyWindow = GetCurrentWindowRead();
	return (ImVec2)MyWindow->GetCachedGeometry().GetAbsoluteSize();
}

ImVec2 GetWindowContentSize()
{
	ImSlateWindow* MyWindow = GetCurrentWindowRead();
	return MyWindow->GetContentSize();
}

float GetWindowByAlpha()
{
	ImSlateWindow* MyWindow = GetCurrentWindowRead();
	return MyWindow->GetColorAndOpacity().A;
}

float GetWindowWidth()
{
	return GetWindowSize().X;
}

float GetWindowHeight()
{
	return GetWindowSize().Y;
}

bool IsCurrentWindowEnded(ImSlateContext& g)
{
	return g.FrameCount == g.CurrentWindow->LastFrameEnded;
}

bool IsCurrentWindowEnded()
{
	ImSlateContext& g = *GImSlate;
	return IsCurrentWindowEnded(g);
}

//////////////////////////////////////////////////////////////////////////
namespace Internal
{

	static FItemSlotPod& AddWidgetImpl(ImSlateContext& g, ImStr name = "$__ImslateDummyItem__$", const TSharedRef<SWidget>& InWidget = SNullWidget::NullWidget)
	{
		auto ItemId = ImSlateScopedId(name);
		SWidget* OutWidget = nullptr;
		int32 ExistingIndex = INDEX_NONE;
		FItemSlotPod* Slot = g.CurrentWindow->FindItem(ItemId, &OutWidget, &ExistingIndex);
		if (Slot && g.CurrentWindow->ReuseItem(ItemId, ExistingIndex))
		{
			// Reused existing slot in-place
		}
		else
		{
			// New item or duplicate ID — create new slot
			FItemSlotPod ItemSlot;
			auto Widget = InWidget;
			Slot = &g.CurrentWindow->AddItem(ItemId, Widget);
			OutWidget = &Widget.Get();
			Slot->Apply(ItemSlot);  // Apply preserves Hash (identity)
		}
		return *Slot;
	}

	static FItemSlotPod& FindOrCreateItemInWindow(ImSlateWindow* CurrentWindow, ImSlateId ItemId, const Internal::FWidgetFactoryType& WidgetConstruct, SWidget*& OutWidget)
	{
		check(CurrentWindow);
		int32 ExistingIndex = INDEX_NONE;
		FItemSlotPod* Slot = CurrentWindow->FindItem(ItemId, &OutWidget, &ExistingIndex);
		if (Slot && CurrentWindow->ReuseItem(ItemId, ExistingIndex))
		{
			// Reused existing slot in-place
		}
		else
		{
			// New item or duplicate ID — create new slot
			FItemSlotPod ItemSlot;
			auto Widget = WidgetConstruct(ItemSlot);
			Slot = &CurrentWindow->AddItem(ItemId, Widget);
			OutWidget = &Widget.Get();
			Slot->Apply(ItemSlot);  // Apply preserves Hash (identity)
		}
		return *Slot;
	}

	SWidget* Item(ImStr Name, const FWidgetFactoryType& WidgetConstruct)
	{
		SWidget* WidgetPtr = nullptr;
		ImSlateContext& g = *GImSlate;
		if (!IsCurrentWindowEnded(g))
		{
			auto ItemId = ImSlateScopedId(Name);
			auto& Slot = FindOrCreateItemInWindow(g.CurrentWindow, ItemId, WidgetConstruct, WidgetPtr);
			// Apply global indent to left padding
			if (g.CurrentIndent > 0.f)
			{
				g.NextItemData.Flags |= ImSlateNextItemDataFlags_Padding;
				g.NextItemData.SlotPadding.Left = g.CurrentIndent;
			}

			// Apply current group context. Write always (even when CurrentGroupId == 0) so that
			// a reused item that left its group gets its stale GroupId cleared.
			g.NextItemData.Flags |= ImSlateNextItemDataFlags_Group;
			g.NextItemData.GroupId = g.CurrentGroupId;
			g.NextItemData.GroupColor = g.CurrentGroupColor;

			// Register item in tree (assigns current fold context as parent)
			g.CurrentWindow->SetItemParent(ItemId);
			if (Slot.ApplyNextItem(g.NextItemData))
				g.CurrentWindow->MarkPanelLayoutDirty();
			g.NextItemData.ClearFlags();
			g.NextItemData.Flags |= ImSlateNextItemDataFlags_NewRow;
			g.NextItemData.bNewRow = true;
			g.NextItemTooltip = FText::GetEmpty();
		}
		return WidgetPtr;
	}
}  // namespace Internal

bool FItemSlotPod::ApplyNextItem(const ImSlateNextItemData& NextItemData)
{
	const FItemSlotPod Before = *this;

	// NewLine/SameLine
	bNewRow = !!(NextItemData.Flags & ImSlateNextItemDataFlags_NewRow);

	// Padding
	if (NextItemData.Flags & ImSlateNextItemDataFlags_Padding)
		SlotPadding = NextItemData.SlotPadding;

	// min/max size
	if (NextItemData.Flags & ImSlateNextItemDataFlags_MaxWidth)
		MaxWidth = NextItemData.MaxWidth;
	if (NextItemData.Flags & ImSlateNextItemDataFlags_MaxHeight)
		MaxHeight = NextItemData.MaxHeight;
	if (NextItemData.Flags & ImSlateNextItemDataFlags_MinWidth)
		MinWidth = NextItemData.MinWidth;
	if (NextItemData.Flags & ImSlateNextItemDataFlags_MinHeight)
		MinHeight = NextItemData.MinHeight;

	if (NextItemData.Flags & ImSlateNextItemDataFlags_FillWidth)
	{
		bFillWidth = NextItemData.bFillWidth;
		StretchValue = NextItemData.StretchValue;
	}
	else if (NextItemData.Flags & ImSlateNextItemDataFlags_Stretch)
	{
		StretchValue = NextItemData.StretchValue;
	}

	// align to col
	if (NextItemData.Flags & ImSlateNextItemDataFlags_AlignCol)
	{
		bAlignCol = NextItemData.bAlignCol;
		StretchToCol = NextItemData.StretchToCol;
	}

	if (NextItemData.Flags & ImSlateNextItemDataFlags_Collapsed)
		bCollapsed = NextItemData.bCollapsed;
	if (NextItemData.Flags & ImSlateNextItemDataFlags_HAlign)
		HAlignment = NextItemData.HAlignment;
	if (NextItemData.Flags & ImSlateNextItemDataFlags_VAlign)
		VAlignment = NextItemData.VAlignment;

	if (NextItemData.Flags & ImSlateNextItemDataFlags_Parent)
		ParentIdx = NextItemData.ParentIdx;

	if (NextItemData.Flags & ImSlateNextItemDataFlags_AspectRatio)
		AspectRatio = NextItemData.AspectRatio;

	if (NextItemData.Flags & ImSlateNextItemDataFlags_Group)
	{
		GroupId = NextItemData.GroupId;
		GroupColor = NextItemData.GroupColor;
	}

	return !(*this == Before);
}

void SameLine()
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags &= ~ImSlateNextItemDataFlags_NewRow;
	g.NextItemData.bNewRow = false;
}
void NewLine()
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_NewRow;
	g.NextItemData.bNewRow = true;
}

void RightAlign(int32 Col)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_AlignCol;
	g.NextItemData.bAlignCol = true;
	g.NextItemData.StretchToCol = Col;
}

void StretchVal(float Val)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_Stretch;
	g.NextItemData.bAlignCol = false;
	g.NextItemData.StretchValue = Val;
}

void Spacing(float Val)
{
	ImSlateContext& g = *GImSlate;
	if (!IsCurrentWindowEnded(g))
	{
		g.NextItemData.Flags |= ImSlateNextItemDataFlags_NewRow | ImSlateNextItemDataFlags_BreakLine | ImSlateNextItemDataFlags_MinHeight;
		g.NextItemData.bNewRow = true;
		g.NextItemData.bBreakLine = true;
		g.NextItemData.SetMinHeight(Val);
		auto& Slot = Internal::AddWidgetImpl(g);
		if (Slot.ApplyNextItem(g.NextItemData))
			g.CurrentWindow->MarkPanelLayoutDirty();
		g.NextItemData.ClearFlags();
		g.NextItemData.Flags |= ImSlateNextItemDataFlags_NewRow;
		g.NextItemData.bNewRow = true;
	}
}

void Dummy(const ImVec2& Size)
{
	ImSlateContext& g = *GImSlate;
	if (!IsCurrentWindowEnded(g))
	{
		g.NextItemData.Flags |= ImSlateNextItemDataFlags_MinWidth | ImSlateNextItemDataFlags_MinHeight;
		g.NextItemData.SetMinWidth(Size.X);
		g.NextItemData.SetMinHeight(Size.Y);
		auto& Slot = Internal::AddWidgetImpl(g);
		g.NextItemData.Flags |= ImSlateNextItemDataFlags_NewRow;
		g.NextItemData.bNewRow = true;
	}
}

struct FFoldMeta
	: public TSharedFromThis<FFoldMeta>
	, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FFoldMeta, ISlateMetaData)
public:
	bool bIsFolded = true;
	bool bJustClicked = false;
	TSharedPtr<STextBlock> TextBlock;
};

bool FoldLine(ImStr Label, const FText& InText, float InHeight /*= 0.f*/)
{
	using Internal::Item;
	auto ItemPtr = Item<SButton>(Label, [&](FItemSlotPod& InItem) {
		InItem.bNewRow = true;
		InItem.bBreakLine = true;
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;
		if (InHeight > 0.f)
			InItem.SetMinHeight(InHeight);

		// Create an SImButton directly (NOT ImFactoryCreate<UImButton>, which builds a plain SButton —
		// casting that to SImButton and writing SImButton members corrupts memory). Pass null owner:
		// the fold header doesn't need a backing UObject.
		// Use UImButton's default style so the header looks the same as it did before this change.
		TSharedRef<SImButton> WidgetRef = SNew(SImButton, nullptr)
			.ButtonStyle(&GetDefault<UImButton>()->GetStyle());
		// Install the fold-header drag behaviour (all scroll/move-window knowledge lives HERE, not in the
		// button — the button only forwards drags to this behaviour). Gated by imslate.DragScrollContent.
		//   - ShouldBubbleOnPress: content CAN'T scroll → don't capture on press; let it bubble to the window
		//     so the window does DetectDrag(self) → move (host switch), exactly like the titlebar.
		//   - OnDragMove: content CAN scroll → pan it (Handled = button keeps capture & keeps forwarding);
		//     otherwise decline (Unhandled) so the bubbled press drives the window move.
		//   - OnDragEnd: end the content pan.
		if (auto* Win = GImSlate->CurrentWindow)
		{
			TWeakPtr<SImSlateWindow> WeakWin = StaticCastSharedRef<SImSlateWindow>(Win->AsShared());
			FImMousePressBehavior Behavior;
			Behavior.ShouldBubbleOnPress = [WeakWin]() {
				auto W = WeakWin.Pin();
				return W.IsValid() && !W->CanScrollContent();  // can't scroll → bubble (window move)
			};
			Behavior.OnDragMove = [WeakWin](FVector2D Press, FVector2D Cur) -> FReply {
				auto W = WeakWin.Pin();
				if (!W || !GImSlateDragScrollContent)
					return FReply::Unhandled();
				if (W->CanScrollContent())
				{
					W->PanContentMove(Press, Cur);
					return FReply::Handled();  // scrolling — button keeps capture & keeps forwarding
				}
				return FReply::Unhandled();  // can't scroll → the bubbled press drives the window move
			};
			Behavior.OnDragEnd = [WeakWin](FVector2D Cur) {
				if (GImSlateDragScrollContent)
					if (auto W = WeakWin.Pin())
						W->PanContentEnd(Cur);
			};
			WidgetRef->SetMousePressBehavior(MoveTemp(Behavior));
		}
		auto Meta = MakeShared<FFoldMeta>();
		WidgetRef->AddMetadata(Meta);
		auto Ptr = &Meta.Get();

		WidgetRef->SetContent(SAssignNew(Meta->TextBlock, STextBlock)
			.Text(FText::FromString(TEXT("\x25B6 ") + InText.ToString()))
			.Font(GetImSlateDefaultFont(12))
			.Clipping(EWidgetClipping::ClipToBoundsAlways));

		WidgetRef->SetOnClicked(CreateWeakLambda(Ptr, [Ptr] {
			Ptr->bJustClicked = true;
			return FReply::Handled();
		}));

		return WidgetRef;
	});

	if (ItemPtr)
	{
		if (auto Meta = ItemPtr ? ItemPtr->GetMetaData<FFoldMeta>() : TSharedPtr<FFoldMeta>())
		{
			if (Meta->bJustClicked)
			{
				Meta->bJustClicked = false;
				Meta->bIsFolded = !Meta->bIsFolded;
			}
			// Update arrow text
			if (Meta->TextBlock.IsValid())
			{
				FString Arrow = Meta->bIsFolded ? TEXT("\x25B6 ") : TEXT("\x25BC ");
				Meta->TextBlock->SetText(FText::FromString(Arrow + InText.ToString()));
			}
			return Meta->bIsFolded;
		}
	}
	return true;  // default folded
}

void Separator()
{
}

// Fold indent stack — per-window, reset each Begin/End cycle
// Safe: ImSlate renders single-threaded, BeginFold/EndFold always paired within Begin/End
static TArray<float, TInlineAllocator<8>>& GetFoldIndentStack()
{
	static TArray<float, TInlineAllocator<8>> Stack;
	return Stack;
}

bool BeginFold(ImStr Label, const FText& InText, float IndentWidth)
{
	ImSlateContext& g = *GImSlate;
	ImSlateId FoldId = ImSlateScopedId(Label);
	bool bIsFolded = FoldLine(Label, InText);

	// FoldLine item is already in the tree (parent set by Item → SetItemParent)
	// Now push this fold as the parent context for content items
	if (!bIsFolded)
	{
		// Fold open — restore visibility of collapsed children
		g.CurrentWindow->SetFoldCollapsed(FoldId, false);
		g.CurrentWindow->PushFoldContext(FoldId);
		GetFoldIndentStack().Push(IndentWidth);
		Indent(IndentWidth);
		return true;
	}
	else
	{
		// Fold closed — mark collapsed, content items not created (short circuit)
		g.CurrentWindow->SetFoldCollapsed(FoldId, true);
		return false;
	}
}

void EndFold()
{
	ImSlateContext& g = *GImSlate;
	auto& Stack = GetFoldIndentStack();
	if (Stack.Num() > 0)
	{
		if (g.CurrentWindow)
			g.CurrentWindow->PopFoldContext();
		Unindent(Stack.Pop());
	}
}

// Group scope stack — per render pass, BeginGroup/EndGroup always paired within Begin/End.
// Safe: ImSlate renders single-threaded (mirrors GetFoldIndentStack).
struct FGroupMeta { uint32 Id; FLinearColor Color; };
static TArray<FGroupMeta, TInlineAllocator<8>>& GetGroupStack()
{
	static TArray<FGroupMeta, TInlineAllocator<8>> Stack;
	return Stack;
}

bool BeginGroup(ImStr Id, const FLinearColor& GroupColor)
{
	ImSlateContext& g = *GImSlate;
	ImSlateId GroupId = ImSlateScopedId(Id);
	GetGroupStack().Push({GroupId, GroupColor});
	g.CurrentGroupId = GroupId;
	g.CurrentGroupColor = GroupColor;
	return true;
}

void EndGroup()
{
	ImSlateContext& g = *GImSlate;
	auto& Stack = GetGroupStack();
	if (Stack.Num() > 0)
	{
		Stack.Pop();
		// Restore the enclosing group context (supports nesting), or clear if none.
		if (Stack.Num() > 0)
		{
			g.CurrentGroupId = Stack.Last().Id;
			g.CurrentGroupColor = Stack.Last().Color;
		}
		else
		{
			g.CurrentGroupId = 0;
			g.CurrentGroupColor = FLinearColor::Transparent;
		}
	}
}

bool TitleLine(ImStr Label, const FText& InText, bool* bOpen)
{
	bool bCloseClicked = false;

	// Title text — fill width
	float Scale = GetImSlateEffectiveScale();
	float BarHeight = 12.f * Scale * 2.5f;

	SetNextItemFillWidth(1.f);
	TextButton(Label, InText, ImVec2(0, BarHeight));

	// Close button
	if (bOpen)
	{
		SameLine();
		FString CloseId = FString::Printf(TEXT("X##close_%.*hs"), Label.Len(), Label.GetData());
		if (Button(FStringView(CloseId), ImVec2(BarHeight, BarHeight)))
		{
			*bOpen = false;
			bCloseClicked = true;
		}
	}

	return bCloseClicked;
}

void Indent(float IndentW /* = 0.0f*/)
{
	ImSlateContext& g = *GImSlate;
	g.CurrentIndent += IndentW;
}

void Unindent(float IndentW /* = 0.0f*/)
{
	ImSlateContext& g = *GImSlate;
	g.CurrentIndent -= IndentW;
	g.CurrentIndent = FMath::Max(g.CurrentIndent, 0.f);
}

static void SetItemCollapsed(FItemSlotPod& Data, float InWidth, ImSlateCond Cond)
{
}
static void SetItemOpen(FItemSlotPod& Data, bool bOpen, ImSlateCond Cond)
{
}
static void SetItemHAlignment(FItemSlotPod& Data, uint8 HAlign, ImSlateCond Cond)
{
}
static void SetItemVAlignment(FItemSlotPod& Data, uint8 VAlign, ImSlateCond Cond)
{
}
static void SetItemStretchToCol(FItemSlotPod& Data, int32 InCol, ImSlateCond Cond)
{
	Data.bAlignCol = true;
	Data.StretchToCol = InCol;
}
static void SetItemMaxHeight(FItemSlotPod& Data, float Val, ImSlateCond Cond)
{
}
static void SetItemMaxWidth(FItemSlotPod& Data, float Val, ImSlateCond Cond)
{
}

void SetNextItemMinWidth(float InVal)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_MinWidth;
	g.NextItemData.SetMinWidth(InVal);
}

void SetNextItemMinHeight(float InVal)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_MinHeight;
	g.NextItemData.SetMinHeight(InVal);
}
void SetNextItemMinSize(float InWidth, float InHeight)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_MinHeight | ImSlateNextItemDataFlags_MinWidth;
	g.NextItemData.SetMinWidth(InWidth);
	g.NextItemData.SetMinHeight(InHeight);
}

void SetNextItemMaxWidth(float InVal)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_MaxWidth;
	g.NextItemData.SetMaxWidth(InVal);
}

void SetNextItemMaxHeight(float InVal)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_MaxWidth;
	g.NextItemData.SetMaxHeight(InVal);
}
void SetNextItemMaxSize(float InWidth, float InHeight)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_MaxHeight | ImSlateNextItemDataFlags_MaxWidth;
	g.NextItemData.SetMaxWidth(InWidth);
	g.NextItemData.SetMaxHeight(InHeight);
}

void SetNextItemFixSize(float InWidth, float InHeight)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_MaxHeight | ImSlateNextItemDataFlags_MaxWidth | ImSlateNextItemDataFlags_MinHeight | ImSlateNextItemDataFlags_MinWidth;
	g.NextItemData.SetMaxWidth(InWidth).SetMinWidth(InWidth);
	g.NextItemData.SetMaxHeight(InHeight).SetMinHeight(InHeight);
}

void SetNextItemAspectRatio(float InRatio)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_AspectRatio;
	g.NextItemData.SetAspectRatio(InRatio);
}

void SetNextItemTooltip(const FText& InText)
{
	GImSlate->NextItemTooltip = InText;
}

void SetNextItemFillWidth(float InFactor)
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_FillWidth;
	g.NextItemData.bFillWidth = true;
	g.NextItemData.StretchValue = InFactor;
}

// Force the next item to size to its content (no horizontal fill). Sets the FillWidth flag with
// bFillWidth=false so ApplyNextItem overrides any control that hard-codes bFillWidth=true (e.g. CheckBox).
void SetNextItemAutoWidth()
{
	ImSlateContext& g = *GImSlate;
	g.NextItemData.Flags |= ImSlateNextItemDataFlags_FillWidth;
	g.NextItemData.bFillWidth = false;
	g.NextItemData.StretchValue = 0.f;
}

extern bool GForceVirtualKeyboard;

void SetVirtualKeyboardEnabled(bool bEnabled)
{
	GForceVirtualKeyboard = bEnabled;
}

bool IsVirtualKeyboardVisible()
{
	if (auto Kb = SImSlateVirtualKeyboard::Get())
		return Kb->IsShowing();
	return false;
}

}  // namespace ImSlate

#include "ImSlateControls.inl"
