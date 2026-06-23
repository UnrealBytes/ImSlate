// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImXConsolePanel.h"

#include "GenericSingletons.h"
#include "ImSlateExtra.h"
#include "XConsoleManager.h"
#include "XConsoleCommandMeta.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "GameFramework/PlayerInput.h"
#include "EnhancedPlayerInput.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"  // FCoreUObjectDelegates::PostLoadMapWithWorld
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"

#if GMP_EXTEND_CONSOLE

// Resolve the current game world (PIE or standalone). Used by both the console command path and the
// hotkey path so they toggle the SAME panel instance.
static UWorld* GetXConsoleGameWorld()
{
	if (!GEngine)
		return nullptr;
	// Prefer the active play world (PIE/standalone); GWorld is correct under the InputProcessor too.
	if (UWorld* Play = GEngine->GetCurrentPlayWorld())
		return Play;
	if (GWorld && GWorld->IsGameWorld())
		return GWorld;
	return nullptr;
}

static void ToggleXConsolePanel(TOptional<bool> bOpen, UWorld* InWorld)
{
	if (InWorld)
		UGenericSingletons::GetSingleton<UImXConsolePanel>(InWorld)->EnableTick(bOpen);
}

static FXConsoleCommandLambdaFull XVar_ImSlateXConsole(
	TEXT("imslate.XConsole"),
	TEXT("Toggle ImSlate XConsole Panel"),
	[](TOptional<bool> bOpen, UWorld* InWorld, FOutputDevice& Ar) {
		ToggleXConsolePanel(bOpen, InWorld);
	});

//////////////////////////////////////////////////////////////////////////
// Diagnostic: simulate a suspend→resume lifecycle and dump Slate pointer-capture state before/after.
// PURPOSE: the "xconsole clicks stop responding after resume" bug is suspected (not yet proven) to be a
// stale pointer capture left over from suspending mid-press. This command lets us GET EVIDENCE in PIE:
//   1) open the panel, press-hold a control (to create a capture),
//   2) run `imslate.DiagResume`,
//   3) read the log: does a captor widget survive the simulated resume? then try clicking.
// It only DUMPS (and broadcasts the engine lifecycle delegates) — it does NOT release capture, so we can
// observe the natural post-resume state first. Once the cause is confirmed, the fix goes in a resume hook.
//////////////////////////////////////////////////////////////////////////
static void DumpSlateCaptureState(const TCHAR* When)
{
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate.DiagResume] %s: SlateApplication not initialized"), When);
		return;
	}
	FSlateApplication& App = FSlateApplication::Get();
	int32 UserCount = 0;
	App.ForEachUser([&](FSlateUser& User) {
		++UserCount;
		const bool bHasCapture = User.HasAnyCapture();
		FString Captors;
		for (const TSharedRef<SWidget>& W : User.GetCaptorWidgets())
			Captors += FString::Printf(TEXT("%s "), *W->GetTypeAsString());
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate.DiagResume] %s: User[%d] HasAnyCapture=%d Captors=[%s]"),
			When, User.GetUserIndex(), bHasCapture ? 1 : 0, *Captors);
	});
	UE_LOG(LogTemp, Warning, TEXT("[ImSlate.DiagResume] %s: %d user(s); FocusedWidget=%s"),
		When, UserCount,
		App.GetUserFocusedWidget(0).IsValid() ? *App.GetUserFocusedWidget(0)->GetTypeAsString() : TEXT("<none>"));
}

static FAutoConsoleCommand GCmd_ImSlateDiagResume(
	TEXT("imslate.DiagResume"),
	TEXT("Diagnostic: dump Slate pointer-capture state, simulate suspend→resume lifecycle, dump again. "
		 "Use to investigate post-resume unresponsive clicks (does a captor survive?). Dumps only; no fix."),
	FConsoleCommandDelegate::CreateLambda([] {
		DumpSlateCaptureState(TEXT("BEFORE"));
		UE_LOG(LogTemp, Warning, TEXT("[ImSlate.DiagResume] broadcasting WillEnterBackground → HasReactivated → HasEnteredForeground"));
		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Broadcast();
		FCoreDelegates::ApplicationHasReactivatedDelegate.Broadcast();
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Broadcast();
		DumpSlateCaptureState(TEXT("AFTER "));
	}));

//////////////////////////////////////////////////////////////////////////
// Desktop hotkey: Alt+~ or Ctrl+~ toggles the panel (game/PIE runtime only)
//
// Implemented via UE's DebugExecBindings (the same mechanism the ImGui plugin uses for its
// Right-Shift toggle): a key+modifier chord is injected into UPlayerInput::DebugExecBindings and
// bound to an exec console command. This is the engine-native debug-key path (like the `~` console
// key) and works reliably inside PIE — unlike a Slate IInputProcessor, which the PIE viewport /
// CommonUI input stack can consume before it reaches us.
//////////////////////////////////////////////////////////////////////////
#if PLATFORM_DESKTOP

// Exec command the chords are bound to. Flips the panel for the current game world.
static FAutoConsoleCommand GCmd_ImSlateXConsoleToggle(
	TEXT("imslate.XConsole.Toggle"),
	TEXT("Toggle the ImSlate XConsole panel (bound to Alt+~ / Ctrl+~ on desktop)."),
	FConsoleCommandDelegate::CreateLambda([] {
		if (UWorld* World = GetXConsoleGameWorld())
			ToggleXConsolePanel(TOptional<bool>(), World);  // no arg = flip current state
	}));

