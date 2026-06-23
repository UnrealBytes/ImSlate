// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImClassPicker.h"
#include "ImAssetRegistryCache.h"
#include "ImSlateVirtualList.h"
#include "Engine/BlueprintCore.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

namespace ImSlate
{

bool SImClassPicker::ShouldFilterClass(const UClass* InClass) const
{
	if (!InClass) return true;

	if (AllowedClasses.Num() > 0)
	{
		bool bAllowed = false;
		for (const UClass* Allowed : AllowedClasses)
		{
			if (InClass->IsChildOf(Allowed))
			{
				bAllowed = true;
				break;
			}
		}
		if (!bAllowed) return true;
	}

	for (const UClass* Disallowed : DisallowedClasses)
	{
		if (InClass->IsChildOf(Disallowed))
			return true;
	}

	if (RequiredInterface && !InClass->ImplementsInterface(RequiredInterface))
		return true;

	if (!bAllowAbstract && InClass->HasAnyClassFlags(CLASS_Abstract))
		return true;

	return false;
}

void SImClassPicker::Construct(const FArguments& InArgs)
{
	OnClassPathChanged = InArgs._OnClassPathChanged;
	MetaClass = InArgs._MetaClass;
	RequiredInterface = InArgs._RequiredInterface;
	AllowedClasses = InArgs._AllowedClasses;
	DisallowedClasses = InArgs._DisallowedClasses;
	bAllowAbstract = InArgs._AllowAbstract;
	bIsBlueprintBaseOnly = InArgs._IsBlueprintBaseOnly;

	const UClass* InitialClass = InArgs._SelectedClass.Get(nullptr);
	if (InitialClass)
	{
		CurrentPath = FSoftClassPath(InitialClass);
	}

#if WITH_EDITOR
	ChildSlot
	[
		SAssignNew(EditorPicker, SClassPropertyEntryBox)
			.MetaClass(MetaClass ? MetaClass : UObject::StaticClass())
			.RequiredInterface(RequiredInterface)
			.AllowedClasses(AllowedClasses)
			.DisallowedClasses(DisallowedClasses)
			.AllowAbstract(bAllowAbstract)
			.IsBlueprintBaseOnly(bIsBlueprintBaseOnly)
			.SelectedClass(InitialClass)
			.AllowNone(InArgs._AllowNone)
			.OnSetClass(FOnSetClass::CreateSP(this, &SImClassPicker::OnEditorClassChanged))
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
				SelectClass(AssetPath);
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
				.Text(FText::FromString(CurrentPath.ToString()))
				.HintText(NSLOCTEXT("ImSlate", "ClassPickerHint", "Type to search classes..."))
				.OnTextChanged(FOnTextChanged::CreateSP(this, &SImClassPicker::OnSearchTextChanged))
				.OnTextCommitted(FOnTextCommitted::CreateSP(this, &SImClassPicker::OnSearchTextCommitted))
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

void SImClassPicker::SetClassPath(const FSoftClassPath& InPath)
{
	if (CurrentPath == InPath) return;
	CurrentPath = InPath;

#if !WITH_EDITOR
	if (RuntimeInput.IsValid() && !RuntimeInput->HasAnyUserFocus().IsSet())
	{
		RuntimeInput->SetText(FText::FromString(InPath.ToString()));
	}
#endif
}

FSoftClassPath SImClassPicker::GetClassPath() const
{
	return CurrentPath;
}

#if WITH_EDITOR
void SImClassPicker::OnEditorClassChanged(const UClass* NewClass)
{
	CurrentPath = NewClass ? FSoftClassPath(NewClass) : FSoftClassPath();
	OnClassPathChanged.ExecuteIfBound(CurrentPath);
}
#else
void SImClassPicker::OnSearchTextChanged(const FText& InText)
{
	RefreshSearchResults(InText.ToString());
}

void SImClassPicker::OnSearchTextCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnCleared) return;

	auto& Data = ListData->GetCurData();
	if (Data.Num() > 0)
	{
		SelectClass(Data[0].GetSoftObjectPath());
	}
	else
	{
		CurrentPath.SetPath(InText.ToString());
		OnClassPathChanged.ExecuteIfBound(CurrentPath);
	}
}

void SImClassPicker::RefreshSearchResults(const FString& SearchText)
{
	TArray<FAssetData> Filtered;

	if (SearchText.Len() >= 2)
	{
		if (auto* Cache = UImAssetRegistryCache::Get())
		{
			TArray<FAssetData> RawResults;
			Cache->SearchAssets(SearchText, MetaClass ? MetaClass : UBlueprintCore::StaticClass(), RawResults, 64);

			for (FAssetData& Asset : RawResults)
			{
				UClass* LoadedClass = Asset.GetClass();
				if (!ShouldFilterClass(LoadedClass))
				{
					Filtered.Add(MoveTemp(Asset));
				}
				if (Filtered.Num() >= 32) break;
			}
		}
	}

	ListData->Reload(MoveTemp(Filtered), true);
}

void SImClassPicker::SelectClass(const FSoftObjectPath& AssetPath)
{
	FString ClassPath = AssetPath.ToString() + TEXT("_C");
	CurrentPath.SetPath(ClassPath);
	RuntimeInput->SetText(FText::FromString(ClassPath));
	OnClassPathChanged.ExecuteIfBound(CurrentPath);

	ListData->Reload(TArray<FAssetData>(), true);
}
#endif

}  // namespace ImSlate
