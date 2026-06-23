// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateInputHistory.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"

namespace ImSlate
{
int32 GImSlateInputHistoryEnabled = 1;
static FAutoConsoleVariableRef CVar_InputHistoryEnabled(
	TEXT("imslate.InputHistory"),
	GImSlateInputHistoryEnabled,
	TEXT("Enable persistent per-key input history in the ImSlate keyboard suggestion bar."));

int32 GImSlateInputHistoryMax = 20;
static FAutoConsoleVariableRef CVar_InputHistoryMax(
	TEXT("imslate.InputHistoryMax"),
	GImSlateInputHistoryMax,
	TEXT("Max entries kept per key in the ImSlate input history."));

static const TCHAR* GHistoryValueKey = TEXT("History");  // ini key holding the recent-first array

FImSlateInputHistory& FImSlateInputHistory::Get()
{
	static FImSlateInputHistory Instance;
	return Instance;
}

const FString& FImSlateInputHistory::GetIniPath() const
{
	if (IniPathCached.IsEmpty())
	{
		// ProjectSavedDir() is RELATIVE (e.g. ../../../<Proj>/Saved/). GConfig keys files by their
		// normalized path, and a relative path can normalize to something it can't match for read-back
		// or won't write where expected → the ini silently never appears. Convert to a FULL path FIRST,
		// then normalize (same order GMPMeta.cpp uses: ConvertRelativePathToFull → NormalizeConfigIniPath).
		const FString Full = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ImSlateInputHistory.ini")));
		IniPathCached = FConfigCacheIni::NormalizeConfigIniPath(Full);
	}
	return IniPathCached;
}

FString FImSlateInputHistory::SanitizeSection(const FString& Key)
{
	// ini section names must not contain '[' ']' or newlines; replace anything unsafe with '_'.
	FString Out = Key;
	Out.ReplaceInline(TEXT("["), TEXT("_"));
	Out.ReplaceInline(TEXT("]"), TEXT("_"));
	Out.ReplaceInline(TEXT("\r"), TEXT("_"));
	Out.ReplaceInline(TEXT("\n"), TEXT("_"));
	return Out;
}

const TArray<FString>& FImSlateInputHistory::GetHistory(const FString& Key)
{
	static const TArray<FString> Empty;
	if (!GImSlateInputHistoryEnabled || Key.IsEmpty() || !GConfig)
		return Empty;

	const FString Section = SanitizeSection(Key);
	if (TArray<FString>* Found = Cache.Find(Section))
		return *Found;

	// Lazy-load from the ini FILE (FConfigFile::Read — matches the WriteSection path; GConfig may not
	// have this dynamic ini loaded).
	TArray<FString>& Entry = Cache.Add(Section);
	FConfigFile File;
	File.Read(GetIniPath());
	File.GetArray(*Section, GHistoryValueKey, Entry);
	return Entry;
}

// Write one section's array directly to the ini FILE via FConfigFile (read-modify-write). GConfig's
// SetArray+Flush on a dynamic, never-loaded custom ini path does NOT reliably create/persist the file
// (Add ran, count was right, but no file appeared). FConfigFile::Read+Write hits the disk directly —
// the same approach GMPMeta.cpp uses for its custom ini.
void FImSlateInputHistory::WriteSection(const FString& Section, const TArray<FString>& Entries)
{
	const FString Path = GetIniPath();
	FConfigFile File;
	File.Read(Path);                 // load existing (other keys preserved); empty if file absent
	if (Entries.Num() > 0)
		File.SetArray(*Section, GHistoryValueKey, Entries);
	else
		File.Remove(Section);        // no entries → drop the whole section
	File.Write(Path);
}

void FImSlateInputHistory::Add(const FString& Key, const FString& Value, int32 MaxOverride)
{
	if (!GImSlateInputHistoryEnabled || Key.IsEmpty() || Value.IsEmpty())
		return;

	const FString Section = SanitizeSection(Key);
	TArray<FString>& Entry = Cache.FindOrAdd(Section);
	// Ensure cache is seeded from the ini file the first time (FindOrAdd may have just created empty).
	if (Entry.Num() == 0)
	{
		FConfigFile File;
		File.Read(GetIniPath());
		File.GetArray(*Section, GHistoryValueKey, Entry);
	}

	Entry.Remove(Value);          // de-dupe
	Entry.Insert(Value, 0);       // most-recent first
	// Per-key cap (MaxOverride, default 10 from Show params) clamped to the global ceiling.
	const int32 GlobalMax = FMath::Max(GImSlateInputHistoryMax, 1);
	const int32 Max = MaxOverride > 0 ? FMath::Min(MaxOverride, GlobalMax) : GlobalMax;
	if (Entry.Num() > Max)
		Entry.SetNum(Max);

	WriteSection(Section, Entry);
}

void FImSlateInputHistory::Remove(const FString& Key, const FString& Value)
{
	if (Key.IsEmpty())
		return;

	const FString Section = SanitizeSection(Key);
	TArray<FString>& Entry = Cache.FindOrAdd(Section);
	if (Entry.Num() == 0)
	{
		FConfigFile File;
		File.Read(GetIniPath());
		File.GetArray(*Section, GHistoryValueKey, Entry);
	}

	if (Entry.Remove(Value) > 0)
		WriteSection(Section, Entry);
}

}  // namespace ImSlate