// Inject (or refresh) a chord → command binding into one player input's DebugExecBindings.
static void ApplyXConsoleKeyBind(UPlayerInput* PlayerInput, const FKey& Key, bool bAlt, bool bCtrl, const TCHAR* Command)
{
	if (!PlayerInput)
		return;

	FKeyBind KeyBind;
	KeyBind.Command = Command;
	KeyBind.Key = Key;
	KeyBind.bDisabled = false;
	// Require exactly this modifier; ignore the others so e.g. Ctrl+~ doesn't also need Alt.
	KeyBind.Alt = bAlt;       KeyBind.bIgnoreAlt = !bAlt;
	KeyBind.Control = bCtrl;  KeyBind.bIgnoreCtrl = !bCtrl;
	KeyBind.Shift = false;    KeyBind.bIgnoreShift = true;
	KeyBind.Cmd = false;      KeyBind.bIgnoreCmd = true;

	// Replace an existing binding for the same command+key, else append. (One command is bound by
	// two chords — Alt and Ctrl — so match on command AND key, not command alone.)
	const int32 Index = PlayerInput->DebugExecBindings.IndexOfByPredicate([&](const FKeyBind& Existing) {
		return Existing.Command.Equals(KeyBind.Command, ESearchCase::IgnoreCase) && Existing.Key == KeyBind.Key;
	});
	if (Index != INDEX_NONE)
		PlayerInput->DebugExecBindings[Index] = KeyBind;
	else
		PlayerInput->DebugExecBindings.Add(KeyBind);
}

// Inject the Alt+~ and Ctrl+~ chords into the EnhancedPlayerInput CDO (so all future PIE/game
// sessions inherit them) and into any already-living player input instances (so a running PIE
// session picks them up immediately). Mirrors ImGui's DebugExecBindings::UpdatePlayerInputs.
static void InstallXConsoleHotkeys()
{
	const TCHAR* Cmd = TEXT("imslate.XConsole.Toggle");
	// NOTE: don't name the param `PI` — UE defines `#define PI` (UnrealMathUtility.h), which would
	// macro-expand it to a float literal.
	auto BindBoth = [&](UPlayerInput* InPlayerInput) {
		ApplyXConsoleKeyBind(InPlayerInput, EKeys::Tilde, /*Alt*/true,  /*Ctrl*/false, Cmd);
		ApplyXConsoleKeyBind(InPlayerInput, EKeys::Tilde, /*Alt*/false, /*Ctrl*/true,  Cmd);
	};

	if (UEnhancedPlayerInput* DefaultPlayerInput = GetMutableDefault<UEnhancedPlayerInput>())
		BindBoth(DefaultPlayerInput);
	for (TObjectIterator<UEnhancedPlayerInput> It; It; ++It)
		BindBoth(*It);
}

// Install on engine init (covers all PIE sessions started afterwards) and again on each map load
// (covers the running session's freshly-created player input). Guarded to desktop only.
struct FImXConsoleHotkeyRegistrar
{
	FImXConsoleHotkeyRegistrar()
	{
		FCoreDelegates::OnPostEngineInit.AddLambda([] { InstallXConsoleHotkeys(); });
		FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda([](UWorld*) { InstallXConsoleHotkeys(); });
	}
};
static FImXConsoleHotkeyRegistrar GImXConsoleHotkeyRegistrar;

#endif  // PLATFORM_DESKTOP

//////////////////////////////////////////////////////////////////////////
// Lifecycle
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::EnableTick(TOptional<bool> bEnable)
{
	do
	{
		if (!GetWorld())
			break;
		bool bTargetOpen = bEnable.Get(!bOpen);
		if (bOpen != bTargetOpen)
		{
			TickHandle.Reset();
			EndShow();
		}
		if (bTargetOpen)
		{
			TickHandle = ImSlate::ImSlateTicker::BindDelegate(
				ImSlate::ImSlateTicker::FOnTick::CreateWeakLambda(this, [this](float Delta) { Tick(Delta); }),
				GetWorld());
			StartShow();
		}
		return;
	} while (false);
}

void UImXConsolePanel::StartShow()
{
	bOpen = true;
	bNeedsRefresh = true;
}

void UImXConsolePanel::EndShow()
{
	bOpen = false;
	CommandTree.Reset();
	VariableTree.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Data - shared helpers
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::ParseDotName(const FString& FullName, FString& OutCategory, FString& OutSubCategory, FString& OutLeafName)
{
	TArray<FString> Parts;
	FullName.ParseIntoArray(Parts, TEXT("."));
	if (Parts.Num() >= 3)
	{
		OutCategory = Parts[0];
		OutSubCategory = Parts[1];
		OutLeafName = FullName.Mid(OutCategory.Len() + 1 + OutSubCategory.Len() + 1);
	}
	else if (Parts.Num() == 2)
	{
		OutCategory = Parts[0];
		OutLeafName = Parts[1];
	}
	else
	{
		OutCategory = TEXT("misc");
		OutLeafName = FullName;
	}
}

void UImXConsolePanel::RefreshAll()
{
	RefreshCommands();
	RefreshVariables();
	bNeedsRefresh = false;
}

//////////////////////////////////////////////////////////////////////////
// Data - Commands
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::RefreshCommands()
{
	CommandTree.Reset();

	auto& Manager = IXConsoleManager::Get();
	TArray<FString> CmdNames = Manager.GetXConsoleCommandList();
	CmdNames.Sort();

	for (const FString& CmdName : CmdNames)
	{
		FImXConsoleCommandInfo Info;
		Info.Name = CmdName;

		if (const GMP::FArrayTypeNames* Props = Manager.GetXConsoleCommandProps(*CmdName))
		{
			Info.ParamTypes.Append(Props->GetData(), Props->Num());
			Info.ParamValues.SetNum(Props->Num());
			Info.ParamEnabled.SetNum(Props->Num());
			for (int32 i = 0; i < Props->Num(); ++i)
				Info.ParamEnabled[i] = !(*Props)[i].ToString().StartsWith(TEXT("TOptional<"));
		}

		if (IConsoleObject* CObj = IConsoleManager::Get().FindConsoleObject(*CmdName))
			Info.Help = CObj->GetHelp();

		ParseDotName(CmdName, Info.Category, Info.SubCategory, Info.LeafName);

		// Apply meta: Category override, Hidden filter, DefaultValue
		if (const FXConsoleObjectMeta* Meta = IXConsoleManager::GetXConsoleMeta(*CmdName))
		{
			if (Meta->SelfMeta.GetMetaBool(TEXT("Hidden")))
				continue;
			if (Meta->SelfMeta.HasMeta(TEXT("Category")))
			{
				FString OverrideCategory = Meta->SelfMeta.GetMeta(TEXT("Category"));
				ParseDotName(OverrideCategory + TEXT(".") + Info.LeafName, Info.Category, Info.SubCategory, Info.LeafName);
			}
			// Apply DefaultValue for params
			for (int32 i = 0; i < Meta->Params.Num() && i < Info.ParamValues.Num(); ++i)
			{
				if (Meta->Params[i].HasMeta(TEXT("DefaultValue")) && Info.ParamValues[i].IsEmpty())
					Info.ParamValues[i] = Meta->Params[i].GetMeta(TEXT("DefaultValue"));
			}
		}

		CommandTree.FindOrAdd(Info.Category).FindOrAdd(Info.SubCategory).Add(MoveTemp(Info));
	}
}

//////////////////////////////////////////////////////////////////////////
// Data - Variables
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::RefreshVariables()
{
	VariableTree.Reset();

	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
		FConsoleObjectVisitor::CreateLambda([this](const TCHAR* Name, IConsoleObject* Obj) {
			IConsoleVariable* CVar = Obj->AsVariable();
			if (!CVar) return;

			FImXConsoleVariableInfo Info;
			Info.Name = Name;
			Info.Help = Obj->GetHelp();
			Info.CVar = CVar;
			Info.CurrentValue = CVar->GetString();

			ParseDotName(Info.Name, Info.Category, Info.SubCategory, Info.LeafName);
			VariableTree.FindOrAdd(Info.Category).FindOrAdd(Info.SubCategory).Add(MoveTemp(Info));
		}),
		TEXT(""));
}

