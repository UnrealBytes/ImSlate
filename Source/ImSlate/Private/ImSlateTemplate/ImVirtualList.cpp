// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImVirtualList.h"

#include "Components/Border.h"
#include "Components/ScrollBar.h"
#include "ImSlateVirtualList.h"
#include "Slate/SObjectWidget.h"

namespace ImSlate
{
struct SImSlateVirtualItem : public SObjectWidget
{
	static FName GetType() { return FName("SImSlateVirtualItem"); }
	static TSharedPtr<SObjectWidget> StaticWidgetConstructFunc(UUserWidget* Widget, TSharedRef<SWidget> Content)
	{
		return SNew(SImSlateVirtualItem, Widget)
		[
			Content
		];
	}

	static TSharedRef<SImSlateVirtualItem> MakeSlateWidget(UUserWidget* UserWidget) { return UserWidget->TakeDerivedWidget<SImSlateVirtualItem>(StaticWidgetConstructFunc); }
	static UUserWidget* TryGetUserWidget(TSharedRef<SWidget> InWidget)
	{
		if (InWidget != SNullWidget::NullWidget && ensure(InWidget->GetType() == SImSlateVirtualItem::GetType()))
			return StaticCastSharedRef<SImSlateVirtualItem>(InWidget)->GetWidgetObject();
		return nullptr;
	}
};

TFunctionRef<TSharedPtr<SObjectWidget>(UUserWidget*, TSharedRef<SWidget>)> GetWidgetConstructFunc()
{
	static auto Func = [](UUserWidget* Widget, TSharedRef<SWidget> Content) {
		return SNew(SImSlateVirtualItem, Widget)
		[
			Content
		];
	};
	return Func;
}

struct FImSlateListDataBP final : public IImSlateListData
{
public:
	FImSlateListDataBP(UImSlateDataStorageBase* InProxy, bool bInDynamicAxis = false, bool bInHeterogeneous = false)
		: IImSlateListData(bInHeterogeneous, bInDynamicAxis)
		, Proxy(InProxy)
	{
	}

	static TSharedRef<FImSlateListDataBP> MakeShared(UImSlateDataStorageBase* InProxy, bool bInDynamicHeight = false, bool bInHeterogeneous = false) { return ::MakeShared<FImSlateListDataBP>(InProxy, bInDynamicHeight, bInHeterogeneous); }

	virtual TSharedPtr<SImSlateVirtualList> GetVirtualList() const override { return VirtualList.Pin(); }
	virtual void SetVirtualList(TSharedPtr<SImSlateVirtualList> InListWidget) override { VirtualList = InListWidget; }

	virtual int32 GetDataCount() const override
	{
		if (ensure(Proxy.IsValid()))
			return Proxy->OnGetDataCount();
		return 0;
	}
	virtual void OnSetData(int32 InIndex, TSharedRef<SWidget> Widget) override
	{
		if (ensure(Proxy.IsValid()))
		{
			if (UUserWidget* UserWidget = SImSlateVirtualItem::TryGetUserWidget(Widget))
			{
				Proxy->OnSetData(InIndex, UserWidget);
			}
		}
	}
	virtual void GenerateDataWidget(int32 InIndex, TSharedRef<SWidget>& InOutWidget) override
	{
		if (ensure(Proxy.IsValid()))
		{
			UUserWidget* UserWidget = ImSlate::SImSlateVirtualItem::TryGetUserWidget(InOutWidget);
			if (ensure(!UserWidget))
			{
				Proxy->OnGenerateWidget(InIndex, UserWidget);
			}
			if (ensure(UserWidget))
			{
				InOutWidget = ImSlate::SImSlateVirtualItem::MakeSlateWidget(UserWidget);
			}
		}
	}

	virtual void ReleaseDataWidget(int32 InIndex, TSharedRef<SWidget>& ReleasedWidget) override
	{
		if (ensure(Proxy.IsValid()))
		{
			UUserWidget* UserWidget = ImSlate::SImSlateVirtualItem::TryGetUserWidget(ReleasedWidget);
			if (UserWidget)
			{
				Proxy->OnReleaseWidget(InIndex, UserWidget);
			}
		}
	}

