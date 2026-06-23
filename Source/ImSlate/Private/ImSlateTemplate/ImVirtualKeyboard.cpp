// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "ImSlateTemplate/ImVirtualKey.h"

#include "ImSlatePrivate.h"
#include "SImViewportGame.h"
#include "ImSlateInputHistory.h"
#include "SImSlateWindow.h"
#include "ImSlateTemplate/ImEditableText.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/InputSettings.h"  // UInputSettings::bUseMouseForTouch (desktop real-mouse drag)
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"  // FPlatformApplicationMisc::ClipboardPaste
#include "Framework/Text/TextLayout.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SOverlay.h"
#include "Styling/CoreStyle.h"          // FCoreStyle::Get / FButtonStyle
#include "Brushes/SlateColorBrush.h"    // FSlateColorBrush (selected-suggestion fill)

namespace ImSlate
{

bool GForceVirtualKeyboard = false;
static FAutoConsoleVariableRef CVar_ForceVirtualKeyboard(
	TEXT("imslate.ForceVirtualKeyboard"),
	GForceVirtualKeyboard,
	TEXT("Force virtual keyboard on desktop for testing."));

// The global GetImSlateEffectiveScale() is tuned for panels; on high-PPI phones the engine
// divides the physical density by ContentScaleFactor (r.MobileContentScaleFactor, e.g. 2.68 on
// iPhone 17), so the effective scale lands ~1.8 and the on-screen keys feel too small. Rather
// than inflate ALL panels globally, the keyboard gets its own extra multiplier on top.
//
// Default is MOBILE-ONLY: on desktop the keys are already plenty large (effective scale already
// carries the monitor DPI, which on a high-PPI editor monitor can be 2.0~2.7), so the extra ×1.5
// just makes the keyboard balloon to near full-screen. Desktop defaults to 1.0; mobile keeps the
// 1.5 compensation. The CVar still overrides on either platform.
float GImSlateKeyboardScale = (PLATFORM_ANDROID || PLATFORM_IOS) ? 1.5f : 1.0f;
static FAutoConsoleVariableRef CVar_ImSlateKeyboardScale(
	TEXT("imslate.KeyboardScale"),
	GImSlateKeyboardScale,
	TEXT("Extra size multiplier for the virtual keyboard only (on top of imslate.LayoutScale). 1.0 = same as panels. Default: mobile 1.5, desktop 1.0."));

// ---- Plan A: keyboard height cap ----
// The keyboard fills the whole viewport, so its cached local height == viewport height. We cache
// it (updated each Tick) plus the keyboard's per-unit-scale logical height (= measured content
// height / the scale it was measured at). GetKbScale() then clamps the scale down so the keyboard
// never exceeds GKeyboardMaxHeightFraction of the viewport. This SHRINKS the keys to fit (no
// clipping), unlike an SBox MaxDesiredHeight which would just crop the bottom rows off.
static float GCachedViewportHeight = 0.f;          // logical px (== keyboard's cached local Y)
static float GCachedKeyboardUnitHeight = 0.f;      // content height at scale=1 (content / scale)

// Hard cap on the WHOLE keyboard (preview + keys) as a fraction of the available viewport height.
// On a large/high-DPI screen (e.g. editor at full size) the DPI-driven scale can balloon the
// keyboard to nearly full-screen; this clamps it so it never eats more than this fraction.
float GKeyboardMaxHeightFraction = 0.5f;
static FAutoConsoleVariableRef CVar_KeyboardMaxHeightFraction(
	TEXT("imslate.KeyboardMaxHeightFraction"),
	GKeyboardMaxHeightFraction,
	TEXT("Max keyboard height as a fraction of the viewport height (0.5 = half-screen). 0 disables the cap."));

// Keyboard-local scale: panel effective scale × keyboard multiplier. ALL keyboard sizing
// (keys, spacing, popups, height caps) AND the key widgets' ComputeDesiredSize / gesture
// thresholds (ImVirtualKey.cpp) use this so the whole keyboard scales uniformly. Exported via
// ImSlateFactory.h as GetImSlateKeyboardScale; GetKbScale is the file-local short alias.
static float GetKbScale()
{
	const float BaseScale = GetImSlateEffectiveScale() * FMath::Max(GImSlateKeyboardScale, 0.1f);
	float Scale = BaseScale;

	// Cap the keyboard to a fraction of the viewport height by clamping the scale. We need both the
	// viewport height and the keyboard's natural per-unit-scale height; both are measured at runtime
	// (see Tick). Until measured, no cap is applied.
	if (GKeyboardMaxHeightFraction > 0.f && GCachedViewportHeight > 0.f && GCachedKeyboardUnitHeight > 0.f)
	{
		const float MaxH = GCachedViewportHeight * GKeyboardMaxHeightFraction;
		const float MaxScale = MaxH / GCachedKeyboardUnitHeight;
		Scale = FMath::Min(Scale, MaxScale);
	}
	Scale = FMath::Max(Scale, 0.1f);
	return Scale;
}

// Exported alias (declared in ImSlateFactory.h) so the key widgets in ImVirtualKey.cpp can use the
// SAME scale as the keyboard layout — otherwise keys sized by GetImSlateEffectiveScale while the
// layout/spacing used GetKbScale, making keys too small relative to their slots.
IMSLATE_API float GetImSlateKeyboardScale()
{
	return GetKbScale();
}

// Minimum edge margin (logical px, ×KbScale) applied on top of the OS safe area. Default 0:
// on desktop the OS safe inset is 0, so the keyboard sits flush to the edge (no extra gap). On
// mobile the OS safe inset already provides the gap. Raise this if you want a guaranteed margin
// on all platforms. NOTE: it's a MAX with the safe inset, NOT added — so a large mobile safe
// inset is used as-is, and this only kicks in where the safe inset is smaller (e.g. desktop).
float GImSlateKeyboardEdgeMargin = 0.f;
static FAutoConsoleVariableRef CVar_ImSlateKeyboardEdgeMargin(
	TEXT("imslate.KeyboardEdgeMargin"),
	GImSlateKeyboardEdgeMargin,
	TEXT("Min keyboard edge margin in logical px (×KeyboardScale), max'd with the OS safe area. 0 = flush on desktop."));

// Horizontal preview-scrub step tuning. Base step threshold = CellWidth × WidthMult (the "speed" knob):
//  - WidthMult: how many CELL WIDTHS of travel make one column step (1.0 = one cell per step; lower = quicker,
//    higher = more travel per step). Same-direction consecutive steps use exactly this — fully responsive.
//  - DebouncePx: a REVERSE-only dead zone (logical px, ×KbScale) added ONLY when the next step would reverse
//    the previous step's direction, so the selection doesn't flicker back and forth across a column boundary.
//    Forward (same-direction) stepping is unaffected.
float GImSlatePreviewHWidthMult = 1.0f;
static FAutoConsoleVariableRef CVar_ImSlatePreviewHWidthMult(
	TEXT("imslate.PreviewHWidthMult"),
	GImSlatePreviewHWidthMult,
	TEXT("Cell-width multiple of horizontal travel per preview digit-column step (the speed knob; 1.0 = 1 cell)."));

// Reverse-direction dead zone (logical px, ×scale) shared by ALL continuous step gestures (preview H/V scrub
// AND SImSlateKey step-drag): added only when the next step would reverse the previous step's direction, so
// nothing flickers back and forth at a boundary. Same-direction stepping is unaffected. Global (not static):
// ImVirtualKey.cpp uses it via the extern in ImVirtualKeyboard.h. Used by ImSlateStepAccumulate below.
float GImSlateStepReverseDebouncePx = 8.0f;
static FAutoConsoleVariableRef CVar_ImSlateStepReverseDebouncePx(
	TEXT("imslate.StepReverseDebouncePx"),
	GImSlateStepReverseDebouncePx,
	TEXT("Extra dead zone in logical px (×scale) to REVERSE any continuous step-drag direction (anti-flicker)."));

// Shared stepping helper — see header doc. Fires Pos/Neg per BaseStep of accumulated travel, with a
// reverse-direction dead zone. Returns net steps (anchor-based callers advance their anchor by this).
int32 ImSlateStepAccumulate(float& Accum, int32& InOutLastDir, float BaseStep, float ScalePx,
	TFunctionRef<void()> PosStep, TFunctionRef<void()> NegStep)
{
	if (BaseStep <= 0.f)
		return 0;
	const float Dead = FMath::Max(GImSlateStepReverseDebouncePx, 0.f) * FMath::Max(ScalePx, 0.f);
	int32 Net = 0;
	// Positive direction: a reverse is when the last fired step was negative → require the extra dead zone.
	while (Accum > (BaseStep + (InOutLastDir < 0 ? Dead : 0.f)))
	{
		PosStep();
		Accum -= BaseStep + (InOutLastDir < 0 ? Dead : 0.f);
		InOutLastDir = +1;
		++Net;
	}
	while (Accum < -(BaseStep + (InOutLastDir > 0 ? Dead : 0.f)))
	{
		NegStep();
		Accum += BaseStep + (InOutLastDir > 0 ? Dead : 0.f);
		InOutLastDir = -1;
		--Net;
	}
	return Net;
}

// Extra LEFT/RIGHT margin on mobile when the screen is in LANDSCAPE, as a fraction of screen
// width. Phones in landscape put cutouts/rounded corners on the left & right; this pulls the
// keyboard edges further in so keys stay reachable and unobscured. Applied via max() like the
// other margins (so it only widens, never shrinks). 0 disables.
float GImSlateKeyboardLandscapeSideFrac = 0.04f;
static FAutoConsoleVariableRef CVar_ImSlateKeyboardLandscapeSideFrac(
	TEXT("imslate.KeyboardLandscapeSideFrac"),
	GImSlateKeyboardLandscapeSideFrac,
	TEXT("Mobile landscape extra left/right keyboard margin as a fraction of screen width (0 = off)."));

// Keyboard edge margin as an FMargin: max(OS safe-area inset, EdgeMargin[, landscape side]) per
// side. Used by BOTH the keyboard body and the sibling suggestion overlay so they line up. Top is
// not padded (the keyboard is bottom-anchored). Returns local Slate units.
static FMargin GetKbSafeMargin()
{
	FMargin Safe(0.f);
	if (FSlateApplication::IsInitialized())
		FSlateApplicationBase::Get().GetSafeZoneSize(Safe, FVector2f::ZeroVector);
	const float E = FMath::Max(GImSlateKeyboardEdgeMargin, 0.f) * GetKbScale();

	// Mobile + landscape: add an extra left/right inset (fraction of screen width).
	float SideExtra = 0.f;
#if PLATFORM_IOS || PLATFORM_ANDROID
	if (GImSlateKeyboardLandscapeSideFrac > 0.f && FSlateApplication::IsInitialized())
	{
		const FVector2D Screen = FSlateApplicationBase::Get().GetPreferredWorkArea().GetSize();
		if (Screen.X > Screen.Y)  // landscape
			SideExtra = Screen.X * GImSlateKeyboardLandscapeSideFrac;
	}
#endif

	// max (not add): mobile safe inset used as-is; E / SideExtra only fill in where it's smaller.
	const float Left  = FMath::Max3(Safe.Left,  E, SideExtra);
	const float Right = FMath::Max3(Safe.Right, E, SideExtra);
	return FMargin(Left, 0.f, Right, FMath::Max(Safe.Bottom, E));
}

TSharedPtr<SImSlateVirtualKeyboard> SImSlateVirtualKeyboard::Get()
{
	if (!GImSlate) return nullptr;
	for (auto& Viewport : GImSlate->Viewports)
	{
		if (Viewport->IsGameViewport())
		{
			auto& GameVP = static_cast<SImViewportGame&>(Viewport.Get());
			auto Kb = GameVP.GetOrCreateVirtualKeyboard();
			if (Kb.IsValid())
				GameVP.EnsureKeyboardInViewport();
			return Kb;
		}
	}
	return nullptr;
}


TSharedPtr<SImSlateVirtualKeyboard> SImSlateVirtualKeyboard::Get(const UObject* WorldContext)
{
	// Resolve the context for the caller's world (PIE-safe). Under "Play As Client" the global
	// context may be the server world, which has no game viewport — using it would return null.
	ImSlateContext* Ctx = WorldContext ? GetGImSlateForWorld(WorldContext) : GImSlate;
	if (!Ctx) return Get(); // fall back to the global lookup

	for (auto& Viewport : Ctx->Viewports)
	{
		if (Viewport->IsGameViewport())
		{
			auto& GameVP = static_cast<SImViewportGame&>(Viewport.Get());
			auto Kb = GameVP.GetOrCreateVirtualKeyboard();
			if (Kb.IsValid())
				GameVP.EnsureKeyboardInViewport();
			return Kb;
		}
	}
	return Get(); // context had no game viewport; try the global as a last resort
}

void SImSlateVirtualKeyboard::HideForWorld(const UObject* WorldContext)
{
	// Non-creating: only touch a keyboard that already exists for this world. Never go through
	// Get()/GetOrCreate/EnsureKeyboardInViewport — during teardown the overlay is gone and that
	// would trip ensure(PinnedViewportOverlayWidget.IsValid()).
	ImSlateContext* Ctx = WorldContext ? GetGImSlateForWorld(WorldContext) : GImSlate;
	if (!Ctx) return;
	for (auto& Viewport : Ctx->Viewports)
	{
		if (!Viewport->IsGameViewport())
			continue;
		auto& GameVP = static_cast<SImViewportGame&>(Viewport.Get());
		if (TSharedPtr<SImSlateVirtualKeyboard> Kb = GameVP.GetExistingVirtualKeyboard())
		{
			if (Kb->IsShowing())
				Kb->Hide(false);
		}
	}
}

bool SImSlateVirtualKeyboard::HideOpenKeyboard(bool bCommit)
{
	// Non-creating sweep: only touch keyboards that already exist. Don't go through Get() — it would
	// lazily create + attach a keyboard.
	if (!GImSlate)
		return false;
	bool bHidAny = false;
	for (auto& Viewport : GImSlate->Viewports)
	{
		if (!Viewport->IsGameViewport())
			continue;
		auto& GameVP = static_cast<SImViewportGame&>(Viewport.Get());
		if (TSharedPtr<SImSlateVirtualKeyboard> Kb = GameVP.GetExistingVirtualKeyboard())
		{
			if (Kb->IsShowing())
			{
				Kb->Hide(bCommit);  // bCommit: true=commit current text, false=discard
				bHidAny = true;
			}
		}
	}
	return bHidAny;
}

bool SImSlateVirtualKeyboard::ShouldUseVirtualKeyboard(const SWidget* InWidget)
{
	// Desktop testing override
	if (GForceVirtualKeyboard)
		return Get().IsValid();

	if (!Get().IsValid())
		return false;

	// Walk up the parent chain to find the owning ImSlate window.
	// In-viewport windows (game viewport overlay) have no OS window/IME → use virtual keyboard.
	// Floated-out windows (host viewport = real SWindow) have OS keyboard → don't.
	if (InWidget)
	{
		TSharedPtr<SWidget> Cur = const_cast<SWidget*>(InWidget)->AsShared();
		while (Cur.IsValid())
		{
			if (Cur->GetType() == TEXT("SImSlateWindow"))
			{
				auto* Window = static_cast<SImSlateWindow*>(Cur.Get());
				return Window->IsViewportGame();
			}
			Cur = Cur->GetParentWidget();
		}
	}

	// No widget context: fall back to platform check
	return FPlatformApplicationMisc::RequiresVirtualKeyboard();
}

// ==================== Layout Data ====================

static FVirtualKeyDef MakeCharKey(const FString& Lower, const FString& Upper, const FString& SwipeUp = FString())
{
	FVirtualKeyDef Def;
	Def.Label = Lower;
	Def.ShiftLabel = Upper;
	Def.Value = Lower;
	Def.ShiftValue = Upper;
	Def.Swipe.Up.Label = SwipeUp;
	Def.Action = EVirtualKeyAction::Char;
	return Def;
}

FVirtualKeyDef SImSlateVirtualKeyboard::MakeBackspaceKey(float Width)
{
	FVirtualKeyDef Def;
	Def.Label = TEXT("Del");
	Def.Action = EVirtualKeyAction::Backspace;
	Def.WidthMultiplier = Width;
	// Swipe up = Clr. Text mode: clear everything left of caret. Number mode: reset to the
	// in-range value nearest zero (0, or the clamp bound closest to 0).
	Def.Swipe.Up.Label = TEXT("Clr");
	Def.Swipe.Up.Callback.BindLambda([this]() {
		if (KeyboardType == EImKeyboardType::Number)
		{
			ClearNumericValue();
			return;
		}
		// Clr removes the whole run left of the caret — one undo snapshot of the full value restores it.
		PushBackspaceUndo();   // snapshot full value first → undo restores the whole run
		CurrentText.RemoveAt(0, CursorPosition);
		CursorPosition = 0;
		ClearDigitHighlight();
		UpdatePreview(); UpdateSuggestions();
	});
	Def.Swipe.Down.Label = TEXT("Hide");
	Def.Swipe.Down.Callback.BindLambda([this]() { Hide(false); });
	// Left/Right are CONTINUOUS (step-drag): left = delete one char per StepW, right = undo one. Uses the
	// same four-way step path + scrolling ruler as the cursor key (rendered inside the popup), replacing
	// the old StepDragVisual "◀ Del | Undo ▶" bar.
	Def.Swipe.Left.Label  = TEXT("\x232B");  Def.Swipe.Left.bStep  = true;  // ⌫ delete
	Def.Swipe.Right.Label = TEXT("\x21BA");  Def.Swipe.Right.bStep = true;  // ↺ undo
	Def.Swipe.OnStep = [this](EImSwipeDir D)
	{
		if (D == EImSwipeDir::Left)
		{
			if (DeleteBackwardWithUndo()) { UpdatePreview(); UpdateSuggestions(); }
		}
		else if (D == EImSwipeDir::Right)
		{
			if (BackspaceUndoStack.Num() > 0) { UndoBackspaceFromStack(); UpdatePreview(); UpdateSuggestions(); }
		}
	};
	return Def;
}

// Space key. Left/Right are CONTINUOUS (step-drag) → move the caret one char per StepW, using the same
// four-way step path + in-popup scrolling ruler as the cursor/Del keys (replaces the old StepDragVisual
// "◀ Cursor | Cursor ▶" bar). A plain tap still inserts a space (Action == Space, handled on release).
FVirtualKeyDef SImSlateVirtualKeyboard::MakeSpaceKey(float Width)
{
	FVirtualKeyDef Def;
	Def.Label = TEXT("\x25C0 space \x25B6");
	Def.Action = EVirtualKeyAction::Space;
	Def.WidthMultiplier = Width;
	Def.Swipe.Left.Label  = TEXT("\x25C0");  Def.Swipe.Left.bStep  = true;
	Def.Swipe.Right.Label = TEXT("\x25B6");  Def.Swipe.Right.bStep = true;
	Def.Swipe.OnStep = [this](EImSwipeDir D)
	{
		if (D == EImSwipeDir::Left)       { MoveCursor(-1); UpdatePreview(); }
		else if (D == EImSwipeDir::Right) { MoveCursor(+1); UpdatePreview(); }
	};
	return Def;
}

static FVirtualKeyDef MakeActionKey(EVirtualKeyAction Action, const FString& Label, float Width = 1.f)
{
	FVirtualKeyDef Def;
	Def.Label = Label;
	Def.Action = Action;
	Def.WidthMultiplier = Width;
	return Def;
}

TArray<TArray<FVirtualKeyDef>> SImSlateVirtualKeyboard::GetQWERTYLayout()
{
	const TCHAR* Row1Chars[] = {TEXT("q"),TEXT("w"),TEXT("e"),TEXT("r"),TEXT("t"),TEXT("y"),TEXT("u"),TEXT("i"),TEXT("o"),TEXT("p")};
	const TCHAR* SwipeDigits[] = {TEXT("1"),TEXT("2"),TEXT("3"),TEXT("4"),TEXT("5"),TEXT("6"),TEXT("7"),TEXT("8"),TEXT("9"),TEXT("0")};

	TArray<TArray<FVirtualKeyDef>> Rows;

	// Row 1: qwertyuiop with swipe-up digits (no separate number row).
	{
		TArray<FVirtualKeyDef> Row;
		for (int32 i = 0; i < 10; ++i)
		{
			FString Lower = Row1Chars[i];
			Row.Add(MakeCharKey(Lower, Lower.ToUpper(), SwipeDigits[i]));
		}
		Rows.Add(MoveTemp(Row));
	}

	// Row 2: asdfghjkl with swipe-up symbols !@#$%&*()
	{
		const TCHAR* Chars[] = {TEXT("a"),TEXT("s"),TEXT("d"),TEXT("f"),TEXT("g"),TEXT("h"),TEXT("j"),TEXT("k"),TEXT("l")};
		const TCHAR* Syms[]  = {TEXT("!"),TEXT("@"),TEXT("#"),TEXT("$"),TEXT("%"),TEXT("&"),TEXT("*"),TEXT("("),TEXT(")")};
		TArray<FVirtualKeyDef> Row;
		for (int32 i = 0; i < 9; ++i)
		{
			FString Lower = Chars[i];
			Row.Add(MakeCharKey(Lower, Lower.ToUpper(), Syms[i]));
		}
		Rows.Add(MoveTemp(Row));
	}

	// Row 3: [Shift] zxcvbnm [Backspace] with swipe-up symbols ~:'-/.?
	{
		const TCHAR* Chars[] = {TEXT("z"),TEXT("x"),TEXT("c"),TEXT("v"),TEXT("b"),TEXT("n"),TEXT("m")};
		const TCHAR* Syms[]  = {TEXT("~"),TEXT(":"),TEXT("'"),TEXT("_"),TEXT("/"),TEXT("."),TEXT("?")};
		TArray<FVirtualKeyDef> Row;
		Row.Add(MakeActionKey(EVirtualKeyAction::Shift, TEXT("\x21E7"), 1.3f));
		for (int32 i = 0; i < 7; ++i)
		{
			FString Lower = Chars[i];
			auto Key = MakeCharKey(Lower, Lower.ToUpper(), Syms[i]);
			Row.Add(MoveTemp(Key));
		}
		// Symmetric Shift on the right end too. Delete moved to the preview row (left of Done),
		// shown for QWERTY/Number via the preview-row visibility logic.
		Row.Add(MakeActionKey(EVirtualKeyAction::Shift, TEXT("\x21E7"), 1.3f));
		Rows.Add(MoveTemp(Row));
	}

	return Rows;
}

TArray<FVirtualKeyDef> SImSlateVirtualKeyboard::GetBottomRow()
{
	// ToggleType + Done moved to the preview row.
	TArray<FVirtualKeyDef> Row;

	// Full-screen row spans 10 columns; keep the same . : space = 1 : 2 ratio.
	auto DotKey = MakeCharKey(TEXT("."), TEXT("."), TEXT(","));
	DotKey.LongPressChars = {TEXT(";"),TEXT("-"),TEXT("\""),TEXT("+"),TEXT("="),TEXT("\\"),TEXT("["),TEXT("]"),TEXT("<"),TEXT(">")};
	DotKey.WidthMultiplier = 3.34f;
	Row.Add(MoveTemp(DotKey));

	Row.Add(MakeSpaceKey(6.66f));
	return Row;
}

static FVirtualKeyDef MakeT9Key(const FString& Label, const FString& Num, const TArray<FString>& Chars)
{
	FVirtualKeyDef Def;
	Def.Label = Label.ToLower();
	Def.ShiftLabel = Label.ToUpper();
	Def.Value = Num;
	Def.Action = EVirtualKeyAction::Char;
	Def.WidthMultiplier = 2.f;

	if (Chars.Num() >= 1) Def.Swipe.Up.Label = Chars[0];
	if (Chars.Num() >= 2) Def.Swipe.Left.Label = Chars[1];
	if (Chars.Num() >= 3) Def.Swipe.Right.Label = Chars[2];
	if (Chars.Num() >= 4) Def.Swipe.Down.Label = Chars[3];

	for (const FString& Ch : Chars)
		Def.LongPressChars.Add(Ch);
	Def.LongPressChars.Add(Num);
	for (const FString& Ch : Chars)
		Def.LongPressChars.Add(Ch.ToUpper());

	return Def;
}

TArray<TArray<FVirtualKeyDef>> SImSlateVirtualKeyboard::GetT9Layout()
{
	TArray<TArray<FVirtualKeyDef>> Rows;

	// Row 1: .,?  ABC  DEF  [⌫]
	{
		TArray<FVirtualKeyDef> Row;

		FVirtualKeyDef PuncKey;
		PuncKey.Label = TEXT(".,?");
		PuncKey.Action = EVirtualKeyAction::Char;
		PuncKey.Value = TEXT("1");
		PuncKey.WidthMultiplier = 2.f;
		PuncKey.Swipe.Up.Label = TEXT(".");
		PuncKey.Swipe.Left.Label = TEXT(",");
		PuncKey.Swipe.Right.Label = TEXT("?");
		PuncKey.Swipe.Down.Label = TEXT("!");
		PuncKey.LongPressChars = {TEXT(";"),TEXT(":"),TEXT("'"),TEXT("1"),TEXT("\""),TEXT("-"),TEXT("/")};
		Row.Add(MoveTemp(PuncKey));

		Row.Add(MakeT9Key(TEXT("ABC"), TEXT("2"), {TEXT("a"),TEXT("b"),TEXT("c")}));
		Row.Add(MakeT9Key(TEXT("DEF"), TEXT("3"), {TEXT("d"),TEXT("e"),TEXT("f")}));
		Row.Add(MakeBackspaceKey(2.f));
		Rows.Add(MoveTemp(Row));
	}

	// Row 2: GHI  JKL  MNO  [⇧]
	{
		TArray<FVirtualKeyDef> Row;
		Row.Add(MakeT9Key(TEXT("GHI"), TEXT("4"), {TEXT("g"),TEXT("h"),TEXT("i")}));
		Row.Add(MakeT9Key(TEXT("JKL"), TEXT("5"), {TEXT("j"),TEXT("k"),TEXT("l")}));
		Row.Add(MakeT9Key(TEXT("MNO"), TEXT("6"), {TEXT("m"),TEXT("n"),TEXT("o")}));
		Row.Add(MakeActionKey(EVirtualKeyAction::Shift, TEXT("\x21E7"), 2.f));
		Rows.Add(MoveTemp(Row));
	}

	// Row 3: PQRS  TUV  WXYZ  [◀▶]
	{
		TArray<FVirtualKeyDef> Row;
		Row.Add(MakeT9Key(TEXT("PQRS"), TEXT("7"), {TEXT("p"),TEXT("q"),TEXT("r"),TEXT("s")}));
		{
			auto TuvKey = MakeT9Key(TEXT("TUV"), TEXT("8"), {TEXT("t"),TEXT("u"),TEXT("v")});
			TuvKey.Swipe.Down.Label = TEXT("0");
			Row.Add(MoveTemp(TuvKey));
		}
		Row.Add(MakeT9Key(TEXT("WXYZ"), TEXT("9"), {TEXT("w"),TEXT("x"),TEXT("y"),TEXT("z")}));

		{
			auto CursorKey = MakeActionKey(EVirtualKeyAction::Left, TEXT("\x25C0 \x25B6"), 2.f);
			CursorKey.Swipe.Left.Label = TEXT("\x25C0");  CursorKey.Swipe.Left.bStep = true;
			CursorKey.Swipe.Right.Label = TEXT("\x25B6"); CursorKey.Swipe.Right.bStep = true;
			CursorKey.Swipe.OnStep = [this](EImSwipeDir D) {
				if (D == EImSwipeDir::Left)  { MoveCursor(-1); UpdatePreview(); }
				else if (D == EImSwipeDir::Right) { MoveCursor(1); UpdatePreview(); }
			};
			Row.Add(MoveTemp(CursorKey));
		}
		Rows.Add(MoveTemp(Row));
	}

	return Rows;
}

TArray<FVirtualKeyDef> SImSlateVirtualKeyboard::GetT9BottomRow()
{
	// ToggleType + Done moved to the preview row.
	TArray<FVirtualKeyDef> Row;
	Row.Add(MakeCharKey(TEXT("0"), TEXT("0")));
	Row[0].WidthMultiplier = 2.f;
	Row.Add(MakeSpaceKey(4.f));
	return Row;
}

TArray<TArray<FVirtualKeyDef>> SImSlateVirtualKeyboard::GetNumberLayout()
{
	// Type-aware numeric keypad / calculator, a uniform 4×4 grid whose RIGHT column + bottom-row tail are
	// swapped by radix: DEC shows the calculator operators (+ − * . / =), HEX shows the hex letters
	// (A B C D E F). Same skeleton, two faces:
	//   DEC:                 HEX:
	//     1 2 3 +              1 2 3 A
	//     4 5 6 −              4 5 6 B
	//     7 8 9 *              7 8 9 C
	//     0 . / =              0 D E F
	// Digit keys feed OnKeyInput→InsertText (hex letters too). Operator chars (+ − * /) also go through the
	// Char pipeline but OnKeyInput routes them to the calculator (CalcAppendOperator) on the DEC pad. '=' is
	// an action (Equals). There is NO backspace / Clr / cursor key — value editing is via the preview drag.
	TArray<TArray<FVirtualKeyDef>> Rows;

	// Digit key (0-9). In HEX every digit is valid; in DEC every digit is valid too (base 10). (8/9 are
	// never disabled now that OCT is gone.)
	auto Digit = [this](const TCHAR* D) { return MakeCharKey(D, D); };
	// Hex letter key (A-F): plain Char input.
	auto HexLetter = [this](const TCHAR* L) { return MakeCharKey(L, L); };
	// Operator key (+ − * /): Char pipeline with Value = the ASCII operator; OnKeyInput detects an operator
	// char on the DEC pad and routes it to CalcAppendOperator instead of inserting text. Display label may
	// be a prettier glyph (− U+2212, × , ÷) while the value stays ASCII for simple comparison.
	auto OpKey = [this](const TCHAR* Label, const TCHAR* Value) {
		FVirtualKeyDef Key = MakeCharKey(Value, Value);
		Key.Label = Label;
		Key.ShiftLabel = Label;
		return Key;
	};
	auto DotKey = [this]() { return MakeCharKey(TEXT("."), TEXT(".")); };
	auto EqualsKey = [this]() { return MakeActionKey(EVirtualKeyAction::Equals, TEXT("=")); };

	const bool bHex = (NumericRadix == 16);

	// Row 1: 1 2 3 | (+ / A)
	{
		TArray<FVirtualKeyDef> Row;
		Row.Add(Digit(TEXT("1"))); Row.Add(Digit(TEXT("2"))); Row.Add(Digit(TEXT("3")));
		Row.Add(bHex ? HexLetter(TEXT("A")) : OpKey(TEXT("+"), TEXT("+")));
		Rows.Add(MoveTemp(Row));
	}
	// Row 2: 4 5 6 | (− / B)
	{
		TArray<FVirtualKeyDef> Row;
		Row.Add(Digit(TEXT("4"))); Row.Add(Digit(TEXT("5"))); Row.Add(Digit(TEXT("6")));
		Row.Add(bHex ? HexLetter(TEXT("B")) : OpKey(TEXT("\x2212"), TEXT("-")));
		Rows.Add(MoveTemp(Row));
	}
	// Row 3: 7 8 9 | (* / C)
	{
		TArray<FVirtualKeyDef> Row;
		Row.Add(Digit(TEXT("7"))); Row.Add(Digit(TEXT("8"))); Row.Add(Digit(TEXT("9")));
		Row.Add(bHex ? HexLetter(TEXT("C")) : OpKey(TEXT("\xD7"), TEXT("*")));
		Rows.Add(MoveTemp(Row));
	}
	// Row 4 DEC: 0 . = /     |  Row 4 HEX: 0 D E F
	{
		TArray<FVirtualKeyDef> Row;
		Row.Add(Digit(TEXT("0")));
		if (bHex)
		{
			Row.Add(HexLetter(TEXT("D"))); Row.Add(HexLetter(TEXT("E"))); Row.Add(HexLetter(TEXT("F")));
		}
		else
		{
			// DEC bottom row tail: [. or ±] = ÷.
			//  - float fields: '.' (decimal point)
			//  - integer fields: a sign (±) key IF the range can actually hold a negative; otherwise a blank
			//    spacer. The sign key's label shows the sign a tap switches TO (value<0 → '+', else '−').
			if (NumericParams.bAllowDecimal)
			{
				Row.Add(DotKey());
			}
			else
			{
				const bool bRangeForbidsNeg = NumericParams.Min.IsSet() && NumericParams.Min.GetValue() >= 0.0;
				if (NumericParams.bAllowNegative && !bRangeForbidsNeg)
				{
					// Sign toggle (clamped). A fixed "±" glyph (+ over −) — the action flips the value's sign.
					// Hidden entirely when flipping is impossible (range can't go negative → blank spacer below).
					Row.Add(MakeActionKey(EVirtualKeyAction::SignToggle, TEXT("\xB1")));
				}
				else
				{
					FVirtualKeyDef Spacer; Spacer.Label = TEXT(""); Spacer.bDisabled = true;  // no sign possible
					Row.Add(MoveTemp(Spacer));
				}
			}
			Row.Add(EqualsKey());
			Row.Add(OpKey(TEXT("\xF7"), TEXT("/")));   // ÷
		}
		Rows.Add(MoveTemp(Row));
	}

	return Rows;
}

// ==================== Numeric helpers (file-local) ====================

// Unsigned mask for a given bit width. 0 or >=64 → all-ones (avoids the 1<<64 UB).
static uint64 NumericWidthMask(int32 BitWidth)
{
	return (BitWidth <= 0 || BitWidth >= 64) ? ~0ull : ((1ull << BitWidth) - 1);
}

// Number of decimal digits in a positive integer (1000→4, 9→1, 0→1).
static int32 NumDecimalDigits(int64 V)
{
	if (V == 0) return 1;
	int32 d = 0; while (V) { V /= 10; ++d; }
	return d;
}

// DEC "highest digit" scrub-down: decrement the LEADING digit.
//  - leading digit > 1: subtract its place value (5000001→4000001→…→1000001; 900→800; 55→45).
//  - leading digit == 1: dropping it to 0 erases the column. Two cases by the SECOND digit:
//      · second digit == 0 (would expose a leading zero) → DEMOTE: leading 1 becomes a new leading 9 one
//        column down, i.e. subtract LP/10 (1000001→900001, 1000→900, 10→9; single digit 1→0).
//      · second digit != 0 → just zero the leading column, the second digit becomes the new lead, i.e.
//        subtract LP (1500001→500001, 1234→234). The new lead then keeps using this same logic.
// Only for the most-significant digit (Val>0).
static int64 TopDigitStepDown(int64 Val /*>0*/)
{
	const int32 d = NumDecimalDigits(Val);
	int64 LP = 1; for (int32 i = 1; i < d; ++i) LP *= 10;  // place value of the leading digit
	const int64 lead = Val / LP;                            // 1..9
	if (lead > 1)
		return Val - LP;                                    // leading digit −1
	if (LP == 1)
		return Val - 1;                                     // single digit "1" → 0
	const int64 secondDigit = (Val / (LP / 10)) % 10;       // the next column's digit
	return (secondDigit == 0) ? (Val - LP / 10)             // 1000001→900001 (demote: 1→9 one column down)
	                          : (Val - LP);                 // 1500001→500001 (zero the lead, 2nd digit leads)
}

// DEC "highest digit" scrub-up: inverse on the pure-power chain (always +leading place value).
// 0→1, 9→10, 800→900, 900→1000.
static int64 TopDigitStepUp(int64 Val /*>=0*/)
{
	if (Val == 0) return 1;
	const int32 d = NumDecimalDigits(Val);
	int64 LP = 1; for (int32 i = 1; i < d; ++i) LP *= 10;
	return Val + LP;
}

void SImSlateVirtualKeyboard::ApplyNumericStep(int32 Direction)
{
	// Step amount: explicit Step if given, else 1 (the +/- spin keys always work).
	const double StepAmt = NumericParams.Step.IsSet() ? NumericParams.Step.GetValue() : 1.0;

	if (NumericParams.bAllowDecimal && NumericRadix == 10)
	{
		// Decimal float.
		double Val = FCString::Atod(*CurrentText);
		Val += Direction * StepAmt;
		if (NumericParams.Min.IsSet()) Val = FMath::Max(Val, NumericParams.Min.GetValue());
		if (NumericParams.Max.IsSet()) Val = FMath::Min(Val, NumericParams.Max.GetValue());
		CurrentText = FString::SanitizeFloat(Val);
	}
	else if (NumericRadix == 16)
	{
		// HEX: two's-complement ring at BitWidth — step on the unsigned view, wrap with the mask, no clamp
		// (consistent with the per-digit HEX scrub in AdjustDigitAtCursor).
		const uint64 Mask = NumericWidthMask(NumericParams.BitWidth);
		uint64 U = FCString::Strtoui64(*CurrentText, nullptr, 16) & Mask;
		U = (Direction > 0) ? (U + (uint64)StepAmt) : (U - (uint64)StepAmt);
		U &= Mask;
		CurrentText = FormatIntInRadix((int64)U, 16, NumericParams.BitWidth);
	}
	else
	{
		// DEC integer: parse, step, clamp, re-format.
		int64 IVal = FCString::Strtoi64(*CurrentText, nullptr, 10);
		IVal += (int64)(Direction * StepAmt);
		if (NumericParams.Min.IsSet()) IVal = FMath::Max(IVal, (int64)NumericParams.Min.GetValue());
		if (NumericParams.Max.IsSet()) IVal = FMath::Min(IVal, (int64)NumericParams.Max.GetValue());
		CurrentText = FormatIntInRadix(IVal, 10, NumericParams.BitWidth);
	}
	CursorPosition = CurrentText.Len();
	UpdatePreview();
	UpdateSuggestions();
	if (OnTextChanged)
		OnTextChanged(CurrentText);
}

// Del swipe-up "Clr" in Number mode: clear to the value nearest zero that's still in range.
// 0 if 0 ∈ [Min,Max]; otherwise the bound closest to 0 (Min if both positive, Max if both negative).
void SImSlateVirtualKeyboard::ClearNumericValue()
{
	const bool bHasMin = NumericParams.Min.IsSet();
	const bool bHasMax = NumericParams.Max.IsSet();
	const double Min = bHasMin ? NumericParams.Min.GetValue() : 0.0;
	const double Max = bHasMax ? NumericParams.Max.GetValue() : 0.0;

	double Target = 0.0;
	if (bHasMin && Min > 0.0)        Target = Min;   // whole range positive → smallest = Min
	else if (bHasMax && Max < 0.0)   Target = Max;   // whole range negative → largest = Max
	// else 0 is in range (or unbounded) → keep 0

	if (NumericParams.bAllowDecimal && NumericRadix == 10)
		CurrentText = FString::SanitizeFloat(Target);
	else
		CurrentText = FormatIntInRadix((int64)Target, NumericRadix, NumericParams.BitWidth);

	CursorPosition = CurrentText.Len();
	CalcReset();  // clearing the value also clears any in-progress calculator expression / memory
	ClearDigitHighlight();  // value reset → drop preview digit selection
	UpdatePreview();
	UpdateSuggestions();
	if (OnTextChanged)
		OnTextChanged(CurrentText);
}

// Format an integer in the given base. DEC: signed decimal (keeps '-'). HEX: two's-complement unsigned
// view at BitWidth — negatives wrap (e.g. 8-bit -1 → FF, 32-bit -1 → FFFFFFFF), zero-padded to BitWidth/4
// nibbles, NEVER a '-' sign. BitWidth 0 → 64-bit mask, no padding.
FString SImSlateVirtualKeyboard::FormatIntInRadix(int64 Value, int32 Radix, int32 BitWidth)
{
	if (Radix == 10)
		return FString::Printf(TEXT("%lld"), Value);

	// HEX (and any non-decimal): two's-complement unsigned view.
	const uint64 Mask = NumericWidthMask(BitWidth);
	uint64 U = (uint64)Value & Mask;
	const TCHAR* Digits = TEXT("0123456789ABCDEF");
	FString Out;
	do
	{
		Out = FString::Chr(Digits[U & 0xF]) + Out;
		U >>= 4;
	} while (U > 0);
	// Zero-pad to the type's nibble count so a negative shows its full-width complement (FF / FFFFFFFF).
	if (BitWidth > 0)
	{
		const int32 Nibbles = BitWidth / 4;
		while (Out.Len() < Nibbles)
			Out = TEXT("0") + Out;
	}
	return Out;  // HEX never carries a sign
}

void SImSlateVirtualKeyboard::CycleNumericRadix()
{
	// Only integers switch base. Toggle DEC(10) ↔ HEX(16). HEX is a two's-complement unsigned ring view;
	// it conflicts with Min/Max clamping, so a clamped field stays DEC-only (the radix key is hidden too —
	// see UpdateToggleTypeKeyVisibility). Guard here in case the key is somehow triggered.
	if (NumericParams.bAllowDecimal)
		return;  // floats have no base switch
	if (NumericParams.Min.IsSet() || NumericParams.Max.IsSet())
		return;  // clamped → no HEX (two's-complement ring would ignore the clamp)

	const int32 BitWidth = NumericParams.BitWidth;
	if (NumericRadix == 10)
	{
		// DEC → HEX: show the two's-complement at BitWidth (negative → FF.. via FormatIntInRadix).
		int64 Val = FCString::Strtoi64(*CurrentText, nullptr, 10);
		NumericRadix = 16;
		CurrentText = FormatIntInRadix(Val, 16, BitWidth);
	}
	else
	{
		// HEX → DEC: read the unsigned width view, then sign-extend back to a signed value when the type is
		// signed and the top bit is set (so FFFFFFFF → -1). Unsigned types keep the magnitude.
		const uint64 Mask = NumericWidthMask(BitWidth);
		uint64 U = FCString::Strtoui64(*CurrentText, nullptr, 16) & Mask;
		int64 Val;
		if (NumericParams.bAllowNegative && BitWidth > 0 && BitWidth < 64 && (U & (1ull << (BitWidth - 1))))
			Val = (int64)(U | ~Mask);   // sign-extend negative
		else
			Val = (int64)U;
		NumericRadix = 10;
		CurrentText = FormatIntInRadix(Val, 10, BitWidth);
	}
	CursorPosition = CurrentText.Len();
	UpdateToggleTypeLabel();  // refresh the DEC/HEX label
	CalcReset();              // operators are meaningless in HEX; entering/leaving DEC clears calc memory
	ClearDigitHighlight();    // base change re-maps digit positions → drop preview selection
	BuildKeyboard();          // operator column ↔ hex letters
	UpdateSignKey();          // ± visibility depends on radix (hidden in HEX)
	UpdatePreview();
}

// ==================== Numeric Calculator ====================

// Calculator is DEC-only. HEX entry has no operators (the layout shows hex letters instead of +−*/=), so
// even if an operator somehow arrives it's a no-op there.
bool SImSlateVirtualKeyboard::IsCalcEnabled() const
{
	return KeyboardType == EKeyboardType::Number && NumericRadix == 10;
}

// Clear ALL calculator memory: the displayed expression, the pending operation, and the repeat (=) state.
void SImSlateVirtualKeyboard::CalcReset()
{
	CalcExpr.Reset();
	CalcCursorPos = 0;
	PendingOperator.Reset();
	StoredOperand = 0.0;
	bCalcFreshOperand = true;
	RepeatOperator.Reset();
	RepeatOperand = 0.0;
	bJustEvaluated = false;
}

// Format a value per the bound field's type (int → no decimals, float → SanitizeFloat). Calculator is
// DEC-only so no radix formatting is needed here.
FString SImSlateVirtualKeyboard::CalcFormatNumber(double Value) const
{
	if (NumericParams.bAllowDecimal)
		return FString::SanitizeFloat(Value);
	return FString::Printf(TEXT("%lld"), (int64)Value);
}

// Compute "A op B" honouring the field's type. Integer fields round to nearest (per design: 7/2 → 4);
// float fields keep full precision. Division by zero sets bOutDivZero and returns A unchanged.
double SImSlateVirtualKeyboard::CalcCompute(double A, double B, const FString& Op, bool& bOutDivZero) const
{
	bOutDivZero = false;
	double R = A;
	if (Op == TEXT("+"))      R = A + B;
	else if (Op == TEXT("-")) R = A - B;
	else if (Op == TEXT("*")) R = A * B;
	else if (Op == TEXT("/"))
	{
		if (B == 0.0) { bOutDivZero = true; return A; }
		R = A / B;
	}
	// Integer field: round to nearest (design choice — 7/2 → 4, 5/2 → 3).
	if (!NumericParams.bAllowDecimal)
		R = FMath::RoundToDouble(R);
	return R;
}

// Clamp Value to [Min,Max], format per type, write to CurrentText, sync the bound field, and leave
// calculator "result" display state: the expression string is cleared (preview now shows the number).
void SImSlateVirtualKeyboard::CalcWriteResult(double Value)
{
	if (NumericParams.Min.IsSet()) Value = FMath::Max(Value, NumericParams.Min.GetValue());
	if (NumericParams.Max.IsSet()) Value = FMath::Min(Value, NumericParams.Max.GetValue());
	CurrentText = CalcFormatNumber(Value);
	CursorPosition = CurrentText.Len();
	CalcExpr.Reset();              // result shown as a plain number, not an expression
	CalcCursorPos = 0;
	UpdatePreview();               // syncs CurrentText to the bound field (CalcExpr empty → numeric mode)
	UpdateSuggestions();
	UpdateSignKey();
}

// Is C one of the four operator chars? (helper for expression-string editing)
static bool IsCalcOperatorChar(TCHAR C)
{
	return C == '+' || C == '-' || C == '*' || C == '/';
}

// Operator key (+ − * /). Expression-string model: seed CalcExpr from the current value on the FIRST operator,
// then append. A trailing operator is REPLACED so consecutive operators switch (5+ then − → 5−). The bound
// field (CurrentText) is NOT touched — it keeps its value until '=' evaluates the whole string.
void SImSlateVirtualKeyboard::CalcAppendOperator(const FString& Op)
{
	if (!IsCalcEnabled() || Op.Len() != 1)
		return;

	if (CalcExpr.IsEmpty())
	{
		// Starting an expression: the current field value becomes the first operand. Use CurrentText verbatim
		// (it is already a valid number; "0" placeholder included) so "5" + "+" → "5+".
		CalcExpr = CurrentText + Op;
	}
	else
	{
		// Mid-expression. If the last char is already an operator, REPLACE it (consecutive operators switch:
		// "5+" then "−" → "5−"). Otherwise append after the just-typed operand ("5+3" then "−" → "5+3−").
		const TCHAR Last = CalcExpr[CalcExpr.Len() - 1];
		if (IsCalcOperatorChar(Last))
			CalcExpr[CalcExpr.Len() - 1] = Op[0];
		else
			CalcExpr += Op;
	}
	CalcCursorPos = CalcExpr.Len();  // caret at the end of the expression
	bJustEvaluated = false;
	UpdatePreview();                 // preview shows the expression; bound field unchanged
	UpdateSignKey();                 // hide ± while an expression is being built
}

// Digit / '.' pressed while an expression is being typed: append it straight to the expression string. The
// bound field (CurrentText) stays frozen until '='. (Only called in expression mode — CalcExpr non-empty.)
void SImSlateVirtualKeyboard::CalcAppendDigit(const FString& Ch)
{
	if (!IsCalcEnabled() || CalcExpr.IsEmpty())
		return;
	// One decimal point per operand: if the CURRENT operand (the run after the last operator) already has a
	// '.', ignore a further '.' — so "3.5" then "." does nothing, but "3.5+2" then "." → "3.5+2." is allowed.
	if (Ch == TEXT("."))
	{
		int32 OpStart = 0;
		for (int32 i = CalcExpr.Len() - 1; i >= 0; --i)
			if (IsCalcOperatorChar(CalcExpr[i])) { OpStart = i + 1; break; }
		if (CalcExpr.Mid(OpStart).Contains(TEXT(".")))
			return;
	}
	CalcExpr += Ch;
	CalcCursorPos = CalcExpr.Len();
	UpdatePreview();
}

// Evaluate a full expression string LEFT-TO-RIGHT (no operator precedence — a plain calculator): tokenise into
// number / operator runs and fold pairwise with CalcCompute. A trailing dangling operator ("5+") is ignored
// (uses the running value). Returns the running result; sets bOutDivZero if any division by zero occurred.
double SImSlateVirtualKeyboard::CalcEvaluateExpression(const FString& Expr, bool& bOutDivZero) const
{
	bOutDivZero = false;
	double Acc = 0.0;
	FString PendingOp;          // operator awaiting its right operand
	FString NumTok;             // the number token being accumulated
	bool bHaveAcc = false;      // has Acc been seeded by the first number yet?

	auto FoldToken = [&](const FString& Num)
	{
		if (Num.IsEmpty())
			return;
		const double Val = FCString::Atod(*Num);
		if (!bHaveAcc)
		{
			Acc = Val;            // first operand seeds the accumulator
			bHaveAcc = true;
		}
		else if (!PendingOp.IsEmpty())
		{
			bool bDZ = false;
			Acc = CalcCompute(Acc, Val, PendingOp, bDZ);
			if (bDZ) bOutDivZero = true;
			PendingOp.Reset();
		}
	};

	for (int32 i = 0; i < Expr.Len(); ++i)
	{
		const TCHAR C = Expr[i];
		// A leading '-' is the SIGN of the first operand (the expression is seeded from CurrentText, which may
		// be negative, e.g. "-5+3"), not subtraction. Operators never sit back-to-back — pressing an operator
		// REPLACES a trailing one (CalcAppendOperator), so "3+-5" can't occur; only i==0 needs the sign case.
		const bool bSignMinus = (C == '-') && (i == 0);
		if (IsCalcOperatorChar(C) && !bSignMinus)
		{
			FoldToken(NumTok);
			NumTok.Reset();
			PendingOp = FString::Chr(C);
		}
		else
		{
			NumTok.AppendChar(C);  // digit, '.', or a sign '-'
		}
	}
	FoldToken(NumTok);             // fold the final number (dangling trailing operator is simply ignored)
	return bHaveAcc ? Acc : FCString::Atod(*CurrentText);
}

// '=' key. Evaluate the whole expression string left-to-right, write the (clamped/formatted) result back into
// the value, and EXIT expression mode → plain numeric insert mode (next key edits the result in place, R017).
// With no expression in progress, '=' just NORMALISES the current value (".088" → "0.088", clamp, format).
void SImSlateVirtualKeyboard::CalcPressEquals()
{
	if (!IsCalcEnabled())
		return;

	if (!CalcExpr.IsEmpty())
	{
		// Evaluate the typed expression. CalcWriteResult clamps + formats + writes CurrentText AND resets
		// CalcExpr (exits expression mode). On divide-by-zero, keep the result CalcEvaluateExpression returns
		// (the running value before the bad division) rather than corrupting the field.
		bool bDivZero = false;
		const double Result = CalcEvaluateExpression(CalcExpr, bDivZero);
		CalcWriteResult(Result);   // also CalcExpr.Reset() → leaves expression mode
	}
	else
	{
		// No expression: normalise the current value in place (loose input like ".088" → "0.088", clamp, format).
		CalcWriteResult(FCString::Atod(*CurrentText));
	}
	// '=' always lands in plain numeric mode: the next key edits the result in place (must NOT trigger the
	// fresh-operand clear — see R017). bJustEvaluated stays false so no special post-'=' digit handling.
	bJustEvaluated = false;
	bCalcFreshOperand = false;
}

// Refresh the preview-row ± key: visible only for signed numeric DEC input whose range allows a negative;
// label tracks the current value's sign (value≥0 → "−" to go negative; value<0 → "+" to go positive).
void SImSlateVirtualKeyboard::UpdateSignKey()
{
	if (!SignKey.IsValid())
		return;
	const bool bRangeForbidsNeg = NumericParams.Min.IsSet() && NumericParams.Min.GetValue() >= 0.0;
	const bool bShow = (KeyboardType == EKeyboardType::Number)
		&& (NumericRadix == 10)            // sign only meaningful in DEC
		&& NumericParams.bAllowNegative
		&& !bRangeForbidsNeg;
	// NOTE: ± stays visible during expression entry too (it signs the FIRST operand). Collapsing it mid-
	// expression used to make the preview row jump — keep a stable layout. (Fields that can't go negative
	// still hide it via bAllowNegative / bRangeForbidsNeg above.)
	SignKey->SetVisibility(bShow ? EVisibility::Visible : EVisibility::Collapsed);
	// Labels are TAttribute lambdas (read the value's sign) — just repaint so they refresh after a change.
	if (bShow)
		SignKey->Invalidate(EInvalidateWidgetReason::Paint);
}

TArray<TArray<FVirtualKeyDef>> SImSlateVirtualKeyboard::GetSymbolLayout()
{
	TArray<TArray<FVirtualKeyDef>> Rows;

	// Row 1: common symbols
	{
		const TCHAR* Syms[] = {TEXT("!"),TEXT("@"),TEXT("#"),TEXT("$"),TEXT("%"),TEXT("^"),TEXT("&"),TEXT("*"),TEXT("("),TEXT(")")};
		TArray<FVirtualKeyDef> Row;
		for (auto S : Syms) Row.Add(MakeCharKey(S, S));
		Row[3].LongPressChars = {TEXT("€"),TEXT("£"),TEXT("¥"),TEXT("₩"),TEXT("¢")};
		Rows.Add(MoveTemp(Row));
	}

	// Row 2: more symbols
	{
		const TCHAR* Syms[] = {TEXT("-"),TEXT("_"),TEXT("="),TEXT("+"),TEXT("["),TEXT("]"),TEXT("{"),TEXT("}"),TEXT("\\")};
		TArray<FVirtualKeyDef> Row;
		for (auto S : Syms) Row.Add(MakeCharKey(S, S));
		Row[0].LongPressChars = {TEXT("–"),TEXT("—"),TEXT("•")};
		Row[2].LongPressChars = {TEXT("≈"),TEXT("≠")};
		Rows.Add(MoveTemp(Row));
	}

	// Row 3: [ABC] remaining symbols + backspace
	{
		TArray<FVirtualKeyDef> Row;
		Row.Add(MakeActionKey(EVirtualKeyAction::ToggleLayer, TEXT("ABC"), 1.3f));
		const TCHAR* Syms[] = {TEXT(";"),TEXT(":"),TEXT("'"),TEXT("\""),TEXT(","),TEXT("<"),TEXT(">")};
		for (auto S : Syms) Row.Add(MakeCharKey(S, S));
		Row[3].LongPressChars = {TEXT("«"),TEXT("»"),TEXT("„"),TEXT("\x201C"),TEXT("\x201D")};
		Row.Add(MakeBackspaceKey(1.3f));
		Rows.Add(MoveTemp(Row));
	}

	return Rows;
}

// Keyboard sizing caps in LOGICAL pixels (multiplied by the keyboard scale at use sites).
static constexpr float GMaxKeyboardHeight = 300.f;

// ==================== Construct ====================

SImSlateVirtualKeyboard::~SImSlateVirtualKeyboard()
{
	// Safety net for the desktop real-mouse toggle (Show() may set bUseMouseForTouch=false). Normally Hide()
	// restores it, but if we're destroyed while still shown (RemoveKeyboard resets without Hide on world
	// teardown), restore here so the global config never leaks.
#if PLATFORM_DESKTOP
	if (bSavedUseMouseForTouch)
	{
		if (UInputSettings* IS = GetMutableDefault<UInputSettings>())
			IS->bUseMouseForTouch = true;
		bSavedUseMouseForTouch = false;
	}
#endif
}

void SImSlateVirtualKeyboard::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::Collapsed);

	float Scale = GetKbScale();

	// Preview-row toggle key: a two-state switch key (radix DEC/HEX or type T26/T9). Its labels are pulled
	// live from KeyboardType/NumericRadix; a tap runs the same ToggleType action as before. (Self-drawn
	// leaf — TAttribute reads don't auto-repaint, so UpdateToggleTypeLabel Invalidates it on change.)
	SAssignNew(ToggleTypeKey, SImSwitchKey)
		.CurrentLabel(TAttribute<FText>::CreateLambda([this]() -> FText {
			if (KeyboardType == EKeyboardType::Number)
				return FText::FromString((NumericRadix == 16) ? TEXT("HEX") : TEXT("DEC"));
			return FText::FromString((KeyboardType == EKeyboardType::QWERTY) ? TEXT("T26") : TEXT("T9"));
		}))
		.TargetLabel(TAttribute<FText>::CreateLambda([this]() -> FText {
			if (KeyboardType == EKeyboardType::Number)
				return FText::FromString((NumericRadix == 16) ? TEXT("DEC") : TEXT("HEX"));
			return FText::FromString((KeyboardType == EKeyboardType::QWERTY) ? TEXT("T9") : TEXT("T26"));
		}))
		.OnClicked_Lambda([this]() { OnKeyAction(EVirtualKeyAction::ToggleType); });

	// Sign (±) key — preview row, right of the radix key. A two-state switch key (like DEC/HEX): the CURRENT
	// sign big top-left, the sign a tap switches TO small bottom-right, diagonal slash between. Labels pull
	// live from the value's sign (value<0 → current '−' / target '+', else current '+' / target '−'). A tap
	// flips the sign (SignToggle). Visibility is managed by UpdateSignKey (signed DEC numeric only).
	SAssignNew(SignKey, SImSwitchKey)
		.CurrentLabel(TAttribute<FText>::CreateLambda([this]() -> FText {
			const bool bNeg = !CurrentText.IsEmpty() && CurrentText[0] == TEXT('-');
			return FText::FromString(bNeg ? TEXT("\x2212") : TEXT("+"));
		}))
		.TargetLabel(TAttribute<FText>::CreateLambda([this]() -> FText {
			const bool bNeg = !CurrentText.IsEmpty() && CurrentText[0] == TEXT('-');
			return FText::FromString(bNeg ? TEXT("+") : TEXT("\x2212"));
		}))
		.OnClicked_Lambda([this]() { OnKeyAction(EVirtualKeyAction::SignToggle); });
	SignKey->SetVisibility(EVisibility::Collapsed);  // shown by UpdateSignKey when applicable

	// Backspace (⌫) key — preview row, left of Done. Reuses MakeBackspaceKey (tap = delete one char;
	// down = Hide, left/right = step delete/undo). The swipe-up "Clr" is removed here (no "Clr" hint above
	// the ⌫ on the numeric preview row, per design), so the numeric grid needs no ⌫ cell.
	PreviewBackspaceKeyDef = MakeShared<FVirtualKeyDef>(MakeBackspaceKey(1.f));
	PreviewBackspaceKeyDef->Swipe.Up = FVirtualKeySwipeEntry{};  // drop swipe-up Clr (label + callback)
	PreviewBackspaceKey = MakeBoundKey(PreviewBackspaceKeyDef.Get());

	DoneKeyDef = MakeShared<FVirtualKeyDef>();
	DoneKeyDef->Action = EVirtualKeyAction::Enter;
	DoneKeyDef->Label = TEXT("Done");
	// Up swipe = clear all text (keyboard stays open).
	DoneKeyDef->Swipe.Up.Label = TEXT("Clr");
	DoneKeyDef->Swipe.Up.Callback.BindLambda([this]() {
		CurrentText.Reset();
		CursorPosition = 0;
		UpdatePreview();
		UpdateSuggestions();
	});
	// Down swipe = Esc: cancel, restoring the text to what it was when the keyboard opened.
	DoneKeyDef->Swipe.Down.Label = TEXT("Esc");
	DoneKeyDef->Swipe.Down.Callback.BindLambda([this]() { Hide(false); });
	DoneKey = MakeBoundKey(DoneKeyDef.Get());

	ChildSlot
	[
		SAssignNew(RootOverlay, SOverlay)

		// Full-screen modal hit-blocker (bottom layer). Visibility toggled in Show()
		// per FVirtualKeyboardShowParams::bBlockBackground.
		+ SOverlay::Slot()
		[
			SAssignNew(BackgroundBlocker, SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f))  // transparent but hit-testable
			.Visibility(EVisibility::Visible)
		]

		// Suggestions: SEPARATE overlay layer (not in the keyboard's vertical stack, so candidate
		// count never changes the keyboard height / origin). VAlign_Bottom + bottom Offset = the
		// preview+keys height pins the band DIRECTLY ABOVE the preview row. The band is only as
		// tall as its content (AutoHeight via VAlign_Bottom), capped at 120; few candidates sit
		// right above the preview, more candidates extend UPWARD from there. This keeps the
		// candidates near the keys (visible) instead of starting from the top of the screen.
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Bottom)
		.Padding(TAttribute<FMargin>::CreateLambda([this]() -> FMargin {
			const float UnitH = PreviewKeysRoot.IsValid() ? PreviewKeysRoot->GetCachedGeometry().GetLocalSize().Y : 0.f;
			// Left/right: keep off the device cutouts (same inset the keyboard body uses).
			// Bottom: sit directly above the preview row, which is itself lifted by the safe-area
			// bottom inset + extra — so add that here too, otherwise the band would overlap it.
			const FMargin Safe = GetKbSafeMargin();
			return FMargin(Safe.Left, 0.f, Safe.Right, UnitH + Safe.Bottom);
		}))
		[
			SNew(SBox)
			// Max height = the empty space ABOVE the preview row: from the viewport top (minus the top safe
			// area) down to the top of the preview+keys block. So candidates wrap UPWARD and fill that whole
			// gap before the scroll box ever scrolls (the old fixed 120px — un-scaled — scrolled almost
			// immediately on high-PPI screens while leaving the space above empty).
			.MaxDesiredHeight(TAttribute<FOptionalSize>::CreateLambda([this]() -> FOptionalSize {
				const float PreviewKeysH = PreviewKeysRoot.IsValid() ? PreviewKeysRoot->GetCachedGeometry().GetLocalSize().Y : 0.f;
				const FMargin Safe = GetKbSafeMargin();
				const float Avail = GCachedViewportHeight - PreviewKeysH - Safe.Bottom - Safe.Top;
				return FOptionalSize(FMath::Max(Avail, 40.f * GetKbScale()));  // never collapse below ~one row
			}))
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				+ SScrollBox::Slot()
				[
					SAssignNew(SuggestionBar, SWrapBox)
					.UseAllottedSize(true)
				]
			]
		]

		// Preview row + keys: the keyboard proper. Bottom-anchored via a top spacer. Its height
		// depends ONLY on preview + keys (suggestions are no longer here), so it is constant
		// regardless of candidate count → stable origin.
		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)

			// Top spacer: pushes preview+keys to the bottom of the (full-screen) overlay.
			+ SVerticalBox::Slot()
			.FillHeight(1.f)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				// Edge margin: keep the keyboard off the device's left/right cutouts and bottom
				// home-indicator. = max(OS safe inset, imslate.KeyboardEdgeMargin) per side; on
				// desktop the inset is 0 and the default EdgeMargin is 0, so the keyboard sits
				// flush (no left/right gap). Top is NOT padded — keyboard is bottom-anchored and
				// the spacer above handles the top. (We use a plain SBox + GetKbSafeMargin instead
				// of SSafeZone so the same max(safe, min) rule applies and isn't double-added.)
				SNew(SBox)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.Padding(TAttribute<FMargin>::CreateLambda([]() -> FMargin { return GetKbSafeMargin(); }))
				[
				SAssignNew(PreviewKeysRoot, SVerticalBox)

				// Preview row: [ToggleType] [preview text] [Done]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(PreviewBorder, SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
					.BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 0.95f))
					.Padding(FMargin(2.f, 2.f))
					.Visibility(EVisibility::Visible)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(54.f * GetKbScale()); }))
							.HeightOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(32.f * GetKbScale()); }))  // match keyboard key height (SImSlateKey: 32×scale)
							[
								ToggleTypeKey.ToSharedRef()
							]
						]
						// Sign (±) key — right of the radix key, left of the preview text. Auto-width;
						// collapsed by UpdateSignKey unless signed DEC numeric input applies.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(40.f * GetKbScale()); }))
							.HeightOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(32.f * GetKbScale()); }))
							[
								SignKey.ToSharedRef()
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						.Padding(FMargin(6.f, 0.f))
						[
							SAssignNew(PreviewEdit, SImEditableText)
							.Font(GetImSlateDefaultFont(11))
							.ColorAndOpacity(FLinearColor::White)
							.Justification(ETextJustify::Center)
							.IsReadOnly(true)  // caret is self-drawn; native caret/edit disabled
						]
						// Backspace (⌫) — left of Done. Persistent preview-row key (numeric grid has no ⌫).
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(48.f * GetKbScale()); }))
							.HeightOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(32.f * GetKbScale()); }))
							[
								PreviewBackspaceKey.ToSharedRef()
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(64.f * GetKbScale()); }))
							.HeightOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(32.f * GetKbScale()); }))  // match keyboard key height (SImSlateKey: 32×scale)
							[
								DoneKey.ToSharedRef()
							]
						]
					]
				]

				// Keyboard: auto height, max 300 logical px (scaled by DPI).
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MaxDesiredHeight(GMaxKeyboardHeight * GetKbScale())
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
						.BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f, 0.95f))
						.Padding(2.f)
						.Visibility(EVisibility::Visible)
						[
							SAssignNew(KeyboardGrid, SVerticalBox)
						]
					]
				]
				]  // end edge-margin SBox (around PreviewKeysRoot)
			]
		]
	];

	if (PreviewEdit.IsValid())
	{
		PreviewEdit->SetPreviewDisplayMode(true);
		// Display-only: don't let the editable widget eat mouse events, so preview-area
		// drag (move cursor) still reaches the keyboard. It only shows text + self-drawn caret.
		PreviewEdit->SetVisibility(EVisibility::HitTestInvisible);
	}

	BuildKeyboard();
}

