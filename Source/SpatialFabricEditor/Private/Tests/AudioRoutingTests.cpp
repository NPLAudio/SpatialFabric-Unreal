// Hardware-free routing/cache/transaction regressions. MIT.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AudioRoutingAuthoring.h"
#include "SceneObjectAuthoring.h"
#include "AudioLoomComponent.h"
#include "UI/SAudioRoutingPanel.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWindow.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/SVirtualWindow.h"
#include "Layout/WidgetPath.h"
#include "Editor/Transactor.h"

namespace
{
TSharedPtr<SWidget> FindRoutingGrid(const TSharedRef<SWidget>& Widget)
{
    if (Widget->GetTag() == FName(TEXT("SpatialFabric.AudioRouting.Grid"))) return Widget;
    FChildren* Children=Widget->GetChildren();
    for (int32 I=0; I<Children->Num(); ++I) if (auto Found=FindRoutingGrid(Children->GetChildAt(I))) return Found;
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioRoutingCacheTest, "SpatialFabric.AudioRouting.CacheValidationOfflineAndRefresh", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioRoutingCacheTest::RunTest(const FString&)
{
    AudioRouting::FCache Cache;
    FString Saved; int32 Saves = 0;
    auto Save = [&](const FString& Json) { Saved = Json; ++Saves; return true; };
    FAudioOutputDeviceInfo D; D.DeviceId = TEXT("Exact:Device/A"); D.FriendlyName = TEXT("Desk"); D.NumChannels = 32; D.SampleRate = 48000; D.bIsValid = true;
    TestTrue(TEXT("Validated explicit refresh saves"), Cache.Accept({D},TEXT(""),Save));
    TestEqual(TEXT("One save"),Saves,1);
    AudioRouting::FCache Reload;
    TestTrue(TEXT("Load inventory without enumeration"), Reload.Load(Saved));
    TestFalse(TEXT("Online is never persisted"),Reload.Devices[0].Online);
    TestEqual(TEXT("Exact IDs preserved"),Reload.Devices[0].Id,D.DeviceId);
    auto Other=D; Other.DeviceId=TEXT("exact:device/a"); Other.FriendlyName=D.FriendlyName;
    TestTrue(TEXT("Case-sensitive IDs with duplicate names remain distinct"),Reload.Accept({D,Other},TEXT(""),[](const FString&) { return true; }));
    TestEqual(TEXT("Exact identity not display name matching"),Reload.Devices.Num(),2);
    TestFalse(TEXT("Fractional schema version rejected"),Reload.Load(TEXT("{\"version\":1.5,\"devices\":[]}")));
    TestFalse(TEXT("Fractional capacity rejected"),Reload.Load(TEXT("{\"version\":1,\"devices\":[{\"id\":\"x\",\"name\":\"x\",\"seen\":\"\",\"channels\":2.5,\"rate\":48000}]}")));
    TestTrue(TEXT("Unplug retains old layout"),Cache.Accept({},TEXT(""),Save));
    TestEqual(TEXT("Unplug retains capacity"),Cache.Devices[0].Channels,32);
    TestFalse(TEXT("Unplug marks offline"),Cache.Devices[0].Online);
    const int32 Before = Saves;
    TestFalse(TEXT("Failed stale result rejected"),Cache.Accept({D},TEXT("enumeration failed"),Save));
    TestEqual(TEXT("No save after error"),Saves,Before);
    TestFalse(TEXT("Duplicate ID rejected atomically"),Cache.Accept({D,D},TEXT(""),Save));
    D.NumChannels = 4097;
    TestFalse(TEXT("Capacity bound"),Cache.Accept({D},TEXT(""),Save));
    D.NumChannels = 2; D.DeviceId = TEXT("bad\nID");
    TestFalse(TEXT("Control characters rejected"),Cache.Accept({D},TEXT(""),Save));
    D.DeviceId = TEXT("Exact:Device/A");
    TestFalse(TEXT("Persistence failure doesn't publish"),Cache.Accept({D},TEXT(""),[](const FString&) { return false; }));
    TestEqual(TEXT("Old capacity remains"),Cache.Devices[0].Channels,32);
    TestFalse(TEXT("Unsupported cache version"),Reload.Load(TEXT("{\"version\":2,\"devices\":[]}")));
    TestTrue(TEXT("Bad schema clears in-memory load only"),Reload.Devices.IsEmpty());
    TestFalse(TEXT("Oversize cache rejected"),Reload.Load(FString::ChrN(AudioRouting::MaxJson+1,TEXT('x'))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioRoutingMatrixTest, "SpatialFabric.AudioRouting.CrosspointMappingAndVisibleGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioRoutingMatrixTest::RunTest(const FString&)
{
    AudioRouting::FInterface D; D.Id = TEXT("Known"); D.Name = TEXT("Desk"); D.Channels = 4096;
    AudioRouting::FMatrix Matrix; Matrix.Capture({}, {D});
    TestEqual(TEXT("Default unknown plus compact 4096-channel group"),Matrix.Columns,4098);
    TestEqual(TEXT("Only group spans, no channel widget/model arrays"),Matrix.Groups.Num(),2);
    int32 Ch=-1;
    TestEqual(TEXT("Last real channel mapping"),Matrix.Column(4097,Ch)->Id,D.Id);
    TestEqual(TEXT("1-based final destination"),Ch,4096);
    Matrix.Column(1,Ch); TestEqual(TEXT("Known Auto column is zero"),Ch,0);
    TestNull(TEXT("Outside rectangle"),Matrix.Column(4098,Ch));
    auto CaseVariant=D; CaseVariant.Id=TEXT("known"); CaseVariant.Channels=2;
    Matrix.Capture({}, {D,CaseVariant});
    TestEqual(TEXT("Matrix groups use exact, case-sensitive IDs"),Matrix.Groups.Num(),3);
    TestFalse(TEXT("Case-sensitive helper differs from FString defaults"),AudioRouting::SameId(D.Id,CaseVariant.Id));
    auto Range = AudioRouting::Visible(68,100,34,1000000);
    TestEqual(TEXT("Horizontal visible first"),Range.First,2);
    TestEqual(TEXT("Horizontal visible end exclusive"),Range.End,5);
    TestTrue(TEXT("Bounded columns, not all endpoints"),Range.End-Range.First <= 4);
    Range = AudioRouting::Visible(0,0,26,1000); TestEqual(TEXT("Fully clipped region paints no rows"),Range.End,0);
    Range = AudioRouting::Visible(26000,100,26,10); TestEqual(TEXT("Scroll beyond content stays bounded"),Range.First,10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioRoutingUndoTest, "SpatialFabric.AudioRouting.OfflineRoutesUndoAndGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioRoutingUndoTest::RunTest(const FString&)
{
    UWorld* Previous=GEditor->GetEditorWorldContext().World();
    UWorld* World=UWorld::CreateWorld(EWorldType::Editor,false);
    GEditor->GetEditorWorldContext().SetCurrentWorld(World);
    ON_SCOPE_EXIT { GEditor->GetEditorWorldContext().SetCurrentWorld(Previous); World->DestroyWorld(false); };
    AActor* A=World->SpawnActor<AActor>(); A->SetFlags(RF_Transactional);
    FText Reason;
    TestTrue(TEXT("Add actual editable native instance"),SceneObjectAuthoring::AddComponents({A},UAudioLoomComponent::StaticClass(),Reason));
    UAudioLoomComponent* C=A->FindComponentByClass<UAudioLoomComponent>();
    if (!C) return false;
    const bool Loop=C->bLoop;
    TestTrue(TEXT("Offline no-inventory route mutation"),AudioRouting::Assign(C,TEXT("Unknown exact ID"),9300,Reason));
    TestEqual(TEXT("Native DeviceId property used"),C->DeviceId,FString(TEXT("Unknown exact ID")));
    TestEqual(TEXT("Unknown out-of-range channel preserved"),C->OutputChannel,9300);
    TestFalse(TEXT("No implicit playback"),C->IsPlaying());
    TestTrue(TEXT("No replay or pending worker"),C->IsRouteEditStopped());
    TestEqual(TEXT("Unrelated loop preserved"),C->bLoop,Loop);
    AudioRouting::FMatrix Matrix; Matrix.Capture({C},{});
    TestEqual(TEXT("Dynamic default distinct from unknown"),Matrix.Groups.Num(),2);
    TestEqual(TEXT("Unknown has no fabricated capacity"),Matrix.Groups[1].Capacity,0);
    TestEqual(TEXT("Unknown Auto and explicit stored destination"),Matrix.Groups[1].Count(),2);
    TestEqual(TEXT("Stored channel maps exactly"),Matrix.Groups[1].ChannelAt(1),9300);
    AudioRouting::FInterface Known; Known.Id=C->DeviceId; Known.Name=TEXT("Cached"); Known.Channels=2;
    Matrix.Capture({C},{Known});
    TestEqual(TEXT("Known out-of-range adds only one stored exception"),Matrix.Groups[1].Count(),4);
    TestEqual(TEXT("Known out-of-range exact mapping"),Matrix.Groups[1].ChannelAt(3),9300);
    GEditor->UndoTransaction();
    TestTrue(TEXT("One Undo restores default ID"),C->DeviceId.IsEmpty());
    TestEqual(TEXT("One Undo restores channel"),C->OutputChannel,0);
    GEditor->RedoTransaction();
    TestEqual(TEXT("Redo restores exact positive channel"),C->OutputChannel,9300);
    TestTrue(TEXT("Changing interface preserves channel"),AudioRouting::Assign(C,TEXT("Another offline ID"),C->OutputChannel,Reason));
    TestEqual(TEXT("No clamp on interface switch"),C->OutputChannel,9300);
    TestTrue(TEXT("Case-only interface change is not a no-op"),AudioRouting::Assign(C,TEXT("another offline ID"),9300,Reason));
    TestTrue(TEXT("Stored route retains exact case"),AudioRouting::SameId(C->DeviceId,TEXT("another offline ID")));
    World->PersistentLevel->bLocked=true;
    TestFalse(TEXT("Fresh level-lock guard"),AudioRouting::Assign(C,TEXT("bad"),1,Reason));
    World->PersistentLevel->bLocked=false;
    UWorld* OldPlay=GEditor->PlayWorld; GEditor->PlayWorld=World;
    TestFalse(TEXT("Fresh PIE guard"),AudioRouting::Assign(C,TEXT("bad"),1,Reason)); GEditor->PlayWorld=OldPlay;
    C->CreationMethod=EComponentCreationMethod::SimpleConstructionScript;
    TestFalse(TEXT("Inherited/construction-script target protected"),AudioRouting::Assign(C,TEXT("bad"),1,Reason)); C->CreationMethod=EComponentCreationMethod::Instance;
    TestFalse(TEXT("Negative channel rejected"),AudioRouting::Assign(C,TEXT("valid"),-1,Reason));
    TestFalse(TEXT("Destroyed target rejected"),AudioRouting::Assign(nullptr,TEXT("valid"),1,Reason));
    {
        auto Panel=SNew(SAudioRoutingPanel);
        TestFalse(TEXT("Actual central panel construction does not start audio"),C->IsPlaying());
        auto Grid=FindRoutingGrid(Panel);
        if (!TestTrue(TEXT("Actual default matrix widget is present"),Grid.IsValid())) return false;
        AudioRouting::FInterface Device; Device.Id=TEXT("Huge"); Device.Name=TEXT("Offline huge interface"); Device.Channels=4096;
        Panel->TestMatrix().Capture({C},{Device});
        const auto Row=Panel->TestMatrix().Rows[0];
        for (int32 I=1; I<10000; ++I) Panel->TestMatrix().Rows.Add(Row);
        auto Window=SNew(SWindow).ClientSize(FVector2D(800,500));
        FSlateWindowElementList Elements(Window);
        FHittestGrid Hittest;
        FPaintArgs Args(nullptr,Hittest,FVector2D::ZeroVector,0,0);
        const FGeometry Geometry=FGeometry::MakeRoot(FVector2D(800,500),FSlateLayoutTransform());
        Grid->SlatePrepass(1.f);
        Grid->Paint(Args,Geometry,FSlateRect(0,0,800,500),Elements,0,FWidgetStyle(),true);
        const int32 Boxes=Elements.GetUncachedDrawElements().Get<(uint8)EElementType::ET_Box>().Num();
        TestTrue(TEXT("Actual paint bounded by viewport, not 41 million crosspoints"),Boxes > 1 && Boxes < 1000);
        // Repaint under a narrow ancestor clip; hit testing uses the same painted culling region.
        FSlateWindowElementList Clipped(Window);
        Grid->Paint(Args,Geometry,FSlateRect(220,100,300,180),Clipped,0,FWidgetStyle(),true);
        const FPointerEvent Outside(0,FVector2D(400,120),FVector2D(400,120),TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
        TestFalse(TEXT("Ancestor-clipped cells reject pointer down"),Grid->OnMouseButtonDown(Geometry,Outside).IsEventHandled());
        // No native window, application tick, OS cursor, or real-user capture. The
        // Slate reply processing applies capture/release to an isolated virtual user.
        // Deliberately not ProcessMouseButtonDownEvent (which operates on native focus/input).
        if (!TestTrue(TEXT("Slate is initialized"),FSlateApplication::IsInitialized())) return false;
        FSlateApplication& Slate=FSlateApplication::Get();
        // Public virtual-user API owns registration and unregisters on handle destruction.
        const auto VirtualUser=Slate.FindOrCreateVirtualUser(32);
        const int32 UserIndex=VirtualUser->GetUserIndex();
        const auto User=Slate.GetUser(UserIndex);
        auto VirtualWindow=SNew(SVirtualWindow).Size(FVector2D(800,700));
        VirtualWindow->SetContent(Panel);
        Slate.RegisterVirtualWindow(VirtualWindow);
        ON_SCOPE_EXIT
        {
            User->ReleaseAllCapture();
            Slate.UnregisterVirtualWindow(VirtualWindow);
        };
        VirtualWindow->SlatePrepass(1.f);
        FArrangedChildren Root(EVisibility::Visible);
        Root.AddWidget(FArrangedWidget(VirtualWindow,VirtualWindow->GetWindowGeometryInScreen()));
        FWidgetPath Path(VirtualWindow,Root);
        if (!TestTrue(TEXT("Real virtual-window hierarchy resolves grid path"),Path.ExtendPathTo(FWidgetMatcher(Grid.ToSharedRef())))) return false;
        const FGeometry G=Path.Widgets.Last().Geometry;
        auto Paint=[&](const FSlateRect& Clip)
        {
            FSlateWindowElementList Draw(VirtualWindow);
            Grid->Paint(Args,G,Clip,Draw,0,FWidgetStyle(),true);
        };
        auto ResetMatrix=[&]()
        {
            Panel->TestMatrix().Capture({C},{Device});
            const auto First=Panel->TestMatrix().Rows[0];
            for (int32 I=1; I<10000; ++I) Panel->TestMatrix().Rows.Add(First);
            Paint(G.GetRenderBoundingRect());
        };
        auto Event=[&](FVector2D Local, bool Down, float Wheel=0.f, bool Shift=false)
        {
            const FVector2D P=G.LocalToAbsolute(Local);
            return FPointerEvent(uint32(UserIndex),FSlateApplication::CursorPointerIndex,P,P,
                Down ? TSet<FKey>{EKeys::LeftMouseButton} : TSet<FKey>{},EKeys::LeftMouseButton,Wheel,
                FModifierKeysState(Shift,false,false,false,false,false,false,false,false));
        };
        auto Down=[&](FVector2D P)
        {
            const auto E=Event(P,true);
            const FReply Reply=Grid->OnMouseButtonDown(G,E);
            // A hierarchy-only path has no VirtualPointerPositions for pointer
            // tunneling. Process replies directly for real per-user capture,
            // without native-window focus or OS input under -NullRHI.
            Slate.ProcessReply(Path,Reply,&Path,&E,UserIndex);
            return Reply.IsEventHandled();
        };
        auto Up=[&](FVector2D P)
        {
            const auto E=Event(P,false);
            const FReply Reply=Grid->OnMouseButtonUp(G,E);
            Slate.ProcessReply(Path,Reply,&Path,&E,UserIndex);
            return Reply.IsEventHandled();
        };
        auto Wheel=[&](float Delta, bool Horizontal)
        {
            return Grid->OnMouseWheel(G,Event(FVector2D(305,113),false,Delta,Horizontal)).IsEventHandled();
        };
        const FVector2D Cell(305,113), OtherCell(237,113);
        ResetMatrix();
        TestTrue(TEXT("Visible down handled"),Down(Cell));
        TestTrue(TEXT("Routed reply establishes real Slate capture"),Grid->HasMouseCaptureByUser(UserIndex,FSlateApplication::CursorPointerIndex));
        TestEqual(TEXT("Captured down alone never mutates"),C->OutputChannel,9300);
        TestTrue(TEXT("Captured up handled"),Up(Cell));
        TestTrue(TEXT("Up authors exact interface"),AudioRouting::SameId(C->DeviceId,Device.Id));
        TestEqual(TEXT("Up authors actual destination"),C->OutputChannel,1);
        TestFalse(TEXT("Release reply clears capture"),User->HasAnyCapture());
        const int32 Transactions=GEditor->Trans->GetQueueLength();
        ResetMatrix(); Down(Cell);
        TestEqual(TEXT("Exactly configured down creates no transaction"),GEditor->Trans->GetQueueLength(),Transactions);
        TestTrue(TEXT("Exactly configured down preserves route"),AudioRouting::SameId(C->DeviceId,Device.Id) && C->OutputChannel==1);
        Up(Cell);
        TestEqual(TEXT("Configured click creates no transaction"),GEditor->Trans->GetQueueLength(),Transactions);
        GEditor->UndoTransaction();
        TestTrue(TEXT("One Undo restores pre-click exact ID"),AudioRouting::SameId(C->DeviceId,TEXT("another offline ID")));
        TestEqual(TEXT("One Undo restores pre-click destination"),C->OutputChannel,9300);
        ResetMatrix();
        auto Unchanged=[&](const TCHAR* What)
        {
            TestTrue(What,AudioRouting::SameId(C->DeviceId,TEXT("another offline ID")) && C->OutputChannel==9300);
            TestFalse(TEXT("Canceled gesture releases capture"),User->HasAnyCapture());
        };
        Down(Cell); User->ReleaseAllCapture(); Up(Cell);
        Unchanged(TEXT("Real capture loss cannot assign"));
        Down(Cell); Grid->OnMouseMove(G,Event(OtherCell,true)); Up(OtherCell);
        Unchanged(TEXT("Move to another crosspoint cannot assign"));
        Down(Cell); Grid->OnMouseMove(G,Event(OtherCell,true)); Grid->OnMouseMove(G,Event(Cell,true)); Up(Cell);
        Unchanged(TEXT("Move away and back cancels pending click"));
        Down(Cell); Wheel(-1,false); Wheel(1,false); Up(Cell);
        Unchanged(TEXT("Scroll away and back cancels pending click"));
        Down(Cell); Panel->TestRefreshCachedMatrix(); ResetMatrix(); Up(Cell);
        Unchanged(TEXT("Explicit cached-model refresh cancels even same-target click"));
        Down(Cell);
        Panel->TestMatrix().Groups[1].Id=TEXT("Replaced interface");
        Up(Cell);
        Unchanged(TEXT("Changed column identity cannot retarget click"));
        ResetMatrix(); Down(Cell);
        Panel->TestMatrix().Rows[0].Component.Reset();
        Up(Cell);
        Unchanged(TEXT("Removed row target cannot assign"));
        ResetMatrix(); Down(Cell);
        C->OutputChannel=9301;
        Up(Cell);
        TestEqual(TEXT("Fresh live route change is not overwritten"),C->OutputChannel,9301);
        C->OutputChannel=9300;
        ResetMatrix(); Down(Cell); World->PersistentLevel->bLocked=true; Up(Cell); World->PersistentLevel->bLocked=false;
        Unchanged(TEXT("Fresh authoring guard rejects level locked after press"));
        const FVector2D ClipMin=G.LocalToAbsolute(FVector2D(220,100));
        const FVector2D ClipMax=G.LocalToAbsolute(FVector2D(254,126));
        Paint(FSlateRect(ClipMin.X,ClipMin.Y,ClipMax.X,ClipMax.Y));
        TestFalse(TEXT("Ancestor-clipped routed down remains unhandled"),Down(Cell));
        Up(Cell); Unchanged(TEXT("Clipped gesture cannot mutate"));
        ResetMatrix(); Down(Cell);
        Paint(FSlateRect(ClipMin.X,ClipMin.Y,ClipMax.X,ClipMax.Y));
        TestFalse(TEXT("Captured vertical wheel outside ancestor clip propagates"),Wheel(-1,false));
        TestFalse(TEXT("Captured horizontal wheel outside ancestor clip propagates"),Wheel(-1,true));
        Up(Cell); Unchanged(TEXT("Ancestor clipping between captured down/up prevents mutation"));
        ResetMatrix();
        for (bool Horizontal : {false,true})
        {
            TestFalse(TEXT("Wheel at start boundary propagates"),Wheel(1,Horizontal));
            TestTrue(TEXT("Wheel into content is consumed"),Wheel(-1,Horizontal));
            TestTrue(TEXT("Wheel back to start is consumed"),Wheel(1,Horizontal));
            TestFalse(TEXT("Wheel unchanged at start propagates"),Wheel(1,Horizontal));
            TestTrue(TEXT("Large wheel reaches far boundary"),Wheel(-1000000,Horizontal));
            TestFalse(TEXT("Wheel unchanged at far boundary propagates"),Wheel(-1,Horizontal));
            TestTrue(TEXT("Wheel back from far boundary is consumed"),Wheel(1000000,Horizontal));
        }
        UAudioLoomComponent* Expiring=NewObject<UAudioLoomComponent>(A);
        ResetMatrix(); Down(Cell);
        Panel->TestMatrix().Rows[0].Component=Expiring;
        Up(Cell);
        Unchanged(TEXT("Replacement row identity cannot complete original press"));
        TestTrue(TEXT("Replacement component route remains default"),Expiring->DeviceId.IsEmpty() && Expiring->OutputChannel==0);
        Panel->TestMatrix().Capture({Expiring},{Device});
        Paint(G.GetRenderBoundingRect());
        Down(Cell);
        const TWeakObjectPtr<UAudioLoomComponent> WeakExpiring=Expiring;
        Expiring->DestroyComponent();
        TestFalse(TEXT("Destroyed component weak target really expires"),WeakExpiring.IsValid());
        const int32 BeforeExpiredUp=GEditor->Trans->GetQueueLength();
        Up(Cell);
        TestEqual(TEXT("Expired captured target creates no transaction"),GEditor->Trans->GetQueueLength(),BeforeExpiredUp);
        Unchanged(TEXT("Expired target cannot mutate another component"));
        TestTrue(TEXT("Interaction fixture never starts playback"),C->IsRouteEditStopped());
    }
    return true;
}
#endif
