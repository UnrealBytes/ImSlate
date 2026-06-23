// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/SoftObjectPath.h"
#include "AssetRegistry/AssetData.h"
#include "ImSlateListDataInc.h"

#if WITH_EDITOR
#include "PropertyCustomizationHelpers.h"
#endif

class SEditableText;

namespace ImSlate
{

class IMSLATE_API SImClassPicker : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnClassPathChanged, const FSoftClassPath&);

	SLATE_BEGIN_ARGS(SImClassPicker)
		: _MetaClass(nullptr)
		, _RequiredInterface(nullptr)
		, _AllowAbstract(false)
		, _IsBlueprintBaseOnly(false)
		, _AllowNone(true)
	{}
		SLATE_ARGUMENT(UClass*, MetaClass)
		SLATE_ARGUMENT(const UClass*, RequiredInterface)
		SLATE_ARGUMENT(TArray<const UClass*>, AllowedClasses)
		SLATE_ARGUMENT(TArray<const UClass*>, DisallowedClasses)
		SLATE_ARGUMENT(bool, AllowAbstract)
		SLATE_ARGUMENT(bool, IsBlueprintBaseOnly)
		SLATE_ARGUMENT(bool, AllowNone)
		SLATE_ATTRIBUTE(const UClass*, SelectedClass)
		SLATE_EVENT(FOnClassPathChanged, OnClassPathChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetClassPath(const FSoftClassPath& InPath);
	FSoftClassPath GetClassPath() const;

private:
	bool ShouldFilterClass(const UClass* InClass) const;

	FOnClassPathChanged OnClassPathChanged;
	FSoftClassPath CurrentPath;
	UClass* MetaClass = nullptr;
	const UClass* RequiredInterface = nullptr;
	TArray<const UClass*> AllowedClasses;
	TArray<const UClass*> DisallowedClasses;
	bool bAllowAbstract = false;
	bool bIsBlueprintBaseOnly = false;

#if WITH_EDITOR
	void OnEditorClassChanged(const UClass* NewClass);
	TSharedPtr<SClassPropertyEntryBox> EditorPicker;
#else
	void OnSearchTextChanged(const FText& InText);
	void OnSearchTextCommitted(const FText& InText, ETextCommit::Type CommitType);
	void RefreshSearchResults(const FString& SearchText);
	void SelectClass(const FSoftObjectPath& AssetPath);

	TSharedPtr<SEditableText> RuntimeInput;
	TSharedPtr<ImSlate::SImSlateVirtualList> ResultList;
	TSharedPtr<ImSlate::TImSlateListArray<FAssetData>> ListData;
#endif
};

}  // namespace ImSlate