FVector2D SImSlateVirtualKeyboard::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// Fill the whole viewport so this widget's origin is fixed (independent of content height).
	// This keeps popups (anchored in this widget's local space) from drifting when the
	// suggestion row count changes the content height during a Del step-drag.
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint VP = GEngine->GameViewport->Viewport->GetSizeXY();
		if (VP.X > 0 && VP.Y > 0)
			return FVector2D(VP.X, VP.Y);
	}
	// Fallback before the viewport is known: defer to content size.
	return SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
}

SImSlateVirtualKeyboard::EKeyboardLayoutMode SImSlateVirtualKeyboard::ComputeLayoutMode(float Width) const
{
	// Pure function of width: same screen → same mode, independent of toggle history.
	if (Width <= 0.f)
		return EKeyboardLayoutMode::FullScreen;  // unknown yet — start collapsed

	float Scale = GetKbScale();
	// Base key height is 32 logical px; Scale already includes the keyboard multiplier (GetKbScale).
	// (The old FMath::Min(32, GMaxKeyHeight=48) was dead — 32 is always smaller, never reached 48.
	// Size is now driven by imslate.KeyboardScale.)
	float KeyH = 32.f * Scale;
	float ComfortKeyW = KeyH * 1.3f;
	float SideWidth = 5.f * ComfortKeyW + 5.f * 4.f * Scale;
	float MinGap = 60.f * Scale;
	bool bCanSplit = (SideWidth * 2.f + MinGap) <= Width;

	return bCanSplit ? EKeyboardLayoutMode::Split : EKeyboardLayoutMode::FullScreen;
}

