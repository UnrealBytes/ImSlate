// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Engine/World.h"
#include "GMPCore.h"
#include "ImSlateVirtualList.h"
#include "Templates/SharedPointer.h"
#include "UnrealCompatibility.h"

namespace ImSlate
{
class SImSlateVirtualList;
class IImSlateListData : public TSharedFromThis<IImSlateListData>
{
protected:
	friend class SImSlateVirtualList;
	virtual ~IImSlateListData() = default;
	IImSlateListData(bool bInHeterogeneous, bool bInNeedPrepass = false)
		: bHeterogeneous(bInHeterogeneous)
		, bNeedPrepass(bInNeedPrepass)
	{
	}

	virtual TSharedPtr<SImSlateVirtualList> GetVirtualList() const = 0;
	virtual void SetVirtualList(TSharedPtr<SImSlateVirtualList> InVirtualList) = 0;

	virtual int32 GetDataCount() const = 0;
	virtual void OnSetData(int32 InIndex, TSharedRef<SWidget> Widget) = 0;

	virtual float GetItemAxis(int32 InIndex = 0) const = 0;

	virtual void OnPosChanged(float VirtualPos) {}
	virtual void GenerateDataWidget(int32 InIndex, TSharedRef<SWidget>& InOutWidget) = 0;
	virtual void ReleaseDataWidget(int32 InIndex, TSharedRef<SWidget>& ReleasedWidget) = 0;

public:
	void StartFiltering(UWorld* InWorld, const FGMPStructUnion& UnionData) { OnFilterStarted(InWorld, UnionData); }
	void SetHeterogeneous() { bHeterogeneous = true; }
	void SetNeedPrepassItem() { bNeedPrepass = true; }
	bool IsHeterogeneous() const { return bHeterogeneous; }
	bool NeedPrepassItem() const { return bNeedPrepass; }

	void ReloadToPos(float VirtualPos, bool bItemAlign = true) { SetVirtualPos(VirtualPos, bItemAlign, true); }
	void ScrollToPos(float VirtualPos, bool bItemAlign = false) { SetVirtualPos(VirtualPos, bItemAlign, false); }

protected:
	IMSLATE_API float GetVirtualPos() const;
	IMSLATE_API void ScrollToItem(int32 InIndex, bool bCenterAlign = false);
	IMSLATE_API void UpdateItem(int32 InIndex = -1, bool bReConstructWidget = false);

private:
	uint8 bHeterogeneous : 1;
	uint8 bNeedPrepass : 1;
	IMSLATE_API void SetVirtualPos(float VirtualPos, bool bItemAlign, bool bReset);
	virtual void OnFilterStarted(UWorld* InWorld, const FGMPStructUnion& UnionData) { ensure(false); }
};

template<typename DataType, typename S = SWidget>
class TImSlateListArray : public IImSlateListData
{
public:
	TImSlateListArray(bool bInHeterogeneous)
		: IImSlateListData(bInHeterogeneous)
	{
	}

	TSharedRef<TImSlateListArray> MakeShared(bool bInHeterogeneous) { return ::MakeShared<TImSlateListArray>(bInHeterogeneous); }

	using FOnBindingDataDelegate = TDelegate<void(int32, const TSharedRef<S>&, DataType&)>;
	void SetOnBindingData(FOnBindingDataDelegate f) { OnBindingData = MoveTemp(f); }
	void SetOnBindingData(TFunction<void(int32, const TSharedRef<S>&, DataType&)> f) { SetOnBindingData(FOnBindingDataDelegate::CreateLambda(MoveTemp(f))); }

	using FWidgetFactoryDelegate = TDelegate<void(DataType&, TSharedRef<S>&)>;
	void SetWidgetFactory(FWidgetFactoryDelegate f) { OnWidgetBinding = MoveTemp(f); }
	void SetWidgetFactory(TFunction<void(DataType&, TSharedRef<S>&)> f) { SetWidgetFactory(FWidgetFactoryDelegate::CreateLambda(MoveTemp(f))); }

	using FOnBindingReleaseDelegate = TDelegate<void(int32, TSharedRef<S>)>;
	void SetOnBindingRelease(FOnBindingReleaseDelegate f) { OnBindingRelease = MoveTemp(f); }
	void SetOnBindingRelease(TFunction<void(int32, TSharedRef<S>)> f) { SetOnBindingRelease(FOnBindingReleaseDelegate::CreateLambda(MoveTemp(f))); }

	using FItemAxisBindingDelegate = TDelegate<float(int32)>;
	void SetItemAxisBinding(FItemAxisBindingDelegate f) { ItemAxisBinding = MoveTemp(f); }
	void SetItemAxisBinding(TFunction<float(int32)> f) { SetItemAxisBinding(FItemAxisBindingDelegate::CreateLambda(MoveTemp(f))); }

