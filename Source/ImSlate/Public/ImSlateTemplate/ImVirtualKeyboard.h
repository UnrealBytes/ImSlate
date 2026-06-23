// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "ImVirtualKeyboard.generated.h"

class SConstraintCanvas;
class SWrapBox;
class SWidget;
class SBorder;
class SVerticalBox;
class SHorizontalBox;
class SOverlay;
class STextBlock;

namespace ImSlate
{
// SImEditableText now lives in namespace ImSlate (ImEditableText.h) — forward-declare it here
// in-namespace so PreviewEdit binds to the real ImSlate::SImEditableText.
class SImEditableText;

extern float GImSlateStepReverseDebouncePx;

int32 ImSlateStepAccumulate(float& Accum, int32& InOutLastDir, float BaseStep, float ScalePx,
	TFunctionRef<void()> PosStep, TFunctionRef<void()> NegStep);

enum class EVirtualKeyAction : uint8
{
	Char,
	Backspace,
	Shift,
	CapsLock,
	Enter,
	Cancel,
	ToggleLayer,
	Space,
	Left,
	Right,
	UndoBackspace,
	ToggleType,
	StepUp,    // numeric keypad: value += step (clamped to [Min,Max])
	StepDown,  // numeric keypad: value -= step
	Equals,    // numeric calculator: evaluate the pending operation (=) ; repeated '=' repeats last op
	SignToggle,// numeric calculator: flip the sign of the current value (preview-row ± key)
};

enum class EImSwipeDir : uint8 { None, Up, Down, Left, Right };

struct FVirtualKeySwipeEntry
{
	FString Label;
	FSimpleDelegate Callback;   // single-shot: fired once on release (swipe-and-let-go)
	bool bStep = false;         // continuous: when true, this direction fires the key-level OnStep
	                            // repeatedly during the drag (one call per StepW of travel).

	bool IsSet() const { return !Label.IsEmpty(); }
};

struct FVirtualKeySwipe
{
	FVirtualKeySwipeEntry Up;
	FVirtualKeySwipeEntry Down;
	FVirtualKeySwipeEntry Left;
	FVirtualKeySwipeEntry Right;

	TFunction<void(EImSwipeDir)> OnStep;

	bool HasAny() const { return Up.IsSet() || Down.IsSet() || Left.IsSet() || Right.IsSet(); }
	bool HasStep() const { return Up.bStep || Down.bStep || Left.bStep || Right.bStep; }
	const FVirtualKeySwipeEntry& Entry(EImSwipeDir D) const
	{
		switch (D)
		{
		case EImSwipeDir::Up:    return Up;
		case EImSwipeDir::Down:  return Down;
		case EImSwipeDir::Left:  return Left;
		case EImSwipeDir::Right: return Right;
		default:                 return Up;  // None: arbitrary; callers guard on direction
		}
	}
};

struct FVirtualKeyDef
{
	FString Label;
	FString ShiftLabel;
	FString Value;
	FString ShiftValue;
	FVirtualKeySwipe Swipe;
	TArray<FString> LongPressChars;
	EVirtualKeyAction Action = EVirtualKeyAction::Char;
	float WidthMultiplier = 1.f;
	bool bDisabled = false;  // grayed-out, non-interactive (e.g. 8/9 while in OCT base)

