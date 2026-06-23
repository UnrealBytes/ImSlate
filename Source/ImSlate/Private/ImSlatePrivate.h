// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "ImSlate.h"
#include "ImSlate3D.h"   // ImSlate::FImWorldPlacement (SetNextWindow3DTransform) — gated by IMSLATE3D_API
#include "PrivateFieldAccessor.h"

namespace ImSlate
{
// clang-format off
// Flags stored in ImSlateViewport::Flags, giving indications to the platform backends.
enum ImSlateViewportFlags_
{
	ImSlateViewportFlags_None					= 0,
	ImSlateViewportFlags_IsPlatformWindow		= 1 << 0,	// Represent a Platform Window
	ImSlateViewportFlags_IsPlatformMonitor		= 1 << 1,	// Represent a Platform Monitor (unused yet)
	ImSlateViewportFlags_OwnedByApp				= 1 << 2,	// Platform Window: is created/managed by the application (rather than a dear ImSlate backend)
	ImSlateViewportFlags_NoDecoration			= 1 << 3,	// Platform Window: Disable platform decorations: title bar, borders, etc. (generally set all windows, but if ImSlateConfigFlags_ViewportsDecoration is set we only set this on popups/tooltips)
	ImSlateViewportFlags_NoTaskBarIcon			= 1 << 4,	// Platform Window: Disable platform task bar icon (generally set on popups/tooltips, or all windows if ImSlateConfigFlags_ViewportsNoTaskBarIcon is set)
	ImSlateViewportFlags_NoFocusOnAppearing		= 1 << 5,	// Platform Window: Don't take focus when created.
	ImSlateViewportFlags_NoFocusOnClick			= 1 << 6,	// Platform Window: Don't take focus when clicked on.
	ImSlateViewportFlags_NoInputs				= 1 << 7,	// Platform Window: Make mouse pass through so we can drag this window while peaking behind it.
	ImSlateViewportFlags_NoRendererClear		= 1 << 8,	// Platform Window: Renderer doesn't need to clear the framebuffer ahead (because we will fill it entirely).
	ImSlateViewportFlags_TopMost				= 1 << 9,	// Platform Window: Display on top (for tooltips only).
	ImSlateViewportFlags_Minimized				= 1 << 10,	// Platform Window: Window is minimized, can skip render. When minimized we tend to avoid using the viewport pos/size for clipping window or testing if they are contained in the viewport.
	ImSlateViewportFlags_NoAutoMerge			= 1 << 11,	// Platform Window: Avoid merging this window into another host window.
	ImSlateViewportFlags_CanHostOtherWindows	= 1 << 12,	// Main viewport: can host multiple ImSlate windows (secondary viewports are associated to a single window).
};

// Flags for ImSlate::IsItemHovered(), ImSlate::IsWindowHovered()
// Note: if you are trying to check whether your mouse should be dispatched to Dear ImSlate or to your app, you should use 'io.WantCaptureMouse' instead! Please read the FAQ!
// Note: windows with the ImSlateWindowFlags_NoInputs flag are ignored by IsWindowHovered() calls.
enum ImSlateHoveredFlags_
{
	ImSlateHoveredFlags_None							= 0,		// Return true if directly over the item/window, not obstructed by another window, not obstructed by an active popup or modal blocking inputs under them.
	ImSlateHoveredFlags_ChildWindows					= 1 << 0,	// IsWindowHovered() only: Return true if any children of the window is hovered
	ImSlateHoveredFlags_RootWindow						= 1 << 1,	// IsWindowHovered() only: Test from root window (top most parent of the current hierarchy)
	ImSlateHoveredFlags_AnyWindow						= 1 << 2,	// IsWindowHovered() only: Return true if any window is hovered
	ImSlateHoveredFlags_NoPopupHierarchy				= 1 << 3,	// IsWindowHovered() only: Do not consider popup hierarchy (do not treat popup emitter as parent of popup) (when used with _ChildWindows or _RootWindow)
	ImSlateHoveredFlags_DockHierarchy					= 1 << 4,	// IsWindowHovered() only: Consider docking hierarchy (treat dockspace host as parent of docked window) (when used with _ChildWindows or _RootWindow)
	ImSlateHoveredFlags_AllowWhenBlockedByPopup			= 1 << 5,	// Return true even if a popup window is normally blocking access to this item/window
	//ImSlateHoveredFlags_AllowWhenBlockedByModal		= 1 << 6,	// Return true even if a modal popup window is normally blocking access to this item/window. FIXME-TODO: Unavailable yet.
	ImSlateHoveredFlags_AllowWhenBlockedByActiveItem	= 1 << 7,	// Return true even if an active item is blocking access to this item/window. Useful for Drag and Drop patterns.
	ImSlateHoveredFlags_AllowWhenOverlapped				= 1 << 8,	// IsItemHovered() only: Return true even if the position is obstructed or overlapped by another window
	ImSlateHoveredFlags_AllowWhenDisabled				= 1 << 9,	// IsItemHovered() only: Return true even if the item is disabled
	ImSlateHoveredFlags_RectOnly						= ImSlateHoveredFlags_AllowWhenBlockedByPopup | ImSlateHoveredFlags_AllowWhenBlockedByActiveItem | ImSlateHoveredFlags_AllowWhenOverlapped,
	ImSlateHoveredFlags_RootAndChildWindows				= ImSlateHoveredFlags_RootWindow | ImSlateHoveredFlags_ChildWindows,
};

// Storage for current popup stack
struct ImSlatePopupData
{
    ImSlateId             PopupId;        // Set on OpenPopup()
    ImSlateWindow*        Window;         // Resolved on BeginPopup() - may stay unresolved if user never calls OpenPopup()
    ImSlateWindow*        SourceWindow;   // Set on OpenPopup() copy of NavWindow at the time of opening the popup
    int                 OpenFrameCount; // Set on OpenPopup()
    ImSlateId             OpenParentId;   // Set on OpenPopup(), we need this to differentiate multiple menu sets from each others (e.g. inside menu bar vs loose menu items)
    ImVec2              OpenPopupPos;   // Set on OpenPopup(), preferred popup position (typically == OpenMousePos when using mouse)
    ImVec2              OpenMousePos;   // Set on OpenPopup(), copy of mouse position at the time of opening popup

