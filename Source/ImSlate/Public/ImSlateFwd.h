// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "SlateCore.h"

#include "GMPCore.h"
#include "Layout/SlateRect.h"

#if defined(UE_BUILD_DEBUGGAME)
DECLARE_LOG_CATEGORY_EXTERN(LogImSlate, Log, All);
#else
DECLARE_LOG_CATEGORY_EXTERN(LogImSlate, Log, All);
#endif

class UWidget;
using ImWindowFlags = int32;  // ImSlateWindowFlags_
using ImSlateId = uint32;

// clang-format off
// Enumeration for ImSlate::SetWindow***(), SetNextWindow***(), SetNextItem***() functions
// Represent a condition.
// Important: Treat as a regular enum! Do NOT combine multiple values using binary operators! All the functions above treat 0 as a shortcut to ImSlateCond_Always.
enum ImSlateCond_
{
	ImSlateCond_None				= 0,		// No condition (always set the variable), same as _Always
	ImSlateCond_Always				= 1 << 0,	// No condition (always set the variable)
	ImSlateCond_Once				= 1 << 1,	// Set the variable once per runtime session (only the first call will succeed)
	ImSlateCond_Appearing			= 1 << 2,	// Set the variable if the object/window is appearing after being hidden/inactive (or the first time)
	ImSlateCond_FirstUseEver		= 1 << 3,	// Set the variable if the object/window has no persistently saved data (no entry in .ini file)

	ImSlateCond_FirstClearMask = (ImSlateCond_Once | ImSlateCond_Appearing),
};

// Flags for ImSlate::Begin()
enum ImSlateWindowFlags_
{
	ImSlateWindowFlags_None							= 0,
	ImSlateWindowFlags_NoTitleBar					= 1 << 0,	// Disable title-bar
	ImSlateWindowFlags_NoResize						= 1 << 1,	// Disable user resizing with the lower-right grip
	ImSlateWindowFlags_NoMove						= 1 << 2,	// Disable user moving the window
	ImSlateWindowFlags_NoScrollbar					= 1 << 3,	// Disable scrollbars (window can still scroll with mouse or programmatically)
	ImSlateWindowFlags_NoScrollWithMouse			= 1 << 4,	// Disable user vertically scrolling with mouse wheel. On child window, mouse wheel will be forwarded to the parent unless NoScrollbar is also set.
	ImSlateWindowFlags_NoCollapse					= 1 << 5,	// Disable user collapsing window by double-clicking on it. Also referred to as Window Menu Button (e.g. within a docking node).
	ImSlateWindowFlags_AlwaysAutoResize				= 1 << 6,	// Resize every window to its content every frame
	ImSlateWindowFlags_NoBackground					= 1 << 7,	// Disable drawing background color (WindowBg, etc.) and outside border. Similar as using SetNextWindowBgAlpha(0.0f).
	ImSlateWindowFlags_NoSavedSettings				= 1 << 8,	// Never load/save settings in .ini file
	ImSlateWindowFlags_NoMouseInputs				= 1 << 9,	// Disable catching mouse, hovering test with pass through.
	// ImSlateWindowFlags_MenuBar						= 1 << 10,	// Has a menu-bar
	// ImSlateWindowFlags_HorizontalScrollbar			= 1 << 11,	// Allow horizontal scrollbar to appear (off by default). You may use SetNextWindowContentSize(ImVec2(width,0.0f)); prior to calling Begin() to specify width. Read code in imgui_demo in the "Horizontal Scrolling" section.
	ImSlateWindowFlags_NoFocusOnAppearing			= 1 << 12,	// Disable taking focus when transitioning from hidden to visible state
	ImSlateWindowFlags_NoBringToFront		= 1 << 13,			// Disable bringing window to front when taking focus (e.g. clicking on it or programmatically giving it focus)
	ImSlateWindowFlags_AlwaysVerticalScrollbar		= 1 << 14,	// Always show vertical scrollbar (even if ContentSize.y < Size.y)
	ImSlateWindowFlags_AlwaysHorizontalScrollbar	= 1 << 15,	// Always show horizontal scrollbar (even if ContentSize.x < Size.x)
	ImSlateWindowFlags_AlwaysUseWindowPadding		= 1 << 16,	// Ensure child windows without border uses style.WindowPadding (ignored by default for non-bordered child windows, because more convenient)
	ImSlateWindowFlags_NoNavInputs					= 1 << 18,	// No gamepad/keyboard navigation within the window
	ImSlateWindowFlags_NoNavFocus					= 1 << 19,	// No focusing toward this window with gamepad/keyboard navigation (e.g. skipped by CTRL+TAB)
	ImSlateWindowFlags_UnsavedDocument				= 1 << 20,	// Display a dot next to the title. When used in a tab/docking context, tab is selected when clicking the X + closure is not assumed (will wait for user to stop submitting the tab). Otherwise closure is assumed when pressing the X, so if you keep submitting the tab may reappear at end of tab bar.
	ImSlateWindowFlags_NoDocking					= 1 << 21,	// Disable docking of this window
	ImSlateWindowFlags_ShowCloseButton				= 1 << 22,	// Show close button of this window

	ImSlateWindowFlags_NoNav						= ImSlateWindowFlags_NoNavInputs | ImSlateWindowFlags_NoNavFocus,
	ImSlateWindowFlags_NoDecoration					= ImSlateWindowFlags_NoTitleBar | ImSlateWindowFlags_NoResize | ImSlateWindowFlags_NoScrollbar | ImSlateWindowFlags_NoCollapse,
	ImSlateWindowFlags_NoInputs						= ImSlateWindowFlags_NoMouseInputs | ImSlateWindowFlags_NoNavInputs | ImSlateWindowFlags_NoNavFocus,