	FString GetDisplayLabel(bool bShift) const
	{
		if (bShift && !ShiftLabel.IsEmpty()) return ShiftLabel;
		return Label;
	}
	FString GetInputValue(bool bShift) const
	{
		if (bShift && !ShiftValue.IsEmpty()) return ShiftValue;
		return Value;
	}
};

using FImSlateSuggestionProvider = TFunction<void(const FString& Input, TArray<FString>& OutSuggestions)>;

enum class EImKeyboardType : uint8 { QWERTY, T9, Number };

struct FImNumericKeyboardParams
{
	bool bAllowDecimal = true;    // false (integer types) → hide the '.' key
	bool bAllowNegative = true;   // false (unsigned types) → hide the '-' key
	bool bHex = false;            // true → add A-F keys (hexadecimal entry)
	int32 BitWidth = 0;           // integer bit width 8/16/32/64 (0 = float / unknown). Drives HEX two's-
	                              // complement display (FF / FFFFFFFF) and base-switch sign extension.
	TOptional<double> Min;        // clamp lower bound for step keys (and typed entry)
	TOptional<double> Max;        // clamp upper bound
	TOptional<double> Step;       // when set, the keypad shows +/- step keys (SpinBox-like)
};

struct FVirtualKeyboardShowParams
{
	FString InitialText;
	int32 CursorPosition = INDEX_NONE;
	TFunction<void(const FString&, ETextCommit::Type)> CommitCallback;
	TFunction<void(const FString&)> OnTextChanged;  // real-time sync on every keystroke
	FImSlateSuggestionProvider SuggestionProvider;
	TWeakPtr<::SWidget> Owner;  // bound editable widget; keyboard follows its lifecycle
	bool bBlockBackground = true;   // true: full-screen modal, taps don't pass through to game
	// true: clicking a suggestion replaces the text AND commits+closes the keyboard (Done).
	// false: clicking a suggestion only replaces the text; the keyboard stays open for more edits.
	bool bSuggestionCommitsAndCloses = true;
	// Initial on-screen layout. Number → pops a numeric-only keypad (no letters, ToggleType hidden).
	EImKeyboardType InitialKeyboardType = EImKeyboardType::QWERTY;
	// Numeric keypad shaping (only used when InitialKeyboardType == Number).
	FImNumericKeyboardParams Numeric;

	// Input history. When HistoryKey is non-empty, committed entries are persisted per key (Saved
	// ini) and shown in the suggestion bar: empty input → recent N entries; typed input → matching
	// history first, then provider results (provider results overlapping history are hidden). History
	// rows get an 'X' delete button. HistoryFilter (optional): only entries passing it are recorded.
	FString HistoryKey;
	TFunction<bool(const FString&)> HistoryFilter;
	// Max history entries kept/shown for this key. Default 10. Caps both the stored list and the
	// suggestion rows. (Global ceiling imslate.InputHistoryMax still applies on top.)
	int32 MaxHistory = 10;
};

class IMSLATE_API SImSlateVirtualKeyboard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlateVirtualKeyboard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	// Safety net: if the keyboard is destroyed while still shown (e.g. SImViewportGame::RemoveKeyboard resets it
	// on world teardown WITHOUT going through Hide), restore bUseMouseForTouch so the desktop faking toggle from
	// Show() never leaks globally.
	virtual ~SImSlateVirtualKeyboard();
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	// Force the keyboard widget to fill the whole viewport so its top-left origin is fixed.
	// Confirmed by logs: without this the widget sized to its content (keys+suggestions), so
	// its kbAbsPos.Y drifted as the suggestion row count changed (338->412->450 high, top
	// moving 728->582->504), which dragged the popups (anchored in this widget's space) along.
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// Preview-drag safety net: if capture is yanked (drag out of viewport in editor / windowed desktop,
	// alt-tab, focus steal) no button-up arrives, so end the drag + drop the ruler here to avoid getting
	// stuck in bPreviewDragging.
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	// Hide the OS cursor during a value-scrub. Slate re-queries the cursor every frame via OnCursorQuery and
	// would otherwise overwrite a direct SetPlatformCursorVisibility(false); returning None here is the
	// engine-sanctioned way (same as SImSlateVirtualList's right-drag scroll).
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;