	virtual float GetItemAxis(int32 InIndex = 0) const override
	{
		if (ensure(Proxy.IsValid()))
			return Proxy->OnGetItemHeight(InIndex);
		return FallbackItemAxis;
	}

	virtual void OnPosChanged(float VirtualPos) override
	{
		if (ensure(Proxy.IsValid()))
			Proxy->OnPosChanged(VirtualPos);
	}

	void SetItemDefaultAxis(float InAxis, bool bRefreshWidget = true)
	{
		FallbackItemAxis = FMath::Max(1.f, InAxis);
		UpdateItem(-1, bRefreshWidget && IsHeterogeneous());
	}

	using IImSlateListData::GetVirtualPos;
	using IImSlateListData::IsHeterogeneous;
	using IImSlateListData::ReloadToPos;
	using IImSlateListData::ScrollToItem;
	using IImSlateListData::ScrollToPos;
	using IImSlateListData::UpdateItem;

	float FallbackItemAxis = 22.f;
	TWeakObjectPtr<UImSlateDataStorageBase> Proxy;
	TWeakPtr<SImSlateVirtualList> VirtualList;
};

}  // namespace ImSlate

UImSlateDataStorageBase::UImSlateDataStorageBase()
{
	BindingData = ImSlate::FImSlateListDataBP::MakeShared(this);
}
void UImSlateDataStorageBase::PostInitProperties()
{
	Super::PostInitProperties();
	// BindingData = ImSlate::FImSlateListDataBP::MakeShared(this);
}

TSharedRef<ImSlate::IImSlateListData> UImSlateDataStorageBase::GetBindingData() const
{
	return BindingData.ToSharedRef();
}

void UImSlateDataStorageBase::TrySetIVirtualListInc(UObject* InIncObj)
{
	if (InIncObj && InIncObj->Implements<UImVirtualListInc>())
	{
		VirtualListInc = InIncObj;
	}
	else
	{
		VirtualListInc = nullptr;
	}
}

void UImSlateDataStorageBase::SetNeedPrepassItem()
{
	BindingData->SetNeedPrepassItem();
}

void UImSlateDataStorageBase::SetHeterogeneous()
{
	BindingData->SetHeterogeneous();
}

void UImSlateDataStorageBase::CallOnVirtualListBind(UImVirtualList* InVirtualList, bool bBind)
{
	if (bBind)
	{
		TrySetIVirtualListInc(InVirtualList);
	}
	else
	{
		VirtualListInc = nullptr;
	}
	OnVirtualListBind(InVirtualList, bBind);
}

float UImSlateDataStorageBase::OnGetItemHeight_Implementation(int32 InIndex)
{
	if (auto Inc = VirtualListInc)
	{
		return IImVirtualListInc::Execute_OnGetItemHeight(Inc, InIndex);
	}
	return BindingData->FallbackItemAxis;
}

float UImSlateDataStorageBase::OnGetItemAxis_Implementation(int32 InIndex)
{
	if (auto Inc = VirtualListInc)
	{
		return IImVirtualListInc::Execute_OnGetItemAxis(Inc, InIndex);
	}
	return BindingData->FallbackItemAxis;
}

void UImSlateDataStorageBase::OnSetData_Implementation(int32 InIndex, UUserWidget* Widget)
{
	if (Widget->Implements<UImVirtualListDataSetter>())
	{
		// direct invoke to widget if interface existed
		IImVirtualListDataSetter::Execute_OnSetData(Widget, InIndex);
	}
	else if (auto Inc = VirtualListInc)
	{
		IImVirtualListInc::Execute_OnSetData(Inc, InIndex, Widget);
	}
}