//////////////////////////////////////////////////////////////////////////
// Execute
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::ExecuteCommand(const FImXConsoleCommandInfo& Info)
{
	FString FullCmd = Info.Name;
	for (int32 i = 0; i < Info.ParamValues.Num(); ++i)
	{
		const FString& TypeStr = Info.ParamTypes[i].ToString();
		bool bIsOptional = TypeStr.StartsWith(TEXT("TOptional<"));

		if (bIsOptional && !Info.ParamEnabled[i])
		{
			FullCmd += TEXT(" \"\"");
			continue;
		}

		const FString& Val = Info.ParamValues[i];
		if (Val.Contains(TEXT(" ")))
			FullCmd += FString::Printf(TEXT(" \"%s\""), *Val);
		else
			FullCmd += TEXT(" ") + Val;
	}

	if (UWorld* World = GetWorld())
	{
		GEngine->Exec(World, *FullCmd);
		UE_LOG(LogTemp, Log, TEXT("[ImXConsole] Executed: %s"), *FullCmd);
	}
}

//////////////////////////////////////////////////////////////////////////
// DrawParamWidget
//////////////////////////////////////////////////////////////////////////

namespace
{
	FString ExtractEnumName(const FString& TypeStr)
	{
		for (const TCHAR* Prefix : {TEXT("TEnumAsByte<"), TEXT("TEnum2Bytes<"), TEXT("TEnum4Bytes<"), TEXT("TEnum8Bytes<")})
		{
			if (TypeStr.StartsWith(Prefix) && TypeStr.EndsWith(TEXT(">")))
			{
				int32 PrefixLen = FCString::Strlen(Prefix);
				return TypeStr.Mid(PrefixLen, TypeStr.Len() - PrefixLen - 1);
			}
		}
		return FString();
	}

	FString ExtractArrayElementType(const FString& TypeStr)
	{
		if (TypeStr.StartsWith(TEXT("TArray<")) && TypeStr.EndsWith(TEXT(">")))
			return TypeStr.Mid(7, TypeStr.Len() - 8);
		return FString();
	}

	// Parse JSON array "[v1,v2,v3]" → TArray<FString>
	void ParseJsonArray(const FString& JsonStr, TArray<FString>& OutElements)
	{
		OutElements.Reset();
		FString Trimmed = JsonStr.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("[")) && Trimmed.EndsWith(TEXT("]")))
			Trimmed = Trimmed.Mid(1, Trimmed.Len() - 2);
		if (!Trimmed.IsEmpty())
			Trimmed.ParseIntoArray(OutElements, TEXT(","));
		for (FString& Elem : OutElements)
			Elem = Elem.TrimStartAndEnd().TrimChar('"');
	}

	// TArray<FString> → JSON array string
	FString ToJsonArray(const TArray<FString>& Elements, bool bIsString)
	{
		FString Result = TEXT("[");
		for (int32 i = 0; i < Elements.Num(); ++i)
		{
			if (i > 0) Result += TEXT(",");
			if (bIsString)
				Result += FString::Printf(TEXT("\"%s\""), *Elements[i]);
			else
				Result += Elements[i];
		}
		Result += TEXT("]");
		return Result;
	}
}