	// [Internal]
	ImSlateWindowFlags_NavFlattened					= 1 << 23,	// [BETA] On child window: allow gamepad/keyboard navigation to cross over parent border to this child or between sibling child windows.
	ImSlateWindowFlags_ChildWindow					= 1 << 24,	// Don't use! For internal use by BeginChild()
	ImSlateWindowFlags_Tooltip						= 1 << 25,	// Don't use! For internal use by BeginTooltip()
	ImSlateWindowFlags_Popup						= 1 << 26,	// Don't use! For internal use by BeginPopup()
	ImSlateWindowFlags_Modal						= 1 << 27,	// Don't use! For internal use by BeginPopupModal()
	ImSlateWindowFlags_ChildMenu					= 1 << 28,	// Don't use! For internal use by BeginMenu()
	ImSlateWindowFlags_DockNodeHost					= 1 << 29,	// Don't use! For internal use by Begin()/NewFrame()
	ImSlateWindowFlags_EventDrived					= 1 << 30,	// Don't use! For internal use by Begin()/NewFrame()
};
// clang-format on

struct ImStr
{
	void* operator new(size_t) = delete;
	void* operator new[](size_t) = delete;

	// String literals — array reference, compile-time safe
	template<int32 N> ImStr(const char (&Literal)[N]) : Kind(Ansi) { new (&AnsiRef) FAnsiStringView(Literal, N - 1); }
	template<int32 N> ImStr(const TCHAR (&Literal)[N]) : Kind(TChar) { new (&TCharRef) FStringView(Literal, N - 1); }

	// Views — zero cost reference
	ImStr(FAnsiStringView InView) : Kind(Ansi) { new (&AnsiRef) FAnsiStringView(InView); }
	ImStr(FStringView InView) : Kind(TChar) { new (&TCharRef) FStringView(InView); }

	// FString — explicit only, prevents implicit temp construction
	explicit ImStr(const FString& InStr) : Kind(TChar) { new (&TCharRef) FStringView(InStr); }

	// const char* — ANSI pointer, safe (no conversion needed)
	ImStr(const char* InStr) : Kind(Ansi) { new (&AnsiRef) FAnsiStringView(InStr); }

	// Dangerous sources — compile error
	ImStr(const TCHAR*) = delete;
	ImStr(FString&&) = delete;

	operator FAnsiStringView() const { return Resolve(); }
	const char* GetData() const { return Resolve().GetData(); }
	int32 Len() const { return Resolve().Len(); }
	bool StartsWith(FAnsiStringView Prefix) const { auto R = Resolve(); return R.Len() >= Prefix.Len() && FCStringAnsi::Strncmp(R.GetData(), Prefix.GetData(), Prefix.Len()) == 0; }

private:
	FAnsiStringView Resolve() const
	{
		if (Kind == Ansi) return AnsiRef;
		if (bResolved) return Resolved;

		auto SrcData = TCharRef.GetData();
		auto SrcLen = TCharRef.Len();
		if (SrcLen == 0) return FAnsiStringView();

		auto ConvLen = FPlatformString::ConvertedLength<char>(SrcData, SrcLen);
		char* Dest = Inline;
		if (ConvLen >= InlineSize)
		{
			Heap.SetNumUninitialized(ConvLen + 1);
			Dest = Heap.GetData();
		}
		FPlatformString::Convert(Dest, ConvLen + 1, SrcData, SrcLen);
		Dest[ConvLen] = '\0';
		Resolved = FAnsiStringView(Dest, ConvLen);
		bResolved = true;
		return Resolved;
	}

	enum EKind : uint8 { Ansi, TChar } Kind;
	union
	{
		FAnsiStringView AnsiRef;
		FStringView TCharRef;
	};

	static constexpr int32 InlineSize = 64;
	mutable char Inline[InlineSize];
	mutable TArray<char> Heap;
	mutable FAnsiStringView Resolved;
	mutable bool bResolved = false;
};
using ImSlateId = uint32;

struct ImVec2 : public FVector2D
{
	using FVector2D::FVector2D;
	ImVec2(const FVector2D& In)
		: FVector2D(In)
	{
	}
	//operator FVector2D() { return *static_cast<FVector2D*>(this); }

	bool HasValidSize() const { return X > 0.f && Y > 0.f; }
};

struct ImVec3 : public FVector
{
	using FVector::FVector;
	ImVec3(const FVector& In)
		: FVector(In)
	{
	}
	//operator FVector() { return *static_cast<FVector*>(this); }
};

struct ImVec4 : public FVector4
{
	using FVector4::FVector4;
	ImVec4(const FVector4& In)
		: FVector4(In)
	{
	}
};

struct ImTransform : public FTransform
{
	using FTransform::FTransform;
	ImTransform(const FTransform& In)
		: FTransform(In)
	{
	}
};

#if 1
struct ImRect : public FSlateRect
{
	using FSlateRect::FSlateRect;
	ImRect(const FSlateRect& In)
		: FSlateRect(In)
	{
	}

	// clang-format off
	ImVec2		GetCenter() const					{ return ImVec2((Left + Right) * 0.5f, (Top + Bottom) * 0.5f); }
	ImVec2		GetSize() const						{ return ImVec2(Right - Left, Bottom - Top); }
	auto		GetWidth() const					{ return Right - Left; }
	auto		GetHeight() const					{ return Bottom - Top; }
	auto		GetArea() const						{ return (Right - Left) * (Bottom - Top); }

	ImVec2		GetTL() const						{ return ImVec2(Left, Top); }      // Top-left
	ImVec2		GetTR() const						{ return ImVec2(Right, Top); }     // Top-right
	ImVec2		GetBL() const						{ return ImVec2(Left, Bottom); }   // Bottom-left
	ImVec2		GetBR() const						{ return ImVec2(Right, Bottom); }  // Bottom-right