void UImSlateDataStorageBase::OnGenerateWidget_Implementation(int32 InIndex, UUserWidget*& InOutWidget)
{
	if (auto Inc = VirtualListInc)
	{
		IImVirtualListInc::Execute_OnGenerateWidget(Inc, InIndex, InOutWidget);
	}
}

void UImSlateDataStorageBase::OnReleaseWidget_Implementation(int32 InIndex, UUserWidget* ReleasedWidget)
{
	if (auto Inc = VirtualListInc)
	{
		IImVirtualListInc::Execute_OnReleaseWidget(Inc, InIndex, ReleasedWidget);
	}
}

void UImSlateDataStorageBase::SetItemAxis(float InAxis, bool bRefreshWidget)
{
	BindingData->SetItemDefaultAxis(InAxis, bRefreshWidget);
}

void UImSlateDataStorageBase::UpdateItem(int32 InIndex, bool bRefreshWidget)
{
	BindingData->UpdateItem(InIndex, bRefreshWidget);
}

void UImSlateDataStorageBase::ScrollToItem(int32 InIndex, bool bCenterAlign)
{
	BindingData->ScrollToItem(InIndex, bCenterAlign);
}

float UImSlateDataStorageBase::GetVirtualPos() const
{
	return BindingData->GetVirtualPos();
}

void UImSlateDataStorageBase::ReloadToPos(float VirtualPos, bool bItemAlign)
{
	BindingData->ReloadToPos(VirtualPos, bItemAlign);
}

void UImSlateDataStorageBase::ScrollToPos(float VirtualPos, bool bItemAlign)
{
	BindingData->ScrollToPos(VirtualPos, bItemAlign);
}

void UImSlateDataStorageBase::Reload(bool bReset)
{
	if (bReset)
		BindingData->ReloadToPos(-1.f, false);
	else
		BindingData->UpdateItem(-1, BindingData->IsHeterogeneous());
}

#if WITH_EDITORONLY_DATA
UImVirtualList::UImVirtualList()
	: UserWidgetPool(*this)
	, UWidgetPool(*this)
{
	EditorPreviewWidgetClass = UBorder::StaticClass();
	UserWidgetPool.SetWorld(GetWorld());
	UWidgetPool.SetWorld(GetWorld());
	OnSynchronizeProperties();
}
#else
UImVirtualList::UImVirtualList()
	: UserWidgetPool(*this)
{
	UserWidgetPool.SetWorld(GetWorld());
}
#endif

void UImVirtualList::SetOverCountRowNum(int32 InNum)
{
	if (auto VirtualList = GetNativeWidget())
	{
		VirtualList->SetOverCountRowNum(InNum);
	}
}

void UImVirtualList::SetTileWidth(float InWidth)
{
	if (auto VirtualList = GetNativeWidget())
	{
		VirtualList->SetTileOrthVal(InWidth);
	}
}

