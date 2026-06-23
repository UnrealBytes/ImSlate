// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImAssetPicker.h"
#include "ImAssetRegistryCache.h"
#include "ImSlateVirtualList.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

namespace ImSlate
{

bool SImAssetPicker::ShouldFilterAsset(const FAssetData& AssetData) const
{
	if (OnShouldFilterAssetDelegate.IsBound() && OnShouldFilterAssetDelegate.Execute(AssetData))
		return true;

	if (AllowedClasses.Num() > 0)
	{
		UClass* AssetClass = AssetData.GetClass();
		bool bAllowed = false;
		for (const UClass* Allowed : AllowedClasses)
		{
			if (AssetClass && AssetClass->IsChildOf(Allowed))
			{
				bAllowed = true;
				break;
			}
		}
		if (!bAllowed) return true;
	}

	for (const UClass* Disallowed : DisallowedClasses)
	{
		UClass* AssetClass = AssetData.GetClass();
		if (AssetClass && AssetClass->IsChildOf(Disallowed))
			return true;
	}

	return false;
}

void SImAssetPicker::Construct(const FArguments& InArgs)
{
	OnAssetPathChanged = InArgs._OnAssetPathChanged;
	OnShouldFilterAssetDelegate = InArgs._OnShouldFilterAsset;
	CurrentPath = InArgs._ObjectPath.Get(FString());
	FilterClass = InArgs._FilterClass;
	AllowedClasses = InArgs._AllowedClasses;
	DisallowedClasses = InArgs._DisallowedClasses;

#if WITH_EDITOR
	ChildSlot
	[
		SAssignNew(EditorPicker, SObjectPropertyEntryBox)
			.AllowedClass(FilterClass ? FilterClass : UObject::StaticClass())
			.ObjectPath(CurrentPath)
			.AllowClear(InArgs._AllowClear)
			.DisplayBrowse(InArgs._DisplayBrowse)
			.DisplayThumbnail(InArgs._DisplayThumbnail)
			.OnObjectChanged(FOnSetObject::CreateSP(this, &SImAssetPicker::OnEditorObjectChanged))
			.OnShouldFilterAsset_Lambda([this](const FAssetData& AssetData) -> bool {
				return ShouldFilterAsset(AssetData);
			})
	];
#else
	ListData = MakeShared<ImSlate::TImSlateListArray<FAssetData>>(false);
	ListData->SetItemAxis(24.f);
	ListData->SetWidgetFactory([this](FAssetData& Asset, TSharedRef<SWidget>& OutWidget) {
		FString DisplayText = FString::Printf(TEXT("%s  (%s)"), *Asset.AssetName.ToString(), *Asset.PackageName.ToString());
		FSoftObjectPath AssetPath = Asset.GetSoftObjectPath();
		OutWidget = SNew(SButton)
			.ContentPadding(FMargin(4.f, 1.f))
			.OnClicked_Lambda([this, AssetPath]() {
				SelectAsset(AssetPath);
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(FText::FromString(DisplayText))
			];
	});

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(RuntimeInput, SEditableText)
				.Text(FText::FromString(CurrentPath))
				.HintText(NSLOCTEXT("ImSlate", "AssetPickerHint", "Type to search assets..."))
				.OnTextChanged(FOnTextChanged::CreateSP(this, &SImAssetPicker::OnSearchTextChanged))
				.OnTextCommitted(FOnTextCommitted::CreateSP(this, &SImAssetPicker::OnSearchTextCommitted))
		]
		+ SVerticalBox::Slot()
		.MaxHeight(200.f)
		[
			SAssignNew(ResultList, ImSlate::SImSlateVirtualList)
		]
	];

	ResultList->SetData(ListData);
#endif
}

void SImAssetPicker::SetObjectPath(const FString& InPath)
{
	if (CurrentPath == InPath) return;
	CurrentPath = InPath;

#if !WITH_EDITOR
	if (RuntimeInput.IsValid() && !RuntimeInput->HasAnyUserFocus().IsSet())
	{
		RuntimeInput->SetText(FText::FromString(InPath));
	}
#endif
}

FString SImAssetPicker::GetObjectPath() const
{
	return CurrentPath;
}

#if WITH_EDITOR
void SImAssetPicker::OnEditorObjectChanged(const FAssetData& AssetData)
{
	CurrentPath = AssetData.GetSoftObjectPath().ToString();
	OnAssetPathChanged.ExecuteIfBound(AssetData.GetSoftObjectPath());
}
#else
void SImAssetPicker::OnSearchTextChanged(const FText& InText)
{
	RefreshSearchResults(InText.ToString());
}

void SImAssetPicker::OnSearchTextCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnCleared) return;

	auto& Data = ListData->GetCurData();
	if (Data.Num() > 0)
	{
		SelectAsset(Data[0].GetSoftObjectPath());
	}
	else
	{
		CurrentPath = InText.ToString();
		OnAssetPathChanged.ExecuteIfBound(FSoftObjectPath(CurrentPath));
	}
}

void SImAssetPicker::RefreshSearchResults(const FString& SearchText)
{
	TArray<FAssetData> Filtered;

	if (SearchText.Len() >= 2)
	{
		if (auto* Cache = UImAssetRegistryCache::Get())
		{
			TArray<FAssetData> RawResults;
			Cache->SearchAssets(SearchText, FilterClass, RawResults, 64);

			for (FAssetData& Asset : RawResults)
			{
				if (!ShouldFilterAsset(Asset))
				{
					Filtered.Add(MoveTemp(Asset));
				}
				if (Filtered.Num() >= 32) break;
			}
		}
	}

	ListData->Reload(MoveTemp(Filtered), true);
}

void SImAssetPicker::SelectAsset(const FSoftObjectPath& AssetPath)
{
	CurrentPath = AssetPath.ToString();
	RuntimeInput->SetText(FText::FromString(CurrentPath));
	OnAssetPathChanged.ExecuteIfBound(AssetPath);

	ListData->Reload(TArray<FAssetData>(), true);
}
#endif

}  // namespace ImSlate