	bool		Contains(const ImVec2& p) const		{ return p.X >= Left && p.Y >= Top && p.X < Right && p.Y < Bottom; }
	bool		Contains(const ImRect& r) const		{ return r.Left >= Left && r.Top >= Top && r.Right <= Right && r.Bottom <= Bottom; }
	bool		Overlaps(const ImRect& r) const		{ return r.Top < Bottom && r.Bottom > Top && r.Left < Right && r.Right > Left; }
	void		Add(const ImVec2& p)				{ if (Left > p.X) Left = p.X; if (Top > p.Y) Top = p.Y; if (Right < p.X) Right = p.X; if (Bottom < p.Y) Bottom = p.Y; }
	void		Add(const ImRect& r)				{ if (Left > r.Left) Left = r.Left; if (Top > r.Top) Top = r.Top; if (Right < r.Right) Right = r.Right; if (Bottom < r.Bottom) Bottom = r.Bottom; }
	void		Expand(const float amount)			{ Left -= amount; Top -= amount; Right += amount; Bottom += amount; }
	void		Expand(const ImVec2& amount)		{ Left -= amount.X; Top -= amount.Y; Right += amount.X; Bottom += amount.Y; }
	void		Translate(const ImVec2& d)			{ Left += d.X; Top += d.Y; Right += d.X; Bottom += d.Y; }
	void		TranslateX(float dx)				{ Left += dx; Right += dx; }
	void		TranslateY(float dy)				{ Top += dy; Bottom += dy; }
	void		Floor()								{ Left = FMath::Floor(Left); Top = FMath::Floor(Top); Right = FMath::Floor(Right); Bottom = FMath::Floor(Bottom); }
	bool		IsInverted() const					{ return Left > Right || Top > Bottom; }
	ImVec4		ToVec4() const						{ return ImVec4(Left, Top, Right, Bottom); }
	// Simple version, may lead to an inverted rectangle, which is fine for Contains/Overlaps test but not for display.
	void		ClipWith(const ImRect& r)			{ Left = FMath::Max(Left, r.Left); Top = FMath::Max(Top, r.Top); Right = FMath::Min(Right, r.Right); Bottom = FMath::Min(Bottom, r.Bottom); }
	// Full version, ensure both points are fully clipped.
	void		ClipWithFull(const ImRect& r)		{ Left = FMath::Clamp(Left, r.Left, r.Right); Right = FMath::Clamp(Right, r.Left, r.Right); Top = FMath::Clamp(Top, r.Top, r.Bottom); Bottom = FMath::Clamp(Bottom, r.Top, r.Bottom);}
	// clang-format on
};
#else
struct ImRect : public FBox2D
{
	using FBox2D::FBox2D;
	ImRect(const FBox2D& In)
		: FBox2D(In)
	{
	}

	// clang-format off
	ImVec2		GetCenter() const					{ return ImVec2((Min.X + Max.X) * 0.5f, (Min.Y + Max.Y) * 0.5f); }
	ImVec2		GetSize() const						{ return ImVec2(Max.X - Min.X, Max.Y - Min.Y); }
	float		GetWidth() const					{ return Max.X - Min.X; }
	float		GetHeight() const					{ return Max.Y - Min.Y; }
	float		GetArea() const						{ return (Max.X - Min.X) * (Max.Y - Min.Y); }

	ImVec2		GetTL() const						{ return Min; }                   // Top-left
	ImVec2		GetTR() const						{ return ImVec2(Max.X, Min.Y); }  // Top-right
	ImVec2		GetBL() const						{ return ImVec2(Min.X, Max.Y); }  // Bottom-left
	ImVec2		GetBR() const						{ return Max; }                   // Bottom-right

	bool		Contains(const ImVec2& p) const		{ return p.X     >= Min.X && p.Y     >= Min.Y && p.X     <  Max.X && p.Y     <  Max.Y; }
	bool		Contains(const ImRect& r) const		{ return r.Min.X >= Min.X && r.Min.Y >= Min.Y && r.Max.X <= Max.X && r.Max.Y <= Max.Y; }
	bool		Overlaps(const ImRect& r) const		{ return r.Min.Y <  Max.Y && r.Max.Y >  Min.Y && r.Min.X <  Max.X && r.Max.X >  Min.X; }
	void		Add(const ImVec2& p)				{ if (Min.X > p.X)     Min.X = p.X;     if (Min.Y > p.Y)     Min.Y = p.Y;     if (Max.X < p.X)     Max.X = p.X;     if (Max.Y < p.Y)     Max.Y = p.Y; }
	void		Add(const ImRect& r)				{ if (Min.X > r.Min.X) Min.X = r.Min.X; if (Min.Y > r.Min.Y) Min.Y = r.Min.Y; if (Max.X < r.Max.X) Max.X = r.Max.X; if (Max.Y < r.Max.Y) Max.Y = r.Max.Y; }
	void		Expand(const float amount)			{ Min.X -= amount;   Min.Y -= amount;   Max.X += amount;   Max.Y += amount; }
	void		Expand(const ImVec2& amount)		{ Min.X -= amount.X; Min.Y -= amount.Y; Max.X += amount.X; Max.Y += amount.Y; }
	void		Translate(const ImVec2& d)			{ Min.X += d.X; Min.Y += d.Y; Max.X += d.X; Max.Y += d.Y; }
	void		TranslateX(float dx)				{ Min.X += dx; Max.X += dx; }
	void		TranslateY(float dy)				{ Min.Y += dy; Max.Y += dy; }
	// Simple version, may lead to an inverted rectangle, which is fine for Contains/Overlaps test but not for display.
	void		ClipWith(const ImRect& r)			{ Min = ImVec2::Max(Min, r.Min); Max = ImVec2::Min(Max, r.Max); }
	// Full version, ensure both points are fully clipped.
	void		ClipWithFull(const ImRect& r)		{ Min = FMath::Clamp(Min, r.Min, r.Max); Max = FMath::Clamp(Max, r.Min, r.Max); }
	void		Floor()								{ Min.X = FMath::Floor(Min.X); Min.Y = FMath::Floor(Min.Y); Max.X = FMath::Floor(Max.X); Max.Y = FMath::Floor(Max.Y); }
	bool		IsInverted() const					{ return Min.X > Max.X || Min.Y > Max.Y; }
	ImVec4		ToVec4() const						{ return ImVec4(Min.X, Min.Y, Max.X, Max.Y); }
	// clang-format on
};
#endif