void SImSlateVirtualKeyboard::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!bVisible) return;

	// Grab Slate user focus onto the keyboard root ONCE, when it first appears (deferred follow-up to
	// Show()'s bPendingFocus — see Show() for why it can't be done inline). SetKeyboardFocus mirrors the
	// pattern used in SImSearchBox.
	//
	// IMPORTANT: only grab while bPendingFocus is set (the initial appearance), NOT every frame. An
	// unconditional per-frame grab steals GLOBAL keyboard focus back from anything the user clicks —
	// including editor panels OUTSIDE the viewport — which is why their right-click menus stopped opening
	// while the keyboard was up. (Trade-off: a host that re-asserts focus onto itself every frame, e.g. the
	// ImGui Slate widget, can steal it back after this one grab; if that resurfaces, re-scope the re-grab to
	// "focus is inside the game viewport" rather than re-grabbing unconditionally.)
	if (bPendingFocus)
	{
		if (HasAnyUserFocusOrFocusedDescendants())
		{
			bPendingFocus = false;  // already focused
		}
		else if (FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly))
		{
			bPendingFocus = false;
		}
	}

	// AllottedGeometry here is authoritative (unlike GetCachedGeometry timing during
	// Construct/Show). Rebuild only when the resulting layout mode actually changes.
	float Width = AllottedGeometry.GetLocalSize().X;
	if (Width <= 0.f) return;

	EKeyboardLayoutMode DesiredMode = ComputeLayoutMode(Width);
	if (DesiredMode != BuiltLayoutMode || BuiltWidth < 0.f)
	{
		BuiltLayoutMode = DesiredMode;
		BuiltWidth = Width;
		BuildKeyboard();
	}

	// Plan A height-cap inputs (see GetKbScale): the keyboard fills the viewport, so AllottedGeometry
	// Y == viewport height. The per-unit-scale content height = current content desired height /
	// current keyboard scale; dividing out the scale makes it scale-independent, so feeding it back
	// into GetKbScale's clamp converges (the keys are linear in scale).
	GCachedViewportHeight = AllottedGeometry.GetLocalSize().Y;
	if (PreviewKeysRoot.IsValid())
	{
		const float ContentH = PreviewKeysRoot->GetDesiredSize().Y;
		const float CurScale = FMath::Max(GetImSlateKeyboardScale(), 0.1f);
		if (ContentH > 0.f)
			GCachedKeyboardUnitHeight = ContentH / CurScale;
	}
}