void UImVirtualList::ShowScrollBar(ECheckBoxState IsAlwaysShow)
{
	if (auto VirtualList = GetNativeWidget())
	{
		if (auto Scrollbar = VirtualList->GetScrollBar())
		{
			Scrollbar->SetUserVisibility(IsAlwaysShow == ECheckBoxState::Unchecked ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
			Scrollbar->SetScrollBarAlwaysVisible(IsAlwaysShow == ECheckBoxState::Checked);
		}
	}
}

void UImVirtualList::SetScrollbarInfo(bool bTrackAlwaysVisible, float Thickness)
{
	if (auto VirtualList = GetNativeWidget())
	{
		if (auto Scrollbar = VirtualList->GetScrollBar())
		{
			if (Thickness >= 0.f)
				Scrollbar->SetThickness(Thickness);
			Scrollbar->SetScrollBarTrackAlwaysVisible(bTrackAlwaysVisible);
		}
	}
}

void UImVirtualList::SetAnimateScrollDuration(float InAnimateDuration)
{
	if (auto VirtualList = GetNativeWidget())
	{
		VirtualList->SetAnimateScrollDuration(InAnimateDuration);
	}
}

bool UImVirtualList::IsItemOffsetVisible(int32 InIndex, float InRelativePos) const
{
	if (auto VirtualList = GetNativeWidget())
	{
		return VirtualList->IsItemOffsetVisible(InIndex, InRelativePos);
	}
	return false;
}

float UImVirtualList::GetItemPosition(int32 InIndex)
{
	if (auto VirtualList = GetNativeWidget())
	{
		return VirtualList->GetDataOffset(InIndex);
	}
	
	return 0;
}

void UImVirtualList::ScrollToPos(float InVirtualPos, bool bItemAlign)
{
	if (auto VirtualList = GetNativeWidget())
	{
		VirtualList->ScrollToPos(InVirtualPos, bItemAlign);
	}
}

void UImVirtualList::ScrollToItem(int32 InDataIndex, bool bCenterAlign)
{
	if (auto VirtualList = GetNativeWidget())
	{
		VirtualList->ScrollToItem(InDataIndex, bCenterAlign);
	}
}

float UImVirtualList::GetVirtualPos() const
{
	if (auto VirtualList = GetNativeWidget())
	{
		return VirtualList->GetVirtualPos();
	}
	return 0.f;
}

float UImVirtualList::GetCachedTotalAxis(bool bFullItems) const
{
	if (auto VirtualList = GetNativeWidget())
	{
		return VirtualList->GetCachedTotalAxis(bFullItems);
	}
	return -0.f;
}

float UImVirtualList::GetVisibleAeraAxis() const
{
	if (auto VirtualList = GetNativeWidget())
	{
		return VirtualList->GetVisibleAxis();
	}
	return -0.f;
}

void UImVirtualList::Update(int32 InDataIndex, bool bReConstruct)
{
	if (auto VirtualList = GetNativeWidget())
	{
		VirtualList->Update(InDataIndex, bReConstruct);
	}
}

void UImVirtualList::SetDataStorage(UImSlateDataStorageBase* InStorage, float InVirtualPos, float InTileWidth)
{
	auto OldDataStorage = DataStorage;
	if (OldDataStorage)
		OldDataStorage->CallOnVirtualListBind(this, false);

	DataStorage = InStorage;
	if (auto VirtualList = GetNativeWidget())
	{
		DataStorage->CallOnVirtualListBind(this, true);
		VirtualList->SetData(DataStorage->GetBindingData(), InVirtualPos, InTileWidth);
	}
}

void UImVirtualList::SetNativeBindingData(TSharedPtr<ImSlate::IImSlateListData> InDataBindding, float InVirtualPos, float InTileWidth)
{
	if (auto VirtualList = GetNativeWidget())
	{
		ResetPool();
		VirtualList->SetData(InDataBindding, InVirtualPos, InTileWidth);
	}
}

TSharedPtr<ImSlate::SImSlateVirtualList> UImVirtualList::GetNativeWidget() const
{
	return StaticCastSharedPtr<ImSlate::SImSlateVirtualList>(MyWidget.Pin());
}

UUserWidget* UImVirtualList::GetOrCreateInstance(TSubclassOf<UUserWidget> WidgetClass)
{
	return UserWidgetPool.GetOrCreateInstance(WidgetClass, ImSlate::SImSlateVirtualItem::StaticWidgetConstructFunc);
}

void UImVirtualList::ReleaseWidget(UUserWidget* Widget, bool bReleaseSlate)
{
	UserWidgetPool.Release(Widget, bReleaseSlate);
}

void UImVirtualList::ReleaseWidgets(TArray<UUserWidget*> Widgets, bool bReleaseSlate)
{
	UserWidgetPool.Release(Widgets, bReleaseSlate);
}

void UImVirtualList::ResetPool()
{
	UserWidgetPool.ResetPool();
}

UUserWidget* UImVirtualList::GetBackgroundWidget()
{
	return BackgroundWidget;
}

void UImVirtualList::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	OnSynchronizeProperties();

#if WITH_EDITORONLY_DATA && WITH_EDITOR
	if (GIsEditor && IsDesignTime() && MyVirtualList)
	{
		UUserWidget* Inst = nullptr;
		if (BackgroundWidgetClass)
		{
			Inst = UGenericSingletons::CreateInstance<UUserWidget>(GetWorld(), ImSlate::GetTypeClass(BackgroundWidgetClass));
		}
		MyVirtualList->SetBackgroundContent(Inst ? Inst->TakeWidget() : SNullWidget::NullWidget);
		MyVirtualList->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
		BackgroundWidget = Inst;
	}
#endif
}

