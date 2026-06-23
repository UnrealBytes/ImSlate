// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "ImSlateFwd.h"
#include "ImSlate3D.h"   // ImSlate::FImWorldPlacement (SetNextWindow3DTransform) — gated by IMSLATE3D_API

//
#include "ImSlateListDataInc.h"
#include "UnrealCompatibility.h"

namespace ImSlate
{
IMSLATE_API void				PushId(ImStr StrId);						// push string into the ID stack (will hash string).
IMSLATE_API void				PushId(const void* PtrId);					// push pointer into the ID stack (will hash pointer).
IMSLATE_API void				PushId(int32 IntId);						// push integer into the ID stack (will hash integer).
IMSLATE_API void				PopId();									// pop from the ID stack.
IMSLATE_API uint32				GetId(ImStr StrId);							// calculate unique ID (hash of whole ID stack + given parameter). 
IMSLATE_API uint32				GetId(const void* PtrId);					// e.g. if you want to query into ImSlateStorage yourself

struct ImScopeId
{
	ImScopeId(ImStr StrId) { PushId(StrId); }
	ImScopeId(int32 IntId) { PushId(IntId); }
	ImScopeId(const void* PtrId) { PushId(PtrId); }
	~ImScopeId() { PopId(); }
};
#define Z_IMSLATE_IMPL__(s1, s2) s1##s2
#define Z_IMSLATE_IMPL_(s1, s2) Z_IMSLATE_IMPL__(s1, s2)

#define IM_SLATE_SCOPE(x) ImSlate::ImScopeId Z_IMSLATE_IMPL_(ImSlateScope_, __LINE__)(x)

class IMSLATE_API ImSlateTicker
{
public:
	using FOnTickWithWorld = TDelegate<void(float, UWorld*)>;
	using FOnTick = TDelegate<void(float)>;
	static TSharedPtr<void> BindDelegate(FOnTickWithWorld Callback, UWorld* InWorld = GWorld);
	static TSharedPtr<void> BindDelegate(FOnTick Callback, UWorld* InWorld = GWorld);

public:
	ImSlateTicker(FOnTickWithWorld Callback, UWorld* InWorld = GWorld) { Handle = BindDelegate(MoveTemp(Callback), InWorld); }
	ImSlateTicker(FOnTick Callback, UWorld* InWorld = GWorld) { Handle = BindDelegate(MoveTemp(Callback), InWorld); }
	void ClearDelegate() { Handle.Reset(); }

private:
	TSharedPtr<void> Handle;
};

IMSLATE_API void				SetCurrentViewport(ImSlateWindow* CurrentWindow, ImSlateViewport* InViewport);
IMSLATE_API ImSlateViewport*	GetWindowViewport();                        // get viewport currently associated to the current window.

// Windows
IMSLATE_API bool				Begin(ImStr Name, bool* bOpen = nullptr, ImWindowFlags Flags = 0, int32 Id = 0);
IMSLATE_API void				End();

ImSlateWindow*					GetCurrentWindowRead();
ImSlateWindow*					GetCurrentWindow();

// Window manipulation
// next window : before Begin()
IMSLATE_API void				SetNextWindowPos(const ImVec2& Pos, ImSlateCond Cond = 0, const ImVec2& Pivot = ImVec2(0, 0));
IMSLATE_API void				SetNextWindowSize(const ImVec2& InSize, ImSlateCond Cond = 0);
IMSLATE_API void				SetNextContentSize(const ImVec2& InSize, ImSlateCond Cond = 0);
IMSLATE_API void				SetNextWindowCollapsed(bool bCollapsed, ImSlateCond Cond = 0);
IMSLATE_API void				SetNextWindowTopmost(bool Topmost, ImSlateCond Cond = 0);
IMSLATE_API void				SetNextWindowFocus();
IMSLATE_API void				SetNextWindowBgAlpha(float Alpha, ImSlateCond Cond = 0);
IMSLATE_API void				SetNextWindowBgColor(const FLinearColor& InColor, ImSlateCond Cond = 0);
IMSLATE_API void				SetNextWindowTitle(const FText& InTitle, ImSlateCond Cond = 0);

#if defined(IMSLATE3D_API)
// Project the NEXT Begin()'d window's content onto a 3D world plane (perspective trapezoid that tracks the
// scene camera), instead of drawing it flat in 2D. Call each frame before Begin() to keep the window 3D;
// stop calling it and the window reverts to normal 2D. The window is pinned at WorldLocation by its centre
// (Pivot 0.5,0.5 — use the FImWorldPlacement overload for a different pivot).
//   WorldRotation == ZeroRotator (the default)  -> BILLBOARD: the plane always faces the camera.
//   WorldRotation != zero                       -> fixed world orientation (no camera tracking of facing).
IMSLATE_API void				SetNextWindow3DTransform(const FVector& WorldLocation,
												const FRotator& WorldRotation = FRotator::ZeroRotator,
												float WorldScale = 1.0f);
// Same, plus the anchor Pivot (normalized 0..1: which point of the window is pinned at WorldLocation;
// 0.5,0.5 = centre, 0,0 = top-left, 1,1 = bottom-right).
IMSLATE_API void				SetNextWindow3DTransform(const FVector& WorldLocation, const FVector2f& Pivot,
												const FRotator& WorldRotation = FRotator::ZeroRotator);
IMSLATE_API void				SetNextWindow3DTransform(const ImSlate::FImWorldPlacement& Placement);
#endif

// Set current window properties (call after Begin())
IMSLATE_API void				SetCurrentWindowColorAndOpacity(const FLinearColor& InColor);
IMSLATE_API void				SetCurrentWindowForegroundColor(const FSlateColor& InColor);
IMSLATE_API void				SetCurrentWindowContentScale(const FVector2D& InScale);
IMSLATE_API void				SetNextWindowResizeCallback(ImSlateResizeCallback CustomCallback);

IMSLATE_API void				SetNextWindowContentSize(const ImVec2& InSize);
IMSLATE_API void				SetNextWindowViewport(ImSlateId ViewportId, ImSlateCond Cond);			// set next window viewport

// Windows Utilities
//  current window : the window we are appending into while inside a Begin()/End() block.
IMSLATE_API bool				IsWindowValid(ImStr Name);
IMSLATE_API bool				IsWindowAppearing();
IMSLATE_API bool				IsWindowCollapsed();
IMSLATE_API bool				IsWindowFocused(ImSlateFocusedFlags Flags=0); // is current window focused? or its root/child, depending on flags. see flags for options.
IMSLATE_API bool				IsWindowHovered(ImSlateFocusedFlags Flags=0); // is current window hovered (and typically: not blocked by a popup/modal)? see flags for options. NB: If you are trying to check whether your mouse should be dispatched to imgui or to your app, you should use the 'io.WantCaptureMouse' boolean for that! Please read the FAQ!

IMSLATE_API float				GetWindowDpiScale();                        // get DPI scale currently associated to the current window's viewport.
IMSLATE_API ImVec2				GetWindowPos();                             // get current window position in screen space (useful if you want to do your own drawing via the DrawList API)
IMSLATE_API ImVec2				GetWindowSize();                            // get current window size
IMSLATE_API ImVec2				GetWindowContentSize();                     // get current window content size
IMSLATE_API float				GetWindowByAlpha();                         // get current window size
IMSLATE_API float				GetWindowWidth();                           // get current window width (shortcut for GetWindowSize().x)
IMSLATE_API float				GetWindowHeight();                          // get current window height (shortcut for GetWindowSize().y)

IMSLATE_API ImVec2				GetMainViewportSize();                      // local size of the main (game) viewport; (0,0) if not laid out yet. Use to center a window before Begin().

IMSLATE_API void				SetWindowPos(ImStr Name, const ImVec2& Pos, ImSlateCond Cond = 0);			// set named window position.
IMSLATE_API void				SetWindowSize(ImStr Name, const ImVec2& InSize, ImSlateCond Cond = 0);		// set named window size. set axis to 0.0f to force an auto-fit on this axis.
IMSLATE_API void				SetWindowCollapsed(ImStr Name, bool collapsed, ImSlateCond Cond = 0);		// set named window collapsed state
IMSLATE_API void				SetWindowTopmost(ImStr Name, bool topmost, ImSlateCond Cond = 0);			// set named window collapsed state
IMSLATE_API void				SetWindowFocus(ImStr Name, ImSlateCond Cond = 0);							// set named window to be focused / top-most. use NULL to remove focus.
IMSLATE_API void				SetWindowBgAlpha(ImStr Name, float alpha, ImSlateCond Cond = 0);			// set named window collapsed state
IMSLATE_API void				SetWindowTitle(ImStr Name, const FText& InTitle, ImSlateCond Cond = 0);		// set named window title
IMSLATE_API void				SetCurrentWindowTitle(TFunctionRef<FText()> InTitle, ImSlateCond Cond = 0);

// Layout Utilities
IMSLATE_API void				RightAlign(int32 Col);
IMSLATE_API void				StretchVal(float Val);
IMSLATE_API void				SameLine();															// call between widgets or groups to layout them horizontally. X position given in window coordinates.
IMSLATE_API void				Spacing(float Val = 6.f);											// add vertical spacing.
IMSLATE_API void				Dummy(const ImVec2& InSize);										// add a dummy item of given size. unlike InvisibleButton(), Dummy() won't take the mouse click or be navigable into.
IMSLATE_API void				Indent(float indent_w = 0.0f);										// move content position toward the right, by indent_w, or style.IndentSpacing if indent_w <= 0
IMSLATE_API void				Unindent(float indent_w = 0.0f);									// move content position back to the left, by indent_w, or style.IndentSpacing if indent_w <= 0

IMSLATE_API void				NewLine();															// undo a SameLine() or force a new line when in an horizontal-layout context.
IMSLATE_API void				Separator();														// horizontal separator
IMSLATE_API bool				FoldLine(ImStr Label, const FText& InText, float InHeight = 0.f);

// BeginFold/EndFold: auto-indent fold regions, returns true if expanded (draw children then call EndFold)
// Usage: if (ImSlate::BeginFold("id", TEXT("Category"))) { /* children */ ImSlate::EndFold(); }
IMSLATE_API bool				BeginFold(ImStr Label, const FText& InText, float IndentWidth = 16.f);
IMSLATE_API void				EndFold();

// BeginGroup/EndGroup: horizontally-consecutive controls between Begin/End share one group background color.
// Always returns true (mirrors BeginFold's if-usage). Usage:
//   if (ImSlate::BeginGroup("id", Color)) { Button(); SameLine(); InputText(); ImSlate::EndGroup(); }
IMSLATE_API bool				BeginGroup(ImStr Id, const FLinearColor& GroupColor);
IMSLATE_API void				EndGroup();

// TitleLine: standalone title bar control (use with ImSlateWindowFlags_NoTitleLine)
// Returns true if close button was clicked (when bOpen != nullptr)
IMSLATE_API bool				TitleLine(ImStr Label, const FText& InText, bool* bOpen = nullptr);

IMSLATE_API void				SetNextItemAspectRatio(float InRatio);
IMSLATE_API void				SetNextItemFillWidth(float InFactor);
IMSLATE_API void				SetNextItemAutoWidth();
IMSLATE_API void				SetNextItemMinWidth(float InVal);
IMSLATE_API void				SetNextItemMinHeight(float InVal);
IMSLATE_API void				SetNextItemMaxWidth(float InVal);
IMSLATE_API void				SetNextItemMaxHeight(float InVal);
IMSLATE_API void				SetNextItemMinSize(float InWidth, float InHeight);
IMSLATE_API void				SetNextItemMaxSize(float InWidth, float InHeight);
IMSLATE_API void				SetNextItemFixSize(float InWidth, float InHeight);
IMSLATE_API void				SetNextItemTooltip(const FText& InText);

// Virtual Keyboard
IMSLATE_API void				SetVirtualKeyboardEnabled(bool bEnabled);
IMSLATE_API bool				IsVirtualKeyboardVisible();
IMSLATE_API bool				KeyboardButton(ImStr Label, const struct FVirtualKeyboardShowParams& Params, const ImVec2& InSize = ImVec2(0, 0));

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Measure
IMSLATE_API ImVec2			SimpleMeasureText(float Width, const FString& InText, const FSlateFontInfo& FontInfo, const float FontScale = 1.f, const ImVec2& LocalShadowOffset = ImVec2::ZeroVector, const TOptional<float>& MinDesiredWidth = {});

// Displays
IMSLATE_API bool			Text(ImStr Label, const FText& InText, const ImVec2& InSize = ImVec2(0, 0), bool bAutoWrapText = false);
inline		bool			Text(ImStr Label, const FString& InStr, const ImVec2& InSize = ImVec2(0, 0), bool bAutoWrapText = false){ return Text(Label, FText::FromString(InStr), InSize, bAutoWrapText); }
IMSLATE_API bool			Image(ImStr Label, UObject* InTexture, const ImVec2& InSize = ImVec2(0, 0)); // Image

// Inputs
IMSLATE_API bool			InputText(ImStr Label, FString& InStr, const ImVec2& InSize = ImVec2(0, 0), ImSlateInputTextFlags_ Flags = ImSlateInputTextFlags_None, ImStr HistoryKey = "", const FImInputNumericSpec* Numeric = nullptr);

// SearchBox: InputText with history and suggestion dropdown
IMSLATE_API bool			SearchBox(ImStr Label, FString& InOutStr, const TArray<FString>* Suggestions = nullptr, TFunction<void(const FString&, TArray<FString>&)> SuggestionCallback = nullptr, const ImVec2& InSize = ImVec2(0, 0), bool bShowKeyboardButton = false);

IMSLATE_API bool			Button(ImStr Label, const ImVec2& InSize = ImVec2(0, 0));
IMSLATE_API bool			TextButton(ImStr Label, const FText& InText, const ImVec2& InSize = ImVec2(0, 0));

inline		bool			TextButton(ImStr Label, const FString& InStr, const ImVec2& InSize = ImVec2(0, 0)) { return TextButton(Label, FText::FromString(InStr), InSize); }
IMSLATE_API bool			ImageButton(ImStr Label, UObject* InTexture, const ImVec2& InSize = ImVec2(0, 0));

#if defined(IMSLATE3D_API)
// 3D move-gizmo arrow as a pure composition: a transparent in-plane arrow (SImSlate3DPlaneArrow) placed as
// an item in the CURRENT 3D-transformed window (SetNextWindow3DTransform). Drag it to translate Pos along
// WorldAxis; writes the new Pos back and returns true on frames it changed. Per-pixel arrow shape — clicks
// in the transparent gaps fall through. Size the window to the arrow so it reads as a standalone arrow.
// Call inside a Begin/End window that has a 3D transform set.
IMSLATE_API bool			GizmoArrow(ImStr Id, FVector& Pos, const FVector& WorldAxis, const FLinearColor& Color = FLinearColor::Red);
#endif

// AccentColor tints the checked-state fill of the mark (blue by default). Pass e.g. green to set an
// "enable / is-set" toggle apart from a regular value checkbox.
IMSLATE_API bool			CheckBox(ImStr Label, bool& bIsChecked, const ImVec2& InSize = ImVec2(0, 0), const FLinearColor& AccentColor = FLinearColor(0.10f, 0.45f, 0.90f, 1.f));

IMSLATE_API ImSliderStatus_	InputFloat(ImStr Label, decltype(FVector::ZeroVector.X)& ValRef, float ValMin = FLT_MIN, float ValMax = FLT_MAX, float step = 0.0f, float step_fast = 0.0f, int32 NumDecimals = 3, bool bResetState = false, ImSlateFloatFlags_ flags = ImSlateFloatFlags_None, const ImVec2& InSize = ImVec2(0, 0));

IMSLATE_API ImSliderStatus_	NumericFloat(ImStr Label, TOptional<float>& ValRef, float ValMin = FLT_MIN, float ValMax = FLT_MAX, float SliderMin = FLT_MIN, float SliderMax = FLT_MAX, float Delta = 0.0f, float SliderExponent = 0.0f, TOptional<int32> MinDigits = {}, TOptional<int32> MaxDigits = {}, ImSlateInputTextFlags_ flags = ImSlateInputTextFlags_None, const ImVec2& InSize = ImVec2(0, 0));
IMSLATE_API ImSliderStatus_	NumericInt(ImStr Label, TOptional<int32>& ValRef, int32 ValMin = INT_MIN, int32 ValMax = INT_MAX, int32 SliderMin = INT_MIN, int32 SliderMax = INT_MAX, int32 Delta = 0, int32 SliderExponent = 0, ImSlateInputTextFlags_ flags = ImSlateInputTextFlags_None, const ImVec2& InSize = ImVec2(0, 0));


// Containers
IMSLATE_API bool			ListBox(ImStr Label, int32& InOutCurIdx, const TArray<FString>& InSource, const ImVec2& InSize = ImVec2(0, 0)); // ListBox
IMSLATE_API	bool			CheckBox(ImStr Label, ECheckBoxState& InOutCheckState, const ImVec2& InSize = ImVec2(0, 0));
IMSLATE_API bool			VirtualList(ImStr Label, const TSharedRef<IImSlateListData>& InDataStore, const ImVec2& InSize = ImVec2(0, 0));

// Composite API
IMSLATE_API ImSliderStatus_	InputVector(ImStr Label, FVector& ValRef, FVector ValMin = FVector(FLT_MIN, FLT_MIN, FLT_MIN) , FVector ValMax = FVector(FLT_MAX, FLT_MAX, FLT_MAX), float step = 0.0f, float step_fast = 0.0f, int32 NumDecimals = 3, bool bResetState = false, ImSlateInputTextFlags_ flags = ImSlateInputTextFlags_None, const ImVec2& InSize = ImVec2(0, 0));

IMSLATE_API ImSliderStatus_	InputRotator(ImStr Label, FRotator& ValRef, FRotator ValMin = FRotator(-180.f, -180.f, -180.f) , FRotator ValMax = FRotator(180.f, 180.f, 180.f), float step = 0.0f, float step_fast = 0.0f, int32 NumDecimals = 3, bool bResetState = false, ImSlateInputTextFlags_ flags = ImSlateInputTextFlags_None, const ImVec2& InSize = ImVec2(0, 0));

IMSLATE_API bool ComboBox(ImStr Label, int32& InOutCurIdx, const TSharedRef<class FImListDataComboImpl>& InDataStore, ImSlateComboFlags_ Flags = ImSlateComboFlags_None, const ImVec2& InSize = ImVec2(0, 0));

IMSLATE_API bool ComboBoxForEnum(ImStr Label, int64& InOutCurIdx, UEnum* EnumPtr, ImSlateComboFlags_ Flags = ImSlateComboFlags_None, const ImVec2& InSize = ImVec2(0, 0));

// Asset/Class Pickers
// Editor: wraps SObjectPropertyEntryBox/SClassPropertyEntryBox from PropertyEditor
// Non-Editor: falls back to InputText with path string
IMSLATE_API bool AssetPicker(ImStr Label, FSoftObjectPath& InOutPath, UClass* FilterClass = nullptr, const ImVec2& InSize = ImVec2(0, 0));
IMSLATE_API bool ClassPicker(ImStr Label, FSoftClassPath& InOutPath, UClass* MetaClass = nullptr, const ImVec2& InSize = ImVec2(0, 0));

// clang-format on
}  // namespace ImSlate