void SImSlateVirtualKeyboard::BuildKeyboard()
{
	KeyboardGrid->ClearChildren();
	KeyWidgets.Reset();
	PersistentKeyDefs.Reset();
	PersistentKeyDefs.Reserve(128);

	if (GetLayoutMode() == EKeyboardLayoutMode::Split)
		BuildSplitLayout();
	else
		BuildFullScreenLayout();
}

void SImSlateVirtualKeyboard::BuildFullScreenLayout()
{
	float Scale = GetKbScale();
	float KeySpacing = 4.f * Scale;

	auto AddRow = [&](const TArray<FVirtualKeyDef>& Keys) {
		TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);
		BuildKeyRow(RowBox, Keys);
		KeyboardGrid->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, KeySpacing * 0.5f))
		[
			RowBox
		];
	};

	if (KeyboardType == EKeyboardType::Number)
	{
		for (const auto& Row : GetNumberLayout())
			AddRow(Row);
	}
	else if (KeyboardType == EKeyboardType::T9)
	{
		for (const auto& Row : GetT9Layout())
			AddRow(Row);
		AddRow(GetT9BottomRow());
	}
	else
	{
		if (CurrentLayer == 0)
		{
			for (const auto& Row : GetQWERTYLayout())
				AddRow(Row);
		}
		else
		{
			for (const auto& Row : GetSymbolLayout())
				AddRow(Row);
		}

		AddRow(GetBottomRow());
	}
}

void SImSlateVirtualKeyboard::BuildSplitLayout()
{
	float Scale = GetKbScale();
	float KeySpacing = 4.f * Scale;

	auto AddRowsToBox = [&](TSharedRef<SVerticalBox> Box, const TArray<TArray<FVirtualKeyDef>>& Rows) {
		for (const auto& Row : Rows)
		{
			TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);
			BuildKeyRow(RowBox, Row);
			Box->AddSlot().AutoHeight().Padding(FMargin(0.f, KeySpacing * 0.5f))[RowBox];
		}
	};

	auto BuildQWERTYHalf = [&](TSharedRef<SVerticalBox> Half, bool bRight) {
		auto AllRows = (CurrentLayer == 0) ? GetQWERTYLayout() : GetSymbolLayout();

		auto SplitRows = [&](const TArray<TArray<FVirtualKeyDef>>& Src) {
			TArray<TArray<FVirtualKeyDef>> Result;
			for (const auto& Row : Src)
			{
				int32 Mid = Row.Num() / 2;
				TArray<FVirtualKeyDef> HalfRow;
				int32 Start = bRight ? Mid : 0;
				int32 End = bRight ? Row.Num() : Mid;
				for (int32 i = Start; i < End; ++i) HalfRow.Add(Row[i]);
				Result.Add(MoveTemp(HalfRow));
			}
			return Result;
		};

		AddRowsToBox(Half, SplitRows(AllRows));

		// Each half gets its own [. | space] so both sides have matching proportions.
		TArray<FVirtualKeyDef> HalfBottom;
		auto DotKey = MakeCharKey(TEXT("."), TEXT("."), TEXT(","));
		DotKey.LongPressChars = {TEXT(";"),TEXT("-"),TEXT("\""),TEXT("+"),TEXT("="),TEXT("\\"),TEXT("["),TEXT("]"),TEXT("<"),TEXT(">")};
		DotKey.WidthMultiplier = 2.f;
		HalfBottom.Add(MoveTemp(DotKey));
		HalfBottom.Add(MakeSpaceKey(4.f));
		TArray<TArray<FVirtualKeyDef>> BottomRows = {MoveTemp(HalfBottom)};
		AddRowsToBox(Half, BottomRows);
	};

	TSharedRef<SVerticalBox> LeftHalf = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> RightHalf = SNew(SVerticalBox);

	if (KeyboardType == EKeyboardType::Number)
	{
		// Numeric pad: like T9, both halves carry the SAME full numeric layout so either thumb reaches
		// the whole keypad. GetNumberLayout already shapes by type/step/hex.
		auto NumRows = GetNumberLayout();
		AddRowsToBox(LeftHalf, NumRows);
		AddRowsToBox(RightHalf, NumRows);
	}
	else if (KeyboardType == EKeyboardType::T9)
	{
		auto T9Rows = GetT9Layout();
		TArray<TArray<FVirtualKeyDef>> BottomRows = {GetT9BottomRow()};
		AddRowsToBox(LeftHalf, T9Rows);
		AddRowsToBox(LeftHalf, BottomRows);
		AddRowsToBox(RightHalf, T9Rows);
		AddRowsToBox(RightHalf, BottomRows);
	}
	else
	{
		BuildQWERTYHalf(LeftHalf, false);
		BuildQWERTYHalf(RightHalf, true);
	}

	// Both T9 and T26 use the SAME side width — a thumb's reach is a physical span,
	// not a column count. Each half fills this fixed width regardless of key count.
	float SplitScale = GetKbScale();
	float SplitKeyH = 32.f * SplitScale;  // base 32 logical px (× KeyboardScale); old Min(32,48) was dead
	float ComfortKeyW = SplitKeyH * 1.3f;
	float SideWidth = 5.f * ComfortKeyW + 5.f * 4.f * SplitScale;

	KeyboardGrid->AddSlot()
	.FillHeight(1.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Bottom)
		[
			SNew(SBox)
			.WidthOverride(SideWidth)
			[ LeftHalf ]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Bottom)
		[
			SNew(SBox)
			.WidthOverride(SideWidth)
			[ RightHalf ]
		]
	];
}

TSharedRef<SImSlateKey> SImSlateVirtualKeyboard::MakeBoundKey(const FVirtualKeyDef* DefPtr)
{
	TSharedRef<SImSlateKey> KeyWidget = SNew(SImSlateKey)
		.KeyDef(DefPtr)
		.bShiftActive_Lambda([this]() { return IsUpperCase(); })
		.bShiftSingleShot_Lambda([this]() { return ShiftState == EShiftState::SingleShot; })
		.OnKeyInput_Lambda([this](const FVirtualKeyDef& Def, const FString& Val) { DismissPopup(); OnKeyInput(Def, Val); })
		.OnKeyAction_Lambda([this](EVirtualKeyAction Action) { DismissPopup(); OnKeyAction(Action); })
		.OnLongPress_Lambda([this](const FVirtualKeyDef& Def, const FGeometry& Geo) { OnKeyLongPress(Def, Geo); })
		.OnLongPressMove_Lambda([this](int32 Idx) { OnKeyLongPressMove(Idx); })
		.OnLongPressEnd_Lambda([this](int32 Idx) { OnKeyLongPressEnd(Idx); })
		.OnPressVisual_Lambda([this](const FVirtualKeyDef& Def, const FGeometry& Geo, bool bForceStepDrag) { OnKeyPressVisual(Def, Geo, bForceStepDrag); })
		.OnMoveVisual_Lambda([this](const FVector2D& Delta, bool bSwipeReady) { OnKeyMoveVisual(Delta, bSwipeReady); })
		.OnReleaseVisual_Lambda([this]() { OnKeyReleaseVisual(); })
		.OnSpaceCursorZone_Lambda([this](int32 Dir) { OnSpaceCursorZone(Dir); });
	return KeyWidget;
}

void SImSlateVirtualKeyboard::BuildKeyRow(TSharedRef<SHorizontalBox> RowBox, const TArray<FVirtualKeyDef>& Keys)
{
	float Scale = GetKbScale();

	int32 BaseIndex = PersistentKeyDefs.Num();
	PersistentKeyDefs.Append(Keys);

	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		const FVirtualKeyDef* DefPtr = &PersistentKeyDefs[BaseIndex + i];

		TSharedRef<SImSlateKey> KeyWidget = MakeBoundKey(DefPtr);
		RowBox->AddSlot()
		.FillWidth(DefPtr->WidthMultiplier)
		.Padding(FMargin(2.f * Scale, 0.f))
		[
			KeyWidget
		];

		if (DefPtr->Action == EVirtualKeyAction::Space)
		{
			KeyWidget->SetKeyboardWidthGetter(TAttribute<float>::CreateLambda([this]() {
				float W = GetCachedGeometry().GetLocalSize().X;
				return (GetLayoutMode() == EKeyboardLayoutMode::Split) ? W * 0.33f : W;
			}));
		}

		FKeyWidgetEntry Entry;
		Entry.Widget = KeyWidget;
		Entry.Def = DefPtr;
		KeyWidgets.Add(Entry);
	}
}

void SImSlateVirtualKeyboard::UpdateToggleTypeLabel()
{
	// ToggleTypeKey is a SImSwitchKey whose CURRENT/TARGET labels are TAttribute lambdas reading
	// KeyboardType/NumericRadix directly. It's a self-drawn leaf, so a state change doesn't auto-repaint —
	// just invalidate it here to pull the new labels.
	if (ToggleTypeKey.IsValid())
		ToggleTypeKey->Invalidate(EInvalidateWidgetReason::Paint);
}

void SImSlateVirtualKeyboard::UpdateToggleTypeKeyVisibility()
{
	if (!ToggleTypeKey.IsValid())
		return;
	// Letter layouts (QWERTY/T9): the key is the type toggle → always shown.
	// Numeric pad: it becomes the RADIX switcher, pinned here on the preview row's LEFT so its position
	// stays fixed no matter how the keypad grid changes between DEC/OCT/HEX (the grid no longer carries
	// a radix key). Shown only for INTEGER numeric input (floats have no base switching) → hidden.
	// Numeric pad radix key shows only for INTEGER input AND only when the field is NOT clamped: HEX is a
	// two's-complement ring view that ignores Min/Max, so a clamped field is locked to DEC (no radix key).
	const bool bClamped = NumericParams.Min.IsSet() || NumericParams.Max.IsSet();
	const bool bIntegerNumeric = (KeyboardType == EKeyboardType::Number) && !NumericParams.bAllowDecimal && !bClamped;
	const bool bShowToggle = (KeyboardType != EKeyboardType::Number) || bIntegerNumeric;
	ToggleTypeKey->SetVisibility(bShowToggle ? EVisibility::Visible : EVisibility::Collapsed);

	// Preview-row backspace: only on the numeric pad (the QWERTY/T9 grids carry their own ⌫ key, so
	// showing it here too would duplicate it). Sign (±) visibility is handled by UpdateSignKey.
	if (PreviewBackspaceKey.IsValid())
		PreviewBackspaceKey->SetVisibility(
			(KeyboardType == EKeyboardType::Number || KeyboardType == EKeyboardType::QWERTY)
				? EVisibility::Visible : EVisibility::Collapsed);
}

// ==================== Show / Hide ====================

void SImSlateVirtualKeyboard::Show(const FVirtualKeyboardShowParams& Params)
{
	CurrentText = Params.InitialText;
	OriginalText = Params.InitialText;
	CursorPosition = (Params.CursorPosition == INDEX_NONE) ? CurrentText.Len() : Params.CursorPosition;
	CommitCallback = Params.CommitCallback;
	OnTextChanged = Params.OnTextChanged;
	SuggestionProvider = Params.SuggestionProvider;
	BoundOwner = Params.Owner;  // rebind to the new editable widget's lifecycle
	bSuggestionCommitsAndCloses = Params.bSuggestionCommitsAndCloses;

	// Modal background: block taps from reaching the game when requested.
	if (BackgroundBlocker.IsValid())
		BackgroundBlocker->SetVisibility(Params.bBlockBackground ? EVisibility::Visible : EVisibility::Collapsed);

	ShiftState = EShiftState::Default;
	CurrentLayer = 0;
	BackspaceUndoStack.Reset();
	// Apply the requested initial layout (default QWERTY = unchanged behaviour; Number = numeric pad).
	KeyboardType = Params.InitialKeyboardType;
	NumericParams = Params.Numeric;   // shapes the numeric keypad (decimal/negative/hex/step/clamp)
	NumericRadix = Params.Numeric.bHex ? 16 : 10;  // initial entry base (radix-toggle cycles it for integers)
	HistoryKey = Params.HistoryKey;   // non-empty → input history in the suggestion bar
	HistoryFilter = Params.HistoryFilter;
	HistoryMax = Params.MaxHistory;
	UpdateToggleTypeLabel();
	UpdateToggleTypeKeyVisibility();  // numeric pad hides the type-toggle key
	CalcReset();                      // start each session with a clean calculator (no stale expr/memory)
	// CalcReset() leaves bCalcFreshOperand=true (the "next digit starts a fresh operand" flag). But we just
	// loaded the field's existing value into CurrentText above — the first keypress must INSERT into it, not
	// wipe it (OnKeyInput's fresh-operand branch clears CurrentText). So when a value is preloaded, treat it
	// as an existing operand: clear the flag. (Empty initial value keeps the flag → first digit just types.)
	if (!CurrentText.IsEmpty())
		bCalcFreshOperand = false;
	ClearDigitHighlight();            // no preview digit selected until the user presses the preview
	UpdateSignKey();                  // show/hide + label the preview-row ± key per type/range

	// DESKTOP + currently faking-touch (bUseMouseForTouch): while the keyboard is up, disable mouse-as-touch so
	// preview dragging runs the REAL mouse path (OnMouseButtonDown/Move/Up). Only the mouse path can engage
	// high-precision (its reply reaches ProcessReply; touch's does not) → pinned + hidden cursor + clip that
	// actually holds. Two parts: (1) flip bUseMouseForTouch off at the SOURCE so GameViewportClient's
	// MouseEnter/ReceivedFocus stop re-enabling it (GetUseMouseForTouch() is read fresh, not cached); (2) clear
	// the CURRENT bIsGameFakingTouch via SetGameIsFakingTouchEvents(false) (flipping the config alone doesn't
	// reset already-true state). Gated by IsFakingTouchEvents() so non-faking / mobile is a no-op.
#if PLATFORM_DESKTOP
	bSavedUseMouseForTouch = false;
	if (UInputSettings* IS = GetMutableDefault<UInputSettings>())
	{
		// Disable based on the CONFIG (not the live faking state): even if faking isn't active *this instant*
		// (mouse not yet in viewport), the config being true means GameViewportClient will enable it on the next
		// MouseEnter/click — which is exactly the "cursor becomes faketouch after clicking" the user saw. Flipping
		// the config off now makes GetUseMouseForTouch() return false → that re-enable never happens.
		if (IS->bUseMouseForTouch)
		{
			bSavedUseMouseForTouch = true;
			IS->bUseMouseForTouch = false;
		}
		// Also clear any CURRENTLY-active faking state (config flip alone doesn't reset bIsGameFakingTouch).
		if (FSlateApplication::IsInitialized())
			FSlateApplication::Get().SetGameIsFakingTouchEvents(false);
	}
#endif

	bVisible = true;
	SetVisibility(EVisibility::Visible);

	// Build once now (may be collapsed if geometry isn't ready). Tick() will rebuild
	// with the correct split/full mode as soon as it has authoritative geometry.
	BuiltWidth = -1.f;
	BuildKeyboard();

	// Start cursor blink
	if (!CursorBlinkTimer.IsValid())
	{
		CursorBlinkTimer = RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double, float) -> EActiveTimerReturnType {
				if (!bVisible) { CursorBlinkTimer.Reset(); return EActiveTimerReturnType::Stop; }
				// Owner gone (panel closed / widget destroyed) → hide keyboard
				if (!BoundOwner.IsValid())
				{
					CursorBlinkTimer.Reset();
					Hide(false);
					return EActiveTimerReturnType::Stop;
				}
				bCursorVisible = !bCursorVisible;
				if (PreviewEdit.IsValid())
					PreviewEdit->SetPreviewCaretVisible(bCursorVisible);  // caret is drawn by PreviewEdit
				Invalidate(EInvalidateWidgetReason::Paint);  // repaint subtree (incl. PreviewEdit)
				return EActiveTimerReturnType::Continue;
			}));
	}

	UpdatePreview();
	UpdateSuggestions();

	// Take Slate user focus onto the keyboard ROOT so physical keys route here (not to the
	// editor viewport's global shortcuts). Deferred to Tick: right now the widget may have just
	// been made Visible and isn't in the widget tree yet, so SetKeyboardFocus/FindPathToWidget
	// could fail. Tick runs once the keyboard is parented & has authoritative geometry.
	bPendingFocus = true;
}

void SImSlateVirtualKeyboard::Hide(bool bCommit)
{
	if (!bVisible) return;

	// If the keyboard is dismissed mid value-scrub, restore the (hidden) cursor first so it never gets
	// stuck invisible.
	EndPreviewMouseDrag();

	// Restore mouse-as-touch if Show() disabled it (desktop faking). Only when we actually toggled it.
#if PLATFORM_DESKTOP
	if (bSavedUseMouseForTouch)
	{
		if (UInputSettings* IS = GetMutableDefault<UInputSettings>())
			IS->bUseMouseForTouch = true;
		bSavedUseMouseForTouch = false;
	}
#endif

	bVisible = false;
	bPendingFocus = false;
	SetVisibility(EVisibility::Collapsed);

	// Release focus back to the game/editor viewport so its shortcuts work again once the
	// keyboard is gone (we no longer SupportsKeyboardFocus while hidden).
	if (FSlateApplication::IsInitialized() && HasAnyUserFocusOrFocusedDescendants())
		FSlateApplication::Get().SetAllUserFocusToGameViewport();

	if (bCommit)
	{
		// Record successful (Enter/Done) input into history when enabled and it passes the filter.
		if (!HistoryKey.IsEmpty() && !CurrentText.IsEmpty() && (!HistoryFilter || HistoryFilter(CurrentText)))
			FImSlateInputHistory::Get().Add(HistoryKey, CurrentText, HistoryMax);

		if (CommitCallback)
			CommitCallback(CurrentText, ETextCommit::OnEnter);
	}
	else
	{
		if (CommitCallback)
			CommitCallback(OriginalText, ETextCommit::OnCleared);
	}

	CommitCallback = nullptr;
	OnTextChanged = nullptr;
	SuggestionProvider = nullptr;
	BoundOwner.Reset();
	CurrentSuggestions.Reset();
	SelectedSuggestionIndex = -1;
	HistoryKey.Reset();
	HistoryFilter = nullptr;
	HistoryMax = 10;
}

bool SImSlateVirtualKeyboard::IsOwnedBy(const SWidget* Widget) const
{
	return Widget != nullptr && BoundOwner.Pin().Get() == Widget;
}

void SImSlateVirtualKeyboard::NotifyOwnerDestroyed(const SWidget* Widget)
{
	// Auto-hide only when the editable we're bound to is the one being destroyed.
	// If the keyboard was rebound to another editable, this is a no-op.
	if (IsOwnedBy(Widget))
		Hide(false);
}

// ==================== Text Editing ====================

void SImSlateVirtualKeyboard::InsertText(const FString& Text)
{
	if (CursorPosition >= CurrentText.Len())
		CurrentText += Text;
	else
		CurrentText.InsertAt(CursorPosition, Text);
	CursorPosition += Text.Len();
	ClearDigitHighlight();    // text structure changed → stale preview selection no longer valid
	BackspaceUndoStack.Reset();  // a fresh insertion ends the delete/undo sequence (stored positions would
	                             // no longer line up with the new text), matching typical editor behaviour
}

void SImSlateVirtualKeyboard::DeleteBackward()
{
	if (CursorPosition > 0 && CurrentText.Len() > 0)
	{
		CurrentText.RemoveAt(CursorPosition - 1, 1);
		CursorPosition--;
		ClearDigitHighlight();  // text structure changed → stale preview selection no longer valid
	}
}

// Remove redundant leading zeros from the integer part of CurrentText, keeping the caret on the same logical
// spot. "05"→"5", "0.1"+digit "05.1"→"5.1", "007"→"7"; a value that IS just zero stays "0" ("0", "0.5",
// "0." keep their single leading 0). A leading '-' sign is preserved. DEC plain-numeric only (caller gates).
void SImSlateVirtualKeyboard::StripLeadingZeros()
{
	// Where the integer part starts (after an optional '-' sign).
	const int32 Start = (CurrentText.Len() > 0 && CurrentText[0] == TEXT('-')) ? 1 : 0;

	// End of the integer part = first '.' (or end of string).
	int32 DotPos = CurrentText.Len();
	for (int32 i = Start; i < CurrentText.Len(); ++i)
		if (CurrentText[i] == TEXT('.')) { DotPos = i; break; }

	// Count leading '0's in the integer part, but never strip the LAST integer digit (so an all-zero integer
	// part collapses to a single "0", and "0.5" keeps its "0").
	int32 Zeros = 0;
	while (Start + Zeros < DotPos - 1 && CurrentText[Start + Zeros] == TEXT('0'))
		++Zeros;

	if (Zeros == 0)
		return;

	CurrentText.RemoveAt(Start, Zeros);
	// Shift the caret left by however many stripped zeros sat at or before it.
	if (CursorPosition > Start)
		CursorPosition = FMath::Max(Start, CursorPosition - Zeros);
}

// Record a deletion for undo: the removed text and the index it was removed from. Each call is one undo
// step; UndoBackspaceFromStack restores the most recent one.
void SImSlateVirtualKeyboard::PushBackspaceUndo()
{
	// Snapshot the COMPLETE value + caret as they are RIGHT NOW (i.e. just before the deletion the caller is
	// about to perform). Undo restores this snapshot wholesale. Empty value → nothing meaningful to restore.
	if (CurrentText.IsEmpty())
		return;
	FBackspaceUndoEntry Entry;
	Entry.Snapshot = CurrentText;
	Entry.Caret = CursorPosition;
	BackspaceUndoStack.Add(MoveTemp(Entry));
}