	// Physical keyboard support: the keyboard ROOT takes Slate user focus while shown (set in
	// Tick on the first frame after Show, so the widget is in the tree). Physical key events
	// are routed here and reuse the exact same OnKeyInput/OnKeyAction logic as the on-screen
	// keys, so the preview + bound editable + suggestions stay in sync. The root is a
	// SCompoundWidget (not SEditableText), so holding focus never pops the OS keyboard on mobile.
	virtual bool SupportsKeyboardFocus() const override { return bVisible; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;
	void Show(const FVirtualKeyboardShowParams& Params);
	void Hide(bool bCommit);
	bool IsShowing() const { return bVisible; }
	void UpdateSuggestionsAsync(TArray<FString> InResults);
	// Sync text AND caret from the bound editable (physical-keyboard input). CaretPos < 0 means
	// "unknown" → keep clamped old position; otherwise move the preview caret to it.
	void SyncFromEditor(const FString& Text, int32 CaretPos = -1);

	// Lifecycle binding: keyboard belongs to the widget that called Show().
	// When that widget is destroyed, the keyboard auto-hides.
	bool IsOwnedBy(const class SWidget* Widget) const;
	void NotifyOwnerDestroyed(const class SWidget* Widget);

	// True while the preview value is being scrub-dragged. Keys query this to stay inert during a scrub
	// (so a finger drifting onto a key mid-drag doesn't type). Cleared when all scrub fingers lift.
	bool IsPreviewDragging() const { return bPreviewDragging; }

	static TSharedPtr<SImSlateVirtualKeyboard> Get();
	// PIE-safe variant: resolves the game viewport keyboard for WorldContext's world instead of the
	// process-global context. Required under "Play As Client", where multiple worlds exist and the
	// global context may point at the server world (which has no game viewport). Falls back to Get().
	static TSharedPtr<SImSlateVirtualKeyboard> Get(const class UObject* WorldContext);
	// Non-creating hide for WorldContext's world: hides an already-open keyboard without lazily
	// creating/attaching one. Safe to call during teardown (EndPlayMap / Deinitialize), where Get()
	// would re-create + attach into an already-invalid viewport overlay and trip an ensure.
	static void HideForWorld(const class UObject* WorldContext);
	// Returns true only when the focused edit lives inside the game viewport (overlay, no OS window/IME).
	// Floated-out windows (host viewport = real SWindow) use the OS keyboard instead.
	static bool ShouldUseVirtualKeyboard(const class SWidget* InWidget = nullptr);

	// Hide any currently-open virtual keyboard WITHOUT creating one (non-creating, unlike Get()).
	// bCommit: true → commit the in-progress text (Hide(true)); false → discard (Hide(false)).
	// Safe to call any time; no-op if none is open. Returns true if a keyboard was open and hidden.
	static bool HideOpenKeyboard(bool bCommit);

	// App deactivate / background: discard + close (a keyboard surviving into resume is a known
	// suspend/resume crash). Thin wrapper over HideOpenKeyboard(false).
	static void HideOpenKeyboardForAppLifecycle() { HideOpenKeyboard(false); }

private:
	// Alias to the namespace-level enum (now includes Number) so existing cpp references to
	// EKeyboardType::QWERTY / ::T9 keep compiling unchanged.
	using EKeyboardType = EImKeyboardType;
	enum class EKeyboardLayoutMode { FullScreen, Split };
	enum class EShiftState : uint8 { Default, SingleShot, Locked };

	bool bVisible = false;
	bool bPendingFocus = false;  // Show() set this; Tick() grabs user focus on the first frame in-tree
	bool bSuggestionCommitsAndCloses = true;  // see FVirtualKeyboardShowParams
	TWeakPtr<::SWidget> BoundOwner;
	EShiftState ShiftState = EShiftState::Default;
	int32 CurrentLayer = 0;
	EKeyboardType KeyboardType = EKeyboardType::QWERTY;
	FImNumericKeyboardParams NumericParams;  // shapes the numeric keypad (set from Show params)
	int32 NumericRadix = 10;                 // integer entry base: 10 / 16 / 2 (radix-toggle key cycles)

	// ---- Numeric calculator state (DEC base only; OCT/HEX have no operators) ----
	// Expression-string model: the keypad doubles as a calculator. While typing an expression, CalcExpr holds
	// the WHOLE string the user entered ("3+4-7+8") and is shown verbatim in the preview; digits and operators
	// append straight to it. CurrentText (the value synced to the bound field via OnTextChanged) is FROZEN
	// during expression entry and only rewritten when '=' evaluates the whole string left-to-right. CalcExpr
	// non-empty ⇔ expression mode (the 5 IsEmpty() call sites use this as the mode switch).
	FString CalcExpr;            // the full expression string being typed (empty = plain numeric mode)
	int32 CalcCursorPos = 0;     // caret position WITHIN CalcExpr (expression mode; left/right drag browses it)
	// Legacy two-operand state-machine fields — no longer drive entry (the expression string does), but kept
	// because bJustEvaluated / bCalcFreshOperand still gate the plain-numeric insert path (R017) and Show().
	FString PendingOperator;     // (legacy) unused by the expression model
	double StoredOperand = 0.0;  // (legacy) unused by the expression model
	bool bCalcFreshOperand = true;  // plain-numeric: true → first digit after Show/= starts fresh (R017 gate)
	FString RepeatOperator;      // (legacy) unused
	double RepeatOperand = 0.0;  // (legacy) unused
	bool bJustEvaluated = false; // set right after '='; plain-numeric insert path consults it
	FString HistoryKey;                      // non-empty → input history enabled (set from Show params)
	TFunction<bool(const FString&)> HistoryFilter;  // only entries passing this are recorded
	int32 HistoryMax = 10;                   // per-key cap (from Show params); default 10
	// Layout mode is derived purely from the Tick-provided geometry width (not from
	// toggle history), so the same screen always yields the same mode. We track the
	// width the keyboard was last *built* against to know when a rebuild is needed.
	float BuiltWidth = -1.f;
	EKeyboardLayoutMode BuiltLayoutMode = EKeyboardLayoutMode::FullScreen;
	FString CurrentText;
	int32 CursorPosition = 0;
	FString OriginalText;
	// Backspace undo HISTORY: a stack of full SNAPSHOTS taken right BEFORE each deletion (the whole value +
	// caret at that moment), so a right-swipe undo restores the exact prior state by overwriting CurrentText.
	// Snapshots (not per-char increments) sidestep the synthetic-"0" placeholder problem entirely: deleting a
	// numeric field to empty makes UpdatePreview substitute "0", but undo just overwrites the whole value so
	// that placeholder never leaks into the restored text (e.g. "0.123" del-to-empty then undo → "0.123",
	// not ".123"). One snapshot per delete step; a "Clr" that removes a run stores one snapshot too.
	struct FBackspaceUndoEntry
	{
		FString Snapshot;  // the COMPLETE CurrentText as it was just before this deletion
		int32 Caret = 0;   // the caret position (CursorPosition) at that same moment
	};
	TArray<FBackspaceUndoEntry> BackspaceUndoStack;

	TFunction<void(const FString&, ETextCommit::Type)> CommitCallback;
	TFunction<void(const FString&)> OnTextChanged;
	FImSlateSuggestionProvider SuggestionProvider;

	TSharedPtr<SImEditableText> PreviewEdit;  // preview text + self-drawn caret (engine layout)
	TSharedPtr<::SBorder> PreviewBorder;
	TSharedPtr<::SWidget> BackgroundBlocker;  // full-screen modal hit-blocker
	// Toggle-type + Done keys flanking the preview row (persist across BuildKeyboard).
	// ToggleType is a two-state switch key (radix DEC/HEX, type T26/T9); its labels are pulled live via
	// TAttribute from KeyboardType/NumericRadix (UpdateToggleTypeLabel just invalidates it to repaint).
	TSharedPtr<class SImSwitchKey> ToggleTypeKey;
	TSharedPtr<class SImSlateKey> DoneKey;
	TSharedPtr<FVirtualKeyDef> DoneKeyDef;
	// Sign (±) key on the preview row, right of the radix key. Shown only for signed numeric input whose
	// range allows a negative; label tracks the current value's sign (value≥0 → "−" to go negative;
	// value<0 → "+" to go positive). Tapping flips the current value's sign (clamped).
	// Sign (±) key — a two-state switch key (like DEC/HEX): current sign big top-left, the sign a tap
	// switches TO small bottom-right, with a diagonal slash. Labels pulled live from the value's sign.
	TSharedPtr<class SImSwitchKey> SignKey;
	// Backspace (⌫) key on the preview row, left of Done (the numeric grid has no room for it). Persistent.
	TSharedPtr<class SImSlateKey> PreviewBackspaceKey;
	TSharedPtr<FVirtualKeyDef> PreviewBackspaceKeyDef;
	TSharedPtr<FActiveTimerHandle> CursorBlinkTimer;
	bool bCursorVisible = true;
	bool bPreviewDragging = false;
	// Desktop only: while the keyboard is shown we temporarily disable bUseMouseForTouch so preview dragging runs
	// the REAL mouse path (high-precision: pinned + hidden cursor). This stores the original value to restore on
	// Hide. Set true only when we actually toggled it (was faking + desktop). See Show()/Hide().
	bool bSavedUseMouseForTouch = false;
	FVector2D PreviewDragLastPos = FVector2D::ZeroVector;
	float PreviewDragAccum = 0.f;   // horizontal travel accumulator (switch selected digit, per cell×1.1)
	float PreviewDragAccumY = 0.f;  // vertical travel accumulator (adjust selected digit, per StepW)
	int32 PreviewDragAxis = 0;      // locked drag axis: 0=undecided, 1=horizontal(select), 2=vertical(value)
	int32 PreviewHLastStepDir = 0;  // last horizontal digit-step direction (+1/-1/0); reversing it needs the
	                                // extra reverse-debounce dead zone (anti-flicker at a column boundary).
	int32 PreviewVLastStepDir = 0;  // last vertical value-step direction (+1=up/-1=down/0); same reverse-debounce.
	// Relay-style multi-touch for the scrub: at most ONE finger drives at a time (no speed-doubling / no
	// axis-split jitter), but when the driving finger lifts while another is still down, driving hands off to
	// that finger (relay) so the scrub isn't interrupted — supports "slide to the screen edge, lift, continue
	// with another finger". At most 2 fingers tracked; a 3rd is ignored.
	int32 PreviewDragPointerIndex = -1;            // the finger currently DRIVING the scrub (-1 = none)
	struct FPreviewTouch { int32 Index = -1; FVector2D LastPos = FVector2D::ZeroVector; };
	FPreviewTouch PreviewTouches[2];               // fingers pressed inside the preview area
	int32 PreviewTouchCount = 0;
	int32 FindPreviewTouch(int32 Idx) const { for (int32 i = 0; i < 2; ++i) if (PreviewTouches[i].Index == Idx) return i; return INDEX_NONE; }
	// Self-simulated "high-precision" mouse for the value scrub (UE's native UseHighPrecisionMouseMovement
	// is disabled in our virtual-window environment). Mouse only: on press we hide the cursor and remember
	// its screen pos as the anchor; each move computes delta = current - anchor, applies it, then warps the
	// cursor back to the anchor (so it stays pinned and the drag is effectively infinite). The warp itself
	// fires a synthetic OnMouseMove which we must skip.
	FVector2D PreviewDragAnchorScreen = FVector2D::ZeroVector;
	bool bPreviewIgnoreSyntheticMove = false;
	// Preview digit-scrub: the currently highlighted/adjusted DIGIT char index (INDEX_NONE = none). Set on
	// press (hit-test), moved left/right by horizontal drag (with 10% elastic threshold), value-adjusted by
	// vertical drag. Numeric pad + plain-numeric (non-expression) state only.
	int32 SelectedDigitIndex = INDEX_NONE;
	TSharedPtr<class SImStepRuler> PreviewStepRuler;  // value-axis ruler overlaid on the preview during vertical scrub
	TSharedPtr<::SWidget> PreviewStepRulerHost;   // the SBox slot wrapper in RootOverlay (for clean removal)
	TSharedPtr<::SWrapBox> SuggestionBar;  // engine-global SWrapBox; `class SWrapBox` here would mint a bogus ImSlate::SWrapBox
	// Current candidate list + keyboard-navigated selection. SelectedSuggestionIndex == -1 means no
	// selection (Up/Down haven't been used, or input changed). Up/Down move it; Enter on a selected
	// candidate commits THAT word instead of the typed text.
	TArray<FString> CurrentSuggestions;
	int32 SelectedSuggestionIndex = -1;
	int32 SuggestionHistoryCount = 0;  // leading CurrentSuggestions that are history rows (deletable via Ctrl+Backspace)
	void MoveSuggestionSelection(int32 Delta);  // Down=+1 / Up=-1, clamped to [-1, Num-1], no wrap
	TSharedPtr<::SVerticalBox> KeyboardGrid;
	TSharedPtr<::SVerticalBox> PreviewKeysRoot;  // preview + keys unit (suggestions float above it)
	TSharedPtr<SConstraintCanvas> KeyboardCanvas;  // main anchor-based layout (engine global type)
	TSharedPtr<::SOverlay> RootOverlay;  // top-most layer for transient popups
	TSharedPtr<class SImSlateKeyPopup> ActivePopup;
	TSharedPtr<::SWidget> SwipeVisual;
	TSharedPtr<class SImStepRuler> StepRulerH;  // horizontal ticks (value axis, vertical drag)
	TSharedPtr<class SImStepRuler> StepRulerV;  // vertical ticks (cursor axis, horizontal drag)
	TSharedPtr<::SWidget> StepDragVisual;
	TSharedPtr<::STextBlock> StepDragLeftText;
	TSharedPtr<::STextBlock> StepDragRightText;
	const FVirtualKeyDef* ActiveKeyDef = nullptr;
	TMap<FString, TSharedPtr<::STextBlock>> SwipeDirectionTexts;
	TWeakPtr<class SImSlateKey> ActiveLongPressKey;

	struct FKeyWidgetEntry
	{
		TSharedPtr<class SImSlateKey> Widget;
		const FVirtualKeyDef* Def = nullptr;
	};
	TArray<FKeyWidgetEntry> KeyWidgets;
	TArray<FVirtualKeyDef> PersistentKeyDefs;

	EKeyboardLayoutMode ComputeLayoutMode(float Width) const;  // pure: mode from width
	EKeyboardLayoutMode GetLayoutMode() const { return BuiltLayoutMode; }  // last built mode

	void BuildKeyboard();
	void BuildFullScreenLayout();
	void BuildSplitLayout();
	void BuildKeyRow(TSharedRef<::SHorizontalBox> Row, const TArray<FVirtualKeyDef>& Keys);
	TSharedRef<class SImSlateKey> MakeBoundKey(const FVirtualKeyDef* DefPtr);
	void UpdateToggleTypeLabel();
	void UpdateToggleTypeKeyVisibility();  // numeric pad hides the type-toggle key
	void UpdatePreview();
	void UpdateSuggestions();
	// First HistoryCount items render with an 'X' delete button (recent input history); the rest are
	// plain provider suggestions. HistoryCount 0 = the legacy plain bar.
	void PopulateSuggestionBar(const TArray<FString>& Suggestions, int32 HistoryCount = 0);

	void OnKeyInput(const FVirtualKeyDef& KeyDef, const FString& InputValue);
	void OnKeyAction(EVirtualKeyAction Action);
	void OnKeyLongPress(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry);
	void OnKeyLongPressMove(int32 HighlightIndex);
	void OnKeyLongPressEnd(int32 SelectedIndex);
	void OnKeyPressVisual(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry, bool bForceStepDrag);
	void OnKeyMoveVisual(const FVector2D& Delta, bool bSwipeReady);
	void OnKeyReleaseVisual();
	void OnSpaceCursorZone(int32 Direction);
	float SwipeVisualBaseY = 0.f;
	int32 AutoScrollDirection = 0;
	TSharedPtr<FActiveTimerHandle> AutoScrollTimer;
	TQueue<TArray<FString>> AsyncSuggestionQueue;
	void DismissPopup();
	void OnSuggestionClicked(const FString& Value);

	bool IsUpperCase() const;
	bool IsInPreviewArea(const FVector2D& AbsPos) const;
	void HandlePreviewDrag(const FVector2D& Delta);  // Delta = per-move cursor delta (high-precision mouse / touch)
	void EndPreviewMouseDrag();  // end a mouse value-scrub: restore cursor (back to press point + visible)
	void SetKeyboardGridHitTestable(bool bHittable);  // toggle the key area hit-testable (off during a preview scrub)
	void BeginPreviewSelectAt(const FVector2D& AbsScreenPos);  // on press: hit-test + set initial highlighted digit
	void MoveSelectedDigit(int32 Dir);        // preview: move highlight to the next digit char in Dir (skips -/.)
	void ClearDigitHighlight();               // drop the preview digit selection (text structure changed)
	// True if Ch may be inserted as text on the numeric pad (digit valid in radix, or '.'/'-' when allowed).
	// Non-numeric layouts always allow. Shared by OnKeyInput (on-screen) and OnKeyChar (physical) so letters
	// never leak into a numeric field from either path.
	bool IsCharAllowedForNumeric(TCHAR Ch) const;
	void ShowPreviewStepRuler();              // preview vertical scrub: overlay the value-axis ruler on the preview
	void HidePreviewStepRuler();              // remove the preview ruler (on release)
	void InsertText(const FString& Text);
	void DeleteBackward();
	void StripLeadingZeros();         // drop redundant leading zeros of the integer part (DEC numeric; "05"→"5")
	// Backspace-at-caret WITH undo: delete the char left of the caret and push it (with its index) onto the
	// undo stack. Returns true if a char was deleted. (Plain DeleteBackward does not record undo.)
	bool DeleteBackwardWithUndo();
	void PushBackspaceUndo();         // snapshot the CURRENT value + caret before a deletion (call pre-delete)
	void UndoBackspaceFromStack();    // pop + restore the whole snapshot (overwrites CurrentText)
	void MoveCursor(int32 Delta);
	void ToggleShift();

	FVirtualKeyDef MakeBackspaceKey(float Width);
	FVirtualKeyDef MakeSpaceKey(float Width);
	TArray<TArray<FVirtualKeyDef>> GetQWERTYLayout();
	TArray<TArray<FVirtualKeyDef>> GetSymbolLayout();
	TArray<FVirtualKeyDef> GetBottomRow();
	TArray<TArray<FVirtualKeyDef>> GetT9Layout();
	TArray<FVirtualKeyDef> GetT9BottomRow();
	TArray<TArray<FVirtualKeyDef>> GetNumberLayout();  // numeric-only keypad (0-9, dot, minus, del, cursor)
	void ApplyNumericStep(int32 Direction);            // +1/-1: value ± step(or 1), clamped
	void AdjustDigitAtCursor(int32 Direction);         // up/down on cursor key: ±1 the digit at caret, with carry, clamped
	void ClearNumericValue();                          // Del swipe-up Clr in Number mode: set to 0, or the clamp bound nearest 0
	void CycleNumericRadix();                          // integer keypad: DEC→HEX→OCT→DEC, converts current value
	// integer → string. DEC: signed decimal. HEX: two's-complement at BitWidth (negative → FF.., zero-padded
	// to BitWidth/4 nibbles), never a '-' sign. BitWidth 0 → HEX uses 64-bit mask, no zero-padding.
	static FString FormatIntInRadix(int64 Value, int32 Radix, int32 BitWidth = 0);

	// ---- Numeric calculator (expression-string model) ----
	// CalcExpr holds the FULL expression the user typed ("3+4-7+8"), shown verbatim in the preview (WYSIWYG).
	// CurrentText (the value synced to the bound field) is frozen while an expression is being typed and is
	// only rewritten when '=' evaluates the whole string. CalcExpr non-empty ⇔ "expression mode".
	bool IsCalcEnabled() const;                        // true only on DEC numeric pad (radix 10)
	void CalcAppendOperator(const FString& Op);        // + − * / : seed CalcExpr from CurrentText (first op) then
	                                                   //   append; a trailing operator is REPLACED (5+− → 5−)
	void CalcAppendDigit(const FString& Ch);           // append a digit/'.' to the expression string (expr mode)
	double CalcEvaluateExpression(const FString& Expr, bool& bOutDivZero) const;  // left-to-right fold of the whole string
	void CalcPressEquals();                            // = : evaluate CalcExpr → result into CurrentText, exit expr mode
	double CalcCompute(double A, double B, const FString& Op, bool& bOutDivZero) const;  // A op B, int/float aware
	void CalcWriteResult(double Value);                // clamp + format Value into CurrentText, sync field, exit expr mode
	void CalcReset();                                  // clear all calculator memory (expr + pending + repeat)
	FString CalcFormatNumber(double Value) const;      // format per int/float type (no radix; calc is DEC-only)
	void UpdateSignKey();                              // refresh ± key label (sign of current value) + visibility

};

}  // namespace ImSlate

UCLASS()
class IMSLATE_API UImVirtualKeyboard : public UObject
{
	GENERATED_BODY()
public:
	static TSharedPtr<ImSlate::SImSlateVirtualKeyboard> GetVirtualKeyboard() { return ImSlate::SImSlateVirtualKeyboard::Get(); }
};