    ImSlatePopupData()    { memset(this, 0, sizeof(*this)); OpenFrameCount = -1; }
};

enum ImSlateNextWindowDataFlags_
{
	ImSlateNextWindowDataFlags_None					= 0,
	ImSlateNextWindowDataFlags_HasPos				= 1 << 0,
	ImSlateNextWindowDataFlags_HasSize				= 1 << 1,
	ImSlateNextWindowDataFlags_HasContentSize		= 1 << 2,
	ImSlateNextWindowDataFlags_HasResizeCallback		= 1 << 3,
	ImSlateNextWindowDataFlags_HasCollapsed			= 1 << 4,
	ImSlateNextWindowDataFlags_HasFocus				= 1 << 5,
	ImSlateNextWindowDataFlags_HasBgAlpha			= 1 << 6,
	ImSlateNextWindowDataFlags_HasScroll			= 1 << 7,
	ImSlateNextWindowDataFlags_HasViewport			= 1 << 8,
	ImSlateNextWindowDataFlags_HasDock				= 1 << 9,
	ImSlateNextWindowDataFlags_HasCloseFunc			= 1 << 10,
	ImSlateNextWindowDataFlags_HasTitle				= 1 << 11,
	ImSlateNextWindowDataFlags_HasTopmost			= 1 << 12,
	ImSlateNextWindowDataFlags_HasBgColor			= 1 << 13,
#if defined(IMSLATE3D_API)
	ImSlateNextWindowDataFlags_Has3DTransform		= 1 << 14,  // SetNextWindow3DTransform: project the window onto a 3D world plane
#endif
};

struct ImSlateNextWindowData
{
	ImSlateNextWindowDataFlags		Flags			= 0;
	ImSlateCond						PosCond			= 0;
	ImSlateCond						SizeCond		= 0;
	ImSlateCond						ContentSizeCond	= 0;
	ImSlateCond						CollapsedCond	= 0;
	ImSlateCond						BgAlphaCond		= 0;
	ImSlateCond						DockCond		= 0;
	ImSlateCond						TitleCond		= 0;
	ImSlateCond						TopmostCond		= 0;
	ImSlateCond						ViewportCond	= 0;

	ImSlateId						ViewportId		= 0;
	ImSlateId						DockId			= 0;

