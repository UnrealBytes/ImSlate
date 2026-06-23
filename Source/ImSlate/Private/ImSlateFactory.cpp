// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateFactory.h"

#include "Engine/World.h"
#include "ImSlate.h"

FUWidgetPool::FUWidgetPool(UWidget& InOwningWidget)
	: OwningWidget(&InOwningWidget)
{
}

FUWidgetPool::~FUWidgetPool()
{
	ResetPool();
}

void FUWidgetPool::RebuildWidgets()
{
	for (auto& Widget : ActiveWidgets)
	{
		CachedSlateByWidgetObject.Add(Widget, Widget->TakeWidget());
	}
}

void FUWidgetPool::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects<UWidget>(ActiveWidgets, OwningWidget.Get());
	Collector.AddReferencedObjects<UWidget>(InactiveWidgets, OwningWidget.Get());
}

void FUWidgetPool::Release(TArray<UWidget*> Widgets, bool bReleaseSlate /*= false*/)
{
	for (auto* Widget : Widgets)
	{
		Release(Widget, bReleaseSlate);
	}
}

void FUWidgetPool::Release(UWidget* Widget, bool bReleaseSlate /*= false*/)
{
	if (Widget != nullptr)
	{
		const int32 ActiveWidgetIdx = ActiveWidgets.Find(Widget);
		if (ActiveWidgetIdx != INDEX_NONE)
		{
			InactiveWidgets.Push(Widget);
			ActiveWidgets.RemoveAt(ActiveWidgetIdx);

			if (bReleaseSlate)
			{
				CachedSlateByWidgetObject.Remove(Widget);
			}
		}
	}
}

void FUWidgetPool::ReleaseAll(bool bReleaseSlate /*= false*/)
{
	InactiveWidgets.Append(ActiveWidgets);
	ActiveWidgets.Empty();

	if (bReleaseSlate)
	{
		CachedSlateByWidgetObject.Reset();
	}
}

void FUWidgetPool::ResetPool()
{
	InactiveWidgets.Reset();
	ActiveWidgets.Reset();
	CachedSlateByWidgetObject.Reset();
}

void FUWidgetPool::ReleaseInactiveSlateResources()
{
	for (auto& InactiveWidget : InactiveWidgets)
	{
		CachedSlateByWidgetObject.Remove(InactiveWidget);
	}
}

void FUWidgetPool::ReleaseAllSlateResources()
{
	CachedSlateByWidgetObject.Reset();
}