namespace ImSlate
{
class SImSlateWindow;
class SImSlateViewport;
class FViewportPopupHolder;

// clang-format off
struct ImSlateContext;							// Dear ImSlate context (opaque structure, unless including ImSlate_internal.h)
IMSLATE_API ImSlateContext* GetGImSlate();
IMSLATE_API ImSlateContext* GetGImSlateForWorld(const class UObject* InObj);	// PIE-safe: context for InObj's world

class SImSlateWindow;
class SImSlateViewport;
using ImSlateWindow				= SImSlateWindow;	// Storage for one window
using ImSlateViewport			= SImSlateViewport;	// A Platform Window (always 1 unless multi-viewport are enabled. One per platform window to output to). In the future may represent Platform Monitor
using ImSlateWindowFlags		= int32;			// -> enum ImSlateWindowFlags_     // Flags: for Begin(), BeginChild()
using ImSlateViewportFlags		= int32;			// -> enum ImSlateViewportFlags_   // Flags: for ImSlateViewport

using ImColor					= FColor;		// Helper functions to create a color that can be converted to either u32 or float4 (*OBSOLETE* please avoid using)
using ImSlateCond				= int32;		// -> enum ImSlateCond_            // Enum: A condition for many Set*() functions

struct ImSlateNextWindowData;			// Storage for SetNextWindow** functions
struct ImSlateNextItemData;				// Storage for SetNextItem** functions

// Forward declarations
struct ImSlateIO;							// Main configuration and I/O between your application and ImSlate
struct ImSlateInputTextCallbackData;		// Shared state of InputText() when using custom ImSlateInputTextCallback (rare/advanced use)
struct ImSlateListClipper;					// Helper to manually clip large list of items
struct ImSlateOnceUponAFrame;				// Helper for running a block of code not more than once a frame
struct ImSlatePayload;						// User data payload for drag and drop operations
struct ImSlatePlatformIO;					// Multi-viewport support: interface for Platform/Renderer backends + viewports to render
struct ImSlatePlatformMonitor;				// Multi-viewport support: user-provided bounds for each connected monitor/display. Used when positioning popups and tooltips to avoid them straddling monitors
struct ImSlateSizeCallbackData;				// Callback data when using SetNextWindowSizeConstraints() (rare/advanced use)
struct ImSlateStorage;						// Helper for key->value storage
struct ImSlateTableSortSpecs;					// Sorting specifications for a table (often handling sort specs for a single column, occasionally more)
struct ImSlateTableColumnSortSpecs;			// Sorting specification for one column of a table
struct ImSlateTextBuffer;					// Helper to hold and append into a text buffer (~string builder)
struct ImSlateTextFilter;					// Helper to parse and apply text filters (e.g. "aaaaa[,bbbbb][,ccccc]")

// Enums/Flags (declared as int for compatibility with old C++, to allow using as flags without overhead, and to not pollute the top of this file)
// - Tip: Use your programming IDE navigation facilities on the names in the _central column_ below to find the actual flags/enum lists!
//   In Visual Studio IDE: CTRL+comma ("Edit.NavigateTo") can follow symbols in comments, whereas CTRL+F12 ("Edit.GoToImplementation") cannot.
//   With Visual Assist installed: ALT+G ("VAssistX.GoToImplementation") can also follow symbols in comments.
using ImSlateCol				= int32;	// -> enum ImSlateCol_             // Enum: A color identifier for styling
using ImSlateDataType			= int32;	// -> enum ImSlateDataType_        // Enum: A primary data type
using ImSlateDir				= int32;	// -> enum ImSlateDir_             // Enum: A cardinal direction
using ImSlateKey				= int32;	// -> enum ImSlateKey_             // Enum: A key identifier (ImSlate-side enum)
using ImSlateNavInput			= int32;	// -> enum ImSlateNavInput_        // Enum: An input identifier for navigation
using ImSlateMouseButton		= int32;	// -> enum ImSlateMouseButton_     // Enum: A mouse button identifier (0=left, 1=right, 2=middle)
using ImSlateMouseCursor		= int32;	// -> enum ImSlateMouseCursor_     // Enum: A mouse cursor identifier
using ImSlateSortDirection		= int32;	// -> enum ImSlateSortDirection_   // Enum: A sorting direction (ascending or descending)
using ImSlateStyleVar			= int32;	// -> enum ImSlateStyleVar_        // Enum: A variable identifier for styling
using ImSlateTableBgTarget		= int32;	// -> enum ImSlateTableBgTarget_   // Enum: A color target for TableSetBgColor()
using ImDrawFlags				= int32;	// -> enum ImDrawFlags_          // Flags: for ImDrawList functions
using ImDrawListFlags			= int32;	// -> enum ImDrawListFlags_      // Flags: for ImDrawList instance
using ImFontAtlasFlags			= int32;	// -> enum ImFontAtlasFlags_     // Flags: for ImFontAtlas build
using ImSlateBackendFlags		= int32;	// -> enum ImSlateBackendFlags_    // Flags: for io.BackendFlags
using ImSlateButtonFlags		= int32;	// -> enum ImSlateButtonFlags_     // Flags: for InvisibleButton()
using ImSlateColorEditFlags		= int32;	// -> enum ImSlateColorEditFlags_  // Flags: for ColorEdit4(), ColorPicker4() etc.
using ImSlateConfigFlags		= int32;	// -> enum ImSlateConfigFlags_     // Flags: for io.ConfigFlags
using ImSlateComboFlags			= int32;	// -> enum ImSlateComboFlags_      // Flags: for BeginCombo()
using ImSlateDockNodeFlags		= int32;	// -> enum ImSlateDockNodeFlags_   // Flags: for DockSpace()
using ImSlateDragDropFlags		= int32;	// -> enum ImSlateDragDropFlags_   // Flags: for BeginDragDropSource(), AcceptDragDropPayload()
using ImSlateFocusedFlags		= int32;	// -> enum ImSlateFocusedFlags_    // Flags: for IsWindowFocused()
using ImSlateHoveredFlags		= int32;	// -> enum ImSlateHoveredFlags_    // Flags: for IsItemHovered(), IsWindowHovered() etc.
using ImSlateInputTextFlags		= int32;	// -> enum ImSlateInputTextFlags_  // Flags: for InputText(), InputTextMultiline()
using ImSlateKeyModFlags		= int32;	// -> enum ImSlateKeyModFlags_     // Flags: for io.KeyMods (Ctrl/Shift/Alt/Super)
using ImSlatePopupFlags			= int32;	// -> enum ImSlatePopupFlags_      // Flags: for OpenPopup*(), BeginPopupContext*(), IsPopupOpen()
using ImSlateSelectableFlags	= int32;	// -> enum ImSlateSelectableFlags_ // Flags: for Selectable()
using ImSlateSliderFlags		= int32;	// -> enum ImSlateSliderFlags_     // Flags: for DragFloat(), DragInt(), SliderFloat(), SliderInt() etc.
using ImSlateTabBarFlags		= int32;	// -> enum ImSlateTabBarFlags_     // Flags: for BeginTabBar()
using ImSlateTabItemFlags		= int32;	// -> enum ImSlateTabItemFlags_    // Flags: for BeginTabItem()
using ImSlateTableFlags			= int32;	// -> enum ImSlateTableFlags_      // Flags: For BeginTable()
using ImSlateTableColumnFlags	= int32;	// -> enum ImSlateTableColumnFlags_// Flags: For TableSetupColumn()
using ImSlateTableRowFlags		= int32;	// -> enum ImSlateTableRowFlags_   // Flags: For TableNextRow()
using ImSlateTreeNodeFlags		= int32;	// -> enum ImSlateTreeNodeFlags_   // Flags: for TreeNode(), TreeNodeEx(), CollapsingHeader()

//-----------------------------------------------------------------------------
// [SECTION] Forward declarations
//-----------------------------------------------------------------------------

struct ImBitVector;					// Store 1-bit per value
struct ImDrawDataBuilder;			// Helper to build a ImDrawData instance
struct ImDrawListSharedData;		// Data shared between all ImDrawList instances
struct ImSlateColorMod;				// Stacked color modifier, backup of modified data so we can restore it
struct ImSlateContextHook;			// Hook for extensions like ImSlateTestEngine
struct ImSlateDataTypeInfo;			// Type information associated to a ImSlateDataType enum
struct ImSlateDockContext;			// Docking system context
struct ImSlateDockRequest;			// Docking system dock/undock queued request
struct ImSlateDockNode;				// Docking system node (hold a list of Windows OR two child dock nodes)
struct ImSlateDockNodeSettings;		// Storage for a dock node in .ini file (we preserve those even if the associated dock node isn't active during the session)
struct ImSlateGroupData;				// Stacked storage data for BeginGroup()/EndGroup()
struct ImSlateInputTextState;			// Internal state of the currently focused/edited text input box
struct ImSlateLastItemData;			// Status storage for last submitted items
struct ImSlateMenuColumns;			// Simple column measurement, currently used for MenuItem() only
struct ImSlateNavItemData;			// Result of a gamepad/keyboard directional navigation move query result
struct ImSlateMetricsConfig;			// Storage for ShowMetricsWindow() and DebugNodeXXX() functions
struct ImSlateOldColumnData;			// Storage data for a single column for legacy Columns() api
struct ImSlateOldColumns;				// Storage data for a columns set for legacy Columns() api
struct ImSlatePopupData;				// Storage for current popup stack
struct ImSlateSettingsHandler;		// Storage for one type registered in the .ini file
struct ImSlateStackSizes;				// Storage of stack sizes for debugging/asserting
struct ImSlateStyleMod;				// Stacked style modifier, backup of modified data so we can restore it
struct ImSlateTabBar;					// Storage for a tab bar
struct ImSlateTabItem;				// Storage for a tab item (within a tab bar)
struct ImSlateTable;					// Storage for a table
struct ImSlateTableColumn;			// Storage for one column of a table
struct ImSlateTableTempData;			// Temporary storage for one table (one per table in the stack), shared between tables.
struct ImSlateTableSettings;			// Storage for a table .ini settings
struct ImSlateTableColumnsSettings;	// Storage for a column .ini settings
struct ImSlateWindowTempData;			// Temporary storage for one window (that's the data which in theory we could ditch at the end of the frame, in practice we currently keep it for each window)
struct ImSlateWindowSettings;			// Storage for a window .ini settings (we keep one of those even if the actual window wasn't instanced during this session)


// Use your programming IDE "Go to definition" facility on the names of the center columns to find the actual flags/enum lists.
using ImSlateDataAuthority 			= int32;	// -> enum ImSlateDataAuthority_      // Enum: for storing the source authority (dock node vs window) of a field
using ImSlateLayoutType				= int32;	// -> enum ImSlateLayoutType_         // Enum: Horizontal or vertical
using ImSlateLayoutItemType			= int32;	// -> enum ImSlateLayoutItemType_    // Enum: Item or Spring
using ImSlateActivateFlags			= int32;	// -> enum ImSlateActivateFlags_      // Flags: for navigation/focus function (will be for ActivateItem() later)
using ImSlateItemFlags				= int32;	// -> enum ImSlateItemFlags_          // Flags: for PushItemFlag()
using ImSlateItemStatusFlags		= int32;	// -> enum ImSlateItemStatusFlags_    // Flags: for DC.LastItemStatusFlags
using ImSlateOldColumnFlags			= int32;	// -> enum ImSlateOldColumnFlags_     // Flags: for BeginColumns()
using ImSlateNavHighlightFlags		= int32;	// -> enum ImSlateNavHighlightFlags_  // Flags: for RenderNavHighlight()
using ImSlateNavDirSourceFlags		= int32;	// -> enum ImSlateNavDirSourceFlags_  // Flags: for GetNavInputAmount2d()
using ImSlateNavMoveFlags			= int32;	// -> enum ImSlateNavMoveFlags_       // Flags: for navigation requests
using ImSlateNextItemDataFlags		= int32;	// -> enum ImSlateNextItemDataFlags_  // Flags: for SetNextItemXXX() functions
using ImSlateNextWindowDataFlags	= int32;	// -> enum ImSlateNextWindowDataFlags_// Flags: for SetNextWindowXXX() functions
using ImSlateScrollFlags			= int32;	// -> enum ImSlateScrollFlags_        // Flags: for ScrollToItem() and navigation requests
using ImSlateSeparatorFlags			= int32;	// -> enum ImSlateSeparatorFlags_     // Flags: for SeparatorEx()
using ImSlateTextFlags				= int32;	// -> enum ImSlateTextFlags_          // Flags: for TextEx()
using ImSlateTooltipFlags			= int32;	// -> enum ImSlateTooltipFlags_       // Flags: for BeginTooltipEx()
// clang-format on

// Flags for ImSlate::IsWindowFocused()
enum ImSlateFocusedFlags_
{
	ImSlateFocusedFlags_None							= 0,
	ImSlateFocusedFlags_ChildWindows					= 1 << 0,	// Return true if any children of the window is focused
	ImSlateFocusedFlags_RootWindow						= 1 << 1,	// Test from root window (top most parent of the current hierarchy)
	ImSlateFocusedFlags_AnyWindow						= 1 << 2,	// Return true if any window is focused. Important: If you are trying to tell how to dispatch your low-level inputs, do NOT use this. Use 'io.WantCaptureMouse' instead! Please read the FAQ!
	ImSlateFocusedFlags_NoPopupHierarchy				= 1 << 3,	// Do not consider popup hierarchy (do not treat popup emitter as parent of popup) (when used with _ChildWindows or _RootWindow)
	ImSlateFocusedFlags_DockHierarchy					= 1 << 4,	// Consider docking hierarchy (treat dockspace host as parent of docked window) (when used with _ChildWindows or _RootWindow)
	ImSlateFocusedFlags_RootAndChildWindows				= ImSlateFocusedFlags_RootWindow | ImSlateFocusedFlags_ChildWindows,
};

// Flags for ImSlate::InputStateFloat
enum ImSlateFloatFlags_
{
	ImSlateFloatFlags_None = 0,
	ImSlateFloatFlags_ReadOnly = 1 << 0,
};

// Flags for ImSlate::Numeric
enum ImSliderStatus_
{
	ImSliderStatus_Normal = 0,
	ImSliderStatus_BeginSlider,
	ImSliderStatus_EndSlider,
	ImSliderStatus_ValueChanged,
	ImSliderStatus_Committed,
};

// Flags for ImSlate::ComboBox
enum ImSlateComboFlags_
{
	ImSlateComboFlags_None = 0,
	ImSlateComboFlags_ReadOnly = 1 << 0,
};

// Flags for ImSlate::InputText
enum ImSlateInputTextFlags_
{
	ImSlateInputTextFlags_None = 0,
	ImSlateInputTextFlags_CharsDecimal = 1 << 0,          // Allow 0123456789.+-*/
	ImSlateInputTextFlags_CharsHexadecimal = 1 << 1,      // Allow 0123456789ABCDEFabcdef
	ImSlateInputTextFlags_CharsUppercase = 1 << 2,        // Turn a..z into A..Z
	ImSlateInputTextFlags_CharsNoBlank = 1 << 3,          // Filter out spaces, tabs
	ImSlateInputTextFlags_AutoSelectAll = 1 << 4,         // Select entire text when first taking mouse focus
	ImSlateInputTextFlags_EnterReturnsTrue = 1 << 5,      // Return 'true' when Enter is pressed (as opposed to every time the value was modified). Consider looking at the IsItemDeactivatedAfterEdit() function.
	ImSlateInputTextFlags_CallbackCompletion = 1 << 6,    // Callback on pressing TAB (for completion handling)
	ImSlateInputTextFlags_CallbackHistory = 1 << 7,       // Callback on pressing Up/Down arrows (for history handling)
	ImSlateInputTextFlags_CallbackAlways = 1 << 8,        // Callback on each iteration. User code may query cursor position, modify text buffer.
	ImSlateInputTextFlags_CallbackCharFilter = 1 << 9,    // Callback on character inputs to replace or discard them. Modify 'EventChar' to replace or discard, or return 1 in callback to discard.
	ImSlateInputTextFlags_AllowTabInput = 1 << 10,        // Pressing TAB input a '\t' character into the text field
	ImSlateInputTextFlags_CtrlEnterForNewLine = 1 << 11,  // In multi-line mode, unfocus with Enter, add new line with Ctrl+Enter (default is opposite: unfocus with Ctrl+Enter, add line with Enter).
	ImSlateInputTextFlags_NoHorizontalScroll = 1 << 12,   // Disable following the cursor horizontally
	ImSlateInputTextFlags_AlwaysOverwrite = 1 << 13,      // Overwrite mode
	ImSlateInputTextFlags_ReadOnly = 1 << 14,             // Read-only mode
	ImSlateInputTextFlags_Password = 1 << 15,             // Password mode, display all characters as '*'
	ImSlateInputTextFlags_NoUndoRedo = 1 << 16,           // Disable undo/redo. Note that input text owns the text data while active, if you want to provide your own undo/redo stack you need e.g. to call ClearActiveID().
	ImSlateInputTextFlags_CharsScientific = 1 << 17,      // Allow 0123456789.+-*/eE (Scientific notation input)
	ImSlateInputTextFlags_CallbackResize =
		1
		<< 18,  // Callback on buffer capacity changes request (beyond 'buf_size' parameter value), allowing the string to grow. Notify when the string wants to be resized (for string types which hold a cache of their Size). You will be provided a new BufSize in the callback and NEED to honor it. (see misc/cpp/imgui_stdlib.h for an example of using this)
	ImSlateInputTextFlags_CallbackEdit = 1 << 19  // Callback on any edit (note that InputText() already returns true on edit, the callback is useful mainly to manipulate the underlying buffer while focus is active)
};

// Numeric shaping for InputText's on-screen numeric keypad: type (integer/unsigned/hex) + optional
// clamp range + optional step (SpinBox-like +/- keys). Passed to InputText so the keypad can hide
// the dot for integers, hide minus for unsigned, add A-F for hex, and clamp/step typed values.
struct FImInputNumericSpec
{
	bool bInteger = false;    // integer type → keypad hides '.'
	bool bUnsigned = false;   // unsigned type → keypad hides '-'
	bool bHex = false;        // hexadecimal → keypad adds A-F
	TOptional<double> Min;    // clamp lower bound (also used by step)
	TOptional<double> Max;    // clamp upper bound
	TOptional<double> Step;   // when set → keypad shows +/- step keys
};

// Resizing callback data to apply custom constraint. As enabled by SetNextWindowSizeConstraints(). Callback is called during the next Begin().
// NB: For basic min/max size constraint on each axis you don't need to use the callback! The SetNextWindowSizeConstraints() parameters are enough.
struct ImSlateSizeCallbackData
{
	ImSlateWindow* Window;       // Read-only.
	ImVec2 Pos;                  // Read-only.   Window position, for reference.
	ImVec2 ContentSize;          // Read-only.   Current content size.
	mutable ImVec2 DesiredSize;  // Read-write.  Desired size, based on user's mouse position. Write to this field to restrain resizing.
};
using ImSlateResizeCallback = GMP::TGMPWeakFunction<void(const ImSlateSizeCallbackData&)>;

class FItemSlotPod
{
public:
	FMargin SlotPadding;

protected:
	float MaxWidth;
	float MaxHeight;
	float MinWidth;
	float MinHeight;

public:
	uint32 Hash;
	union
	{
		uint32 FlagBits;
		struct
		{
			uint8 Cond : 4;
			uint8 HAlignment : 4;
			uint8 VAlignment : 4;
			uint8 bFillWidth : 1;
			uint8 bAlignCol : 1;
			uint8 bNewRow : 1;
			uint8 bBreakLine : 1;
			uint8 bCollapsed : 1;
		};
	};