// Delete the char left of the caret AND record it (with its position) so a right-swipe undo can restore it
// exactly where it was. Returns true if something was deleted.
bool SImSlateVirtualKeyboard::DeleteBackwardWithUndo()
{
	if (CursorPosition <= 0 || CurrentText.Len() == 0)
		return false;
	PushBackspaceUndo();   // snapshot the whole value + caret BEFORE removing, so undo restores it exactly
	DeleteBackward();
	return true;
}

// Undo the most recent recorded deletion: reinsert the stored text at its original index and place the
// caret just after it. Restores position-faithfully regardless of where the caret moved meanwhile.
void SImSlateVirtualKeyboard::UndoBackspaceFromStack()
{
	if (BackspaceUndoStack.Num() == 0)
		return;
	// Restore the snapshot taken before the last deletion: overwrite the WHOLE value + caret. Because the
	// entire value is replaced, any synthetic "0" placeholder UpdatePreview left behind (numeric field deleted
	// to empty) is simply discarded — no special-casing, and a real leading "0" survives intact. This is the
	// fix for "0.123 del-to-empty then undo → .123": the old per-char model couldn't tell the placeholder "0"
	// from a restored real "0" and dropped the latter; snapshots have no such ambiguity.
	const FBackspaceUndoEntry Entry = BackspaceUndoStack.Pop();
	CurrentText = Entry.Snapshot;
	CursorPosition = FMath::Clamp(Entry.Caret, 0, CurrentText.Len());
	ClearDigitHighlight();
}

void SImSlateVirtualKeyboard::MoveCursor(int32 Delta)
{
	CursorPosition = FMath::Clamp(CursorPosition + Delta, 0, CurrentText.Len());
}

void SImSlateVirtualKeyboard::AdjustDigitAtCursor(int32 Direction)
{
	// Up/Down on the cursor key bumps the DIGIT to the LEFT of the caret by ±1, with carry — i.e.
	// add/subtract that digit's place value to the whole number so 9→0 carries into the next column.
	// Works for integers (any radix) and decimal floats; the caret is re-anchored to the same place.
	const FString Old = CurrentText;
	if (Old.IsEmpty())
		return;

	// Digit to adjust. Preview drag-to-edit drives an explicit SelectedDigitIndex (the highlighted digit) —
	// use it directly when valid. Otherwise (cursor-key path / physical input) fall back to the digit just
	// LEFT of the caret (skip non-digits like '-'/'.'); if the caret is at the very start, fall back to the
	// FIRST digit to the RIGHT so a left-anchored caret still bumps the leading (most-significant) digit.
	int32 Idx;
	bool bFromLeftEdge = false;
	if (SelectedDigitIndex != INDEX_NONE && Old.IsValidIndex(SelectedDigitIndex) && FChar::IsHexDigit(Old[SelectedDigitIndex]))
	{
		Idx = SelectedDigitIndex;
	}
	else
	{
		Idx = FMath::Clamp(CursorPosition, 0, Old.Len()) - 1;
		while (Idx >= 0 && !FChar::IsHexDigit(Old[Idx]))
			--Idx;
		if (Idx < 0)
		{
			for (int32 i = 0; i < Old.Len(); ++i)
				if (FChar::IsHexDigit(Old[i])) { Idx = i; bFromLeftEdge = true; break; }
			if (Idx < 0)
				return;  // no digit at all
		}
	}

	const bool bFloat = NumericParams.bAllowDecimal && NumericRadix == 10;
	const bool bHex   = !bFloat && NumericRadix == 16;
	const int32 DotPos = Old.Find(TEXT("."));

	// Is the adjusted column the most-significant digit (first hex-digit char in the string)?
	int32 FirstDigit = INDEX_NONE;
	for (int32 i = 0; i < Old.Len(); ++i)
		if (FChar::IsHexDigit(Old[i])) { FirstDigit = i; break; }
	const bool bIsTopDigit = (Idx == FirstDigit);

	if (bHex)
	{
		// HEX: two's-complement ring at BitWidth. Add/subtract the column's place value (16^right) on the
		// unsigned view and wrap with the width mask — no '-' sign, no Min/Max clamp (the ring IS the range).
		int32 RightDigits = 0;
		for (int32 i = Idx + 1; i < Old.Len(); ++i)
			if (FChar::IsHexDigit(Old[i])) ++RightDigits;
		uint64 Place = 1; for (int32 i = 0; i < RightDigits; ++i) Place *= 16ull;
		const uint64 Mask = NumericWidthMask(NumericParams.BitWidth);
		uint64 U = FCString::Strtoui64(*Old, nullptr, 16) & Mask;
		U = (Direction > 0) ? (U + Place) : (U - Place);
		U &= Mask;  // wrap on the [0, 2^width) ring
		CurrentText = FormatIntInRadix((int64)U, 16, NumericParams.BitWidth);
	}
	else if (!bFloat && bIsTopDigit)
	{
		// DEC most-significant digit: continuous cross-magnitude step (1000→900→…→10→9→…→0). Only for
		// positive values; negatives fall back to the standard place-value step below.
		int64 Val = FCString::Strtoi64(*Old, nullptr, 10);
		if (Val > 0)
		{
			Val = (Direction < 0) ? TopDigitStepDown(Val) : TopDigitStepUp(Val);
			if (NumericParams.Min.IsSet()) Val = FMath::Max(Val, (int64)NumericParams.Min.GetValue());
			if (NumericParams.Max.IsSet()) Val = FMath::Min(Val, (int64)NumericParams.Max.GetValue());
			CurrentText = FormatIntInRadix(Val, 10, NumericParams.BitWidth);
		}
		else
		{
			// Val <= 0: standard place-value step (place = 10^right).
			int32 RightDigits = 0;
			for (int32 i = Idx + 1; i < Old.Len(); ++i)
				if (FChar::IsHexDigit(Old[i])) ++RightDigits;
			int64 Place = 1; for (int32 i = 0; i < RightDigits; ++i) Place *= 10;
			Val += (int64)Direction * Place;
			if (NumericParams.Min.IsSet()) Val = FMath::Max(Val, (int64)NumericParams.Min.GetValue());
			if (NumericParams.Max.IsSet()) Val = FMath::Min(Val, (int64)NumericParams.Max.GetValue());
			CurrentText = FormatIntInRadix(Val, 10, NumericParams.BitWidth);
		}
	}
	else if (!bFloat)
	{
		// DEC non-top integer column: standard place-value step.
		int32 RightDigits = 0;
		for (int32 i = Idx + 1; i < Old.Len(); ++i)
			if (FChar::IsHexDigit(Old[i])) ++RightDigits;
		int64 Place = 1; for (int32 i = 0; i < RightDigits; ++i) Place *= 10;

		int64 Val = FCString::Strtoi64(*Old, nullptr, 10);
		Val += (int64)Direction * Place;
		if (NumericParams.Min.IsSet()) Val = FMath::Max(Val, (int64)NumericParams.Min.GetValue());
		if (NumericParams.Max.IsSet()) Val = FMath::Min(Val, (int64)NumericParams.Max.GetValue());
		CurrentText = FormatIntInRadix(Val, 10, NumericParams.BitWidth);
	}
	else
	{
		// Decimal float. The cursor's digit drives a place-value step, EXCEPT the most-significant integer
		// digit which uses the same cross-magnitude "top digit" step as integers (100000→90000, not →0). The
		// fractional part is preserved across a top-digit step (500000.5→400000.5→…→90000.5). Works on the
		// MAGNITUDE so negatives behave symmetrically (sign kept): -100000 toward 0 → -90000, away → -200000.
		double Val = FCString::Atod(*Old);
		const bool bIntegerSide = (DotPos == INDEX_NONE || Idx < DotPos);
		const double IntPartD = FMath::TruncToDouble(FMath::Abs(Val));

		if (bIsTopDigit && bIntegerSide && IntPartD > 0.0)
		{
			// Same handling as an integer field's top digit, applied to the |INTEGER part|; keep the fraction
			// and the sign. "Down" (Direction<0) shrinks the magnitude, "Up" grows it — for a negative value
			// that means down→toward 0, up→more negative (the digit under the caret still goes the way it
			// visually should, since it's the magnitude's leading digit).
			const double Sign = (Val < 0.0) ? -1.0 : 1.0;
			const double Frac = FMath::Abs(Val) - IntPartD;
			int64 IntPart = (int64)IntPartD;
			IntPart = (Direction < 0) ? TopDigitStepDown(IntPart) : TopDigitStepUp(IntPart);
			Val = Sign * ((double)IntPart + Frac);
		}
		else
		{
			// Standard place-value step: 10^(exponent of the cursor's digit relative to the dot).
			int32 Exp;
			if (bIntegerSide)
			{
				const int32 End = (DotPos == INDEX_NONE) ? Old.Len() : DotPos;
				int32 RightDigits = 0;
				for (int32 i = Idx + 1; i < End; ++i) if (FChar::IsHexDigit(Old[i])) ++RightDigits;
				Exp = RightDigits;
			}
			else
			{
				Exp = -(Idx - DotPos);  // fractional side: 1-based position after the dot → negative exponent
			}
			const double Place = FMath::Pow(10.0, (double)Exp);
			Val += (double)Direction * Place;
		}
		if (NumericParams.Min.IsSet()) Val = FMath::Max(Val, NumericParams.Min.GetValue());
		if (NumericParams.Max.IsSet()) Val = FMath::Min(Val, NumericParams.Max.GetValue());
		// PRESERVE the fractional digit count of the OLD text while SCRUBBING — SanitizeFloat strips trailing
		// zeros (1.50→1.5), which shrinks the decimals and makes the highlight jump as the user drags a low
		// fractional digit to 0. Only this drag-scrub path needs the fixed width; normal typing/commit is
		// unaffected (this code only runs from HandlePreviewDrag → AdjustDigitAtCursor). If the old text had N
		// fractional digits, format the new value with exactly N.
		{
			int32 OldFracDigits = 0;
			if (DotPos != INDEX_NONE)
				for (int32 i = DotPos + 1; i < Old.Len(); ++i)
					if (FChar::IsHexDigit(Old[i])) ++OldFracDigits;
			CurrentText = (OldFracDigits > 0)
				? FString::Printf(TEXT("%.*f"), OldFracDigits, Val)
				: FString::SanitizeFloat(Val);
		}
	}

	// Re-anchor the caret to stay on the SAME digit column.
	if (!bFromLeftEdge)
	{
		// Normal case: keep the same number of digits to the caret's RIGHT (caret sits right of the
		// adjusted digit), so it stays put even if a carry changed the total digit count.
		int32 RightOfCaret = 0;
		for (int32 i = Idx + 1; i < Old.Len(); ++i) if (FChar::IsHexDigit(Old[i])) ++RightOfCaret;
		int32 NewIdx = CurrentText.Len();
		int32 Seen = 0;
		for (int32 i = CurrentText.Len() - 1; i >= 0; --i)
		{
			if (FChar::IsHexDigit(CurrentText[i])) ++Seen;
			if (Seen > RightOfCaret) { NewIdx = i + 1; break; }
			if (i == 0) NewIdx = 0;
		}
		CursorPosition = FMath::Clamp(NewIdx, 0, CurrentText.Len());
	}
	else
	{
		// Left-edge case (caret was at the far left, bumping the leading digit).
		auto CountDigits = [](const FString& S) { int32 n = 0; for (TCHAR C : S) if (FChar::IsHexDigit(C)) ++n; return n; };
		const int32 OldDigits = CountDigits(Old);
		const int32 NewDigits = CountDigits(CurrentText);
		if (NewDigits <= OldDigits)
		{
			// No carry grew the number → keep the caret at the far left so repeated up-scrolls keep
			// bumping the SAME leading digit.
			CursorPosition = 0;
		}
		else
		{
			// A carry added a new most-significant digit. Don't leave the caret at the far left; place
			// it just AFTER the newly added leading digit(s), so it sits before the originally-adjusted
			// column (which has shifted right by the number of new digits).
			const int32 Added = NewDigits - OldDigits;
			int32 NewIdx = 0, Seen = 0;
			for (int32 i = 0; i < CurrentText.Len(); ++i)
			{
				if (Seen >= Added) { NewIdx = i; break; }
				if (FChar::IsHexDigit(CurrentText[i])) ++Seen;
				NewIdx = i + 1;
			}
			CursorPosition = FMath::Clamp(NewIdx, 0, CurrentText.Len());
		}
	}

	// Preview drag-to-edit: re-lock the highlight onto the SAME digit COLUMN, independent of the caret
	// re-anchor above (which can mis-place the caret to index 0 when the value changes sign or digit count).
	// We anchor by "number of digits to the RIGHT of the adjusted digit" — that count is stable across a
	// ±1 bump of a single column, so we re-find the digit with the same right-count in the new string. This
	// also keeps the selection (and therefore the ruler) on-screen instead of snapping to the front.
	const int32 PrevSelForRuler = SelectedDigitIndex;  // detect whether the selected column actually moved
	if (SelectedDigitIndex != INDEX_NONE)
	{
		// Right-of-Idx digit count in the OLD string (Idx was the adjusted column).
		int32 RightDigits = 0;
		for (int32 i = Idx + 1; i < Old.Len(); ++i)
			if (FChar::IsHexDigit(Old[i])) ++RightDigits;
		// Walk the NEW string from the right; the target digit is the one with exactly RightDigits digits
		// to its right. If a carry GREW the number (more digits now), the leading new digit has the most
		// right-digits, so this still lands on the right column; if it can't be matched, fall back to the
		// most-significant digit (never index 0 / the sign).
		int32 NewSel = INDEX_NONE, Seen = 0;
		for (int32 i = CurrentText.Len() - 1; i >= 0; --i)
		{
			if (!FChar::IsHexDigit(CurrentText[i])) continue;
			if (Seen == RightDigits) { NewSel = i; break; }
			++Seen;
		}
		if (NewSel == INDEX_NONE)
		{
			// No exact match (digit count shrank) → snap to the most-significant digit.
			for (int32 i = 0; i < CurrentText.Len(); ++i)
				if (FChar::IsHexDigit(CurrentText[i])) { NewSel = i; break; }
		}
		SelectedDigitIndex = NewSel;  // INDEX_NONE only if no digit remains at all
		CursorPosition = (NewSel != INDEX_NONE) ? FMath::Clamp(NewSel + 1, 0, CurrentText.Len()) : CurrentText.Len();
		if (PreviewEdit.IsValid())
			PreviewEdit->SetHighlightDigit(SelectedDigitIndex);
	}

	UpdatePreview();
	UpdateSuggestions();
	UpdateSignKey();  // value (and possibly its sign) changed → refresh the ± key label
	// Re-align the scrolling ruler when the selected column moved OR the digit count changed (a magnitude
	// demotion like 1000001→900001 keeps the same index but shifts every cell's pixel X, so the ruler must
	// follow). A plain same-width step keeps the ruler put to preserve its continuous scroll offset.
	if (PreviewStepRuler.IsValid()
		&& (SelectedDigitIndex != PrevSelForRuler || CurrentText.Len() != Old.Len()))
		ShowPreviewStepRuler();
	if (OnTextChanged)
		OnTextChanged(CurrentText);
}

bool SImSlateVirtualKeyboard::IsUpperCase() const
{
	bool bDefaultUpper = (KeyboardType == EKeyboardType::T9);
	switch (ShiftState)
	{
	case EShiftState::SingleShot: return !bDefaultUpper;
	case EShiftState::Locked:     return !bDefaultUpper;
	default:                      return bDefaultUpper;
	}
}

void SImSlateVirtualKeyboard::ToggleShift()
{
	switch (ShiftState)
	{
	case EShiftState::Default:    ShiftState = EShiftState::SingleShot; break;
	case EShiftState::SingleShot: ShiftState = EShiftState::Locked; break;
	case EShiftState::Locked:     ShiftState = EShiftState::Default; break;
	}
}

void SImSlateVirtualKeyboard::UpdatePreview()
{
	bCursorVisible = true;
	// Numeric field never shows an empty value: deleting the last char (backspace / Clr) leaves "" which is
	// not a number and breaks digit hit-test / ruler placement. Fall back to "0" so there is always a digit.
	// (Only in plain numeric mode — not while a calculator expression is on screen, and not for text fields.)
	if (KeyboardType == EKeyboardType::Number && CalcExpr.IsEmpty()
		&& (CurrentText.IsEmpty() || CurrentText == TEXT("-")))
	{
		CurrentText = TEXT("0");
		CursorPosition = CurrentText.Len();
		SelectedDigitIndex = INDEX_NONE;  // re-picked on next press; stale index would point past "0"
	}
	// Calculator: while an expression is being built (CalcExpr non-empty, e.g. "5+3"), the preview shows
	// the EXPRESSION but the bound field is NOT synced — the field keeps its last confirmed value until
	// '=' produces a result. In plain numeric / text mode CalcExpr is empty and preview == CurrentText
	// (the value), which IS synced. This separation keeps the (numeric-only) bound field from ever
	// receiving a non-numeric expression string like "5+".
	const bool bExprMode = !CalcExpr.IsEmpty();
	const FString& Display = bExprMode ? CalcExpr : CurrentText;
	if (PreviewEdit.IsValid())
	{
		PreviewEdit->SetText(FText::FromString(Display));
		PreviewEdit->SetPreviewCaretVisible(true);
		// Expression mode: caret follows CalcCursorPos (left/right drag browses it). Plain mode: CursorPosition.
		const int32 Caret = bExprMode
			? FMath::Clamp(CalcCursorPos, 0, Display.Len())
			: FMath::Clamp(CursorPosition, 0, Display.Len());
		PreviewEdit->GoTo(FTextLocation(0, Caret));
	}
	if (OnTextChanged && !bExprMode)
		OnTextChanged(CurrentText);
}

int32 SImSlateVirtualKeyboard::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Caret is now self-drawn by PreviewEdit (SImEditableText preview mode) using the
	// engine's text layout, so the keyboard no longer paints it here.
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

// ==================== Editor Sync ====================

void SImSlateVirtualKeyboard::SyncFromEditor(const FString& Text, int32 CaretPos)
{
	if (!bVisible) return;

	// Follow the editor's real caret when provided (physical typing/arrows move it); else keep
	// the old position clamped. Compute the target caret first so we can detect caret-only
	// changes (e.g. a bare arrow key moves the caret without changing the text).
	const int32 NewCaret = (CaretPos >= 0)
		? FMath::Clamp(CaretPos, 0, Text.Len())
		: FMath::Clamp(CursorPosition, 0, Text.Len());

	// Bail only when NOTHING changed — both text and caret. Previously bailing on text-equality
	// alone meant arrow-key caret moves (text unchanged) never updated the preview caret.
	if (CurrentText == Text && CursorPosition == NewCaret) return;

	CurrentText = Text;
	CursorPosition = NewCaret;
	bCursorVisible = true;
	ClearDigitHighlight();  // editor-driven text/caret change → drop any stale preview digit selection
	if (PreviewEdit.IsValid())
	{
		PreviewEdit->SetText(FText::FromString(CurrentText));
		PreviewEdit->SetPreviewCaretVisible(true);
		PreviewEdit->GoTo(FTextLocation(0, CursorPosition));
	}
	UpdateSuggestions();
}

// ==================== Preview Drag + Cursor Blink ====================

bool SImSlateVirtualKeyboard::IsInPreviewArea(const FVector2D& AbsPos) const
{
	if (PreviewBorder.IsValid())
		return PreviewBorder->GetCachedGeometry().IsUnderLocation(AbsPos);
	return false;
}

// On press in the preview area: hit-test the digit under the finger and make it the initial selection.
// Numeric pad + plain-numeric (no calculator expression) only; otherwise leaves selection cleared.
void SImSlateVirtualKeyboard::BeginPreviewSelectAt(const FVector2D& AbsScreenPos)
{
	SelectedDigitIndex = INDEX_NONE;
	if (!CalcExpr.IsEmpty() || KeyboardType != EKeyboardType::Number || !PreviewEdit.IsValid())
	{
		if (PreviewEdit.IsValid()) PreviewEdit->SetHighlightDigit(INDEX_NONE);
		return;
	}
	const int32 Hit = PreviewEdit->HitTestDigitIndex(AbsScreenPos);
	if (Hit != INDEX_NONE)
	{
		SelectedDigitIndex = Hit;
		PreviewEdit->SetHighlightDigit(Hit);
		CursorPosition = FMath::Clamp(Hit + 1, 0, CurrentText.Len());  // caret right of the selected digit
		if (PreviewEdit.IsValid()) PreviewEdit->GoTo(FTextLocation(0, CursorPosition));
	}
	else
	{
		PreviewEdit->SetHighlightDigit(INDEX_NONE);
	}
}

// Move the highlighted digit to the next digit char in Dir (+1 = right/lower place, -1 = left/higher),
// skipping non-digits ('-' '.'). Stops at the edge if there is no further digit.
void SImSlateVirtualKeyboard::MoveSelectedDigit(int32 Dir)
{
	if (SelectedDigitIndex == INDEX_NONE)
		return;
	int32 i = SelectedDigitIndex + Dir;
	while (i >= 0 && i < CurrentText.Len() && !FChar::IsHexDigit(CurrentText[i]))
		i += Dir;
	if (i < 0 || i >= CurrentText.Len())
		return;  // no further digit — stay put
	SelectedDigitIndex = i;
	if (PreviewEdit.IsValid())
	{
		PreviewEdit->SetHighlightDigit(i);
		CursorPosition = FMath::Clamp(i + 1, 0, CurrentText.Len());
		PreviewEdit->GoTo(FTextLocation(0, CursorPosition));
	}
	// If the step ruler is up, move it under the newly-highlighted digit immediately (horizontal switching used
	// to leave the ruler under the old digit until the next vertical drag re-positioned it).
	if (PreviewStepRuler.IsValid())
		ShowPreviewStepRuler();
}

// Drop the preview digit selection + its highlight. Called whenever the text STRUCTURE changes from a
// non-scrub path (typing a digit, backspace, editor sync), so a stale highlight never points at a moved
// or deleted character. NOT called from AdjustDigitAtCursor (it re-locks the selection itself).
void SImSlateVirtualKeyboard::ClearDigitHighlight()
{
	SelectedDigitIndex = INDEX_NONE;
	if (PreviewEdit.IsValid())
		PreviewEdit->SetHighlightDigit(INDEX_NONE);
}

