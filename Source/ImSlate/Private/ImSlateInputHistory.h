// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace ImSlate
{
// Persistent per-key input history, stored in Saved/ImSlateInputHistory.ini (one [section] per key,
// most-recent-first). Used by the virtual keyboard / editable text to show recent entries in the
// suggestion bar. In-memory cache is lazily loaded from the ini; Add/Remove write through + flush.
class FImSlateInputHistory
{
public:
	static FImSlateInputHistory& Get();

	// Recent-first history for Key (lazily loaded from ini, then cached). Empty if disabled / none.
	const TArray<FString>& GetHistory(const FString& Key);

	// Record a value: de-dupe, move to front, cap, persist. Cap = min(MaxOverride>0 ? MaxOverride :
	// imslate.InputHistoryMax, imslate.InputHistoryMax). MaxOverride lets the caller (Show params) set
	// a smaller per-key limit (default 10) without exceeding the global ceiling.
	void Add(const FString& Key, const FString& Value, int32 MaxOverride = 0);

	// Delete one value from a key's history, persist.
	void Remove(const FString& Key, const FString& Value);

private:
	const FString& GetIniPath() const;
	static FString SanitizeSection(const FString& Key);  // ini section names can't contain []/newlines
	void WriteSection(const FString& Section, const TArray<FString>& Entries);  // FConfigFile read-modify-write to disk

	TMap<FString, TArray<FString>> Cache;  // section → recent-first values
	mutable FString IniPathCached;
};

extern int32 GImSlateInputHistoryEnabled;  // CVar imslate.InputHistory
extern int32 GImSlateInputHistoryMax;      // CVar imslate.InputHistoryMax

}  // namespace ImSlate