	union
	{
		mutable int32 StretchToCol;
		mutable float StretchValue;
	};
	mutable int32 ParentIdx = -1;
	float AspectRatio = 0.f;

	// BeginGroup/EndGroup: items sharing the same non-zero GroupId draw a shared background of GroupColor
	uint32 GroupId = 0;
	FLinearColor GroupColor = FLinearColor::Transparent;

	FItemSlotPod(uint32 InKey = 0)
		: SlotPadding(FMargin(0))
		, MaxWidth(0.f)
		, MaxHeight(0.f)
		, MinWidth(0.f)
		, MinHeight(0.f)
		, Hash(InKey)
		, FlagBits(0)
		, StretchToCol(-1.f)
	{
		bNewRow = 1;
		bFillWidth = 1;
	}

	friend bool operator==(const FItemSlotPod& Lhs, const FItemSlotPod& Rhs)
	{
		// Per-field comparison of LAYOUT properties only
		// Hash is identity (ImSlateId), not layout — excluded
		return Lhs.SlotPadding == Rhs.SlotPadding
			&& Lhs.MaxWidth == Rhs.MaxWidth && Lhs.MaxHeight == Rhs.MaxHeight
			&& Lhs.MinWidth == Rhs.MinWidth && Lhs.MinHeight == Rhs.MinHeight
			&& Lhs.FlagBits == Rhs.FlagBits
			&& Lhs.StretchToCol == Rhs.StretchToCol
			&& Lhs.ParentIdx == Rhs.ParentIdx
			&& Lhs.AspectRatio == Rhs.AspectRatio
			&& Lhs.GroupId == Rhs.GroupId
			&& Lhs.GroupColor == Rhs.GroupColor;
	}
	bool Equal(const FItemSlotPod& Other) const { return *this == Other; }
	void Apply(const FItemSlotPod& Other)
	{
		uint32 SavedHash = Hash;  // Hash is identity, not layout
		*this = Other;
		Hash = SavedHash;
	}

