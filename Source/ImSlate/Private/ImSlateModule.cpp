// Copyright ImSlate, Inc. All Rights Reserved.
#include "EngineMinimal.h"
#include "SlateGlobals.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GMPCore.h"
#include "GMPSignals.h"
#include "GenericSingletons.h"
#include "ImSlate.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "SImSlateViewport.h"
#include "SImViewportGame.h"
#include "SImViewportHost.h"
#include "UnrealEngine.h"
#include "WorldLocalStorages.h"
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "ImSlatePanelScrollProcessor.h"
#include "Framework/Application/SlateApplication.h"
#if defined(IMSLATE3D_API)
#include "ImSlate3DHost.h"                 // FImSlate3DHost — install Engine hooks for the 3D module
#include "Engine/LocalPlayer.h"            // GetProjectionData
#include "SceneView.h"                     // FSceneViewProjectionData
#include "Widgets/SViewport.h"             // GetGameViewportWidget return type
#include "GameFramework/InputSettings.h"   // bUseMouseForTouch
#endif

#if WITH_EDITOR
#include "Editor/LevelEditor/Public/SLevelViewport.h"
#include "Editor/UnrealEd/Public/LevelEditorViewport.h"
#include "Editor/UnrealEd/Public/SEditorViewport.h"
#endif

#if WITH_EDITOR
ImSlate::ImSlateContext* EditorGImSlate = nullptr;
#else
IMSLATE_API ImSlate::ImSlateContext* GImSlate = nullptr;
#endif

// ImSlate viewport Z-order — must be above ImGui (IMGUI_WIDGET_Z_ORDER = 10000)
// so ImSlate panels render on top of ImGui debug windows.
static constexpr int32 IMSLATE_VIEWPORT_Z_ORDER = 12000;

namespace ImSlate
{
struct FWorldContextRoot;
FORCEINLINE ImSlateContext*& GetGlobalImSlate()
{
#if WITH_EDITOR
	return EditorGImSlate;
#else
	return GImSlate;
#endif
}
extern void BeginFrameImpl(float DeltaTime, ImSlateContext* Ptr);
extern void EndFrameImpl(ImSlateContext* Ptr);

static TArray<FWorldContextRoot*, TInlineAllocator<4>> ImSlateRoots;
static auto& GetSlateRoots()
{
	return ImSlateRoots;
}

struct FWorldContextRoot : public ImSlateContext
{
	template<typename F>
	static bool OnViewportCreated(UWorld* World, F Func /* = [World] { OnWorldViewport(World) */)
	{
		if (auto Viewport = World->GetGameViewport())
		{
#if WITH_EDITOR || 1
			CallOnWorldNextTick(World, MoveTemp(Func));
#else
			OnWorldViewport(World);
#endif
		}
		else
		{
			UGameViewportClient::OnViewportCreated().AddWeakLambda(World, MoveTemp(Func));
		}
	}
	static void OnWorldViewport(UWorld* World) { WorldLocalStorages::GetLocalValue<ImSlate::FWorldContextRoot>(World, World); }