// Whitelist of insertable characters on the numeric pad. DEC integer → 0-9 only (letters like f/d/g are
// rejected); HEX → 0-9 A-F; '.' only for float DEC; '-' only when negatives allowed. Operators/'=' are
// handled separately and never inserted as text. Non-numeric layouts: everything allowed.
bool SImSlateVirtualKeyboard::IsCharAllowedForNumeric(TCHAR Ch) const
{
	if (KeyboardType != EKeyboardType::Number)
		return true;
	const TCHAR C = FChar::ToUpper(Ch);
	if (C >= '0' && C <= '9')   return ((C - '0') < NumericRadix);
	if (C >= 'A' && C <= 'F')   return ((10 + (C - 'A')) < NumericRadix);  // only in HEX
	if (Ch == '.')              return NumericParams.bAllowDecimal && (NumericRadix == 10);
	if (Ch == '-')              return NumericParams.bAllowNegative && (NumericRadix == 10);  // no sign in HEX
	return false;  // any other char (letters g-z, punctuation, …) is not valid in a numeric field
}

void SImSlateVirtualKeyboard::HandlePreviewDrag(const FVector2D& Delta)
{
	// Delta is the per-move cursor delta: mouse passes GetCursorDelta (relative, cursor hidden during the
	// scrub so it stays put visually); touch passes a position diff. Both are in LOCAL pixels here.
	const float StepY = 12.f * GetKbScale();  // vertical travel per ±1 value step

	// Calculator expression mode (preview shows "5+3"): the digit-scrub model doesn't apply. Horizontal
	// drag browses the caret along the EXPRESSION string (CalcCursorPos); input still appends to the end.
	if (!CalcExpr.IsEmpty())
	{
		PreviewDragAccum += Delta.X;
		while (PreviewDragAccum >  StepY) { CalcCursorPos = FMath::Min(CalcCursorPos + 1, CalcExpr.Len()); PreviewDragAccum -= StepY; UpdatePreview(); }
		while (PreviewDragAccum < -StepY) { CalcCursorPos = FMath::Max(CalcCursorPos - 1, 0);            PreviewDragAccum += StepY; UpdatePreview(); }
		return;
	}
	// Non-numeric text input: original behaviour — horizontal drag moves the text caret.
	if (KeyboardType != EKeyboardType::Number)
	{
		PreviewDragAccum += Delta.X;
		while (PreviewDragAccum >  StepY) { MoveCursor(+1); PreviewDragAccum -= StepY; UpdatePreview(); }
		while (PreviewDragAccum < -StepY) { MoveCursor(-1); PreviewDragAccum += StepY; UpdatePreview(); }
		return;
	}

	// Accumulate the delta on BOTH axes FIRST (high-precision raw deltas arrive ~1px/frame, far below any
	// single-frame threshold — judging the axis on one frame's delta never locked it, so nothing moved).
	PreviewDragAccum += Delta.X;
	PreviewDragAccumY += Delta.Y;

	// Axis selection from ACCUMULATED travel (not one frame). Lock once the dominant axis passes a small
	// threshold; allow re-locking mid-drag when the other axis clearly takes over (reset both accumulators
	// so leftover travel doesn't instantly trigger).
	const float AccX = FMath::Abs(PreviewDragAccum), AccY = FMath::Abs(PreviewDragAccumY);
	const float AxisLockThresh = 3.f;
	if (PreviewDragAxis == 0)
	{
		if (AccX > AccY && AccX > AxisLockThresh)      PreviewDragAxis = 1;
		else if (AccY > AccX && AccY > AxisLockThresh) { PreviewDragAxis = 2; ShowPreviewStepRuler(); }
	}
	else if (PreviewDragAxis == 1 && AccY > AccX + AxisLockThresh)
	{
		PreviewDragAxis = 2; PreviewDragAccum = 0.f; PreviewDragAccumY = 0.f; ShowPreviewStepRuler();
	}
	else if (PreviewDragAxis == 2 && AccX > AccY + AxisLockThresh)
	{
		// Switch to horizontal (digit-select). Keep the step ruler VISIBLE — user wants it shown during
		// horizontal switching too (only hidden on release, by EndPreviewMouseDrag).
		PreviewDragAxis = 1; PreviewDragAccum = 0.f; PreviewDragAccumY = 0.f;
	}

	if (PreviewDragAxis == 1 && KeyboardType == EKeyboardType::Number && SelectedDigitIndex != INDEX_NONE)
	{
		// Horizontal: switch the highlighted digit. Base step = CellWidth × WidthMult; reverse-debounce via the
		// shared helper (Accum>0 = drag right = next column). Same-direction steps stay snappy.
		float L = 0.f, R = 0.f, CellW = 12.f * GetKbScale();
		if (PreviewEdit.IsValid() && PreviewEdit->GetDigitCellBounds(SelectedDigitIndex, L, R))
			CellW = FMath::Max(4.f, R - L);
		const float BaseStep = CellW * FMath::Max(GImSlatePreviewHWidthMult, 0.1f);
		ImSlateStepAccumulate(PreviewDragAccum, PreviewHLastStepDir, BaseStep, GetKbScale(),
			[this] { MoveSelectedDigit(+1); }, [this] { MoveSelectedDigit(-1); });
	}
	else if (PreviewDragAxis == 2 && KeyboardType == EKeyboardType::Number && SelectedDigitIndex != INDEX_NONE)
	{
		// Vertical: bump the highlighted digit by ±1 (carry + clamp), base step = StepY; reverse-debounce via the
		// shared helper. Screen Y grows downward, so AccumY>0 (drag DOWN) decreases the value and AccumY<0 (drag
		// UP) increases it — hence PosStep = AdjustDigitAtCursor(-1), NegStep = AdjustDigitAtCursor(+1).
		ImSlateStepAccumulate(PreviewDragAccumY, PreviewVLastStepDir, StepY, GetKbScale(),
			[this] { AdjustDigitAtCursor(-1); }, [this] { AdjustDigitAtCursor(+1); });
		if (PreviewStepRuler.IsValid())
		{
			// Delta is already in LOCAL pixels (callers divided out the DPI scale), and the ruler renders in
			// local space — feed Delta.Y directly so the ticks scroll in lockstep with the value.
			PreviewStepRuler->SetOffset(PreviewStepRuler->GetOffset() + (float)Delta.Y);
		}
	}
}

// Overlay a value-axis (horizontal-tick) ruler over the CURRENTLY SELECTED DIGIT during a vertical scrub,
// so the scrolling ticks sit right on the digit being changed (not stretched across the whole text box).
void SImSlateVirtualKeyboard::ShowPreviewStepRuler()
{
	if (!RootOverlay.IsValid() || !PreviewEdit.IsValid())
		return;
	HidePreviewStepRuler();  // never stack two

	const FGeometry RootGeo = RootOverlay->GetCachedGeometry();
	const FGeometry PrevGeo = PreviewEdit->GetCachedGeometry();
	const float RootScale = FMath::Max(0.01f, RootGeo.GetAccumulatedLayoutTransform().GetScale());

	// Default: span the whole PreviewEdit (used when there is no selected digit, e.g. fallback).
	FVector2D LocalPos = RootGeo.AbsoluteToLocal(PrevGeo.GetAbsolutePosition());
	FVector2D LocalSize = FVector2D(PrevGeo.GetAbsoluteSize()) / RootScale;

	// Preferred: clamp the ruler's WIDTH to the selected digit's cell (so ticks sit over that digit), but
	// make its HEIGHT fill the whole preview ROW (PreviewBorder). The cell's X edges come from the SAME
	// coordinate path as the blinking caret (GetLocalXAt = GetLocationAt/Scale): left edge = the digit's X,
	// right edge = the next index's X (where the caret sits). This guarantees the ruler aligns exactly with
	// the caret/highlight rather than drifting.
	float CellL = 0.f, CellR = 0.f;
	if (SelectedDigitIndex != INDEX_NONE
		&& PreviewEdit->GetLocalXAt(SelectedDigitIndex, CellL)
		&& PreviewEdit->GetLocalXAt(SelectedDigitIndex + 1, CellR)
		&& CellR != CellL)
	{
		if (CellL > CellR) { const float T = CellL; CellL = CellR; CellR = T; }  // normalize (RTL safety)
		const FVector2D AbsL = PrevGeo.LocalToAbsolute(FVector2D(CellL, 0.f));
		const FVector2D AbsR = PrevGeo.LocalToAbsolute(FVector2D(CellR, 0.f));
		const float CellAbsW = AbsR.X - AbsL.X;
		// Vertical extent: full preview-row height when available, else fall back to the text height.
		const FGeometry RowGeo = PreviewBorder.IsValid() ? PreviewBorder->GetCachedGeometry() : PrevGeo;
		const FVector2D RowAbsPos = RowGeo.GetAbsolutePosition();
		const FVector2D RowAbsSize = FVector2D(RowGeo.GetAbsoluteSize());
		// Top-left = (selected digit's X, preview-row top); size = (cell width, full row height).
		const FVector2D AbsTL(AbsL.X, RowAbsPos.Y);
		LocalPos = RootGeo.AbsoluteToLocal(AbsTL);
		LocalSize = FVector2D(CellAbsW, RowAbsSize.Y) / RootScale;
	}

	PreviewStepRuler = SNew(SImStepRuler)
		.Axis(EOrientation::Orient_Horizontal)   // value axis: horizontal ticks that scroll vertically
		.StepW(12.f * GetKbScale());
	PreviewStepRuler->SetTickFractions(1.0f, 0.5f);  // long tick = full width (1 char), short = half
	PreviewStepRuler->SetOffset(0.f);
	PreviewStepRuler->SetVisibility(EVisibility::HitTestInvisible);

	TSharedRef<SWidget> Host = SNew(SBox)
		.WidthOverride(LocalSize.X)
		.HeightOverride(LocalSize.Y)
		[
			PreviewStepRuler.ToSharedRef()
		];
	PreviewStepRulerHost = Host;

	RootOverlay->AddSlot()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(FMargin(LocalPos.X, LocalPos.Y, 0.f, 0.f))
	[
		Host
	];
}

void SImSlateVirtualKeyboard::HidePreviewStepRuler()
{
	if (PreviewStepRulerHost.IsValid() && RootOverlay.IsValid())
		RootOverlay->RemoveSlot(PreviewStepRulerHost.ToSharedRef());
	PreviewStepRulerHost.Reset();
	PreviewStepRuler.Reset();
}

FReply SImSlateVirtualKeyboard::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (IsInPreviewArea(MouseEvent.GetScreenSpacePosition()))
	{
		// Start every press from a CLEAN drag state regardless of how the previous one ended (leaked flags from
		// SetCursorPos-synthesized moves, lost capture, etc.) — don't trust prior teardown.
		bPreviewIgnoreSyntheticMove = false;
		bPreviewDragging = true;
		SetKeyboardGridHitTestable(false);  // scrubbing → keys can't be hit by a second finger (multi-touch misfire)
		PreviewDragLastPos = MouseEvent.GetScreenSpacePosition();
		PreviewDragAccum = 0.f;
		PreviewDragAccumY = 0.f;
		PreviewDragAxis = 0;
		BeginPreviewSelectAt(MouseEvent.GetScreenSpacePosition());
		// Remember the press point so we can restore the cursor THERE on release (only meaningful under
		// high-precision, where the cursor is pinned + hidden — see EndPreviewMouseDrag).
		PreviewDragAnchorScreen = MouseEvent.GetScreenSpacePosition();
		// SSpinBox pattern (Slate/Private/Widgets/Input/SSpinBox.cpp:301): grab capture + high-precision in one
		// reply, plus LockMouseToWidget to confine the cursor to the keyboard rect (it fills the viewport). All
		// three are applied together by ProcessReply and torn down together by the matching ReleaseMouse* replies
		// on button-up / capture-lost. High-precision DOES engage here (logs: PIE TopWindowVirtual=0, HiPrec=1
		// from the first move), so the cursor is genuinely pinned and the lock's ClipCursor doesn't yank it.
		// SetUserFocus mirrors SSpinBox.cpp:301 — re-assert focus onto the keyboard root on press. Without it,
		// after clicking OUTSIDE the preview area (which leaves focus elsewhere), a subsequent press-to-drag may
		// not route moves/capture to us → "drag does nothing after clicking out and back". Same target as the
		// Tick/bPendingFocus grab (the keyboard root), so they don't fight.
		return FReply::Handled()
			.CaptureMouse(SharedThis(this))
			.UseHighPrecisionMouseMovement(SharedThis(this))
			.LockMouseToWidget(SharedThis(this))
			.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return FReply::Handled();
}

void SImSlateVirtualKeyboard::EndPreviewMouseDrag()
{
	if (!bPreviewDragging)
		return;
	// Restore the cursor to the press point ONLY when high-precision was engaged (non-PIE): there the cursor
	// was pinned + hidden, so moving it back to the anchor is both meaningful and invisible (do it BEFORE
	// clearing bPreviewDragging, while OnCursorQuery still hides it). When high-precision did NOT engage (PIE),
	// the cursor was visible and followed the mouse freely — yanking it back to the press point would be a
	// jarring visible jump, and SetCursorPos is unreliable there anyway, so leave it where it is.
	if (FSlateApplication::Get().IsUsingHighPrecisionMouseMovment())
	{
		// SetCursorPos warps the cursor, which SYNTHESIZES a mouse-move event. In PIE that warp is unreliable and
		// the synthesized move arrives while bPreviewDragging is still true (we clear it below, AFTER this, to keep
		// the cursor hidden during the warp — see :2412). That synthesized frame then scrubs the value from a stale
		// position. Flag it so OnMouseMove skips exactly that frame. (This is what bPreviewIgnoreSyntheticMove was
		// designed for; it had been set but never read.)
		bPreviewIgnoreSyntheticMove = true;
		FSlateApplication::Get().SetCursorPos(PreviewDragAnchorScreen);
	}
	// NOTE: capture / high-precision / mouse-lock are NOT released here — EndPreviewMouseDrag is void and can't
	// return an FReply. The normal button-up path releases them via the reply in OnMouseButtonUp; the abnormal
	// capture-lost path re-issues that reply explicitly in OnMouseCaptureLost. This keeps every teardown on the
	// engine's standard ProcessReply path (the only place that turns high-precision back off — SlateApplication
	// .cpp:3578), matching SSpinBox. Doing it by hand here is what previously leaked high-precision globally.
	bPreviewDragging = false;
	// Scrub over → release all finger ownership (next touch starts a fresh scrub).
	PreviewDragPointerIndex = -1;
	PreviewTouches[0] = FPreviewTouch{};
	PreviewTouches[1] = FPreviewTouch{};
	PreviewTouchCount = 0;
	PreviewHLastStepDir = 0;  // fresh scrub next time → no stale reverse-debounce direction
	PreviewVLastStepDir = 0;
	HidePreviewStepRuler();
	bPreviewIgnoreSyntheticMove = false;
	SetKeyboardGridHitTestable(true);  // scrub fully ended (last finger up) → keys hittable again
}

void SImSlateVirtualKeyboard::SetKeyboardGridHitTestable(bool bHittable)
{
	// During a preview scrub the whole key area is made HitTestInvisible so a SECOND finger can't press a key
	// (mouse capture is per-PointerIndex — SlateUser.h:65-76 — so it only locks the driving finger). SetVisibility
	// does NOT rebuild (only BuildKeyboard does), and capture is hit-test-independent, so the in-progress scrub is
	// unaffected. The preview row lives in RootOverlay, NOT in KeyboardGrid, so it stays interactive throughout.
	if (KeyboardGrid.IsValid())
		KeyboardGrid->SetVisibility(bHittable ? EVisibility::Visible : EVisibility::HitTestInvisible);
}

FCursorReply SImSlateVirtualKeyboard::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	// Hide the OS cursor while scrubbing — but ONLY when high-precision mode actually engaged, i.e. the cursor
	// is genuinely PINNED in place (non-PIE / real desktop runtime). Hiding only makes sense to mask a pinned
	// cursor: if it's pinned, hide it so the user doesn't see a frozen arrow; if it's NOT pinned (PIE, where
	// bIsVirtualInteraction blocks high-precision — SlateApplication.cpp:3536), hiding would make the cursor
	// vanish while it still physically moves around → user loses it. So when not pinned, leave it visible and
	// let it follow the mouse freely. (Slate re-queries the cursor every frame, so None reliably overrides
	// the normal cursor — see reference-ue-high-precision-mouse.)
	if (bPreviewDragging && FSlateApplication::Get().IsUsingHighPrecisionMouseMovment())
		return FCursorReply::Cursor(EMouseCursor::None);
	return FCursorReply::Unhandled();
}

FReply SImSlateVirtualKeyboard::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const bool bWasCapturing = HasMouseCapture();
	EndPreviewMouseDrag();
	// Tear down capture + high-precision + mouse-lock together via the reply (SSpinBox pattern). ReleaseMouseCapture
	// is what drives ProcessReply:3578 to turn high-precision back off; ReleaseMouseLock undoes LockMouseToWidget.
	return bWasCapturing
		? FReply::Handled().ReleaseMouseCapture().ReleaseMouseLock()
		: FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bPreviewDragging)
	{
		// Skip the single synthesized move produced by EndPreviewMouseDrag's SetCursorPos warp (it set this flag
		// right before warping). Consuming it would scrub the value from the stale anchor position. One-shot.
		if (bPreviewIgnoreSyntheticMove)
		{
			bPreviewIgnoreSyntheticMove = false;
			return FReply::Handled();
		}

		// SSpinBox guard (SSpinBox.cpp:425): bPreviewDragging is our own flag and CAN leak true without a real
		// drag (e.g. a synthesized move after teardown). HasMouseCapture() is the authoritative "drag in progress"
		// test — every real drag (mouse OR faking-touch) holds capture. If we don't, bail rather than scrub from a
		// stale position.
		if (!HasMouseCapture())
		{
			EndPreviewMouseDrag();
			return FReply::Unhandled();
		}

		// HandlePreviewDrag thresholds are in LOCAL pixels, mouse deltas are in SCREEN pixels — convert first.
		const float DPIScale = GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
		const float InvScale = (DPIScale > 0.f) ? (1.f / DPIScale) : 1.f;

		// GetCursorDelta() is correct in BOTH modes: high-precision → raw delta (ScreenSpacePosition frozen);
		// otherwise → ScreenPos-minus-last. Either way it's the per-event motion.
		const FVector2D Delta = MouseEvent.GetCursorDelta();
		if (!Delta.IsNearlyZero())
			HandlePreviewDrag(Delta * InvScale);
	}
	return FReply::Handled();
}

void SImSlateVirtualKeyboard::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Pure SSpinBox pattern: just end our own drag bookkeeping. Do NOT issue any ReleaseMouseCapture /
	// high-precision reply here. OnMouseCaptureLost ALSO fires on the NORMAL button-up path (OnMouseButtonUp's
	// ReleaseMouseCapture reply → engine ReleaseCapture → this callback). Re-issuing a reply here re-entered
	// ReleaseCapture and corrupted the global mouse state mid-teardown — high-precision never cleared (HiPrec
	// stayed 1) and the next drag came up HiPrec=0 with giant deltas. Capture + high-precision are released by
	// OnMouseButtonUp's reply (normal end) and by the engine itself when capture is taken away; this callback
	// must only reset OUR flags so a stale drag doesn't linger.
	EndPreviewMouseDrag();
}