void UImXConsolePanel::DrawParamWidget(const FName& TypeName, FString& Value, bool& bEnabled, bool bIsOptional, int32 Index, const FString& CmdName)
{
	FString WidgetId = FString::Printf(TEXT("%s_p%d"), *CmdName, Index);
	const FString TypeStr = TypeName.ToString();

	// Query meta for this parameter
	const FXConsoleObjectMeta* CmdMeta = IXConsoleManager::GetXConsoleMeta(*CmdName);
	const FXConsoleParamMeta* PMeta = (CmdMeta && CmdMeta->Params.IsValidIndex(Index)) ? &CmdMeta->Params[Index] : nullptr;

	// Extract inner type for TOptional<T>
	FString InnerType = TypeStr;
	if (TypeStr.StartsWith(TEXT("TOptional<")) && TypeStr.EndsWith(TEXT(">")))
		InnerType = TypeStr.Mid(10, TypeStr.Len() - 11);

	const bool bIsOptionalBool = bIsOptional && (InnerType == TEXT("bool"));

	// Wrap an optional parameter's [enable checkbox + value widget] in one group so they read as a
	// single unit (shared faint-green background, matching the green enable toggle). TOptional<bool>
	// is a single tri-state box and needs no grouping.
	const bool bGroupWrap = bIsOptional && !bIsOptionalBool;
	if (bGroupWrap)
	{
		FString GroupId = FString::Printf(TEXT("%s_grp%d"), *CmdName, Index);
		// Colour the group by enable state: green = enabled (arg will be passed), blue = disabled (skipped).
		const FLinearColor GroupCol = bEnabled
			? FLinearColor(0.20f, 0.80f, 0.30f, 0.30f)
			: FLinearColor(0.15f, 0.20f, 0.30f, 0.25f);  // disabled: low-alpha cool blue — distinct from the green "on" tint and doesn't blend with the neutral-grey text/input
		ImSlate::BeginGroup(FStringView(GroupId), GroupCol);
	}

	// TOptional enable checkbox — EXCEPT for TOptional<bool>, which uses a single tri-state
	// checkbox below (Undetermined = unset/no-arg, Checked = true, Unchecked = false) instead
	// of a separate enable box + value box.
	if (bIsOptional && !bIsOptionalBool)
	{
		FString OptId = FString::Printf(TEXT("%s_opt%d"), *CmdName, Index);
		// Green accent marks this as the "enable / is-set" toggle for an optional parameter, to
		// distinguish it from a regular (blue) value checkbox.
		// Auto-width so the checkbox doesn't take a fill-share of the row (it hard-codes bFillWidth=true);
		// otherwise it and the value InputText split the row 50/50, leaving a big gap before the input.
		ImSlate::SetNextItemAutoWidth();
		ImSlate::CheckBox(FStringView(OptId), bEnabled, ImVec2(0, 0), FLinearColor(0.20f, 0.80f, 0.30f, 1.f));
		ImSlate::SameLine();
	}

	// === bool ===
	if (InnerType == TEXT("bool"))
	{
		if (bIsOptionalBool)
		{
			// Tri-state: Undetermined → not passed (bEnabled=false); Checked/Unchecked → true/false.
			ECheckBoxState State = !bEnabled
				? ECheckBoxState::Undetermined
				: ((Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1"))
					? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
			if (ImSlate::CheckBox(FStringView(WidgetId), State))
			{
				if (State == ECheckBoxState::Undetermined)
				{
					bEnabled = false;  // unset → ExecuteCommand passes "" (no arg)
				}
				else
				{
					bEnabled = true;
					Value = (State == ECheckBoxState::Checked) ? TEXT("true") : TEXT("false");
				}
			}
		}
		else
		{
			bool bVal = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			if (ImSlate::CheckBox(FStringView(WidgetId), bVal))
				Value = bVal ? TEXT("true") : TEXT("false");
		}
	}
	// === float / double ===
	else if (InnerType == TEXT("float") || InnerType == TEXT("double"))
	{
		bool bHasUIRange = PMeta && PMeta->HasMeta(TEXT("UIMin")) && PMeta->HasMeta(TEXT("UIMax"));

		if (bHasUIRange)
		{
			// NumericFloat with slider
			TOptional<float> FloatOpt = Value.IsEmpty() ? TOptional<float>() : TOptional<float>(FCString::Atof(*Value));
			float ValMin = PMeta->GetMetaDouble(TEXT("ClampMin"), FLT_MIN);
			float ValMax = PMeta->GetMetaDouble(TEXT("ClampMax"), FLT_MAX);
			float SliderMin = PMeta->GetMetaDouble(TEXT("UIMin"), 0.f);
			float SliderMax = PMeta->GetMetaDouble(TEXT("UIMax"), 1.f);
			float Delta = PMeta->GetMetaDouble(TEXT("Delta"), 0.f);
			ImSlate::SetNextItemMinWidth(150.f);
			if (ImSlate::NumericFloat(FStringView(WidgetId), FloatOpt, ValMin, ValMax, SliderMin, SliderMax, Delta) != ImSlate::ImSliderStatus_Normal)
			{
				if (FloatOpt.IsSet())
					Value = FString::SanitizeFloat(FloatOpt.GetValue());
			}
		}
		else
		{
			// InputFloat with clamp
			double FloatVal = FCString::Atod(*Value);
			double MinVal = FLT_MIN, MaxVal = FLT_MAX;
			if (PMeta)
			{
				if (PMeta->HasMeta(TEXT("ClampMin"))) MinVal = PMeta->GetMetaDouble(TEXT("ClampMin"), FLT_MIN);
				if (PMeta->HasMeta(TEXT("ClampMax"))) MaxVal = PMeta->GetMetaDouble(TEXT("ClampMax"), FLT_MAX);
			}
			ImSlate::SetNextItemMinWidth(100.f);
			if (ImSlate::InputFloat(FStringView(WidgetId), FloatVal, MinVal, MaxVal) != ImSlate::ImSliderStatus_Normal)
			{
				if (PMeta)
				{
					if (PMeta->HasMeta(TEXT("ClampMin"))) FloatVal = FMath::Max(FloatVal, PMeta->GetMetaDouble(TEXT("ClampMin")));
					if (PMeta->HasMeta(TEXT("ClampMax"))) FloatVal = FMath::Min(FloatVal, PMeta->GetMetaDouble(TEXT("ClampMax")));
				}
				Value = FString::SanitizeFloat(FloatVal);
			}
		}
	}
	// === int types ===
	else if (InnerType == TEXT("int32") || InnerType == TEXT("int16") || InnerType == TEXT("int8")
		|| InnerType == TEXT("uint8") || InnerType == TEXT("uint16") || InnerType == TEXT("uint32"))
	{
		bool bHasUIRange = PMeta && PMeta->HasMeta(TEXT("UIMin")) && PMeta->HasMeta(TEXT("UIMax"));

		if (bHasUIRange)
		{
			// NumericInt with slider
			TOptional<int32> IntOpt = Value.IsEmpty() ? TOptional<int32>() : TOptional<int32>(FCString::Atoi(*Value));
			int32 ValMin = PMeta->GetMetaInt(TEXT("ClampMin"), INT_MIN);
			int32 ValMax = PMeta->GetMetaInt(TEXT("ClampMax"), INT_MAX);
			int32 SliderMin = PMeta->GetMetaInt(TEXT("UIMin"), 0);
			int32 SliderMax = PMeta->GetMetaInt(TEXT("UIMax"), 100);
			int32 Delta = PMeta->GetMetaInt(TEXT("Delta"), 0);
			ImSlate::SetNextItemMinWidth(150.f);
			if (ImSlate::NumericInt(FStringView(WidgetId), IntOpt, ValMin, ValMax, SliderMin, SliderMax, Delta) != ImSlate::ImSliderStatus_Normal)
			{
				if (IntOpt.IsSet())
					Value = FString::FromInt(IntOpt.GetValue());
			}
		}
		else
		{
			// InputText: integer numeric keypad (no '.'; unsigned types hide '-'), with optional
			// clamp + step from meta, and per-key input history (WidgetId is stable: CmdName_pIndex).
			FString IntStr = Value;
			ImSlate::FImInputNumericSpec Spec;
			Spec.bInteger = true;
			Spec.bUnsigned = InnerType.StartsWith(TEXT("uint"));
			if (PMeta)
			{
				if (PMeta->HasMeta(TEXT("ClampMin"))) Spec.Min = (double)PMeta->GetMetaInt(TEXT("ClampMin"));
				if (PMeta->HasMeta(TEXT("ClampMax"))) Spec.Max = (double)PMeta->GetMetaInt(TEXT("ClampMax"));
				if (PMeta->HasMeta(TEXT("Delta")))    Spec.Step = (double)PMeta->GetMetaInt(TEXT("Delta"));
			}
			ImSlate::SetNextItemMinWidth(80.f);
			if (ImSlate::InputText(FStringView(WidgetId), IntStr, ImVec2(0, 0), ImSlate::ImSlateInputTextFlags_None, FStringView(WidgetId), &Spec))
			{
				int32 IntVal = FCString::Atoi(*IntStr);
				if (PMeta)
				{
					if (PMeta->HasMeta(TEXT("ClampMin"))) IntVal = FMath::Max(IntVal, PMeta->GetMetaInt(TEXT("ClampMin")));
					if (PMeta->HasMeta(TEXT("ClampMax"))) IntVal = FMath::Min(IntVal, PMeta->GetMetaInt(TEXT("ClampMax")));
				}
				Value = FString::FromInt(IntVal);
			}
		}
	}
	// === enum (single select or bitmask multi-select) ===
	else if (FString EnumName = ExtractEnumName(InnerType); !EnumName.IsEmpty())
	{
		UEnum* EnumPtr = DynamicEnum(EnumName);
		if (!EnumPtr && PMeta && PMeta->HasMeta(TEXT("BitmaskEnum")))
			EnumPtr = DynamicEnum(PMeta->GetMeta(TEXT("BitmaskEnum")));

		bool bIsBitmask = PMeta && PMeta->GetMetaBool(TEXT("Bitmask"));

		if (EnumPtr && bIsBitmask)
		{
			// Bitmask multi-select: checkboxes per enum value
			int32 MaskVal = Value.IsEmpty() ? 0 : FCString::Atoi(*Value);
			int32 NumEntries = EnumPtr->NumEnums() - 1;
			bool bChanged = false;
			for (int32 i = 0; i < NumEntries; ++i)
			{
				int64 EntryVal = EnumPtr->GetValueByIndex(i);
				if (EntryVal == 0 && i > 0) continue;

				FString EntryName = EnumPtr->GetDisplayNameTextByIndex(i).ToString();
				if (EntryName.IsEmpty()) EntryName = EnumPtr->GetNameStringByIndex(i);

				FString CheckId = FString::Printf(TEXT("%s_bm%d"), *WidgetId, i);
				bool bBit = !!(MaskVal & (int32)EntryVal);
				if (ImSlate::CheckBox(FStringView(CheckId), bBit))
				{
					if (bBit) MaskVal |= (int32)EntryVal;
					else MaskVal &= ~(int32)EntryVal;
					bChanged = true;
				}
				ImSlate::SameLine();
				ImSlate::Text(FStringView(CheckId), EntryName);
			}
			if (bChanged) Value = FString::FromInt(MaskVal);
		}
		else if (EnumPtr)
		{
			// Single select combo
			int64 CurVal = Value.IsEmpty() ? 0 : (int64)FCString::Atoi(*Value);
			ImSlate::SetNextItemMinWidth(120.f);
			if (ImSlate::ComboBoxForEnum(FStringView(WidgetId), CurVal, EnumPtr))
				Value = FString::FromInt((int32)CurVal);
		}
		else
		{
			ImSlate::SetNextItemMinWidth(100.f);
			// String param: plain text keyboard + per-key history (WidgetId is stable: CmdName_pIndex).
			ImSlate::InputText(FStringView(WidgetId), Value, ImVec2(0, 0), ImSlate::ImSlateInputTextFlags_None, FStringView(WidgetId));
		}
	}
	// === Object / SoftObject ===
	else if (InnerType.Contains(TEXT("ObjectProperty")) || InnerType.Contains(TEXT("SoftObjectProperty")))
	{
		FSoftObjectPath Path(Value);
		ImSlate::SetNextItemMinWidth(200.f);
		if (ImSlate::AssetPicker(FStringView(WidgetId), Path))
			Value = Path.ToString();
	}
	// === Class / SoftClass ===
	else if (InnerType.Contains(TEXT("ClassProperty")) || InnerType.Contains(TEXT("SoftClassProperty")))
	{
		FSoftClassPath Path(Value);
		ImSlate::SetNextItemMinWidth(200.f);
		if (ImSlate::ClassPicker(FStringView(WidgetId), Path))
			Value = Path.ToString();
	}
	// === TArray<T> ===
	else if (FString ElemType = ExtractArrayElementType(InnerType); !ElemType.IsEmpty())
	{
		TArray<FString> Elements;
		ParseJsonArray(Value, Elements);

		bool bIsStringType = (ElemType == TEXT("String") || ElemType == TEXT("FString") || ElemType == TEXT("FName") || ElemType == TEXT("FText"));
		bool bChanged = false;

		// Draw each element
		for (int32 i = 0; i < Elements.Num(); ++i)
		{
			FString ElemId = FString::Printf(TEXT("%s_e%d"), *WidgetId, i);
			bool bDummy = true;
			ImSlate::SetNextItemMinWidth(80.f);
			DrawParamWidget(FName(*ElemType), Elements[i], bDummy, false, i, WidgetId);

			// Remove button
			ImSlate::SameLine();
			FString RemoveId = FString::Printf(TEXT("X##%s_r%d"), *WidgetId, i);
			if (ImSlate::Button(FStringView(RemoveId)))
			{
				Elements.RemoveAt(i);
				bChanged = true;
				--i;
			}
		}

		// Add button
		FString AddId = FString::Printf(TEXT("+##%s_add"), *WidgetId);
		if (ImSlate::Button(FStringView(AddId)))
		{
			Elements.Add(FString());
			bChanged = true;
		}

		if (bChanged)
			Value = ToJsonArray(Elements, bIsStringType);
	}
	// === string / name / other ===
	else
	{
		ImSlate::SetNextItemFillWidth(1.f);
		// Plain text + per-key history (WidgetId stable: CmdName_pIndex).
		ImSlate::InputText(FStringView(WidgetId), Value, ImVec2(0, 0), ImSlate::ImSlateInputTextFlags_None, FStringView(WidgetId));
	}

	if (bGroupWrap)
		ImSlate::EndGroup();
}

//////////////////////////////////////////////////////////////////////////
// UI - Search bar
//////////////////////////////////////////////////////////////////////////

bool UImXConsolePanel::MatchesSearch(const FString& Name, const FString& Help) const
{
	if (SearchFilter.IsEmpty()) return true;
	TArray<FString> Terms;
	SearchFilter.ParseIntoArray(Terms, TEXT(" "), true);
	for (const FString& Term : Terms)
	{
		if (!Term.IsEmpty() && !Name.Contains(Term, ESearchCase::IgnoreCase))
			return false;
	}
	return true;
}

static constexpr float XConsoleRowHeight = 48.f;

bool UImXConsolePanel::DrawFold(const FString& Id, const FString& DisplayText)
{
	bool& bFolded = FoldStates.FindOrAdd(Id, true);
	FString Arrow = bFolded ? TEXT("\x25B6 ") : TEXT("\x25BC ");
	ImSlate::SetNextItemFillWidth(1.f);
	if (ImSlate::TextButton(FStringView(Id), FText::FromString(Arrow + DisplayText), ImVec2(0, XConsoleRowHeight)))
		bFolded = !bFolded;
	return !bFolded;
}

void UImXConsolePanel::DrawSearchBar()
{
	// Keep commands and variables as separate suggestion lists so the search box only suggests
	// entries from the currently selected tab (ActiveTab 0 = commands, 1 = variables).
	static TArray<FString> CmdNames;
	static TArray<FString> VarNames;
	if (bNeedsRefresh || (CmdNames.IsEmpty() && VarNames.IsEmpty()))
	{
		CmdNames.Reset();
		VarNames.Reset();
		for (auto& [Cat, SubCats] : CommandTree)
			for (auto& [Sub, Cmds] : SubCats)
				for (auto& Cmd : Cmds)
					CmdNames.Add(Cmd.Name);
		for (auto& [Cat, SubCats] : VariableTree)
			for (auto& [Sub, Vars] : SubCats)
				for (auto& Var : Vars)
					VarNames.Add(Var.Name);
	}

	TArray<FString>* Names = (ActiveTab == 0) ? &CmdNames : &VarNames;

	ImSlate::SetNextItemFillWidth(0.7f);
	// No manual keyboard button: focusing the edit auto-shows the virtual keyboard now.
	ImSlate::SearchBox("##XConsoleSearch", SearchFilter, Names, nullptr, ImVec2(0, XConsoleRowHeight), false);

	ImSlate::SameLine();
	if (ImSlate::Button("Refresh", ImVec2(80.f, XConsoleRowHeight)))
		bNeedsRefresh = true;
}

//////////////////////////////////////////////////////////////////////////
// UI - Command drawing
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::DrawCommandEntry(FImXConsoleCommandInfo& Info)
{
	IM_SLATE_SCOPE(FStringView(Info.Name));

	const FXConsoleObjectMeta* Meta = IXConsoleManager::GetXConsoleMeta(*Info.Name);

	// Command name — click to execute
	FString LeafDisplay = (Meta && Meta->SelfMeta.HasMeta(TEXT("DisplayName")))
		? Meta->SelfMeta.GetMeta(TEXT("DisplayName"))
		: Info.LeafName;
	FString Tip = Info.Name;
	if (!Info.Help.IsEmpty()) Tip += TEXT("\n") + Info.Help;
	ImSlate::SetNextItemTooltip(FText::FromString(Tip));
	if (ImSlate::TextButton("CmdName", FText::FromString(LeafDisplay), ImVec2(0, XConsoleRowHeight)))
		ExecuteCommand(Info);

	// Parameters — each on same line, fill remaining width
	for (int32 i = 0; i < Info.ParamTypes.Num(); ++i)
	{
		ImSlate::SameLine();
		const FString& TypeStr = Info.ParamTypes[i].ToString();
		bool bIsOptional = TypeStr.StartsWith(TEXT("TOptional<"));
		DrawParamWidget(Info.ParamTypes[i], Info.ParamValues[i], Info.ParamEnabled[i], bIsOptional, i, Info.Name);
	}

	ImSlate::Spacing(3.f);
}

void UImXConsolePanel::DrawCommandCategory(const FString& Category, TMap<FString, TArray<FImXConsoleCommandInfo>>& SubCats)
{
	const bool bSearching = !SearchFilter.IsEmpty();
	if (bSearching)
	{
		bool bAnyVisible = false;
		for (auto& SubPair : SubCats)
			for (const auto& Cmd : SubPair.Value)
				if (MatchesSearch(Cmd.Name, Cmd.Help)) { bAnyVisible = true; break; }
		if (!bAnyVisible) return;
	}

	if (ImSlate::BeginFold(FStringView(Category), FText::FromString(Category)))
	{
		TArray<FString> SubCatKeys;
		SubCats.GetKeys(SubCatKeys);
		SubCatKeys.Sort();

		for (const FString& SubCat : SubCatKeys)
		{
			TArray<FImXConsoleCommandInfo>& Commands = SubCats[SubCat];

			if (SubCat.IsEmpty())
			{
				for (auto& Cmd : Commands)
					if (MatchesSearch(Cmd.Name, Cmd.Help))
						DrawCommandEntry(Cmd);
			}
			else
			{
				if (bSearching)
				{
					bool bSubVisible = false;
					for (const auto& Cmd : Commands)
						if (MatchesSearch(Cmd.Name, Cmd.Help)) { bSubVisible = true; break; }
					if (!bSubVisible) continue;
				}

				FString SubId = Category + TEXT(".") + SubCat;
				if (ImSlate::BeginFold(FStringView(SubId), FText::FromString(SubCat)))
				{
					for (auto& Cmd : Commands)
						if (MatchesSearch(Cmd.Name, Cmd.Help))
							DrawCommandEntry(Cmd);
					ImSlate::EndFold();
				}
			}
		}
		ImSlate::EndFold();
	}
}

void UImXConsolePanel::DrawCommandsTab()
{
	TArray<FString> Categories;
	CommandTree.GetKeys(Categories);
	Categories.Sort();

	bool bNeedSpacing = false;
	for (const FString& Cat : Categories)
	{
		// Defensive: use Find (not operator[]/FindChecked). A command executed during this same
		// draw pass can mutate CommandTree (rehash) and invalidate Cat — FindChecked would crash.
		auto* SubCatsPtr = CommandTree.Find(Cat);
		if (!SubCatsPtr)
			continue;
		auto& SubCats = *SubCatsPtr;
		if (!SearchFilter.IsEmpty())
		{
			bool bAny = false;
			for (auto& Sub : SubCats)
				for (const auto& Cmd : Sub.Value)
					if (MatchesSearch(Cmd.Name, Cmd.Help)) { bAny = true; break; }
			if (!bAny) continue;
		}
		if (bNeedSpacing) ImSlate::Spacing(2.f);
		DrawCommandCategory(Cat, SubCats);
		bNeedSpacing = true;
	}
}

//////////////////////////////////////////////////////////////////////////
// UI - Variable drawing
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::DrawVariableEntry(FImXConsoleVariableInfo& Info)
{
	if (!Info.CVar) return;

	IM_SLATE_SCOPE(FStringView(Info.Name));

	const FXConsoleObjectMeta* Meta = IXConsoleManager::GetXConsoleMeta(*Info.Name);
	const FXConsoleParamMeta* SelfMeta = Meta ? &Meta->SelfMeta : nullptr;

	FString DisplayLabel = (SelfMeta && SelfMeta->HasMeta(TEXT("DisplayName")))
		? SelfMeta->GetMeta(TEXT("DisplayName"))
		: Info.LeafName;
	FString VarTip = Info.Name;
	if (!Info.Help.IsEmpty()) VarTip += TEXT("\n") + Info.Help;
	ImSlate::SetNextItemTooltip(FText::FromString(VarTip));
	ImSlate::TextButton("VarName", FText::FromString(DisplayLabel), ImVec2(0, XConsoleRowHeight));
	ImSlate::SameLine();

	FString WidgetId = FString::Printf(TEXT("##val_%s"), *Info.Name);

	if (Info.CVar->IsVariableBool())
	{
		bool bVal = Info.CVar->GetBool();
		if (ImSlate::CheckBox(FStringView(WidgetId), bVal))
			Info.CVar->Set(bVal ? 1 : 0);
	}
	else if (Info.CVar->IsVariableFloat())
	{
		bool bHasUIRange = SelfMeta && SelfMeta->HasMeta(TEXT("UIMin")) && SelfMeta->HasMeta(TEXT("UIMax"));
		if (bHasUIRange)
		{
			TOptional<float> FloatOpt = Info.CVar->GetFloat();
			float ValMin = SelfMeta->GetMetaDouble(TEXT("ClampMin"), FLT_MIN);
			float ValMax = SelfMeta->GetMetaDouble(TEXT("ClampMax"), FLT_MAX);
			float SliderMin = SelfMeta->GetMetaDouble(TEXT("UIMin"), 0.f);
			float SliderMax = SelfMeta->GetMetaDouble(TEXT("UIMax"), 1.f);
			ImSlate::SetNextItemMinWidth(150.f);
			if (ImSlate::NumericFloat(FStringView(WidgetId), FloatOpt, ValMin, ValMax, SliderMin, SliderMax) != ImSlate::ImSliderStatus_Normal)
			{
				if (FloatOpt.IsSet())
					Info.CVar->Set(FloatOpt.GetValue());
			}
		}
		else
		{
			double Val = (double)Info.CVar->GetFloat();
			double MinVal = FLT_MIN, MaxVal = FLT_MAX;
			if (SelfMeta)
			{
				if (SelfMeta->HasMeta(TEXT("ClampMin"))) MinVal = SelfMeta->GetMetaDouble(TEXT("ClampMin"));
				if (SelfMeta->HasMeta(TEXT("ClampMax"))) MaxVal = SelfMeta->GetMetaDouble(TEXT("ClampMax"));
			}
			ImSlate::SetNextItemMinWidth(100.f);
			if (ImSlate::InputFloat(FStringView(WidgetId), Val, MinVal, MaxVal) != ImSlate::ImSliderStatus_Normal)
			{
				if (SelfMeta)
				{
					if (SelfMeta->HasMeta(TEXT("ClampMin"))) Val = FMath::Max(Val, SelfMeta->GetMetaDouble(TEXT("ClampMin")));
					if (SelfMeta->HasMeta(TEXT("ClampMax"))) Val = FMath::Min(Val, SelfMeta->GetMetaDouble(TEXT("ClampMax")));
				}
				Info.CVar->Set((float)Val);
			}
		}
	}
	else if (Info.CVar->IsVariableInt())
	{
		FString ValStr = Info.CVar->GetString();
		ImSlate::FImInputNumericSpec Spec;
		Spec.bInteger = true;  // integer CVar → keypad hides '.'
		if (SelfMeta)
		{
			if (SelfMeta->HasMeta(TEXT("ClampMin"))) Spec.Min = (double)SelfMeta->GetMetaInt(TEXT("ClampMin"));
			if (SelfMeta->HasMeta(TEXT("ClampMax"))) Spec.Max = (double)SelfMeta->GetMetaInt(TEXT("ClampMax"));
		}
		ImSlate::SetNextItemMinWidth(100.f);
		if (ImSlate::InputText(FStringView(WidgetId), ValStr, ImVec2(0, 0), ImSlate::ImSlateInputTextFlags_None, FStringView(WidgetId), &Spec))
		{
			int32 IntVal = FCString::Atoi(*ValStr);
			if (SelfMeta)
			{
				if (SelfMeta->HasMeta(TEXT("ClampMin"))) IntVal = FMath::Max(IntVal, SelfMeta->GetMetaInt(TEXT("ClampMin")));
				if (SelfMeta->HasMeta(TEXT("ClampMax"))) IntVal = FMath::Min(IntVal, SelfMeta->GetMetaInt(TEXT("ClampMax")));
			}
			Info.CVar->Set(IntVal);
		}
	}
	else
	{
		FString ValStr = Info.CVar->GetString();
		ImSlate::SetNextItemMinWidth(150.f);
		// String CVar: plain text + per-key history.
		if (ImSlate::InputText(FStringView(WidgetId), ValStr, ImVec2(0, 0), ImSlate::ImSlateInputTextFlags_None, FStringView(WidgetId)))
			Info.CVar->Set(*ValStr);
	}

	ImSlate::Spacing(3.f);
}

void UImXConsolePanel::DrawVariableCategory(const FString& Category, TMap<FString, TArray<FImXConsoleVariableInfo>>& SubCats)
{
	const bool bSearching = !SearchFilter.IsEmpty();
	if (bSearching)
	{
		bool bAnyVisible = false;
		for (auto& SubPair : SubCats)
			for (const auto& Var : SubPair.Value)
				if (MatchesSearch(Var.Name, Var.Help)) { bAnyVisible = true; break; }
		if (!bAnyVisible) return;
	}

	if (ImSlate::BeginFold(FStringView(Category), FText::FromString(Category)))
	{
		TArray<FString> SubCatKeys;
		SubCats.GetKeys(SubCatKeys);
		SubCatKeys.Sort();

		for (const FString& SubCat : SubCatKeys)
		{
			TArray<FImXConsoleVariableInfo>& Vars = SubCats[SubCat];

			if (SubCat.IsEmpty())
			{
				for (auto& Var : Vars)
					if (MatchesSearch(Var.Name, Var.Help))
						DrawVariableEntry(Var);
			}
			else
			{
				if (bSearching)
				{
					bool bSubVisible = false;
					for (const auto& Var : Vars)
						if (MatchesSearch(Var.Name, Var.Help)) { bSubVisible = true; break; }
					if (!bSubVisible) continue;
				}

				FString SubId = Category + TEXT(".") + SubCat;
				if (ImSlate::BeginFold(FStringView(SubId), FText::FromString(SubCat)))
				{
					for (auto& Var : Vars)
						if (MatchesSearch(Var.Name, Var.Help))
							DrawVariableEntry(Var);
					ImSlate::EndFold();
				}
			}
		}
		ImSlate::EndFold();
	}
}

void UImXConsolePanel::DrawVariablesTab()
{
	TArray<FString> Categories;
	VariableTree.GetKeys(Categories);
	Categories.Sort();

	bool bNeedSpacing = false;
	for (const FString& Cat : Categories)
	{
		auto& SubCats = VariableTree[Cat];
		if (!SearchFilter.IsEmpty())
		{
			bool bAny = false;
			for (auto& Sub : SubCats)
				for (const auto& Var : Sub.Value)
					if (MatchesSearch(Var.Name, Var.Help)) { bAny = true; break; }
			if (!bAny) continue;
		}
		if (bNeedSpacing) ImSlate::Spacing(2.f);
		DrawVariableCategory(Cat, SubCats);
		bNeedSpacing = true;
	}
}

//////////////////////////////////////////////////////////////////////////
// Tick
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::Tick(float Delta)
{
	if (!bOpen) return;

	if (bNeedsRefresh)
		RefreshAll();

	const FVector2D WindowSize = FVector2D(700.f, 800.f);

	// First-open position: center on the main viewport. Pivot (0.5,0.5) puts the window's center at this point
	// (see SImSlateWindow.cpp: ActualPos -= Pivot * ActualSize). Fall back to a fixed point if the viewport
	// isn't laid out yet (GetMainViewportSize returns (0,0)) so the panel still appears on the very first frame.
	const ImVec2 ViewportSize = ImSlate::GetMainViewportSize();
	const FVector2D OpenPos = ViewportSize.HasValidSize() ? FVector2D(ViewportSize) * 0.5f : FVector2D(400, 300);

	ImSlate::SetNextWindowPos(OpenPos, ImSlateCond_Once, FVector2D(0.5f, 0.5f));
	ImSlate::SetNextWindowSize(WindowSize, ImSlateCond_Once);
	ImSlate::SetNextWindowBgAlpha(0.92f);
	ImSlate::SetNextWindowBgColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.f), ImSlateCond_Once);

	ImSlate::Begin("XConsole Commands", &bOpen);

	// Search bar
	DrawSearchBar();

	ImSlate::Spacing(6.f);

	// Tab buttons — use TextButton for better visibility
	{
		FString CmdLabel = FString::Printf(TEXT("Commands (%d)"), [this]() {
			int32 N = 0;
			for (auto& C : CommandTree) for (auto& S : C.Value) N += S.Value.Num();
			return N;
		}());
		FString VarLabel = FString::Printf(TEXT("Variables (%d)"), [this]() {
			int32 N = 0;
			for (auto& C : VariableTree) for (auto& S : C.Value) N += S.Value.Num();
			return N;
		}());

		ImSlate::SetNextItemMinWidth(150.f);
		if (ImSlate::TextButton(ActiveTab == 0 ? "##TabCmd_Active" : "##TabCmd",
				FText::FromString(ActiveTab == 0 ? FString::Printf(TEXT("[%s]"), *CmdLabel) : CmdLabel)))
			ActiveTab = 0;

		ImSlate::SameLine();
		ImSlate::SetNextItemMinWidth(150.f);
		if (ImSlate::TextButton(ActiveTab == 1 ? "##TabVar_Active" : "##TabVar",
				FText::FromString(ActiveTab == 1 ? FString::Printf(TEXT("[%s]"), *VarLabel) : VarLabel)))
			ActiveTab = 1;
	}

	ImSlate::Spacing(6.f);

	// Content
	if (ActiveTab == 0)
		DrawCommandsTab();
	else
		DrawVariablesTab();

	ImSlate::End();

	if (!bOpen)
		EnableTick(false);
}

#endif // GMP_EXTEND_CONSOLE