	FWorldContextRoot(UWorld* InWorld)
	{
		RawWorldPtr = InWorld;
		CurrentWorld = InWorld;
#if WITH_EDITOR
		//ensureAlways(InWorld == GWorld);
		GetSlateRoots().Add(this);
#endif
		if (GetGlobalImSlate() == nullptr)
		{
			GetGlobalImSlate() = this;
		}

		this->PIEInstanceID = GetWorldChecked()->GetOutermost()->GetPIEInstanceID();
		if (auto Viewport = InWorld->GetGameViewport())
		{
			this->Viewports.Add(SNew(SImViewportGame, IMSLATE_VIEWPORT_Z_ORDER)
								.GameViewportClient(Viewport)
								.Visibility(EVisibility::SelfHitTestInvisible));
			WeakViewPortClient = Viewport;
		}
#if WITH_EDITOR
		else if (InWorld->WorldType == EWorldType::Editor)
		{
			for (FLevelEditorViewportClient* ViewClient : GEditor->GetLevelViewportClients())
			{
				if (!ViewClient->IsLevelEditorClient())
					continue;
				auto EditorViewport = ViewClient->GetEditorViewportWidget();
				if (!EditorViewport)
					continue;
				auto IncLevelEditor = StaticCastSharedPtr<SLevelViewport>(EditorViewport)->GetParentLevelEditor().Pin();
				if (!IncLevelEditor)
					continue;
				this->Viewports.Add(SNew(SImViewportGame, IMSLATE_VIEWPORT_Z_ORDER)
									.LevelEditor(IncLevelEditor)
									.Visibility(EVisibility::SelfHitTestInvisible));
			}
		}
#endif
		// Unified tick: drive from Slate PreTick so Draw completes before DrawWindows
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().OnPreTick().AddRaw(this, &FWorldContextRoot::SlatePreTick);
		}
		BeginFrameImpl(0.01f, this);
	}
	~FWorldContextRoot()
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().OnPreTick().RemoveAll(this);
		}
		for (auto& Viewport : this->Viewports)
		{
			// Detach the virtual keyboard from the engine viewport overlay BEFORE dropping the
			// viewport — otherwise the overlay keeps the keyboard alive and it lingers on screen
			// after ImSlate is gone (world teardown / editor map switch). Refs are still valid here.
			if (Viewport->IsGameViewport())
				static_cast<SImViewportGame&>(Viewport.Get()).RemoveKeyboard();
			Viewport->RemoveAllWindow();
			Viewport->ClearChildren();
		}
		this->Viewports.Reset();

		if (GetGlobalImSlate() == this)
		{
			GetGlobalImSlate() = nullptr;
		}
#if WITH_EDITOR
		GetSlateRoots().Remove(this);
#endif
	}
	GMP::TSignal<false, float, UWorld*> ImSlateSignals;

protected:
	void EnsureViewportWidget()
	{
		if (Viewports.Num() == 0 && !WeakViewPortClient.IsValid())
		{
			if (auto Viewport = RawWorldPtr ? RawWorldPtr->GetGameViewport() : nullptr)
			{
				Viewports.Add(SNew(SImViewportGame, IMSLATE_VIEWPORT_Z_ORDER)
							.GameViewportClient(Viewport)
							.Visibility(EVisibility::SelfHitTestInvisible));
				WeakViewPortClient = Viewport;
			}
		}
	}

	void SlatePreTick(float DeltaTime)
	{
		// Guard GWorld for this root's World context (PIE, Editor, Game)
		TGuardValue<UWorld*> WorldGuard(reinterpret_cast<UWorld*&>(GWorld), RawWorldPtr);

		EnsureViewportWidget();

		if (this->bIsFrameStarted)
		{
			EndFrameImpl(this);
		}

		BeginFrameImpl(DeltaTime, this);
		ImSlateSignals.FireWithSigSource(RawWorldPtr, DeltaTime, RawWorldPtr);
	}

	TWeakObjectPtr<UGameViewportClient> WeakViewPortClient;
};
}  // namespace ImSlate

class FImSlateModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		GMP::OnGMPTagReady(FSimpleDelegate::CreateLambda([this] {
			FGMPHelper::UnsafeListenMessage(MSGKEY("GameInstance.OnStart"), [this](UWorld* World) { OnGameInstanceStart(World); });
			FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FImSlateModule::OnGameInstanceStart);
		}));
		FWorldDelegates::OnWorldCleanup.AddRaw(this, &FImSlateModule::OnWorldCleanup);

		// App-lifecycle: proactively close any open virtual keyboard when the app deactivates or
		// enters background. A keyboard that survives into resume is the source of a known
		// suspend/resume crash, so closing it beforehand avoids that path entirely (workaround, not
		// a root-cause fix — see issue-imslate-suspend-resume-bug). Both events are covered: a
		// transient deactivate (call/notification shade) and a real backgrounding (lock/Home).
		AppDeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddStatic(&FImSlateModule::OnAppWillBackground);
		AppBackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddStatic(&FImSlateModule::OnAppWillBackground);

		// App-level input preprocessor: lets a vertical drag started ON A CHILD widget (checkbox /
		// button / text box inside a scrollable panel) scroll the panel, even though the child
		// captured the pointer. See FImSlatePanelScrollProcessor. Registered at index 0 (highest
		// priority) so it sees moves before anything else; it only consumes once it confirms a scroll.
		if (FSlateApplication::IsInitialized())
		{
			ScrollProcessor = MakeShared<ImSlate::FImSlatePanelScrollProcessor>();
			FSlateApplication::Get().RegisterInputPreProcessor(ScrollProcessor, 0);
		}

		InstallImSlate3DHost();
	}

	// Wire the ImSlate3D module's Engine-coupled hooks. The 3D widgets live in the low-level ImSlate3D
	// module (no Engine link); these lambdas — running in THIS module, which links Engine — supply the
	// scene camera, the game viewport, and the input-settings flag the widgets need.
	static void InstallImSlate3DHost()
	{
#if defined(IMSLATE3D_API)
		ImSlate::FImSlate3DHost Hooks;
		Hooks.GetCameraProjData = []() -> ImSlate::FImCameraProjData
		{
			ImSlate::FImCameraProjData Out;
			UWorld* World = ImSlate::GetWorldChecked();
			if (!World || !GEngine) { return Out; }
			ULocalPlayer* LP = GEngine->GetLocalPlayerFromControllerId(World, 0);
			if (!LP || !LP->ViewportClient) { return Out; }
			FSceneViewProjectionData ProjData;
			if (!LP->GetProjectionData(LP->ViewportClient->Viewport, ProjData)) { return Out; }
			Out.ViewProj = ProjData.ComputeViewProjectionMatrix();
			Out.ViewRect = ProjData.GetConstrainedViewRect();
			Out.bValid = true;
			return Out;
		};
		Hooks.GetGameViewport = []() -> TSharedPtr<SViewport>
		{
			return GEngine ? GEngine->GetGameViewportWidget() : nullptr;
		};
		Hooks.IsMouseForTouch = []() -> bool
		{
			const UInputSettings* IS = UInputSettings::GetInputSettings();
			return IS && IS->bUseMouseForTouch;
		};
		ImSlate::FImSlate3DHost::Install(MoveTemp(Hooks));
#endif
	}

	virtual void ShutdownModule() override
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
		FWorldDelegates::OnWorldCleanup.RemoveAll(this);
		if (AppDeactivateHandle.IsValid())
			FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(AppDeactivateHandle);
		if (AppBackgroundHandle.IsValid())
			FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(AppBackgroundHandle);

		if (FSlateApplication::IsInitialized() && ScrollProcessor.IsValid())
			FSlateApplication::Get().UnregisterInputPreProcessor(ScrollProcessor);
		ScrollProcessor.Reset();
	}

	// Both lifecycle events route here; the keyboard hide is idempotent and non-creating.
	static void OnAppWillBackground()
	{
		ImSlate::SImSlateVirtualKeyboard::HideOpenKeyboardForAppLifecycle();
	}

private:
	FDelegateHandle AppDeactivateHandle;
	FDelegateHandle AppBackgroundHandle;
	TSharedPtr<ImSlate::FImSlatePanelScrollProcessor> ScrollProcessor;

public:

	void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
	{
		WorldLocalStorages::RemoveLocalValue<ImSlate::FWorldContextRoot>(InWorld);
#if WITH_EDITOR && 0
		for (auto& Item : ImSlate::GetSlateRoots())
		{
			if (Item->RawWorldPtr == InWorld)
			{
				// delete Ctx;
				break;
			}
		}
#endif
	}

	void OnGameInstanceStart(UWorld* World)
	{
#if WITH_EDITOR
		if (!ensure(World) || !World->IsGameWorld())
			return;
#else
		if (!World)
			return;
#endif
		auto OnViewportCreatedCb = [World] { ImSlate::FWorldContextRoot::OnWorldViewport(World); };
		if (auto Viewport = World->GetGameViewport())
		{
#if WITH_EDITOR
			CallOnWorldNextTick(World, MoveTemp(OnViewportCreatedCb));
#else
			OnViewportCreatedCb();
#endif
		}
		else
		{
			UGameViewportClient::OnViewportCreated().AddWeakLambda(World, MoveTemp(OnViewportCreatedCb));
		}
	}
};