FReply SImSlateVirtualKeyboard::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	const int32 Idx = InTouchEvent.GetPointerIndex();
	const FVector2D Pos = InTouchEvent.GetScreenSpacePosition();

	// RELAY multi-touch: a scrub is already running. If this NEW finger also lands in the preview area, register
	// it (up to 2) and HAND DRIVING OVER to it — but DON'T reset the accumulators/axis, so the in-progress
	// scrub continues seamlessly. A 3rd finger, or a touch outside the preview, is ignored. Only the driving
	// finger moves the value (no speed-doubling); when it lifts, driving relays to the other finger (see Ended).
	if (bPreviewDragging)
	{
		if (IsInPreviewArea(Pos) && PreviewTouchCount < 2 && FindPreviewTouch(Idx) == INDEX_NONE)
		{
			PreviewTouches[PreviewTouchCount].Index = Idx;
			PreviewTouches[PreviewTouchCount].LastPos = Pos;
			++PreviewTouchCount;
			PreviewDragPointerIndex = Idx;   // newest finger drives
			PreviewDragLastPos = Pos;         // seed so its first move delta is ~0 (no jump)
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		return FReply::Handled();
	}

	if (IsInPreviewArea(Pos))
	{
		bPreviewIgnoreSyntheticMove = false;  // start clean (parity with mouse OnMouseButtonDown)
		bPreviewDragging = true;
		SetKeyboardGridHitTestable(false);    // scrubbing → keys can't be hit by a second finger (multi-touch misfire)
		PreviewTouches[0].Index = Idx;        // first finger
		PreviewTouches[0].LastPos = Pos;
		PreviewTouchCount = 1;
		PreviewDragPointerIndex = Idx;        // this finger drives the scrub
		PreviewDragLastPos = Pos;
		PreviewDragAccum = 0.f;
		PreviewDragAccumY = 0.f;
		PreviewDragAxis = 0;
		BeginPreviewSelectAt(Pos);
		// CRITICAL: take capture even on the TOUCH path. Capture is managed per-PointerIndex (SlateUser.h:65-76),
		// so FReply::CaptureMouse() is valid for touch/faking-touch (PointerIndex 0) and makes HasMouseCapture()
		// return true. Without it, this widget's OnMouseMove HasMouseCapture() guard (added for the mouse path)
		// would bail every frame when bUseMouseForTouch=True routes the drag through touch — that's the "drag does
		// nothing" bug. SetUserFocus re-asserts keyboard focus on press (mirrors mouse path / SSpinBox.cpp:301).
		// LockMouseToWidget: faking-touch is backed by a REAL mouse with a REAL OS cursor that follows movement,
		// so without a clip the cursor drifts out of the viewport (user-reported "still flies out"). Clip it to
		// this widget's rect (fills the viewport). No high-precision (touch can't engage it — its reply never
		// reaches ProcessReply), so the cursor is CONFINED, not pinned. NOTE: LockCursorInternal is gated by
		// IsForegroundWindow() (SlateUser.cpp:982) — under PIE this may silently no-op; verify on real runtime.
		return FReply::Handled()
			.CaptureMouse(SharedThis(this))
			.LockMouseToWidget(SharedThis(this))
			.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (!bPreviewDragging)
		return FReply::Handled();

	const int32 Idx = InTouchEvent.GetPointerIndex();
	const FVector2D Pos = InTouchEvent.GetScreenSpacePosition();

	// Keep every tracked finger's last position current (even a non-driving one), so if the driving finger
	// lifts, the relay can seed PreviewDragLastPos from the survivor's real position (no jump).
	const int32 Slot = FindPreviewTouch(Idx);
	if (Slot != INDEX_NONE)
		PreviewTouches[Slot].LastPos = Pos;

	// Only the DRIVING finger moves the value (relay model: one active finger at a time, no speed-doubling).
	if (Idx != PreviewDragPointerIndex)
		return FReply::Handled();

	// Same authoritative guard as OnMouseMove: if bPreviewDragging leaked true without real capture, this is a
	// stale move — reset and bail rather than scrub from a stale position.
	if (!HasMouseCapture())
	{
		EndPreviewMouseDrag();
		return FReply::Unhandled();
	}
	// Touch has no high-precision/raw mode — feed the position diff (converted to LOCAL pixels so the
	// thresholds match, same as the mouse path) and track the last position.
	const float DPIScale = GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
	const float InvScale = (DPIScale > 0.f) ? (1.f / DPIScale) : 1.f;
	HandlePreviewDrag((Pos - PreviewDragLastPos) * InvScale);
	PreviewDragLastPos = Pos;
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	const int32 Idx = InTouchEvent.GetPointerIndex();
	const int32 Slot = FindPreviewTouch(Idx);
	if (!bPreviewDragging || Slot == INDEX_NONE)
		return FReply::Handled();  // a finger we weren't tracking lifted — nothing to do

	// Remove this finger from the tracked slots (compact so [0] stays the survivor).
	for (int32 i = Slot; i < PreviewTouchCount - 1; ++i)
		PreviewTouches[i] = PreviewTouches[i + 1];
	PreviewTouches[--PreviewTouchCount] = FPreviewTouch{};

	if (PreviewTouchCount > 0)
	{
		// RELAY: other finger(s) still down → hand driving over to the survivor and keep the scrub alive. Seed
		// PreviewDragLastPos from the survivor's tracked position so the next move delta is ~0 (no jump). Release
		// THIS finger's capture only; keep the mouse-lock (still scrubbing).
		PreviewDragPointerIndex = PreviewTouches[0].Index;
		PreviewDragLastPos = PreviewTouches[0].LastPos;
		return FReply::Handled().ReleaseMouseCapture();
	}

	// Last finger up → end the scrub. Symmetric with OnMouseButtonUp: teardown via EndPreviewMouseDrag (its
	// SetCursorPos restore is high-precision-gated, so touch skips it) + release capture + mouse-lock.
	const bool bWasCapturing = HasMouseCapture();
	EndPreviewMouseDrag();
	return bWasCapturing
		? FReply::Handled().ReleaseMouseCapture().ReleaseMouseLock()
		: FReply::Handled().ReleaseMouseLock();
}

void SImSlateVirtualKeyboard::UpdateSuggestions()
{
	if (!SuggestionBar.IsValid()) return;

	// Input changed → the candidate list is being refreshed, so the old keyboard selection is no
	// longer meaningful. Reset it. (Up/Down navigation rebuilds the bar via PopulateSuggestionBar
	// directly, NOT through here, so this never clears a fresh selection.)
	SelectedSuggestionIndex = -1;

	// Consume async results first (history merge below handles the sync path; async keeps legacy).
	TArray<FString> AsyncResults;
	if (AsyncSuggestionQueue.Dequeue(AsyncResults))
	{
		PopulateSuggestionBar(AsyncResults);
		return;
	}

	// History entries (recent-first), filtered by current input. These render first, with X buttons.
	TArray<FString> HistoryMatches;
	if (!HistoryKey.IsEmpty())
	{
		const TArray<FString>& Hist = FImSlateInputHistory::Get().GetHistory(HistoryKey);
		const int32 MaxShow = HistoryMax > 0 ? HistoryMax : Hist.Num();
		for (const FString& H : Hist)
		{
			// Empty input → all recent; typed input → case-insensitive substring match.
			if (CurrentText.IsEmpty() || H.Contains(CurrentText, ESearchCase::IgnoreCase))
			{
				HistoryMatches.Add(H);
				if (HistoryMatches.Num() >= MaxShow)
					break;  // cap recent rows to the per-key limit (default 10)
			}
		}
	}

	// Provider results after history; drop any that duplicate a history entry (history wins).
	TArray<FString> Suggestions = HistoryMatches;
	if (SuggestionProvider)
	{
		TArray<FString> Provided;
		SuggestionProvider(CurrentText, Provided);
		for (const FString& P : Provided)
		{
			if (!HistoryMatches.ContainsByPredicate([&P](const FString& H){ return H.Equals(P, ESearchCase::IgnoreCase); }))
				Suggestions.Add(P);
		}
	}

	if (Suggestions.Num() == 0) { SuggestionBar->ClearChildren(); return; }
	PopulateSuggestionBar(Suggestions, HistoryMatches.Num());
}

void SImSlateVirtualKeyboard::UpdateSuggestionsAsync(TArray<FString> InResults)
{
	AsyncSuggestionQueue.Enqueue(MoveTemp(InResults));
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
		[this](double, float) -> EActiveTimerReturnType {
			if (bVisible) UpdateSuggestions();
			return EActiveTimerReturnType::Stop;
		}));
}

void SImSlateVirtualKeyboard::PopulateSuggestionBar(const TArray<FString>& Suggestions, int32 HistoryCount)
{
	if (!SuggestionBar.IsValid()) return;
	SuggestionBar->ClearChildren();

	float Scale = GetKbScale();
	int32 MaxItems = 12;
	const int32 Count = FMath::Min(Suggestions.Num(), MaxItems);

	// Cache the visible candidates so Up/Down can navigate them and Enter can commit the selection.
	CurrentSuggestions.Reset();
	for (int32 i = 0; i < Count; ++i)
		CurrentSuggestions.Add(Suggestions[i]);
	SuggestionHistoryCount = FMath::Min(HistoryCount, Count);  // for Ctrl+Backspace delete-history
	// Keep selection in range (a shorter new list may invalidate the old index).
	if (SelectedSuggestionIndex >= Count)
		SelectedSuggestionIndex = -1;

	// Selected vs unselected candidate buttons share the SAME flat brush type and the SAME paddings,
	// so they are identical in size/shape and the label sits at the same place — only the fill color
	// differs. (The earlier approach reused the default "Button" style for unselected and a flat box
	// for selected; their differing brush margins/paddings made the selected one a different shape
	// and pushed its text lower.) Built once and reused.
	auto MakeFlatStyle = [](const FLinearColor& FillColor) -> FButtonStyle {
		FButtonStyle S = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
		FSlateColorBrush Fill(FillColor);
		S.SetNormal(Fill);
		S.SetHovered(Fill);
		S.SetPressed(Fill);
		S.SetNormalPadding(FMargin(0.f));   // identical padding for both states → identical text pos
		S.SetPressedPadding(FMargin(0.f));
		return S;
	};
	static const FButtonStyle UnselectedStyle = MakeFlatStyle(FLinearColor(0.18f, 0.18f, 0.18f, 1.f));  // dark grey
	static const FButtonStyle SelectedStyle   = MakeFlatStyle(FLinearColor(0.10f, 0.45f, 0.90f, 1.f));  // blue

	const FSlateFontInfo NormalFont = GetImSlateDefaultFont(8);

	for (int32 i = 0; i < Count; ++i)
	{
		FString SugCopy = Suggestions[i];
		const bool bSelected = (i == SelectedSuggestionIndex);
		const bool bIsHistory = (i < HistoryCount);

		// Main label. (No clock-icon prefix — the ⏰ glyph isn't in the font and rendered as a ◇? tofu;
		// history rows are already distinguished by the trailing X delete button.)
		const FText LabelText = FText::FromString(SugCopy);
		TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ContentPadding(FMargin(6.f * Scale, 2.f * Scale))
				.ButtonStyle(bSelected ? &SelectedStyle : &UnselectedStyle)
				// Fire on press, not click: the keyboard window never delivers a full OnClicked.
				.ClickMethod(EButtonClickMethod::MouseDown)
				.TouchMethod(EButtonTouchMethod::Down)
				.OnClicked_Lambda([this, SugCopy]() { OnSuggestionClicked(SugCopy); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(LabelText)
					// Keep the SAME font when selected — switching to Bold widens the label, which pushes
					// the FillWidth main button and shifts the trailing X delete button (the row's layout
					// jumped on highlight). Selection is shown by SelectedStyle's background instead.
					.Font(NormalFont)
					.ColorAndOpacity(FSlateColor(bSelected ? FLinearColor(0.3f, 0.8f, 1.f) : FLinearColor::White))
				]
			];

		// History rows: trailing 'X' to delete that entry from the persisted history.
		if (bIsHistory && !HistoryKey.IsEmpty())
		{
			Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f * Scale, 0.f))
			[
				SNew(SButton)
				.ContentPadding(FMargin(5.f * Scale, 2.f * Scale))
				.ButtonStyle(&UnselectedStyle)
				.ClickMethod(EButtonClickMethod::MouseDown)
				.TouchMethod(EButtonTouchMethod::Down)
				.OnClicked_Lambda([this, SugCopy]()
				{
					FImSlateInputHistory::Get().Remove(HistoryKey, SugCopy);
					UpdateSuggestions();  // rebuild the bar without the deleted entry
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("\x2715")))  // ×
					.Font(NormalFont)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				]
			];
		}

		SuggestionBar->AddSlot().Padding(2.f * Scale)[ Content ];
	}

	Invalidate(EInvalidateWidgetReason::Layout);
}

void SImSlateVirtualKeyboard::MoveSuggestionSelection(int32 Delta)
{
	if (CurrentSuggestions.Num() == 0)
	{
		SelectedSuggestionIndex = -1;
		return;
	}
	const int32 Last = CurrentSuggestions.Num() - 1;
	int32 NewIdx;
	if (SelectedSuggestionIndex < 0 && Delta < 0)
	{
		// Nothing selected yet + Up → wrap up into the LAST item (so the first Up press selects
		// something instead of staying unselected). Down from nothing still goes to the first item.
		NewIdx = Last;
	}
	else
	{
		// Range is [-1, Last]: -1 = nothing selected. Clamp (no wrap) at the ends.
		NewIdx = FMath::Clamp(SelectedSuggestionIndex + Delta, -1, Last);
	}
	if (NewIdx != SelectedSuggestionIndex)
	{
		SelectedSuggestionIndex = NewIdx;
		// Rebuild the bar to repaint the highlight (cheap — at most MaxItems buttons). Pass a COPY:
		// PopulateSuggestionBar resets CurrentSuggestions before refilling from its argument, so
		// passing the member directly would clear it mid-read. MUST re-pass the history count, else the
		// rebuilt rows lose their history flag → the ⏰ prefix and X delete button disappear on highlight.
		const TArray<FString> Copy = CurrentSuggestions;
		PopulateSuggestionBar(Copy, SuggestionHistoryCount);
	}
}

// ==================== Input Handling ====================

void SImSlateVirtualKeyboard::OnKeyInput(const FVirtualKeyDef& KeyDef, const FString& InputValue)
{
	FString Text = InputValue;
	if (IsUpperCase() && Text.Len() == 1 && FChar::IsAlpha(Text[0]))
		Text = Text.ToUpper();

	// Calculator (expression-string model): on the DEC numeric pad, the operator keys (+ − * /) build the
	// expression string instead of being inserted as text. ('-' here is the subtraction operator; a negative
	// VALUE is entered via the ± key, not this key.)
	if (IsCalcEnabled() && Text.Len() == 1)
	{
		const TCHAR C = Text[0];
		if (C == '+' || C == '-' || C == '*' || C == '/')
		{
			CalcAppendOperator(Text);   // seed from CurrentText on first op, append / replace trailing op
			return;
		}
		// While an expression is being typed, digits / '.' append straight to the expression string (the
		// bound value stays frozen until '='). Outside expression mode they fall through to plain insertion.
		if (!CalcExpr.IsEmpty())
		{
			const bool bStartsOperand = (C >= '0' && C <= '9') || C == '.';
			if (bStartsOperand)
			{
				CalcAppendDigit(Text);
				return;
			}
		}
	}

	// Numeric pad: reject any character that isn't valid for this field (letters f/d/g, punctuation, A-F in
	// DEC, etc.). Operators / '=' were handled above. Shared whitelist with the physical-key path (OnKeyChar).
	if (Text.Len() == 1 && !IsCharAllowedForNumeric(Text[0]))
		return;

	// A number may have only ONE decimal point: the first '.' wins, any further '.' is ignored (so tapping
	// '.' again — anywhere — does nothing rather than producing an invalid "0.5.3"). Numeric pad only; the
	// expression path returned above, so "1.5+2.3" (two dots across DIFFERENT operands) is unaffected.
	if (Text == TEXT(".") && KeyboardType == EKeyboardType::Number && CurrentText.Contains(TEXT(".")))
		return;

	// Plain numeric / text insert. R017: a digit right after Show/'=' on a preloaded value must INSERT into it
	// (bCalcFreshOperand is already cleared at those points), not replace — handled by InsertText at the caret.
	bJustEvaluated = false;
	bCalcFreshOperand = false;

	InsertText(Text);

	// Strip redundant leading zeros from the integer part (DEC plain-numeric only): typing a digit onto the
	// placeholder/leading "0" should give a clean number — "0"→"1" (not "01"), "0.1" with the caret after the
	// "0" + "5" → "5.1" (not "05.1"), "007"→"7". The integer part keeps a single "0" when that's all it is
	// ("0", "0.5" stay; an all-zero run collapses to one "0"). A leading '-' (sign) is preserved. Not for HEX
	// (it has width-padded zero formatting) and not in expression mode (which returned above; "0-1" is safe).
	if (KeyboardType == EKeyboardType::Number && NumericRadix == 10)
		StripLeadingZeros();

	if (ShiftState == EShiftState::SingleShot)
		ShiftState = EShiftState::Default;

	UpdatePreview();
	UpdateSuggestions();
	UpdateSignKey();  // ± label tracks the (possibly changed) value sign; no-op outside signed DEC numeric
}

void SImSlateVirtualKeyboard::OnKeyAction(EVirtualKeyAction Action)
{
	switch (Action)
	{
	case EVirtualKeyAction::Backspace:
		// Calculator expression mode: backspace edits the EXPRESSION string (delete its last char), not the
		// frozen value. Emptying it exits expression mode (preview falls back to the value via UpdatePreview).
		if (IsCalcEnabled() && !CalcExpr.IsEmpty())
		{
			CalcExpr = CalcExpr.LeftChop(1);
			CalcCursorPos = CalcExpr.Len();
			UpdatePreview();
			UpdateSuggestions();
			break;
		}
		if (DeleteBackwardWithUndo())
		{
			UpdatePreview();
			UpdateSuggestions();
		}
		break;
	case EVirtualKeyAction::Shift:
		ToggleShift();
		break;
	case EVirtualKeyAction::CapsLock:
		ShiftState = (ShiftState == EShiftState::Locked) ? EShiftState::Default : EShiftState::Locked;
		break;
	case EVirtualKeyAction::Enter:
		Hide(true);
		break;
	case EVirtualKeyAction::Cancel:
		Hide(false);
		break;
	case EVirtualKeyAction::Space:
		// Calculator: space doubles as '=' (user request). Only on the DEC numeric pad; text fields still
		// insert a real space.
		if (IsCalcEnabled())
		{
			CalcPressEquals();
			UpdateSuggestions();
			break;
		}
		InsertText(TEXT(" "));
		UpdatePreview();
		UpdateSuggestions();
		break;
	case EVirtualKeyAction::Left:
		MoveCursor(-1);
		UpdatePreview();
		break;
	case EVirtualKeyAction::Right:
		MoveCursor(1);
		UpdatePreview();
		break;
	case EVirtualKeyAction::UndoBackspace:
		if (BackspaceUndoStack.Num() > 0)
		{
			UndoBackspaceFromStack();
			UpdatePreview();
			UpdateSuggestions();
		}
		break;
	case EVirtualKeyAction::ToggleLayer:
		CurrentLayer = (CurrentLayer + 1) % 2;
		BuildKeyboard();
		break;
	case EVirtualKeyAction::ToggleType:
		if (KeyboardType == EKeyboardType::Number)
		{
			// On the numeric pad the toggle-type key doubles as the radix switcher (integers only).
			CycleNumericRadix();
		}
		else
		{
			KeyboardType = (KeyboardType == EKeyboardType::QWERTY) ? EKeyboardType::T9 : EKeyboardType::QWERTY;
			CurrentLayer = 0;
			UpdateToggleTypeLabel();
			BuildKeyboard();
		}
		break;
	case EVirtualKeyAction::StepUp:
		ApplyNumericStep(+1);
		break;
	case EVirtualKeyAction::StepDown:
		ApplyNumericStep(-1);
		break;
	case EVirtualKeyAction::Equals:
		CalcPressEquals();
		break;
	case EVirtualKeyAction::SignToggle:
	{
		// Expression mode: ± signs the FIRST operand of the expression string (the leading number). A leading
		// '-' is removed, otherwise one is prepended — "5+3" ↔ "-5+3". The bound value stays frozen until '='.
		if (IsCalcEnabled() && !CalcExpr.IsEmpty())
		{
			if (CalcExpr.Len() > 0 && CalcExpr[0] == TEXT('-'))
			{
				CalcExpr.RemoveAt(0, 1);
				CalcCursorPos = FMath::Max(0, CalcCursorPos - 1);
			}
			else
			{
				CalcExpr.InsertAt(0, TEXT('-'));
				CalcCursorPos += 1;
			}
			UpdatePreview();
			break;
		}
		// Preview-row ± key: flip the sign of the current value, clamped.
		const bool bFloat = NumericParams.bAllowDecimal && NumericRadix == 10;
		if (bFloat)
		{
			double V = -FCString::Atod(*CurrentText);
			if (NumericParams.Min.IsSet()) V = FMath::Max(V, NumericParams.Min.GetValue());
			if (NumericParams.Max.IsSet()) V = FMath::Min(V, NumericParams.Max.GetValue());
			CurrentText = FString::SanitizeFloat(V);
		}
		else
		{
			int64 V = -FCString::Strtoi64(*CurrentText, nullptr, NumericRadix);
			if (NumericParams.Min.IsSet()) V = FMath::Max(V, (int64)NumericParams.Min.GetValue());
			if (NumericParams.Max.IsSet()) V = FMath::Min(V, (int64)NumericParams.Max.GetValue());
			CurrentText = FormatIntInRadix(V, NumericRadix, NumericParams.BitWidth);
		}
		CursorPosition = CurrentText.Len();
		UpdatePreview();
		UpdateSuggestions();
		UpdateSignKey();      // preview-row ± switch key repaints to show the new current/target signs
		// (Grid ± key is a fixed "±" glyph, so no keyboard rebuild is needed here.)
		break;
	}
	default:
		break;
	}
}

FReply SImSlateVirtualKeyboard::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	if (!bVisible)
		return FReply::Unhandled();

	const TCHAR Ch = InCharacterEvent.GetCharacter();
	// Only printable characters get inserted. Control chars (Backspace/Enter/Esc/Tab/etc.) and Space
	// are handled in OnKeyDown so we don't insert them twice. The OS already applied Shift/CapsLock
	// to produce the correct case, so insert the character verbatim — do NOT route through OnKeyInput
	// (its IsUpperCase()/ToUpper() would re-case it against the virtual keyboard's ShiftState).
	// Non-printables are still SWALLOWED (return Handled): the keyboard is modal while open, so no
	// char event may leak to the layer underneath.
	if (Ch < 32 || Ch == 127 || Ch == TEXT(' '))
		return FReply::Handled();

	// Numeric pad: route physical chars through OnKeyInput so they get the SAME treatment as on-screen keys
	// — operators (+ − * /) drive the calculator, '=' ... (handled in OnKeyDown), and the numeric whitelist
	// rejects letters/punctuation. Digits/operators have no case, so the re-casing concern (below) doesn't
	// apply here. Other layouts keep inserting verbatim (OS already applied Shift/CapsLock).
	if (KeyboardType == EKeyboardType::Number)
	{
		FVirtualKeyDef Tmp;
		Tmp.Action = EVirtualKeyAction::Char;
		Tmp.Value = FString::Chr(Ch);
		OnKeyInput(Tmp, Tmp.Value);
		return FReply::Handled();
	}

	InsertText(FString::Chr(Ch));
	UpdatePreview();
	UpdateSuggestions();
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bVisible)
		return FReply::Unhandled();

	const FKey Key = InKeyEvent.GetKey();

	// Ctrl+V paste: no EVirtualKeyAction for paste, so read the clipboard and reuse the insert path.
	if (Key == EKeys::V && InKeyEvent.IsControlDown())
	{
		FString Clip;
		FPlatformApplicationMisc::ClipboardPaste(Clip);
		// Single-line: strip newlines so a multi-line clipboard doesn't break the preview.
		Clip.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		Clip.ReplaceInline(TEXT("\r"), TEXT(" "));
		Clip.ReplaceInline(TEXT("\n"), TEXT(" "));
		if (!Clip.IsEmpty())
		{
			InsertText(Clip);
			UpdatePreview();
			UpdateSuggestions();
		}
		return FReply::Handled();
	}

	// Ctrl+Backspace deletes the highlighted HISTORY suggestion (only the leading history rows are
	// deletable). Must be checked BEFORE the plain-Backspace char-delete path below.
	if (Key == EKeys::BackSpace && InKeyEvent.IsControlDown())
	{
		if (!HistoryKey.IsEmpty() && SelectedSuggestionIndex >= 0 && SelectedSuggestionIndex < SuggestionHistoryCount
			&& CurrentSuggestions.IsValidIndex(SelectedSuggestionIndex))
		{
			FImSlateInputHistory::Get().Remove(HistoryKey, CurrentSuggestions[SelectedSuggestionIndex]);
			SelectedSuggestionIndex = -1;
			UpdateSuggestions();  // rebuild without the deleted entry
			return FReply::Handled();
		}
	}

	// Up/Down navigate the candidate (suggestion) list when there is one.
	if (Key == EKeys::Down)             { MoveSuggestionSelection(+1); return FReply::Handled(); }
	if (Key == EKeys::Up)               { MoveSuggestionSelection(-1); return FReply::Handled(); }

	// Enter: if a candidate is highlighted (Up/Down picked one), commit THAT word; otherwise the
	// usual Done (commit the typed text).
	if (Key == EKeys::Enter)
	{
		if (CurrentSuggestions.IsValidIndex(SelectedSuggestionIndex))
			OnSuggestionClicked(CurrentSuggestions[SelectedSuggestionIndex]);
		else
			OnKeyAction(EVirtualKeyAction::Enter);
		return FReply::Handled();
	}

	// Map physical keys onto the SAME actions as the on-screen keys (OnKeyAction → UpdatePreview
	// → bound editable + suggestions stay in sync).
	if (Key == EKeys::Escape)           { OnKeyAction(EVirtualKeyAction::Cancel);    return FReply::Handled(); }
	if (Key == EKeys::BackSpace)        { OnKeyAction(EVirtualKeyAction::Backspace); return FReply::Handled(); }
	if (Key == EKeys::Left)             { OnKeyAction(EVirtualKeyAction::Left);      return FReply::Handled(); }
	if (Key == EKeys::Right)            { OnKeyAction(EVirtualKeyAction::Right);     return FReply::Handled(); }
	if (Key == EKeys::SpaceBar)         { OnKeyAction(EVirtualKeyAction::Space);     return FReply::Handled(); }

	// While the keyboard is open it is the focused, modal input target: SWALLOW every remaining key
	// (F-keys, letters' raw KeyDown, etc.) so nothing leaks to the game / editor viewport shortcuts
	// underneath. (Printable chars are produced via OnKeyChar; this only governs KeyDown routing.)
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Modal while open: swallow ALL key-up events too, so the paired up of any key (whose down we
	// swallowed) can't trigger a viewport shortcut underneath that fires on release.
	if (!bVisible)
		return FReply::Unhandled();
	return FReply::Handled();
}