	auto& SetAspectRatio(float InAspectRatio)
	{
		AspectRatio = InAspectRatio;
		return *this;
	}

	auto& SetMaxWidth(float InMaxWidth)
	{
		MaxWidth = FMath::Max(MinWidth, InMaxWidth);
		return *this;
	}

	auto& SetMaxHeight(float InMaxHeight)
	{
		MaxHeight = FMath::Max(MinHeight, InMaxHeight);
		return *this;
	}

	auto& SetMinWidth(float InMinWidth)
	{
		MinWidth = FMath::Max(0.f, InMinWidth);
		return *this;
	}

	auto& SetMinHeight(float InMinHeight)
	{
		MinHeight = FMath::Max(0.f, InMinHeight);
		return *this;
	}

	float GetWidth(float Val) const
	{
		Val = FMath::Max(Val, MinWidth);
		return (MaxWidth > 0.f) ? FMath::Min(MaxWidth, Val) : Val;
	}

	float GetHeight(float Val) const
	{
		Val = FMath::Max(Val, MinHeight);
		return (MaxHeight > 0.f) ? FMath::Min(MaxHeight, Val) : Val;
	}

	FVector2D GetSize(FVector2D Val) const
	{
		Val = FVector2D::Max(Val, FVector2D(MinWidth, MinHeight));

		if (MaxWidth > 0.f)
			Val.X = FMath::Min(MaxWidth, Val.X);
		if (MaxHeight > 0.f)
			Val.Y = FMath::Min(MaxHeight, Val.Y);
		return Val;
	}

