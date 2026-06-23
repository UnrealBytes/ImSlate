// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "ImSlateTemplates.h"
#include "Templates/SharedPointer.h"
#include "UObject/StrongObjectPtr.h"

#include "ImSlateStyleSetting.generated.h"

template<typename T>
struct TWidgetProxy
{
	TWidgetProxy(UWidget* InObj)
		: Obj(InObj)
		, Widget(StaticCastSharedRef<T>(Obj->TakeWidget()))
	{
	}
	UWidget* Obj;
	TSharedRef<T> Widget;

	// operator TSharedRef<T>() const { return Widget; }
	TSharedRef<T>& Expose() { return Widget; }
	TSharedRef<T>& Expose(UWidget*& OutObj)
	{
		OutObj = Obj;
		return Widget;
	}
	template<typename V>
	TSharedRef<T>& Expose(TStrongObjectPtr<V>& OutObj)
	{
		OutObj.Reset(Obj);
		return Widget;
	}
};

UCLASS(BlueprintType, defaultconfig, config = "ImSlateStyle")
class IMSLATE_API UXImSlateStyleSetting : public UObject
{
	GENERATED_BODY()
public:
	UXImSlateStyleSetting();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	FLinearColor WindowHeaderColor = FLinearColor::Gray;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	FLinearColor WindowContentColor = FLinearColor(0.42f, 0.50f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImButton> ImSlateButton = UImButton::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImImageButton> ImSlateImageButton = UImImageButton::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImTextBlock> ImSlateText = UImTextBlock::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImEditableText> ImSlateEditableText = UImEditableText::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImSpinBox> ImSlateSpinBox = UImSpinBox::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImCheckBox> ImSlateCheckBox = UImCheckBox::StaticClass();

	// NOTE: no ImSlateNumericWidget override here. The numeric widget base (UImSlateNumericWidget) is
	// ABSTRACT with six concrete templated subclasses (Float/Double/Int32/...), so there is no single
	// representative class to register. Registering the abstract base made FindData's super-chain lookup
	// resolve concrete subclasses to the abstract base → StaticAllocateObject crash. ImFactoryCreate now
	// falls back to the concrete InClass (see GetDefaultImpl), so numeric widgets need no entry here.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImImage> ImSlateImage = UImImage::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImComboButton> ImSlateComboButton = UImComboButton::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImVirtualComboButton> ImSlateVirtualComboButton = UImVirtualComboButton::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImFoldLine> ImSlateFoldLine = UImFoldLine::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImBorder> ImSlateBorder = UImBorder::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImEditableTextBox> ImSlateEditableTextBox = UImEditableTextBox::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImVirtualList> ImVirtualList = UImVirtualList::StaticClass();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config)
	TSubclassOf<UImSearchBox> ImSearchBox = UImSearchBox::StaticClass();

public:
	void PreloadClasses() const;
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
	template<typename S, typename U>
	static auto CreateWidget(TSubclassOf<U> Type)
	{
		return TWidgetProxy<S>(NewObject<U>(ImSlate::GetImSlateWorldChecked(), Type));
	}
	template<typename S, typename U>
	static auto CreateWidget(TSubclassOf<U> Type, const TCHAR* DefaultClass)
	{
		check(DefaultClass);
		if (!ensure(Type))
			Type = LoadClass<U>(nullptr, DefaultClass);
		return TWidgetProxy<S>(NewObject<U>(ImSlate::GetImSlateWorldChecked(), Type));
	}

	// 	template<typename S, typename U>
	// 	static TSharedPtr<S> Create(TSubclassOf<U> Type)
	// 	{
	// 		if (!ensure(Type))
	// 			return nullptr;
	//
	// 		auto CDOOfU = GetMutableDefault<U>();
	// 		auto WidgetRef = GS_ACCESS_PROTECT(CDOOfU, U, RebuildWidget)->RebuildWidget();
	//
	// 		return StaticCastSharedRef<S>(WidgetRef);
	// 	}
};