void UImVirtualList::OnSynchronizeProperties_Implementation()
{
#if WITH_EDITORONLY_DATA && WITH_EDITOR
	using namespace ImSlate;
	if (GIsEditor && IsDesignTime() && (MyVirtualList || !GetWorld()->IsGameWorld()) && EditorPreviewItemCount >= 0 && EditorPreviewWidgetClass.Get())
	{
		auto WidgetRef = RebuildWidget();
		struct FSharedData
		{
		};
		MyVirtualList->SetTileOrthVal(EditorPreviewItemWidth);

		auto BindingDataRef = MakeShared<TImSlateListArray<TSharedPtr<FSharedData>>>(false);
		BindingDataRef->SetOnBindingData([](int32 DataIndex, const TSharedRef<SWidget>& WidgetRef, TSharedPtr<FSharedData>& DataRef) {});
		BindingDataRef->SetWidgetFactory([this](TSharedPtr<FSharedData>& InData, TSharedRef<SWidget>& InOutWidget) {
			//
			InOutWidget = ImFactoryCreate(EditorPreviewWidgetClass, &UWidgetPool);
		});
		BindingDataRef->SetItemAxisBinding([this](int32 InIndex) { return EditorPreviewItemHeight; });
		TArray<TSharedPtr<FSharedData>> Datas;
		for (auto i = 0; i < EditorPreviewItemCount; ++i)
			Datas.Add(MakeShared<FSharedData>());
		BindingDataRef->Reload(Datas);
		GetNativeWidget()->SetData(BindingData);
		BindingDataRef->ScrollToPos(EditorPreviewVirtualPos);
		BindingData = BindingDataRef;
	}
#endif
}

void UImVirtualList::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyVirtualList.Reset();
}

TSharedRef<ImSlate::SImSlateVirtualList> UImVirtualList::ConstructImWidget() const
{
	using namespace ImSlate;
	auto WidgetRef = SNew(SImSlateVirtualList)
						.WorldCtxPtr(GetWorld())
						.OnUserScrolled(FOnUserScrolled::CreateWeakLambda(this, [this](float Val) { OnUserScrolled.Broadcast(Val); }))
						.OnScrollBarVisibilityChanged(FOnScrollBarVisibilityChanged::CreateWeakLambda(this, [this](EVisibility Vis) { OnScrollBarVisibilityChanged.Broadcast(ConvertRuntimeToSerializedVisibility(Vis)); }))
						.OnVirtualPosChanged(FOnVirtualPosChanged::CreateWeakLambda(this, [this](float Val) { OnVirtualPosChanged.Broadcast(Val); }));
#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	WidgetRef->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	if (!GIsEditor || !IsDesignTime())
	{
		UUserWidget* Inst = nullptr;
		if (BackgroundWidgetClass)
		{
			Inst = UGenericSingletons::CreateInstance<UUserWidget>(GetWorld(), ImSlate::GetTypeClass(BackgroundWidgetClass));
		}
		WidgetRef->SetBackgroundContent(Inst ? Inst->TakeWidget() : SNullWidget::NullWidget);
		BackgroundWidget = Inst;
	}

	return WidgetRef;
}

TSharedRef<SWidget> UImVirtualList::RebuildWidget()
{
	auto VirtualList = ConstructImWidget();
	MyVirtualList = VirtualList;
	return VirtualList;
}

void UImVirtualList::OnBindingChanged(const FName& Property)
{
	Super::OnBindingChanged(Property);
}