	bool IsFullRow() const { return bNewRow && bBreakLine; }

	// Returns true if any layout property actually changed
	bool ApplyNextItem(const ImSlateNextItemData& NextItemData);
};

namespace Murmur3
{
	inline uint32 HashFinalize(uint32 len, uint32 h = 0)
	{
		h ^= len;

		h ^= h >> 16;
		h *= 0x85ebca6b;
		h ^= h >> 13;
		h *= 0xc2b2ae35;
		h ^= h >> 16;
		return h;
	}
	inline uint32 HashUpdate(const void* key, uint32 len, uint32 h = 0, uint32* outlen = nullptr)
	{
		uint32 k;
		auto data = (const uint8*)key;
		static auto murmur_32_scramble = [](uint32 k) {
			k *= 0xcc9e2d51;
			k = (k << 15) | (k >> 17);
			k *= 0x1b873593;
			return k;
		};

		for (uint32 i = len >> 2; i; i--)
		{
			memcpy(&k, data, sizeof(uint32));
			data += sizeof(uint32);

			k *= 0xcc9e2d51;
			k = (k << 15) | (k >> 17);
			k *= 0x1b873593;

			h ^= k;
			h = (h << 13) | (h >> 19);
			h = h * 5 + 0xe6546b64;
		}

		if (auto left = len & 3)
		{
			k = 0;
			if (outlen)
			{
				// fill to 4 bytes round
				memcpy(&k, data, sizeof(uint32));
				h ^= murmur_32_scramble(k);
				h = (h << 13) | (h >> 19);
				h = h * 5 + 0xe6546b64;
				len = len - left + 4;
				*outlen = len;
			}
			else
			{
				for (uint32 i = left; i; i--)
				{
					k <<= 8;
					k |= data[i - 1];
				}
			}
			k *= 0xcc9e2d51;
			k = (k << 15) | (k >> 17);
			k *= 0x1b873593;
			h ^= k;
		}

		return h;
	}
	struct FHasher
	{
		uint32 h = 0;
		uint32 len = 0;