	ImVec2							PosVal;
	ImVec2							PosPivotVal;

	ImVec2							SizeVal;
	ImVec2							ContentSizeVal;
	ImVec2							ScrollVal;
	bool							PosUndock		= false;
	bool							CollapsedVal	= false;
	bool							TopmostVal		= false;
	float							BgAlphaVal		= 0.f;
	FLinearColor					BgColorVal		= FLinearColor::Black;
	ImSlateCond						BgColorCond		= 0;

	
	ImVec2							MenuBarOffsetMinVal;

	ImTransform						LocalToWorld;
	FText							TitleVal;

	ImSlateResizeCallback			ResizeCallback;

#if defined(IMSLATE3D_API)
	ImSlate::FImWorldPlacement		Placement3DVal;  // consumed by Begin() when _Has3DTransform is set
#endif

	ImSlateNextWindowData()			= default;
	inline void ClearFlags()		{ Flags = 0; }
};

enum ImSlateItemFlags_
{
	ImSlateItemFlags_None						= 0,
	ImSlateItemFlags_NoTabStop					= 1 << 0,	// false	// Disable keyboard tabbing (FIXME: should merge with _NoNav)
	ImSlateItemFlags_ButtonRepeat				= 1 << 1,	// false	// Button() will return true multiple times based on io.KeyRepeatDelay and io.KeyRepeatRate settings.
	ImSlateItemFlags_Disabled					= 1 << 2,	// false	// Disable interactions but doesn't affect visuals. See BeginDisabled()/EndDisabled(). See github.com/ocornut/imgui/issues/211
	ImSlateItemFlags_NoNav						= 1 << 3,	// false	// Disable keyboard/gamepad directional navigation (FIXME: should merge with _NoTabStop)
	ImSlateItemFlags_NoNavDefaultFocus			= 1 << 4,	// false	// Disable item being a candidate for default focus (e.g. used by title bar items)
	ImSlateItemFlags_SelectableDontClosePopup	= 1 << 5,	// false	// Disable MenuItem/Selectable() automatically closing their popup window
	ImSlateItemFlags_MixedValue					= 1 << 6,	// false	// [BETA] Represent a mixed/indeterminate value, generally multi-selection where values differ. Currently only supported by Checkbox() (later should support all sorts of widgets)
	ImSlateItemFlags_ReadOnly					= 1 << 7,	// false	// [ALPHA] Allow hovering interactions but underlying value is not changed.
	ImSlateItemFlags_Inputable					= 1 << 8	// false	// [WIP] Auto-activate input mode when tab focused. Currently only used and supported by a few items before it becomes a generic feature.
};

enum ImSlateNextItemDataFlags_
{
	ImSlateNextItemDataFlags_None			= 0,
	ImSlateNextItemDataFlags_Padding		= 1 << 0,
	ImSlateNextItemDataFlags_MaxWidth		= 1 << 1,
	ImSlateNextItemDataFlags_MaxHeight		= 1 << 2,
	ImSlateNextItemDataFlags_NewRow			= 1 << 3,
	ImSlateNextItemDataFlags_FillWidth		= 1 << 4,
	ImSlateNextItemDataFlags_AlignCol		= 1 << 5,
	ImSlateNextItemDataFlags_BreakLine		= 1 << 6,
	ImSlateNextItemDataFlags_Collapsed		= 1 << 7,
	ImSlateNextItemDataFlags_HAlign			= 1 << 8,
	ImSlateNextItemDataFlags_VAlign			= 1 << 9,
	ImSlateNextItemDataFlags_Parent			= 1 << 10,
	ImSlateNextItemDataFlags_Stretch		= 1 << 11,
	ImSlateNextItemDataFlags_MinWidth		= 1 << 12,
	ImSlateNextItemDataFlags_MinHeight		= 1 << 13,
	ImSlateNextItemDataFlags_AspectRatio	= 1 << 14,
	ImSlateNextItemDataFlags_Group			= 1 << 15,
};

struct ImSlateNextItemData : public FItemSlotPod
{
	ImSlateNextItemDataFlags		Flags;
	
