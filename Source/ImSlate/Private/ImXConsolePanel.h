// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ImSlate.h"
#include "ImXConsolePanel.generated.h"

struct FImXConsoleCommandInfo
{
	FString Name;
	FString Help;
	FString Category;
	FString SubCategory;
	FString LeafName;
	TArray<FName> ParamTypes;
	TArray<FString> ParamValues;
	TArray<bool> ParamEnabled;
};

struct FImXConsoleVariableInfo
{
	FString Name;
	FString Help;
	FString Category;
	FString SubCategory;
	FString LeafName;
	FString CurrentValue;
	IConsoleVariable* CVar = nullptr;
};

UCLASS()
class UImXConsolePanel : public UObject
{
	GENERATED_BODY()

public:
	void EnableTick(TOptional<bool> bEnable);

private:
	void StartShow();
	void EndShow();
	void Tick(float Delta);

	// Data
	void RefreshAll();
	void RefreshCommands();
	void RefreshVariables();
	static void ParseDotName(const FString& FullName, FString& OutCategory, FString& OutSubCategory, FString& OutLeafName);

	TMap<FString, TMap<FString, TArray<FImXConsoleCommandInfo>>> CommandTree;
	TMap<FString, TMap<FString, TArray<FImXConsoleVariableInfo>>> VariableTree;
	bool bNeedsRefresh = true;

	// UI
	void DrawSearchBar();
	bool MatchesSearch(const FString& Name, const FString& Help) const;

	void DrawCommandsTab();
	void DrawVariablesTab();

	void DrawCommandCategory(const FString& Category, TMap<FString, TArray<FImXConsoleCommandInfo>>& SubCats);
	void DrawCommandEntry(FImXConsoleCommandInfo& Info);
	void DrawParamWidget(const FName& TypeName, FString& Value, bool& bEnabled, bool bIsOptional, int32 Index, const FString& CmdName);
	void ExecuteCommand(const FImXConsoleCommandInfo& Info);

	void DrawVariableCategory(const FString& Category, TMap<FString, TArray<FImXConsoleVariableInfo>>& SubCats);
	void DrawVariableEntry(FImXConsoleVariableInfo& Info);

	// FoldLine with arrow indicator helper
	bool DrawFold(const FString& Id, const FString& DisplayText);

	FString SearchFilter;
	bool bOpen = false;
	int32 ActiveTab = 0;
	TMap<FString, bool> FoldStates;
	TSharedPtr<void> TickHandle;
};