		FHasher& Update(const void* In, uint32 Len)
		{
			h = HashUpdate(In, Len, h, &len);
			return *this;
		}
		uint32 Finalize(const void* In, uint32 Len) const
		{
			auto Tmplen = Len;
			auto Tmph = HashUpdate(In, Len, h, &Tmplen);
			return HashFinalize(Tmplen, Tmph);
		}
		uint32 Finalize() const
		{
			check(h || len);
			return HashFinalize(len, h);
		}
		operator uint32() const { return Finalize(); }
	};

}  // namespace Murmur3

namespace FNV1a
{
	inline uint32 HashUpdate(const void* InKey, const uint32 InLen, uint32 Base = 0x811c9dc5)
	{
		const ImSlateId Prime = 0x1000193;
		const uint8* Data = (const uint8*)InKey;

		for (uint32 i = 0; i < InLen; ++i)
		{
			uint8 Value = Data[i];
			Base = Base ^ Value;
			Base *= Prime;
		}
		return Base;
	}

	struct FHasher
	{
		uint32 h = 0x811c9dc5;

		FHasher& Update(const void* In, uint32 Len)
		{
			h = HashUpdate(In, Len, h);
			return *this;
		}
		uint32 Finalize(const void* In, uint32 Len) const
		{
			auto Tmph = HashUpdate(In, Len, h);
			return Tmph;
		}
		uint32 Finalize() const { return h; }
		operator uint32() const { return Finalize(); }
	};

}  // namespace FNV1a

namespace FTextKeyHash
{
	struct FHasher
	{
		uint32 h = 0x811c9dc5;

		FHasher& Update(const void* In, uint32 Len)
		{
			FString InputStr((const TCHAR*)In, Len);
			h = TextKeyUtil::HashString(InputStr, h);
			return *this;
		}
		uint32 Finalize(const void* In, uint32 Len) const
		{
			FString InputStr((const TCHAR*)In, Len);
			auto Tmph = TextKeyUtil::HashString(InputStr, h);
			return Tmph;
		}
		uint32 Finalize() const { return h; }
		operator uint32() const { return Finalize(); }
	};

}  // namespace FTextKeyHash

struct ImSlateIdStack
{
	using FHasher = FNV1a::FHasher;
	struct ImStackInfo
	{
		FHasher Hasher;

		auto Update(const void* InData, const uint32 Len)
		{
			auto Hash = Hasher;
			return Hash.Update(InData, Len);
		}
	};
	TArray<ImStackInfo, TInlineAllocator<16>> StackInfos = {ImStackInfo{}};

	static uint32 FullHash(const void* InData, uint32 Len) { return FHasher().Finalize(InData, Len); }
	static uint32 FullHash(ImStr Strv) { return FullHash(Strv.GetData(), Strv.Len()); }
	uint32 GetId(const void* InData, uint32 Len) const { return StackInfos.Last().Hasher.Finalize(InData, Len); }
	uint32 GetId(ImStr Strv) const { return GetId(Strv.GetData(), Strv.Len()); }
	uint32 GetId() const { return StackInfos.Last().Hasher.Finalize(); }

	uint32 Push(const void* InData, uint32 Len)
	{
		auto Hasher = StackInfos.Last().Update(InData, Len);
		StackInfos.Add({Hasher});
		return Hasher;
	}
	uint32 Push(ImStr Strv) { return Push(Strv.GetData(), Strv.Len()); }

	void Pop()
	{
		if (ensureAlways(StackInfos.Num() > 1))
		{
			StackInfos.RemoveAt(StackInfos.Num() - 1);
		}
	}
};
IMSLATE_API UWorld* GetImSlateWorldChecked();
IMSLATE_API UWorld* GetWorldChecked(ImSlate::ImSlateContext* Ctx);

namespace Internal
{
	template<typename T = SWidget>
	using TWidgetFactoryType = TFunctionRef<TSharedRef<T>(FItemSlotPod&)>;
	using FWidgetFactoryType = TWidgetFactoryType<>;

	IMSLATE_API SWidget* Item(ImStr InName, const FWidgetFactoryType& WidgetFactory);

	template<typename T = SWidget>
	inline auto Item(ImStr InName, const TWidgetFactoryType<T>& WidgetFactory)
	{
		static_assert(std::is_base_of<SWidget, T>::value, "err");
		return static_cast<T*>(Item(InName, *reinterpret_cast<const FWidgetFactoryType*>(&WidgetFactory)));
	}
}  // namespace Internal
}  // namespace ImSlate

#if WITH_EDITOR
#define GImSlate ImSlate::GetGImSlate()
#else
extern IMSLATE_API ImSlate::ImSlateContext* GImSlate;
#endif