	ImSlateNextItemData()			{ memset(this, 0, sizeof(*this)); }
	inline void ClearFlags()	{ Flags = ImSlateNextItemDataFlags_None; }
};

// Status storage for the last submitted item
struct ImSlateLastItemData
{
	ImSlateId					ID;
	ImSlateItemFlags			InFlags;				// See ImSlateItemFlags_
	ImSlateItemStatusFlags		StatusFlags;			// See ImSlateItemStatusFlags_
	ImRect						Rect;					// Full rectangle
	ImRect						NavRect;				// Navigation scoring rectangle (not displayed)
	ImRect						DisplayRect;			// Display rectangle (only if ImSlateItemStatusFlags_HasDisplayRect is set)

	ImSlateLastItemData()		{ memset(this, 0, sizeof(*this)); }
};

struct IMSLATE_API ImSlateStackSizes
{
	int16		SizeOfIDStack;
	int16		SizeOfColorStack;
	int16		SizeOfStyleVarStack;
	int16		SizeOfFontStack;
	int16		SizeOfFocusScopeStack;
	int16		SizeOfGroupStack;
	int16		SizeOfItemFlagsStack;
	int16		SizeOfBeginPopupStack;
	int16		SizeOfDisabledStack;

	ImSlateStackSizes() { memset(this, 0, sizeof(*this)); }
	void SetToCurrentState();
	void CompareWithCurrentState();
};

// Data saved for each window pushed into the stack
struct ImSlateWindowStackData
{
	SImSlateWindow*					Window;
	ImSlateLastItemData				ParentLastItemDataBackup;
	ImSlateStackSizes				StackSizesOnBegin;			// Store size of various stacks for asserting
};


struct ImSlateStorage
{
    struct ImSlateStoragePair
    {
        ImSlateId key;
        union { int64 val_i; int64 val_l; float val_f; float val_d; void* val_p; };
        ImSlateStoragePair(ImSlateId _key, int32 _val_l)    { key = _key; val_l = _val_l; }
        ImSlateStoragePair(ImSlateId _key, int64 _val_i)    { key = _key; val_i = _val_i; }
        ImSlateStoragePair(ImSlateId _key, float _val_f)    { key = _key; val_f = _val_f; }
        ImSlateStoragePair(ImSlateId _key, double _val_d)   { key = _key; val_d = _val_d; }
        ImSlateStoragePair(ImSlateId _key, void* _val_p)    { key = _key; val_p = _val_p; }
    };

    TArray<ImSlateStoragePair>      Data;

    // - Get***() functions find pair, never add/allocate. Pairs are sorted so a query is O(log N)
    // - Set***() functions find pair, insertion on demand if missing.
    // - Sorted insertion is costly, paid once. A typical frame shouldn't need to insert any new pair.
    void                  Clear() { Data.Empty(); }
    IMSLATE_API int64     GetInt(ImSlateId key, int64 default_val = 0) const;
    IMSLATE_API void      SetInt(ImSlateId key, int64 val);
    IMSLATE_API bool      GetBool(ImSlateId key, bool default_val = false) const;
    IMSLATE_API void      SetBool(ImSlateId key, bool val);
    IMSLATE_API float     GetFloat(ImSlateId key, float default_val = 0.0f) const;
    IMSLATE_API void      SetFloat(ImSlateId key, float val);
    IMSLATE_API void*     GetVoidPtr(ImSlateId key) const; // default_val is NULL
    IMSLATE_API void      SetVoidPtr(ImSlateId key, void* val);

    // - Get***Ref() functions finds pair, insert on demand if missing, return pointer. Useful if you intend to do Get+Set.
    // - References are only valid until a new value is added to the storage. Calling a Set***() function or a Get***Ref() function invalidates the pointer.
    // - A typical use case where this is convenient for quick hacking (e.g. add storage during a live Edit&Continue session if you can't modify existing struct)
    //      float* pvar = ImSlate::GetFloatRef(key); ImSlate::SliderFloat("var", pvar, 0, 100.0f); some_var += *pvar;
    IMSLATE_API int64*    GetIntRef(ImSlateId key, int64 default_val = 0);
    IMSLATE_API bool*     GetBoolRef(ImSlateId key, bool default_val = false);
    IMSLATE_API float*    GetFloatRef(ImSlateId key, float default_val = 0.0f);
    IMSLATE_API void**    GetVoidPtrRef(ImSlateId key, void* default_val = NULL);

