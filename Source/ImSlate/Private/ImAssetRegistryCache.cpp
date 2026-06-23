// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImAssetRegistryCache.h"
#include "AssetRegistry/AssetRegistryModule.h"

const TArray<FAssetData>& UImAssetRegistryCache::GetAssets(UClass* FilterClass)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	if (FilterClass)
	{
		FTopLevelAssetPath ClassPath = FilterClass->GetClassPathName();
		if (auto* Found = CachedByClass.Find(ClassPath))
		{
			return *Found;
		}

		FARFilter Filter;
		Filter.ClassPaths.Add(ClassPath);
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;

		TArray<FAssetData>& Result = CachedByClass.Add(ClassPath);
		Registry.GetAssets(Filter, Result);
		return Result;
	}
	else
	{
		if (!bAllCached)
		{
			Registry.GetAllAssets(CachedAll, true);
			bAllCached = true;
		}
		return CachedAll;
	}
}

void UImAssetRegistryCache::SearchAssets(const FString& SearchText, UClass* FilterClass, TArray<FAssetData>& OutResults, int32 MaxResults)
{
	OutResults.Reset();
	if (SearchText.Len() < 2) return;

	const TArray<FAssetData>& AllAssets = GetAssets(FilterClass);

	for (const FAssetData& Asset : AllAssets)
	{
		if (Asset.AssetName.ToString().Contains(SearchText, ESearchCase::IgnoreCase) ||
			Asset.PackageName.ToString().Contains(SearchText, ESearchCase::IgnoreCase))
		{
			OutResults.Add(Asset);
			if (OutResults.Num() >= MaxResults) break;
		}
	}
}

void UImAssetRegistryCache::Invalidate()
{
	CachedByClass.Reset();
	CachedAll.Reset();
	bAllCached = false;
}
