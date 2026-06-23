// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "Components/Widget.h"
#include "Components/ScrollBox.h"
#include "ImSlateFactory.h"
#include "ImSlateListDataInc.h"
#include "ImSlateWidgetPool.h"
#include "UObject/Interface.h"

#include "ImVirtualList.generated.h"

UINTERFACE(Blueprintable, MinimalAPI)
class UImVirtualListDataSetter : public UInterface
{
	GENERATED_BODY()
};
class IImVirtualListDataSetter
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintImplementableEvent)
	void OnSetData(int32 Index);
};

UINTERFACE(Blueprintable, MinimalAPI)
class UImVirtualListInc : public UInterface
{
	GENERATED_BODY()
public:
};
class IImVirtualListInc
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintImplementableEvent, Category = "ImSlateDataStorage")
	float OnGetItemAxis(int32 InIndex = 0);
	UFUNCTION(BlueprintImplementableEvent, Category = "ImSlateDataStorage")
	float OnGetItemHeight(int32 InIndex = 0);

	UFUNCTION(BlueprintImplementableEvent, Category = "ImSlateDataStorage")
	void OnGenerateWidget(int32 InIndex, UUserWidget*& InOutWidget);
	UFUNCTION(BlueprintImplementableEvent, Category = "ImSlateDataStorage")
	void OnReleaseWidget(int32 InIndex, UUserWidget* ReleasedWidget);

	UFUNCTION(BlueprintImplementableEvent, Category = "ImSlateDataStorage")
	void OnSetData(int32 InIndex, UUserWidget* Widget);
};

namespace ImSlate
{
class SImSlateVirtualList;
struct FImSlateListDataBP;
}  // namespace ImSlate

UCLASS(Abstract, Blueprintable, BlueprintType, editinlinenew)
class IMSLATE_API UImSlateDataStorageBase : public UBlueprintableObject
{
	GENERATED_BODY()
public:
	UImSlateDataStorageBase();
	TSharedRef<ImSlate::IImSlateListData> GetBindingData() const;

	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage")
	float GetVirtualPos() const;

	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage")
	void Reload(bool bReset = true);

	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage")
	void UpdateItem(int32 InIndex = -1, bool bRefreshWidget = false);

	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage")
	void ReloadToPos(float VirtualPos, bool bItemAlign = true);

	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage")
	void ScrollToPos(float VirtualPos, bool bItemAlign = false);

	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage")
	void ScrollToItem(int32 InIndex, bool bCenterAlign = false);

	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage")
	void SetItemAxis(float InAxis, bool bRefreshWidget = true);
	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage")
	void SetItemHeight(float InHeight, bool bRefreshWidget = true) { SetItemAxis(InHeight, bRefreshWidget); }

	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage", meta = (MetaClass = "/Script/CoreUObject.Object", MustImplement = "/Script/ImSlate.ImVirtualListInc"))
	void TrySetIVirtualListInc(UObject* InIncObj);

	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage")
	void SetNeedPrepassItem();

	UFUNCTION(BlueprintCallable, Category = "ImSlateDataStorage")
	void SetHeterogeneous();

protected:
	friend class UImVirtualList;
	friend struct ImSlate::FImSlateListDataBP;
	TSharedPtr<ImSlate::FImSlateListDataBP> BindingData;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "ImSlateDataStorage")
	UObject* VirtualListInc = nullptr;

	virtual void PostInitProperties() override;

protected:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ImSlateDataStorage")
	int32 OnGetDataCount();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ImSlateDataStorage")
	float OnGetItemAxis(int32 InIndex = 0);
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ImSlateDataStorage")
	float OnGetItemHeight(int32 InIndex = 0);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ImSlateDataStorage")
	void OnGenerateWidget(int32 InIndex, UUserWidget*& InOutWidget);
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ImSlateDataStorage")
	void OnReleaseWidget(int32 InIndex, UUserWidget* ReleasedWidget);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ImSlateDataStorage")
	void OnSetData(int32 InIndex, UUserWidget* Widget);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ImSlateDataStorage")
	void OnPosChanged(float VirtualPos);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ImSlateDataStorage")
	void OnVirtualListBind(UImVirtualList* InVirtualList, bool bBind);

private:
	void CallOnVirtualListBind(UImVirtualList* InVirtualList, bool bBind);
	virtual void OnVirtualListBind_Implementation(UImVirtualList* InVirtualList, bool bBind) {}

	virtual int32 OnGetDataCount_Implementation() { return 0; }
	virtual void OnPosChanged_Implementation(float VirtualPos) {}

	virtual float OnGetItemAxis_Implementation(int32 InIndex);
	virtual float OnGetItemHeight_Implementation(int32 InIndex);
	virtual void OnGenerateWidget_Implementation(int32 InIndex, UUserWidget*& InOutWidget);
	virtual void OnReleaseWidget_Implementation(int32 InIndex, UUserWidget* ReleasedWidget);
	virtual void OnSetData_Implementation(int32 InIndex, UUserWidget* Widget);
};