    // Use on your own storage if you know only integer are being stored (open/close all tree nodes)
    IMSLATE_API void      SetAllInt(int val);

    // For quicker full rebuild of a storage (instead of an incremental one), you may add all your contents and then sort once.
    IMSLATE_API void      BuildSortByKey();
};

// clang-format on

struct ImSlateContext;
struct FImSlateGCRoot final : public FGCObject
{
	FImSlateGCRoot(ImSlateContext& InContext)
		: ImContext(InContext)
	{
	}

	void AddReferencedObject(const UObject* InObj);
	void AddWindowedReferencedObject(SImSlateWindow* InWindow, const UObject* InObj);

protected:
	ImSlateContext& ImContext;
	virtual FString GetReferencerName() const override { return "FImSlateGCRoot"; }
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
};

struct ImSlateContext
{
	UWorld*														RawWorldPtr = nullptr;
	TWeakObjectPtr<UWorld>										CurrentWorld;
	ImSlateIdStack												IDStack;

	TMap<uint32, TSharedRef<SImSlateWindow>>					WindowsById;
	TMap<uint32, TSharedRef<SImSlateWindow>>					LastWindowsById;
	TArray<SImSlateWindow*,TInlineAllocator<8>>					WindowStack;

	TMap<TSharedRef<SImSlateViewport>, TSharedPtr<FViewportPopupHolder>>	HostById;
	TMap<TSharedRef<SImSlateViewport>, TSharedPtr<FViewportPopupHolder>>	LastHostById;
	TArray<TSharedRef<SImSlateViewport>, TInlineAllocator<4>>				Viewports;

	SImSlateWindow*												CurrentWindow = nullptr;
	SImSlateViewport*											CurrentViewport = nullptr;

	double														Time = 0.f;
	int32														FrameCount = 0;
	int32														FrameCountEnded = 0;
	int32														FrameCountPlatformEnded = 0;
	int32														FrameCountRendered = 0;
	
	bool														WithinFrameScope = false;						// Set by NewFrame(), cleared by EndFrame()
	bool														WithinFrameScopeWithImplicitWindow = false;		// Set by NewFrame(), cleared by EndFrame() when the implicit debug window has been pushed
	bool														WithinEndChild = false;							// Set within EndChild()

	TArray<ImSlateWindowStackData>								CurrentWindowStack;

	TArray<ImSlatePopupData>									OpenPopupStack;								// Which popups are open (persistent)
	TArray<ImSlatePopupData>									BeginPopupStack;							// Which level of BeginPopup() we are in (reset every frame)

	// Next window/item data
	ImSlateNextWindowData										NextWindowData;								// Storage for SetNextWindow** functions
	ImSlateNextItemData											NextItemData;								// Storage for SetNextItem** functions
	ImSlateLastItemData											LastItemData;								// Storage for last submitted item (setup by ItemAdd)
	ImSlateItemFlags											CurrentItemFlags = 0;						// == g.ItemFlagsStack.back()

	FText														NextItemTooltip;

	struct FLongPressTooltip
	{
		FText Text;
		FVector2D AbsolutePosition = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		bool bVisible = false;
	}															LongPressTooltip;

	float														CurrentIndent = 0.f;
	TArray<float, TInlineAllocator<4>>							IndentStack;

	// BeginGroup/EndGroup: current group context applied to subsequent items (0 = no group)
	uint32														CurrentGroupId = 0;
	FLinearColor												CurrentGroupColor = FLinearColor::Transparent;

	bool														bIsFrameStarted = false;
	bool														bIsFrameEnded = true;
	int32														PIEInstanceID = -1;

	
	TArray<TObjectPtr<const UObject>>							ReferencedObjects;
	TUniquePtr<FImSlateGCRoot>									GCRoot;
	IMSLATE_API TUniquePtr<FImSlateGCRoot>&						GetGCRoot();

#if WITH_EDITOR
	UWorld* GetWorldChecked() const	{ checkSlow(CurrentWorld.IsValid() && RawWorldPtr->IsValidLowLevel()); return CurrentWorld.Get(); }
#else
	UWorld* GetWorldChecked() const	{ checkSlow(RawWorldPtr->IsValidLowLevel()); return RawWorldPtr; }
#endif
};
}  // namespace ImSlate