	using FOnScrolledEvent = TDelegate<void(float)>;
	void SetOnScrolledEvent(FOnScrolledEvent f) { OnPosScrolled = MoveTemp(f); }
	void SetOnScrolledEvent(TFunction<void(float)> f) { SetOnScrolledEvent(FOnScrolledEvent::CreateLambda(MoveTemp(f))); }

	using FOnDataCountDelegate = TDelegate<int32(const TArray<DataType>&)>;
	void SetOnDataCount(FOnDataCountDelegate f) { OnCountBinding = MoveTemp(f); }
	void SetOnDataCount(TFunction<int32(const TArray<DataType>&)> f) { SetOnDataCount(FOnDataCountDelegate::CreateLambda(MoveTemp(f))); }

	TArray<DataType>& GetCurData()
	{
		ensure(!IsFiltering());
		return CurDataArr;
	}

	TArray<DataType>& GetOrignalData() { return OrignalData.Num() > 0 ? OrignalData : CurDataArr; }

	void ScrollToItem(int32 InIndex, bool bCenterAlign = false)
	{
		check(CurDataArr.IsValidIndex(InIndex));
		IImSlateListData::ScrollToItem(InIndex, bCenterAlign);
	}

	void Reload(const TArray<DataType>& NewData, bool bReset = true)
	{
		ensure(!IsFiltering());
		if (&NewData != &CurDataArr)
		{
			CurDataArr.Empty(NewData.Num());
			OrignalData.Reset();
			CurDataArr.Append(NewData);
			IImSlateListData::ReloadToPos(-1.f, bReset);
		}
		else
		{
			IImSlateListData::UpdateItem(-1, bReset);
		}
	}
	void Reload(TArray<DataType>&& NewData, bool bReset = true)
	{
		ensure(!IsFiltering());
		if (&NewData != &CurDataArr)
		{
			CurDataArr = MoveTemp(NewData);
			OrignalData.Reset();
			IImSlateListData::ReloadToPos(-1.f, bReset);
		}
		else
		{
			IImSlateListData::UpdateItem(-1, bReset);
		}
	}

	void SetItemAxis(float InAxis, bool bReConstructWidget = true)
	{
		FallbackItemAxis = FMath::Max(1.f, InAxis);
		IImSlateListData::UpdateItem(-1, bReConstructWidget);
	}

public:
	FORCEINLINE bool IsFiltered() const { return OrignalData.Num() > 0; }
	FORCEINLINE bool IsFiltering() const { return FilterTask.IsValid(); }
	using FDataQueryDelegate = TDelegate<bool(const DataType&)>;
	void BeginFiltering(UWorld* InWorld, FDataQueryDelegate InFilter, double InStepDuration = 0.013)
	{
		if (FilterTask)
		{
			FilterTask->CancelTask();
		}
		else if (!IsFiltered())
		{
			OrignalData = MoveTemp(CurDataArr);
		}

		CurDataArr.Reset();
		IImSlateListData::ReloadToPos(-1.f);

		FilterTask = ::MakeShared<FFilterTask>(*this, MoveTemp(InFilter), InStepDuration);
		FilterTask->StartTask(InWorld, CreateWeakLambda(this, [this, Ptr{FilterTask.Get()}] {
								  if (Ptr == FilterTask.Get())
								  {
									  FilterTask.Reset();
									  UE_LOG(LogTemp, Log, TEXT("FilterTask reset when finish"));
								  }
								  UE_LOG(LogTemp, Log, TEXT("FilterTask Finished"));
							  }));
	}
	void EndFiltering(bool bReset = true)
	{
		UE_LOG(LogTemp, Log, TEXT("FilterTask ClearFiltering enter"));
		if (FilterTask)
		{
			FilterTask->CancelTask();
			FilterTask.Reset();
			Reload(MoveTemp(OrignalData), bReset);
		}
		else if (IsFiltered())
		{
			Reload(MoveTemp(OrignalData), bReset);
			UE_LOG(LogTemp, Log, TEXT("FilterTask ClearFiltering done"));
		}
	}

public:
	struct FFilterContext
	{
		UWorld* const World;
		TImSlateListArray* const Storage;
		const FGMPStructUnion& Data;
	};

	using FOnCustomFiltering = TDelegate<bool(const FFilterContext&)>;
	void SetCustomFiltering(FOnCustomFiltering&& InFilter)
	{
		EndFiltering();
		OnCustomFiltering = MoveTemp(InFilter);
	}