//////////////////////////////////////////////////////////////////////////

UCLASS()
class IMSLATE_API UImVirtualList
	: public UWidget
	, public TImFactory<ImSlate::SImSlateVirtualList>
{
	GENERATED_BODY()
public:
	UImVirtualList();

	// Set the buffer rows (a few more rows outside the visible area)
	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	void SetOverCountRowNum(int32 InNum);

	// Set the Tile mode and Item width
	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	void SetTileWidth(float InWidth);

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	void ScrollToPos(float InVirtualPos, bool bItemAlign = false);

	// make sure item is at least be visible
	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	void ScrollToItem(int32 InDataIndex, bool bCenterAlign = false);
	
	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	float GetVirtualPos() const;

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	float GetCachedTotalAxis(bool bFullItems = false) const;
	
	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	float GetVisibleAeraAxis() const;

	// Update specified data, default is to update all visible data
	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	void Update(int32 InDataIndex = -1, bool bRefreshItem = false);

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	void ShowScrollBar(ECheckBoxState IsAlwaysShow);

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	void SetScrollbarInfo(bool bTrackAlwaysVisible, float Thickness = 16.f);

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	bool IsItemOffsetVisible(int32 InIndex, float InRelativePos = -0.f) const;

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList")
	float GetItemPosition(int32 InIndex);
	
	UFUNCTION(BlueprintCallable, Category = "ImVirtualList|DataStorage")
	void SetDataStorage(class UImSlateDataStorageBase* InStorage, float InVirtualPos = -0.f, float InTileWidth = 0.f);

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList|DataStorage")
	void SetAnimateScrollDuration(float InAnimateDuration);

public:
	void SetNativeBindingData(TSharedPtr<ImSlate::IImSlateListData> InBindingData, float InVirtualPos = -0.f, float InTileWidth = 0.f);
	TSharedPtr<ImSlate::SImSlateVirtualList> GetNativeWidget() const;
	TSharedRef<ImSlate::SImSlateVirtualList> ConstructImWidget() const;

protected:
	UPROPERTY(Transient)
	FUserWidgetPool UserWidgetPool;

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList|WidgetPool", meta = (DeterminesOutputType = "WidgetClass", DynamicOutputParam))
	UUserWidget* GetOrCreateInstance(TSubclassOf<UUserWidget> WidgetClass);

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList|WidgetPool")
	void ReleaseWidget(UUserWidget* Widget, bool bReleaseSlate = false);

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList|WidgetPool")
	void ReleaseWidgets(TArray<UUserWidget*> Widgets, bool bReleaseSlate = false);

	UFUNCTION(BlueprintCallable, Category = "ImVirtualList|WidgetPool")
	void ResetPool();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVirtualPosChangedDelegate, float, NewPos);
	UPROPERTY(BlueprintAssignable, Category = "ImVirtualList")
	FOnVirtualPosChangedDelegate OnVirtualPosChanged;

	UPROPERTY(BlueprintAssignable, Category = "ImVirtualList")
	FOnUserScrolledEvent OnUserScrolled;

	/** Called when the scrollbar visibility has changed */
	UPROPERTY(BlueprintAssignable, Category = "ImVirtualList")
	FOnScrollBarVisibilityChangedEvent OnScrollBarVisibilityChanged;

#if WITH_EDITORONLY_DATA
	// only used in editor preview
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImVirtualList|EditorPreview", meta = (UIMin = "0"))
	int32 EditorPreviewItemCount = 50;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImVirtualList|EditorPreview", meta = (UIMin = "10"))
	float EditorPreviewItemHeight = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImVirtualList|EditorPreview")
	int32 EditorPreviewItemWidth = -1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImVirtualList|EditorPreview", meta = (UIMin = "0"))
	float EditorPreviewVirtualPos = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImVirtualList|EditorPreview")
	TSubclassOf<UWidget> EditorPreviewWidgetClass;
	UPROPERTY(Transient)
	FUWidgetPool UWidgetPool;
	TSharedPtr<ImSlate::IImSlateListData> BindingData;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ImVirtualList|Background")
	TSubclassOf<UUserWidget> BackgroundWidgetClass;
	UPROPERTY(Transient)
	mutable UUserWidget* BackgroundWidget = nullptr;
	UFUNCTION(BlueprintCallable, Category = "ImVirtualList|Background")
	UUserWidget* GetBackgroundWidget();

	UPROPERTY(Transient)
	UImSlateDataStorageBase* DataStorage = nullptr;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ImVirtualList|User Interface")
	void OnSynchronizeProperties();

	virtual void SynchronizeProperties() override;

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	TSharedPtr<ImSlate::SImSlateVirtualList> MyVirtualList;

	virtual void OnBindingChanged(const FName& Property) override;

protected:
	IM_SLATE_PALETTECATEGORY()
};
