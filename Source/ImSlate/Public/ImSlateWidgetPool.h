// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "Components/Widget.h"
#include "GenericSingletons.h"

#include "ImSlateWidgetPool.generated.h"

USTRUCT()
struct IMSLATE_API FUWidgetPool
{
	GENERATED_BODY()

public:
	FUWidgetPool() = default;
	FUWidgetPool(UWidget& InOwningWidget);
	~FUWidgetPool();

	/** In the case that you don't have an owner widget, you should set a world to your pool, or it won't be able to construct widgets. */
	void SetWorld(UWorld* InOwningWorld) { OwningWorld = InOwningWorld; }

	/** Triggers RebuildWidget on all currently active UserWidget instances */
	void RebuildWidgets();

	/** Report any references to UObjects to the reference collector (only necessary if this is not already a UPROPERTY) */
	void AddReferencedObjects(FReferenceCollector& Collector);

	bool IsInitialized() const { return OwningWidget.IsValid() || OwningWorld.IsValid(); }
	const TArray<UWidget*>& GetActiveWidgets() const { return ActiveWidgets; }

	/**
	 * Gets an instance of a widget of the given class.
	 * The underlying slate is stored automatically as well, so the returned widget is fully constructed and GetCachedWidget will return a valid SWidget.
	 */
	template<typename UWidgetT = UWidget>
	UWidgetT* GetOrCreateInstance(TSubclassOf<UWidgetT> WidgetClass)
	{
		// Just make a normal SWidget, same as would happen in TakeWidget
		return AddActiveWidgetInternal(WidgetClass, [](UWidget* Widget, TSharedRef<SWidget> Content) { return nullptr; });
	}

	using WidgetConstructFunc = TFunctionRef<TSharedPtr<SWidget>(UWidget*, TSharedRef<SWidget>)>;

	/** Gets an instance of the widget this factory is for with a custom underlying SWidget type */
	template<typename UWidgetT = UWidget>
	UWidgetT* GetOrCreateInstance(TSubclassOf<UWidgetT> WidgetClass, WidgetConstructFunc ConstructWidgetFunc)
	{
		return AddActiveWidgetInternal(WidgetClass, ConstructWidgetFunc);
	}

	/** Return a widget object to the pool, allowing it to be reused in the future */
	void Release(UWidget* Widget, bool bReleaseSlate = false);

	/** Return a widget object to the pool, allowing it to be reused in the future */
	void Release(TArray<UWidget*> Widgets, bool bReleaseSlate = false);

	/** Returns all active widget objects to the inactive pool and optionally destroys all cached underlying slate widgets. */
	void ReleaseAll(bool bReleaseSlate = false);

	/** Full reset of all created widget objects (and any cached underlying slate) */
	void ResetPool();

	/** Reset of all cached underlying Slate widgets, only for inactive widgets in the pool. */
	void ReleaseInactiveSlateResources();

	/** Reset of all cached underlying Slate widgets, but not the active UWidget objects */
	void ReleaseAllSlateResources();

private:
	template<typename UWidgetT = UWidget>
	UWidgetT* AddActiveWidgetInternal(TSubclassOf<UWidgetT> WidgetClass, WidgetConstructFunc ConstructWidgetFunc)
	{
		if (!ensure(IsInitialized()) || !WidgetClass)
		{
			return nullptr;
		}

		UWidget* WidgetInstance = nullptr;
		for (UWidget* InactiveWidget : InactiveWidgets)
		{
			if (InactiveWidget->GetClass() == WidgetClass)
			{
				WidgetInstance = InactiveWidget;
				InactiveWidgets.RemoveSingleSwap(InactiveWidget);
				break;
			}
		}

		UWidget* OwningWidgetPtr = OwningWidget.Get();
		if (!WidgetInstance)
		{
			if (OwningWidgetPtr)
			{
				WidgetInstance = UGenericSingletons::CreateInstance<UWidget>(OwningWidgetPtr, WidgetClass.Get());
			}
			else
			{
				WidgetInstance = UGenericSingletons::CreateInstance<UWidget>(OwningWorld.Get(), WidgetClass.Get());
			}
		}

		if (WidgetInstance)
		{
			ActiveWidgets.Add(WidgetInstance);

			// For pools owned by a widget, we never want to construct Slate widgets before the owning widget itself has built any Slate
			if (!OwningWidgetPtr || OwningWidgetPtr->GetCachedWidget().IsValid())
			{
				TSharedPtr<SWidget>& CachedSlateWidget = CachedSlateByWidgetObject.FindOrAdd(WidgetInstance);
				if (!CachedSlateWidget.IsValid())
				{
					CachedSlateWidget = WidgetInstance->TakeWidget();
				}
			}
		}

		return Cast<UWidgetT>(WidgetInstance);
	}

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> ActiveWidgets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> InactiveWidgets;

	TWeakObjectPtr<UWidget> OwningWidget;
	TWeakObjectPtr<UWorld> OwningWorld;
	TMap<TObjectPtr<UWidget>, TSharedPtr<SWidget>> CachedSlateByWidgetObject;
};