	using FFilterExpr = TDelegate<bool(const DataType&, const FGMPStructUnion&)>;
	void SetFilterExpr(FFilterExpr&& InExpr)
	{
		EndFiltering();
		FilterExpr = MoveTemp(InExpr);
	}
	void SetFilterExpr(TFunction<bool(const DataType&, const FGMPStructUnion&)> f) { SetFilterExpr(FFilterExpr::CreateLambda(MoveTemp(f))); }

protected:
	FOnCustomFiltering OnCustomFiltering;
	FFilterExpr FilterExpr;
	virtual void OnFilterStarted(UWorld* InWorld, const FGMPStructUnion& UnionData) override
	{
		if (!UnionData.IsValid())
		{
			EndFiltering();
			return;
		}

		if (OnCustomFiltering.IsBound() && OnCustomFiltering.Execute({InWorld, this, UnionData}))
		{
			return;
		}
		else if (FilterExpr.IsBound())
		{
			BeginFiltering(InWorld, FDataQueryDelegate::CreateLambda([FilterExpr{this->FilterExpr}, UnionData /*{UnionData.Duplicate()}*/](const DataType& Data) { return FilterExpr.Execute(Data, UnionData); }));
		}
	}

protected:
	virtual TSharedPtr<SImSlateVirtualList> GetVirtualList() const override { return VirtualList.Pin(); }
	virtual void SetVirtualList(TSharedPtr<SImSlateVirtualList> InListWidget) override { VirtualList = InListWidget; }
	virtual void OnSetData(int32 InIndex, TSharedRef<SWidget> Widget) override
	{
		check(CurDataArr.IsValidIndex(InIndex) && Widget != SNullWidget::NullWidget);
		OnBindingData.ExecuteIfBound(InIndex, StaticCastSharedRef<S>(Widget), CurDataArr[InIndex]);
	}
	virtual int32 GetDataCount() const override { return OnCountBinding.IsBound() ? OnCountBinding.Execute(CurDataArr) : CurDataArr.Num(); }
	virtual void OnPosChanged(float VirtualPos) override { OnPosScrolled.ExecuteIfBound(VirtualPos); }
	virtual void GenerateDataWidget(int32 InIndex, TSharedRef<SWidget>& InOutWidget) override
	{
		check(CurDataArr.IsValidIndex(InIndex) && OnWidgetBinding.IsBound());
		OnWidgetBinding.Execute(CurDataArr[InIndex], InOutWidget);
	}
	virtual void ReleaseDataWidget(int32 InIndex, TSharedRef<SWidget>& ReleasedWidget) override
	{
		//
		OnBindingRelease.ExecuteIfBound(InIndex, StaticCastSharedRef<S>(ReleasedWidget));
	}

	virtual float GetItemAxis(int32 Index) const override { return ItemAxisBinding.IsBound() ? ItemAxisBinding.Execute(Index) : FallbackItemAxis; };

	float FallbackItemAxis = 22.f;

	TArray<DataType> CurDataArr;
	TArray<DataType> OrignalData;

	FOnBindingDataDelegate OnBindingData;
	FOnBindingReleaseDelegate OnBindingRelease;
	FOnScrolledEvent OnPosScrolled;
	FWidgetFactoryDelegate OnWidgetBinding;
	FItemAxisBindingDelegate ItemAxisBinding;
	FOnDataCountDelegate OnCountBinding;

	TWeakPtr<SImSlateVirtualList> VirtualList;

protected:
	struct FFilterTask
		: public GMP::TGMPFrameTickBase<FFilterTask>
		, public TSharedFromThis<FFilterTask>
	{
		FFilterTask(TImSlateListArray& InDataStore, FDataQueryDelegate&& InFilter, double InStepDuration = 0.013)
			: GMP::TGMPFrameTickBase<FFilterTask>(InStepDuration)
			, DataStore(InDataStore)
			, FilterFunc(MoveTemp(InFilter))
		{
		}
		~FFilterTask() { CancelTask(); }

		void CancelTask()
		{
			CurrentIndex = 0;
			if (CurWorld.IsValid() && TimerHandle.IsValid())
			{
				CurWorld->GetTimerManager().ClearTimer(TimerHandle);
			}
		}

		void StartTask(UWorld* InWorld, FSimpleDelegate OnFinish)
		{
			CurWorld = InWorld;
			OnFinishFunc = MoveTemp(OnFinish);
			CurWorld->GetTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateSPLambda(this, [this] { this->Tick(); }), 0.001f, true);
		}

		void Finish()
		{
			CancelTask();
			OnFinishFunc.ExecuteIfBound();
		}
		TWeakObjectPtr<UWorld> CurWorld;
		FTimerHandle TimerHandle;
		FSimpleDelegate OnFinishFunc;

		TImSlateListArray& DataStore;
		FDataQueryDelegate FilterFunc;
		mutable int32 CurrentIndex = 0;
		bool Step()
		{
			if (CurrentIndex < DataStore.OrignalData.Num())
			{
				auto& DataRef = DataStore.OrignalData[CurrentIndex++];
				if (FilterFunc.Execute(DataRef))
				{
					DataStore.CurDataArr.Add(DataRef);
					//DataStore.IImSlateListData::ReloadToPos(-1.f);
					DataStore.UpdateItem(DataStore.CurDataArr.Num() - 1);
				}
			}

			return CurrentIndex < DataStore.OrignalData.Num();
		}
	};

	TSharedPtr<FFilterTask> FilterTask;
};
}  // namespace ImSlate
