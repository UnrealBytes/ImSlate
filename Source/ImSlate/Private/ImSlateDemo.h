// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "ImSlate.h"
#include "ImSlateTemplates.h"
#include "ImSlateDemo.generated.h"

class UMaterial;
class UTexture2D;
namespace ImSlate
{
struct FImListDataComboData;
class FImListDataComboImpl;
// Example: ListView_Test
struct FImListViewItemBase : public TSharedFromThis<FImListViewItemBase>
{
	FString CType;
	FAnsiStringView Label;

	template<typename T>
	T* As()
	{
		static_assert(std::is_base_of<FImListViewItemBase, T>::value, "err");
		return CType == ITS::TypeStr<T>() ? static_cast<T*>(this) : nullptr;
	}

	template<typename T>
	const T* As() const
	{
		static_assert(std::is_base_of<FImListViewItemBase, T>::value, "err");
		return CType == ITS::TypeStr<T>() ? static_cast<const T*>(this) : nullptr;
	}

protected:
	FImListViewItemBase(FString InType)
		: CType(MoveTemp(InType))
	{
	}
};

template<typename T>
struct TImListViewItemBase : public FImListViewItemBase
{
	TImListViewItemBase()
		: FImListViewItemBase(ITS::TypeStr<T>())
	{
	}
};

struct FImListViewItemButton : public TImListViewItemBase<FImListViewItemButton>
{
};

struct FImListViewItemTextButton : public TImListViewItemBase<FImListViewItemTextButton>
{
	FImListViewItemTextButton(const FString& InStr)
		: ButtonText(InStr)
	{
	}

public:
	FString ButtonText = TEXT("None");
};

struct FImListViewItemText : public TImListViewItemBase<FImListViewItemText>
{
	FImListViewItemText(const FString& InStr)
		: Text(InStr)
	{
	}

public:
	FString Text = TEXT("None");
};

struct FImListViewItemEditableText : public TImListViewItemBase<FImListViewItemEditableText>
{
	FImListViewItemEditableText(const FString& InStr)
		: EditText(InStr)
	{
	}

public:
	FString EditText = TEXT("None");
};

struct FImListViewItemFloat : public TImListViewItemBase<FImListViewItemFloat>
{
	FImListViewItemFloat(float InVal)
		: Val(InVal)
	{
	}

public:
	float Val = 0.f;
};

bool ListView_Test(ImStr label, TSharedPtr<FImListViewItemBase>& InOutSelected, const TArray<TSharedPtr<FImListViewItemBase>>& InSource, const ImVec2& InSize = ImVec2(0, 0), bool bClearSelection = false);  // ListView_Test

bool TreeView_Test(ImStr label, TSharedPtr<FImListViewItemBase>& InOutSelected, const TArray<TSharedPtr<FImListViewItemBase>>& InSource, const ImVec2& InSize = ImVec2(0, 0), bool bClearSelection = false);  // ListView_Test

bool VirualList_Test(ImStr label, TSharedPtr<FImListViewItemBase>& InOutSelected, const TSharedPtr<TImSlateListArray<TSharedPtr<FImListViewItemBase>>>& InDataBinding, const ImVec2& InSize = ImVec2(0, 0), bool bClearSelection = false);

struct FImComboBoxItem_Test
	: public IImComboBoxItem
{
	FImComboBoxItem_Test(const FString& InName)
		: Val(InName)
	{
	}

public:
	FString Val = TEXT("Name_None");
	bool bSelected = false;
	virtual TSharedRef<SWidget> GenWidget();
	virtual void SelectionChanged(bool bInSelected, ESelectInfo::Type);
	virtual bool OnMeetConditions(const FText& InFilterText);
};

}  // namespace ImSlate

UCLASS()
class UImSlateDemo : public UObject
{
	GENERATED_BODY()
public:
	UImSlateDemo();

	UPROPERTY()
	UTexture2D* ImageButtonTestTexture = nullptr;

	UPROPERTY()
	UMaterial* ImageButtonTestMaterial = nullptr;

protected:
	virtual void StartShow();
	virtual void EndShow();

public:
	virtual void DrawUI(class FPrimitiveDrawInterface* PDI);
	void Tick(float Delta);
	void EnableTick(TOptional<bool> bEnable);
	bool bDemoOpen = false;

private:
	bool BoolVal = false;
	TSharedPtr<class ImSlate::TImSlateListArray<TSharedPtr<ImSlate::FImListViewItemBase>>> BindingData = nullptr;
	int32 CurrentIndex = 0;

	TSharedPtr<ImSlate::FImListViewItemBase> VirtualListSelectedItem = nullptr;
	TSharedPtr<ImSlate::IImComboBoxItem> ComboBoxSelectedItem = nullptr;

	TArray<TSharedPtr<ImSlate::IImComboBoxItem>> ComboBoxSourceArray;

	TSharedPtr<ImSlate::FImListDataComboImpl> ComboBindingData = nullptr;
	int32 ComboCurrentIndex = 0;
	TArray<TSharedPtr<ImSlate::FImListDataComboData>> ComboBindingDataSource;
	TSharedPtr<void> TickHandle;
};