void SImSlateVirtualKeyboard::OnKeyLongPress(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry)
{
	if (KeyDef.LongPressChars.Num() == 0 || !RootOverlay.IsValid()) return;
	DismissPopup();

	ActivePopup = SNew(SImSlateKeyPopup)
		.Chars(KeyDef.LongPressChars);

	// Position popup above the key, clamped to keyboard bounds (local coords)
	FVector2D KeyLocalPos = GetCachedGeometry().AbsoluteToLocal(KeyGeometry.GetAbsolutePosition());
	FVector2D KeyLocalSize = KeyGeometry.GetLocalSize();
	FVector2D MySize = GetCachedGeometry().GetLocalSize();
	float PopupPadding = 8.f;
	float CellW = ActivePopup->GetCellWidth() + 2.f;
	float PopupWidth = KeyDef.LongPressChars.Num() * CellW + PopupPadding * 2.f;
	float KeyCenterX = KeyLocalPos.X + KeyLocalSize.X * 0.5f;
	float PopupX = KeyCenterX - PopupWidth * 0.5f;
	PopupX = FMath::Clamp(PopupX, 0.f, FMath::Max(0.f, MySize.X - PopupWidth));
	// Place the popup fully ABOVE the key: offset by the popup's own height + a gap,
	// not a fixed 50 (which left the popup overlapping the key row).
	float PopupHeight = ActivePopup->GetDesiredSize().Y;
	if (PopupHeight <= 0.f)
		PopupHeight = 48.f * GetKbScale();  // fallback before first layout
	float PopupY = KeyLocalPos.Y - PopupHeight - 6.f;
	PopupY = FMath::Max(0.f, PopupY);

	RootOverlay->AddSlot()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(FMargin(PopupX, PopupY, 0.f, 0.f))
	[
		ActivePopup.ToSharedRef()
	];

	// Default selection is the MIDDLE entry — matches the finger-at-center index that
	// HandleRelease computes (round(0/CellW)+HalfCount = Count/2) when released without moving.
	ActivePopup->SetHighlightIndex(KeyDef.LongPressChars.Num() / 2);

	// Tell the key where the popup center is so it can compute correct indices.
	// IMPORTANT: the key compares against ScreenPos (ABSOLUTE screen space), so BOTH the
	// anchor X and the cell width must be in absolute space. PopupX/CellW are local (unscaled),
	// so multiply by DPIScale. Previously CellW was passed unscaled → XOffset(abs)/CellW(local)
	// mismatched by the DPI factor → the selection jumped multiple cells per cell of movement.
	FVector2D MyAbsPos = GetCachedGeometry().GetAbsolutePosition();
	float DPIScale = GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
	float PopupCenterAbsX = MyAbsPos.X + (PopupX + PopupWidth * 0.5f) * DPIScale;
	float CellWAbs = CellW * DPIScale;
	for (auto& Entry : KeyWidgets)
	{
		if (Entry.Def == &KeyDef && Entry.Widget.IsValid())
		{
			Entry.Widget->SetLongPressPopupInfo(PopupCenterAbsX, CellWAbs, KeyDef.LongPressChars.Num());
			ActiveLongPressKey = Entry.Widget;
			break;
		}
	}
}

void SImSlateVirtualKeyboard::OnKeyLongPressMove(int32 HighlightIndex)
{
	if (ActivePopup.IsValid())
		ActivePopup->SetHighlightIndex(HighlightIndex);
}

void SImSlateVirtualKeyboard::OnKeyLongPressEnd(int32 SelectedIndex)
{
	if (ActivePopup.IsValid())
	{
		FString SelectedChar = ActivePopup->GetCharAt(SelectedIndex);
		if (!SelectedChar.IsEmpty())
		{
			InsertText(SelectedChar);
			UpdatePreview();
			UpdateSuggestions();
		}
	}
	DismissPopup();
}

void SImSlateVirtualKeyboard::OnKeyPressVisual(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry, bool bForceStepDrag)
{
	if (!RootOverlay.IsValid()) return;
	OnKeyReleaseVisual();

	// Step-drag hint when: Space/Del (always), OR the caller forced it (Done's horizontal drag).
	// Done's VERTICAL swipe passes bForceStepDrag=false → falls through to the four-way popup.
	bool bIsStepDrag = bForceStepDrag
		|| KeyDef.Action == EVirtualKeyAction::Space
		|| KeyDef.Action == EVirtualKeyAction::Backspace;

	if (bIsStepDrag)
	{
		float Scale = GetKbScale();
		FSlateFontInfo Font = GetImSlateDefaultFont(7);
		FLinearColor Dim(0.5f, 0.5f, 0.5f, 1.f);

		bool bIsSpace = (KeyDef.Action == EVirtualKeyAction::Space);
		FString LeftLabel = bIsSpace ? TEXT("\x25C0 Cursor") : TEXT("\x25C0 Del");
		FString RightLabel = bIsSpace ? TEXT("Cursor \x25B6") : TEXT("Undo \x25B6");

		// Position the popup in RootOverlay's OWN coordinate space (it is the popup's parent).
		// Using this widget's geometry instead caused drift: RootOverlay's origin moves as the
		// suggestion row count changes the layout, so converting via the popup's actual parent
		// keeps the popup pinned to the key's absolute position regardless of that drift.
		const FGeometry& OverlayGeo = RootOverlay->GetCachedGeometry();
		FVector2D KeyLocalPos = OverlayGeo.AbsoluteToLocal(KeyGeometry.GetAbsolutePosition());
		FVector2D KeyLocalSize = KeyGeometry.GetLocalSize();
		FVector2D MySize = OverlayGeo.GetLocalSize();
		// Width must fit the actual text (left + separator + right + paddings), not just scale
		// off the key width — a narrow key (e.g. Del) was clipping "◀ Del | Undo ▶".
		TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		float TextW = (float)FM->Measure(LeftLabel, Font).X + (float)FM->Measure(RightLabel, Font).X;
		float SepAndPad = (float)FM->Measure(TEXT("\x2502"), Font).X + 16.f * Scale /*sep slot padding*/ + 20.f * Scale /*border padding*/;
		float PopupWidth = FMath::Max(KeyLocalSize.X * 1.2f, TextW + SepAndPad);
		float PopupX = KeyLocalPos.X + KeyLocalSize.X * 0.5f - PopupWidth * 0.5f;
		PopupX = FMath::Clamp(PopupX, 0.f, FMath::Max(0.f, MySize.X - PopupWidth));
		float PopupY = KeyLocalPos.Y - 32.f;
		PopupY = FMath::Max(0.f, PopupY);

		StepDragVisual = SNew(SBox)
			.WidthOverride(PopupWidth)
			[
				SNew(SBox)
				.HeightOverride(28.f * GetKbScale())
				[
					SNew(SBorder)
					.BorderImage(&GetPopupBgBrush())
					.Padding(FMargin(10.f, 4.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Left).VAlign(VAlign_Center)
						[ SAssignNew(StepDragLeftText, STextBlock).Text(FText::FromString(LeftLabel)).Font(Font).ColorAndOpacity(Dim) ]
						+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(8.f, 0.f))
						[ SNew(STextBlock).Text(FText::FromString(TEXT("\x2502"))).Font(Font).ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f)) ]
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
						[ SAssignNew(StepDragRightText, STextBlock).Text(FText::FromString(RightLabel)).Font(Font).ColorAndOpacity(Dim) ]
					]
				]
			];

		RootOverlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(PopupX, PopupY, 0.f, 0.f))
		[ StepDragVisual.ToSharedRef() ];
		return;
	}

	if (!KeyDef.Swipe.HasAny()) return;
	ActiveKeyDef = &KeyDef;

	float Scale = GetKbScale();
	FSlateFontInfo Font = GetImSlateDefaultFont(12);  // slightly smaller than before (was 14) to fit cells snugly
	FLinearColor HintColor(0.9f, 0.9f, 0.9f, 1.f);

	auto ApplyCase = [this](const FString& S) -> FString {
		if (S.Len() == 1 && FChar::IsAlpha(S[0]))
			return IsUpperCase() ? S.ToUpper() : S.ToLower();
		return S;
	};

	const FString UpText    = ApplyCase(KeyDef.Swipe.Up.Label);
	const FString DownText  = ApplyCase(KeyDef.Swipe.Down.Label);
	const FString LeftText  = ApplyCase(KeyDef.Swipe.Left.Label);
	const FString RightText = ApplyCase(KeyDef.Swipe.Right.Label);
	const FString CenterText = KeyDef.Value.IsEmpty() ? KeyDef.GetDisplayLabel(IsUpperCase()) : KeyDef.Value;

	// UNIFIED RULE: build the popup as rows × columns, omitting any empty row/column so the
	// popup hugs exactly the cells that exist. CellSize is a font-based square constant
	// (independent of key geometry). Each cell has a uniform minimum size, widened only if
	// its own text is longer (so e.g. "Done"/"Esc" are never clipped). No empty thirds, no
	// empty rows: up-only-and-center → 2 rows; full four-way → 3 rows × 3 cols.
	TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float GlyphH = (float)FM->GetMaxCharacterHeight(Font);
	const float CellSize = GlyphH + 10.f * Scale;     // uniform square minimum
	const float CellPadX = 12.f * Scale;              // horizontal breathing room for wide labels

	const bool bHasUp    = !UpText.IsEmpty();
	const bool bHasDown  = !DownText.IsEmpty();
	const bool bHasLeft  = !LeftText.IsEmpty();
	const bool bHasRight = !RightText.IsEmpty();

	SwipeDirectionTexts.Reset();

	// A single cell: fixed height, width = max(CellSize, text + padding) so it never clips.
	auto MakeCell = [&](const FString& Text, const FString& DirKey) -> TSharedRef<SWidget> {
		float W = FMath::Max(CellSize, (float)FM->Measure(Text, Font).X + CellPadX);
		TSharedPtr<STextBlock> Tb;
		TSharedRef<SBox> Box = SNew(SBox)
			.WidthOverride(W)
			.HeightOverride(CellSize)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SAssignNew(Tb, STextBlock)
				.Text(FText::FromString(Text))
				.Font(Font)
				.ColorAndOpacity(HintColor)
				.Justification(ETextJustify::Center)
			];
		if (!DirKey.IsEmpty())
			SwipeDirectionTexts.Add(DirKey, Tb);
		return Box;
	};
	// An empty placeholder cell (keeps a column aligned when only one side exists).
	auto MakeSpacer = [&]() -> TSharedRef<SWidget> {
		return SNew(SBox).WidthOverride(CellSize).HeightOverride(CellSize);
	};

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);

	// Up row (only if Up exists) — centered over the middle column.
	if (bHasUp)
	{
		Rows->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[ MakeCell(UpText, TEXT("U")) ];
	}

	// Middle row: [Left] Center [Right]. Spacers keep Center centered when one side is empty.
	{
		TSharedRef<SHorizontalBox> Mid = SNew(SHorizontalBox);
		if (bHasLeft || bHasRight)
			Mid->AddSlot().AutoWidth()[ bHasLeft ? MakeCell(LeftText, TEXT("L")) : MakeSpacer() ];
		Mid->AddSlot().AutoWidth()[ MakeCell(CenterText, TEXT("C")) ];
		if (bHasLeft || bHasRight)
			Mid->AddSlot().AutoWidth()[ bHasRight ? MakeCell(RightText, TEXT("R")) : MakeSpacer() ];
		Rows->AddSlot().AutoHeight().HAlign(HAlign_Center)[ Mid ];
	}

	// Down row (only if Down exists).
	if (bHasDown)
	{
		Rows->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[ MakeCell(DownText, TEXT("D")) ];
	}

	TSharedRef<SWidget> Grid = Rows;
	// Popup auto-sizes to its content (SBorder wraps the rows); compute extents for placement.
	const int32 NumRows = 1 + (bHasUp ? 1 : 0) + (bHasDown ? 1 : 0);
	const int32 NumCols = (bHasLeft || bHasRight) ? 3 : 1;
	const float GridH = CellSize * NumRows;
	// Popup auto-sizes to its rows (no forced override), so a wide Center label (e.g. "Done")
	// is fully contained. Border padding adds to the size.
	const float BorderPad = 2.f * Scale;

	// Step rulers are drawn as a TRANSPARENT overlay covering the popup's content area — no background,
	// no extra band, no size change. They just paint tick lines over the existing popup. L/R (cursor)
	// axis → vertical ticks scrolling horizontally; U/D (value) axis → horizontal ticks scrolling
	// vertically. Both fill the same area (ticks self-clip to their axis). Reveal+scroll in OnKeyMoveVisual.
	const bool bHorizAxis = KeyDef.Swipe.Left.bStep || KeyDef.Swipe.Right.bStep;
	const bool bVertAxis  = KeyDef.Swipe.Up.bStep   || KeyDef.Swipe.Down.bStep;
	const float StepW = 12.f * GetImSlateEffectiveScale();
	StepRulerV.Reset();
	StepRulerH.Reset();

	TSharedRef<SOverlay> PopupBody = SNew(SOverlay);
	PopupBody->AddSlot()[ Grid ];  // base layer: the four-way cells
	if (bHorizAxis)
	{
		StepRulerV = SNew(SImStepRuler).Axis(EOrientation::Orient_Vertical).StepW(StepW);
		StepRulerV->SetVisibility(EVisibility::Collapsed);
		PopupBody->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[ StepRulerV.ToSharedRef() ];
	}
	if (bVertAxis)
	{
		StepRulerH = SNew(SImStepRuler).Axis(EOrientation::Orient_Horizontal).StepW(StepW);
		StepRulerH->SetVisibility(EVisibility::Collapsed);
		PopupBody->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[ StepRulerH.ToSharedRef() ];
	}

	SwipeVisual = SNew(SBorder)
		.BorderImage(&GetPopupBgBrush())
		.Padding(BorderPad)
		[ PopupBody ];

	// Estimate popup extents for placement (rows/cols * cell + border). Center column may be
	// wider than CellSize for long labels, so use the measured center width as a floor.
	float CenterW = FMath::Max(CellSize, (float)FM->Measure(CenterText, Font).X + CellPadX);
	float ContentW = (NumCols == 3) ? (CellSize * 2.f + CenterW) : CenterW;
	float PopupW = ContentW + BorderPad * 2.f;
	float PopupH = GridH + BorderPad * 2.f;

	FVector2D KeyLocalPos = GetCachedGeometry().AbsoluteToLocal(KeyGeometry.GetAbsolutePosition());
	FVector2D KeyLocalSize = KeyGeometry.GetLocalSize();
	FVector2D MySize = GetCachedGeometry().GetLocalSize();
	float PopupX = KeyLocalPos.X + KeyLocalSize.X * 0.5f - PopupW * 0.5f;
	PopupX = FMath::Clamp(PopupX, 0.f, FMath::Max(0.f, MySize.X - PopupW));
	float PopupY = KeyLocalPos.Y - PopupH - 4.f;
	PopupY = FMath::Max(0.f, PopupY);

	RootOverlay->AddSlot()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(FMargin(PopupX, PopupY, 0.f, 0.f))
	[ SwipeVisual.ToSharedRef() ];
}

void SImSlateVirtualKeyboard::OnKeyMoveVisual(const FVector2D& Delta, bool bSwipeReady)
{
	// Step rulers: scroll with the finger along each axis (in local px). Show a ruler only once the
	// finger moves along its axis; the dominant axis stays visible. Delta is absolute screen px.
	if (StepRulerH.IsValid() || StepRulerV.IsValid())
	{
		const float DPIScale = GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
		const float LocalX = (DPIScale > 0.f) ? (float)Delta.X / DPIScale : (float)Delta.X;
		const float LocalY = (DPIScale > 0.f) ? (float)Delta.Y / DPIScale : (float)Delta.Y;
		if (StepRulerV.IsValid())  // horizontal drag → vertical ticks scroll along X
		{
			if (FMath::Abs(LocalX) > 2.f) StepRulerV->SetVisibility(EVisibility::HitTestInvisible);
			StepRulerV->SetOffset(LocalX);
		}
		if (StepRulerH.IsValid())  // vertical drag → horizontal ticks scroll along Y
		{
			if (FMath::Abs(LocalY) > 2.f) StepRulerH->SetVisibility(EVisibility::HitTestInvisible);
			StepRulerH->SetOffset(LocalY);
		}
	}

	if (StepDragVisual.IsValid())
	{
		FLinearColor Dim(0.5f, 0.5f, 0.5f, 1.f);
		FLinearColor Active(0.3f, 0.8f, 1.f, 1.f);
		bool bLeft = Delta.X < -4.f;
		bool bRight = Delta.X > 4.f;
		if (StepDragLeftText.IsValid())  StepDragLeftText->SetColorAndOpacity(bLeft ? Active : Dim);
		if (StepDragRightText.IsValid()) StepDragRightText->SetColorAndOpacity(bRight ? Active : Dim);
		return;
	}

	// Single-shot swipe: the popup itself slides up with the finger. Skip for step keys — their ruler
	// scrolls internally and the popup must stay put (an embedded ruler must not drag the whole popup).
	if (SwipeVisual.IsValid() && !StepRulerV.IsValid() && !StepRulerH.IsValid())
	{
		float DPIScale = GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
		float ScaledY = (DPIScale > 0.f) ? (float)Delta.Y / DPIScale : (float)Delta.Y;
		float ClampedY = FMath::Min(ScaledY, 0.f);
		SwipeVisual->SetRenderTransform(FSlateRenderTransform(FVector2f(0.f, ClampedY)));
	}

	const FLinearColor Normal(0.7f, 0.7f, 0.7f, 1.f);
	const FLinearColor Highlight(0.3f, 0.8f, 1.f, 1.f);

	// Highlight the direction label(s) for the active drag. STEP keys highlight per-axis INDEPENDENTLY
	// (a drag can be moving the cursor AND adjusting the value at once), so both the horizontal (◀/▶)
	// and vertical (+/−) labels light up according to each axis's sign past a small deadzone. Single-
	// shot swipe keys keep the original single-direction (dominant-axis) highlight.
	const bool bStepKey = ActiveKeyDef && ActiveKeyDef->Swipe.HasStep();
	TSet<FString> ActiveDirs;
	if (bSwipeReady && ActiveKeyDef)
	{
		const float Dead = 4.f;
		if (bStepKey)
		{
			auto Lit = [&](bool bCond, const FVirtualKeySwipeEntry& E, const TCHAR* DirTag)
			{ if (bCond && E.bStep) ActiveDirs.Add(DirTag); };
			Lit(Delta.X >  Dead, ActiveKeyDef->Swipe.Right, TEXT("R"));
			Lit(Delta.X < -Dead, ActiveKeyDef->Swipe.Left,  TEXT("L"));
			Lit(Delta.Y < -Dead, ActiveKeyDef->Swipe.Up,    TEXT("U"));
			Lit(Delta.Y >  Dead, ActiveKeyDef->Swipe.Down,  TEXT("D"));
		}
		else
		{
			// Single-shot: dominant axis only.
			FString Dir = TEXT("C");
			const FVirtualKeySwipeEntry* E = nullptr;
			if (FMath::Abs(Delta.X) > FMath::Abs(Delta.Y))
			{ Dir = Delta.X > 0 ? TEXT("R") : TEXT("L"); E = Delta.X > 0 ? &ActiveKeyDef->Swipe.Right : &ActiveKeyDef->Swipe.Left; }
			else
			{ Dir = Delta.Y < 0 ? TEXT("U") : TEXT("D"); E = Delta.Y < 0 ? &ActiveKeyDef->Swipe.Up : &ActiveKeyDef->Swipe.Down; }
			if (E && E->IsSet())
				ActiveDirs.Add(Dir);
		}
	}

	for (auto& [Dir, Text] : SwipeDirectionTexts)
	{
		if (Text.IsValid())
			Text->SetColorAndOpacity(ActiveDirs.Contains(Dir) ? Highlight : Normal);
	}
}

void SImSlateVirtualKeyboard::OnKeyReleaseVisual()
{
	if (StepDragVisual.IsValid() && RootOverlay.IsValid())
	{
		RootOverlay->RemoveSlot(StepDragVisual.ToSharedRef());
		StepDragVisual.Reset();
		StepDragLeftText.Reset();
		StepDragRightText.Reset();
	}
	if (SwipeVisual.IsValid() && RootOverlay.IsValid())
	{
		RootOverlay->RemoveSlot(SwipeVisual.ToSharedRef());
		SwipeVisual.Reset();
	}
	// Rulers are children of SwipeVisual (removed just above), so they vanish with it — only drop refs.
	StepRulerV.Reset();
	StepRulerH.Reset();
	SwipeDirectionTexts.Reset();
	ActiveKeyDef = nullptr;
}

void SImSlateVirtualKeyboard::OnSpaceCursorZone(int32 Direction)
{
	AutoScrollDirection = Direction;

	if (Direction != 0 && !AutoScrollTimer.IsValid())
	{
		AutoScrollTimer = RegisterActiveTimer(0.08f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double, float) -> EActiveTimerReturnType {
				if (AutoScrollDirection != 0)
				{
					MoveCursor(AutoScrollDirection);
					UpdatePreview();
					return EActiveTimerReturnType::Continue;
				}
				AutoScrollTimer.Reset();
				return EActiveTimerReturnType::Stop;
			}));
	}
	else if (Direction == 0 && AutoScrollTimer.IsValid())
	{
		auto Timer = AutoScrollTimer;
		AutoScrollTimer.Reset();
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
}

void SImSlateVirtualKeyboard::DismissPopup()
{
	if (ActivePopup.IsValid() && RootOverlay.IsValid())
	{
		RootOverlay->RemoveSlot(ActivePopup.ToSharedRef());
		ActivePopup.Reset();
	}
}

void SImSlateVirtualKeyboard::OnSuggestionClicked(const FString& Value)
{
	CurrentText = Value;
	CursorPosition = CurrentText.Len();
	if (bSuggestionCommitsAndCloses)
	{
		Hide(true);  // replace + commit + close
	}
	else
	{
		// Replace only; keep the keyboard open for further editing.
		UpdatePreview();
		UpdateSuggestions();
	}
}

}  // namespace ImSlate