namespace ImSlate
{
UWorld* GetWorldChecked(const UObject* InCtx)
{
	auto World = GEngine->GetWorldFromContextObject(InCtx, EGetWorldErrorMode::ReturnNull);
	World = World ? World : GWorld;
#if WITH_EDITOR
	if (GIsEditor && (GIsPlayInEditorWorld || (GEditor && GEditor->PlayWorld != nullptr)))
	{
		static auto FindFirstPIEWorld = [] {
			UWorld* World = nullptr;
			auto& WorldContexts = GEngine->GetWorldContexts();
			for (const FWorldContext& Context : WorldContexts)
			{
				auto CurWorld = Context.World();
				if (IsValid(CurWorld) && (CurWorld->WorldType == EWorldType::PIE /* || CurWorld->WorldType == EWorldType::Game*/))
				{
					World = CurWorld;
					break;
				}
			}

			ensure(World);
			return World;
		};

		FWorldContext* WorldContext = GEngine->GetWorldContextFromPIEInstance(FMath::Max(0, UE::GetPlayInEditorID()));
		if (ensure(WorldContext && (WorldContext->WorldType == EWorldType::PIE /* || WorldContext->WorldType == EWorldType::Game*/)))
		{
			World = WorldContext->World();
		}
		else
		{
			World = FindFirstPIEWorld();
		}
	}
#else
	check(World && World->IsGameWorld());
#endif
	return World;
}
IMSLATE_API UWorld* GetWorldChecked(ImSlateContext* g)
{
	return g->GetWorldChecked();
}

FWorldContextRoot* GetGImSlate(const UObject* InObj)
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		UWorld* World = GEngine->GetWorldFromContextObject(InObj, EGetWorldErrorMode::ReturnNull);
		World = World ? World : GetWorldChecked();
		for (auto& Item : GetSlateRoots())
		{
			if (Item->RawWorldPtr == World)
			{
				return Item;
			}
		}
		return &WorldLocalStorages::GetLocalValue<FWorldContextRoot>(World, World);
	}
	else
#endif
	{
		return static_cast<FWorldContextRoot*>(GetGlobalImSlate());
	}
}
ImSlateContext* GetGImSlate()
{
	return GetGImSlate(nullptr);
}

// Exported per-world accessor: returns the ImSlate context bound to InObj's world (PIE-safe),
// upcast to the public base type so callers outside this TU can use it (e.g. to find the game
// viewport / virtual keyboard for the correct client world under "Play As Client").
IMSLATE_API ImSlateContext* GetGImSlateForWorld(const UObject* InObj)
{
	return GetGImSlate(InObj);
}

TSharedPtr<void> ImSlateTicker::BindDelegate(ImSlateTicker::FOnTickWithWorld Delegate, UWorld* InWorld)
{
	if (auto* Ptr = GetGImSlate(InWorld))
	{
		auto Elm = Ptr->ImSlateSignals.Connect((UObject*)nullptr, MoveTemp(Delegate));
		return Ptr->ImSlateSignals.BindSignalConnection(Elm);
	}
	return nullptr;
}
TSharedPtr<void> ImSlateTicker::BindDelegate(ImSlateTicker::FOnTick Delegate, UWorld* InWorld)
{
	if (auto* Ptr = GetGImSlate(InWorld))
	{
		auto Elm = Ptr->ImSlateSignals.Connect((UObject*)nullptr, MoveTemp(Delegate));
		return Ptr->ImSlateSignals.BindSignalConnection(Elm);
	}
	return nullptr;
}

}  // namespace ImSlate

IMPLEMENT_MODULE(FImSlateModule, ImSlate)
