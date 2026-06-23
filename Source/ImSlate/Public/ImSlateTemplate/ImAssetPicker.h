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

class IMSLATE_API SImAssetPicker : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnAssetPathChanged, const FSoftObjectPath&);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnShouldFilterAsset, const FAssetData&);

	SLATE_BEGIN_ARGS(SImAssetPicker)
		: _FilterClass(nullptr)
		, _AllowClear(true)
		, _DisplayThumbnail(false)
		, _DisplayBrowse(true)
	{}
		SLATE_ARGUMENT(UClass*, FilterClass)
		SLATE_ARGUMENT(TArray<const UClass*>, AllowedClasses)
		SLATE_ARGUMENT(TArray<const UClass*>, DisallowedClasses)
		SLATE_ATTRIBUTE(FString, ObjectPath)
		SLATE_ARGUMENT(bool, AllowClear)
		SLATE_EVENT(FOnAssetPathChanged, OnAssetPathChanged)
		SLATE_EVENT(FOnShouldFilterAsset, OnShouldFilterAsset)

		// Editor-only visual options (Runtime ignores)
		SLATE_ARGUMENT(bool, DisplayThumbnail)
		SLATE_ARGUMENT(bool, DisplayBrowse)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetObjectPath(const FString& InPath);
	FString GetObjectPath() const;

private:
	bool ShouldFilterAsset(const FAssetData& AssetData) const;

	FOnAssetPathChanged OnAssetPathChanged;
	FOnShouldFilterAsset OnShouldFilterAssetDelegate;
	FString CurrentPath;
	UClass* FilterClass = nullptr;
	TArray<const UClass*> AllowedClasses;
	TArray<const UClass*> DisallowedClasses;

#if WITH_EDITOR
	void OnEditorObjectChanged(const FAssetData& AssetData);
	TSharedPtr<SObjectPropertyEntryBox> EditorPicker;
#else
	void OnSearchTextChanged(const FText& InText);
	void OnSearchTextCommitted(const FText& InText, ETextCommit::Type CommitType);
	void RefreshSearchResults(const FString& SearchText);
	void SelectAsset(const FSoftObjectPath& AssetPath);

	TSharedPtr<SEditableText> RuntimeInput;
	TSharedPtr<ImSlate::SImSlateVirtualList> ResultList;
	TSharedPtr<ImSlate::TImSlateListArray<FAssetData>> ListData;
#endif
};

}  // namespace ImSlate
