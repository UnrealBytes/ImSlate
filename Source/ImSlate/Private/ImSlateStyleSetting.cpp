// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateStyleSetting.h"

//
#include "ClassDataStorage.h"
#include "Editor/UnrealEditorUtils.h"

namespace ImSlate
{
FText GetPaletteCategory()
{
	return NSLOCTEXT("ImSlate", "ImSlate", "ImSlate");
}
namespace Factory
{
	struct FDefalutClassData
	{
		TSubclassOf<UWidget> Class;
	};

	struct FDefalutClassDataMap : public ClassStorage::TClassStorageImpl<FDefalutClassData>
	{
	};

	FDefalutClassDataMap DefaultClassStorage;

	UClass* GetDefaultImpl(TSubclassOf<UWidget> InClass)
	{
		check(InClass.Get());
		// FindData walks the SUPER chain, so a query for a concrete subclass (e.g. UImNumericFloatWidget)
		// can resolve to a mapping registered on a base class. If that mapping points at an ABSTRACT class
		// (e.g. the numeric widget base UImSlateNumericWidget, which has six concrete templated subclasses
		// and no single representative), instantiating it would crash in StaticAllocateObject. Fall back to
		// the concrete InClass whenever the resolved class is missing or abstract.
		FDefalutClassData* Ptr = DefaultClassStorage.FindData(InClass.Get());
		UClass* Resolved = (Ptr && Ptr->Class) ? Ptr->Class.Get() : nullptr;
		if (!Resolved || Resolved->HasAnyClassFlags(CLASS_Abstract))
			return InClass;
		return Resolved;
	}

	void ModifyDefault(UClass* InClass, UClass* InNativeClass)
	{
		check(InNativeClass);
		ensure(InClass);
		DefaultClassStorage.ModifyData(
			InNativeClass,
			true,
			[&](FDefalutClassData& InOutData) { InOutData.Class = InClass; },
			false);
	}
	template<typename U>
	void ModifyDefault(TSubclassOf<U> InClass)
	{
		ModifyDefault(InClass.Get(), U::StaticClass());
	}
}  // namespace Factory
}  // namespace ImSlate

UXImSlateStyleSetting::UXImSlateStyleSetting()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		UnrealEditorUtils::AddConfigurationOnProject(this, FName("ImSlateStyle"));
	}
}

void UXImSlateStyleSetting::PreloadClasses() const
{
	using namespace ImSlate::Factory;
#if 1
	for (FProperty* Property = GetClass()->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
	{
		if (FClassProperty* ClsProp = CastField<FClassProperty>(Property))
		{
			ModifyDefault(*ClsProp->ContainerPtrToValuePtr<UClass*>(this), ClsProp->MetaClass);
		}
	}
#else
	ModifyDefault(ImSlateButton);
	ModifyDefault(ImSlateImageButton);
	ModifyDefault(ImSlateText);
	ModifyDefault(ImSlateBorder);
	ModifyDefault(ImSlateEditableText);
	ModifyDefault(ImSlateSpinBox);
	ModifyDefault(ImSlateCheckBox);
	// ImSlateNumericWidget removed: abstract base, no single representative class (see header note).
	ModifyDefault(ImSlateColorWidget);
	ModifyDefault(ImSlateMobilityWidget);
	ModifyDefault(ImSlateImage);
	ModifyDefault(ImSlateComboButton);
	ModifyDefault(ImSlateVirtualComboButton);
	ModifyDefault(ImSlateFoldLine);
	ModifyDefault(ImSlateEditableTextBox);
	ModifyDefault(ImSlateCollisionData);
	ModifyDefault(ImVirtualList);
	ModifyDefault(ImSearchBox);
#endif
}

void UXImSlateStyleSetting::PostLoad()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
		LoadConfig();
	Super::PostLoad();
	PreloadClasses();
}

void UXImSlateStyleSetting::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject))
		PreloadClasses();
}
