// One painted matrix, not one Slate widget per crosspoint. MIT.
#include "UI/SAudioRoutingPanel.h"
#include "AudioLoomComponent.h"
#include "SceneObjectAuthoring.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

class SAudioRoutingGrid : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAudioRoutingGrid) {} SLATE_END_ARGS()
    AudioRouting::FMatrix* Matrix = nullptr;
    TFunction<void(UAudioLoomComponent*)> Select;
    TFunction<void(UAudioLoomComponent*, const FString&, int32)> Assign;
    FVector2D Scroll = FVector2D::ZeroVector;
    mutable FVector2D Body = FVector2D(1,1);
    mutable FSlateRect Clip;
    TWeakObjectPtr<UAudioLoomComponent> Pressed;
    FString PressedId;
    int32 PressedChannel = -1;
    FString PressedOriginalId;
    int32 PressedOriginalChannel = -1;
    void CancelPress() { Pressed.Reset(); }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { CancelPress(); }
    static constexpr float HeaderX = 220, HeaderY = 100, CW = 34, RH = 26;
    void Construct(const FArguments&) { SetTag(TEXT("SpatialFabric.AudioRouting.Grid")); SetClipping(EWidgetClipping::ClipToBounds); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(800,400); }
    void Clamp()
    {
        Scroll.X = FMath::Clamp(Scroll.X, 0., FMath::Max(0., Matrix->Columns * CW - Body.X));
        Scroll.Y = FMath::Clamp(Scroll.Y, 0., FMath::Max(0., Matrix->Rows.Num() * RH - Body.Y));
    }
    void ScrollTo(bool Horizontal, float Fraction)
    {
        (Horizontal ? Scroll.X : Scroll.Y) = Fraction * (Horizontal ? Matrix->Columns * CW : Matrix->Rows.Num() * RH);
        Clamp(); CancelPress(); Invalidate(EInvalidateWidgetReason::Paint);
    }
    void UpdateBar(const TSharedPtr<SScrollBar>& Bar, bool Horizontal)
    {
        const float Total = Horizontal ? Matrix->Columns * CW : Matrix->Rows.Num() * RH;
        Bar->SetState(Total > 0 ? (Horizontal ? Scroll.X : Scroll.Y) / Total : 0,
            Total > 0 ? FMath::Min(1.f, float(Horizontal ? Body.X : Body.Y) / Total) : 1);
    }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect& Culling,
        FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
    {
        if (!Matrix) return Layer;
        Body = FVector2D(FMath::Max(0., G.GetLocalSize().X - HeaderX), FMath::Max(0., G.GetLocalSize().Y - HeaderY));
        const_cast<SAudioRoutingGrid*>(this)->Clamp();
        Clip = Culling.IntersectionWith(G.GetRenderBoundingRect());
        const auto Font = FAppStyle::GetFontStyle("SmallFont");
        auto Region = [&](float X, float Y, float W, float H)
        {
            Out.PushClip(FSlateClippingZone(G.MakeChild(FVector2D(W,H), FSlateLayoutTransform(FVector2D(X,Y)))));
        };
        auto Box = [&](float X, float Y, float W, float H, FLinearColor Color)
        {
            FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(W,H), FSlateLayoutTransform(FVector2f(X,Y))), FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
        };
        auto Text = [&](float X, float Y, const FString& S, FLinearColor Color = FLinearColor::White)
        {
            FSlateDrawElement::MakeText(Out, Layer+1, G.ToPaintGeometry(FVector2f(200,24), FSlateLayoutTransform(FVector2f(X,Y))), S, Font, ESlateDrawEffect::None, Color);
        };
        Box(0,0,G.GetLocalSize().X,G.GetLocalSize().Y,FLinearColor(.08f,.08f,.09f));
        // Convert ancestor culling to local coordinates before computing visible ranges.
        const FVector2D Min = G.AbsoluteToLocal(FVector2D(Clip.Left, Clip.Top));
        const FVector2D Max = G.AbsoluteToLocal(FVector2D(Clip.Right, Clip.Bottom));
        const float Left = FMath::Max(HeaderX, float(Min.X)), Top = FMath::Max(HeaderY, float(Min.Y));
        const float Right = FMath::Min(float(G.GetLocalSize().X), float(Max.X)), Bottom = FMath::Min(float(G.GetLocalSize().Y), float(Max.Y));
        const auto Cols = AudioRouting::Visible(Scroll.X + Left - HeaderX, Right - Left, CW, Matrix->Columns);
        const auto Rows = AudioRouting::Visible(Scroll.Y + Top - HeaderY, Bottom - Top, RH, Matrix->Rows.Num());
        Region(HeaderX,0,Body.X,HeaderY);
        for (const auto& Group : Matrix->Groups)
        {
            float X = HeaderX + Group.Start * CW - Scroll.X, W = Group.Count() * CW;
            if (X + W <= Left || X >= Right) continue;
            const float LabelX = FMath::Max(X, Left), LabelW = FMath::Min(X+W, Right)-LabelX;
            Region(LabelX,0,LabelW,72);
            Text(LabelX+2,4,Group.Title.Left(120));
            Text(LabelX+2,24,Group.Id.IsEmpty() ? TEXT("Dynamic / Auto") : FString::Printf(TEXT("%d cached channels"),Group.Capacity));
            Out.PopClip();
        }
        for (int32 C = Cols.First; C < Cols.End; ++C)
        {
            int32 Ch = 0; const auto* Group = Matrix->Column(C, Ch);
            float X = HeaderX + C * CW - Scroll.X;
            Box(X,74,CW-1,26,FLinearColor(.22f,.23f,.25f));
            Region(X,74,CW-1,26);
            Text(X+2,76,Ch == 0 ? TEXT("Auto") : FString::FromInt(Ch) + (Ch > Group->Capacity ? TEXT("*") : TEXT("")));
            Out.PopClip();
        }
        Out.PopClip();
        Region(0,HeaderY,HeaderX,Body.Y);
        for (int32 R = Rows.First; R < Rows.End; ++R)
        {
            float Y = HeaderY + R * RH - Scroll.Y;
            Box(0,Y,HeaderX-1,RH-1,FLinearColor(.18f,.19f,.21f));
            Text(4,Y+4,Matrix->Rows[R].Name);
        }
        Out.PopClip();
        Region(HeaderX,HeaderY,Body.X,Body.Y);
        for (int32 R = Rows.First; R < Rows.End; ++R) for (int32 C = Cols.First; C < Cols.End; ++C)
        {
            int32 Ch = 0; const auto* Group = Matrix->Column(C, Ch);
            const auto& Row = Matrix->Rows[R];
            float X = HeaderX + C * CW - Scroll.X, Y = HeaderY + R * RH - Scroll.Y;
            const bool Configured = AudioRouting::SameId(Row.Id,Group->Id) && Row.Channel == Ch;
            Box(X,Y,CW-1,RH-1,Configured ? FLinearColor(.04f,.4f,.12f) : FLinearColor(.14f,.15f,.17f));
            if (Configured) Text(X+8,Y+3,TEXT("\u2713"),FLinearColor(.4f,1.f,.5f));
        }
        Out.PopClip();
        return Layer + 2;
    }
    bool Hit(const FGeometry& G, const FPointerEvent& E, int32& R, int32& C) const
    {
        if (!Matrix || !Clip.ContainsPoint(E.GetScreenSpacePosition())) return false;
        const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
        if (P.Y < HeaderY || P.X < 0 || P.X >= G.GetLocalSize().X || P.Y >= G.GetLocalSize().Y) return false;
        R = FMath::FloorToInt((P.Y-HeaderY+Scroll.Y)/RH);
        C = P.X < HeaderX ? -1 : FMath::FloorToInt((P.X-HeaderX+Scroll.X)/CW);
        return Matrix->Rows.IsValidIndex(R) && C < Matrix->Columns;
    }
    virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
    {
        int32 R=-1,C=-1;
        if (E.GetEffectingButton() != EKeys::LeftMouseButton || !Hit(G,E,R,C)) return FReply::Unhandled();
        auto* Component = Matrix->Rows[R].Component.Get(); Select(Component);
        Pressed.Reset();
        if (C >= 0 && Component)
        {
            const auto* Group = Matrix->Column(C,PressedChannel);
            Pressed = Component; PressedId = Group->Id;
            PressedOriginalId = Component->DeviceId; PressedOriginalChannel = Component->OutputChannel;
        }
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override
    {
        if (E.GetEffectingButton() != EKeys::LeftMouseButton || !HasMouseCaptureByUser(E.GetUserIndex(), E.GetPointerIndex())) return FReply::Unhandled();
        int32 R=-1,C=-1,Ch=0;
        if (Hit(G,E,R,C) && C >= 0)
        {
            const auto* Group = Matrix->Column(C,Ch);
            if (Pressed.IsValid() && Matrix->Rows[R].Component == Pressed && AudioRouting::SameId(Group->Id,PressedId) && Ch == PressedChannel
                && AudioRouting::SameId(Pressed->DeviceId,PressedOriginalId) && Pressed->OutputChannel == PressedOriginalChannel)
                Assign(Pressed.Get(),PressedId,Ch);
        }
        Pressed.Reset(); return FReply::Handled().ReleaseMouseCapture();
    }
    virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override
    {
        int32 R=-1,C=-1,Channel=0;
        if (Pressed.IsValid() && (!Hit(G,E,R,C) || C < 0 || Matrix->Rows[R].Component != Pressed
            || !AudioRouting::SameId(Matrix->Column(C,Channel)->Id,PressedId) || Channel != PressedChannel)) CancelPress();
        const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
        const int32 Index = FMath::FloorToInt((P.X-HeaderX+Scroll.X)/CW);
        int32 Ch=0;
        const auto* Group = P.X >= HeaderX && Matrix ? Matrix->Column(Index,Ch) : nullptr;
        SetToolTipText(Group ? FText::FromString(Group->Title + TEXT("\nExact ID: ") + Group->Id + TEXT("\n") +
            (Ch == 0 ? FString(TEXT("Auto 0: corresponding source channels")) : FString::Printf(TEXT("%d: first source → destination%s"),Ch,Ch > Group->Capacity ? TEXT(" (stored, capacity unverified)") : TEXT("")))) : FText::GetEmpty());
        return FReply::Unhandled();
    }
    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& E) override
    {
        // Slate also routes wheel events to the mouse captor after the pointer
        // leaves its visible region. Do not steal scrolling outside the paint clip.
        if (!Clip.ContainsPoint(E.GetScreenSpacePosition())) return FReply::Unhandled();
        const FVector2D Before = Scroll;
        (E.IsShiftDown() ? Scroll.X : Scroll.Y) -= E.GetWheelDelta() * RH * 3;
        Clamp();
        if (Before != Scroll) CancelPress();
        Invalidate(EInvalidateWidgetReason::Paint);
        return Before == Scroll ? FReply::Unhandled() : FReply::Handled();
    }
};

void SAudioRoutingPanel::Construct(const FArguments&)
{
    Cache.LoadDisk(); Capture();
    TSharedPtr<SScrollBar> H, V;
    SAssignNew(Grid, SAudioRoutingGrid);
    Grid->Matrix = &Matrix;
    Grid->Select = [this](UAudioLoomComponent* C) { Selected = C; };
    Grid->Assign = [this](UAudioLoomComponent* C, const FString& Id, int32 Ch) { Selected = C; Apply(Id,Ch); };
    SAssignNew(H,SScrollBar).Orientation(Orient_Horizontal).OnUserScrolled_Lambda([this](float F) { Grid->ScrollTo(true,F); });
    SAssignNew(V,SScrollBar).Orientation(Orient_Vertical).OnUserScrolled_Lambda([this](float F) { Grid->ScrollTo(false,F); });
    ChildSlot [ SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(4) [ SNew(STextBlock).Text(FText::FromString(TEXT("Audio routing crosspoints — green checks = CONFIGURED, not live. Select a row for dropdowns."))) ]
        + SVerticalBox::Slot().AutoHeight().Padding(4) [ SNew(STextBlock).Text(FText::FromString(TEXT("Auto / 0 = corresponding source channels; 1+ = first source → destination. * = stored, capacity unverified. Shift+wheel scrolls horizontally."))) ]
        + SVerticalBox::Slot().AutoHeight().Padding(4) [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth() [ SNew(SButton).Text(FText::FromString(TEXT("Refresh Interfaces"))).OnClicked_Lambda([this]() { Cache.Refresh(); Capture(true); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().FillWidth(1).Padding(8,0) [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Cache.Status); }).AutoWrapText(true) ] ]
        + SVerticalBox::Slot().AutoHeight().Padding(4) [ SNew(STextBlock).Text_Lambda([this]() { return Selected.IsValid() ? FText::FromString(Selected->GetPathName()) : FText::FromString(TEXT("No row selected. All loaded editor-world playback components are listed; no manager required.")); }) ]
        + SVerticalBox::Slot().AutoHeight().Padding(4) [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1) [ SNew(SComboButton).OnGetMenuContent(this,&SAudioRoutingPanel::InterfaceMenu).ButtonContent() [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Selected.IsValid() ? TEXT("Interface: ") + (Selected->DeviceId.IsEmpty() ? FString(TEXT("Dynamic OS default")) : Selected->DeviceId) : FString(TEXT("Audio interface"))); }) ] ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8,0) [ SNew(SComboButton).OnGetMenuContent(this,&SAudioRoutingPanel::ChannelMenu).ButtonContent() [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Selected.IsValid() ? FString::Printf(TEXT("Channel: %d"),Selected->OutputChannel) : TEXT("Output channel")); }) ] ] ]
        + SVerticalBox::Slot().AutoHeight().Padding(4) [ SNew(STextBlock).Text_Lambda([this]() { FText Reason; if (Selected.IsValid() && !AudioRouting::CanAssign(Selected.Get(),Reason)) return Reason; return Message; }).AutoWrapText(true) ]
        + SVerticalBox::Slot().FillHeight(1) [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1) [ Grid.ToSharedRef() ]
            + SHorizontalBox::Slot().AutoWidth() [ V.ToSharedRef() ] ]
        + SVerticalBox::Slot().AutoHeight() [ H.ToSharedRef() ] ];
    RegisterActiveTimer(.25f,FWidgetActiveTimerDelegate::CreateLambda([this,H,V](double, float)
    { Capture(); Grid->UpdateBar(H,true); Grid->UpdateBar(V,false); return EActiveTimerReturnType::Continue; }));
}
void SAudioRoutingPanel::Capture(bool bCancelPendingPress)
{
    if (Grid && bCancelPendingPress) Grid->CancelPress();
    TArray<TWeakObjectPtr<AActor>> Actors;
    SceneObjectAuthoring::GatherActors(SceneObjectAuthoring::EditorWorld(),Actors);
    TArray<UAudioLoomComponent*> Components;
    for (const auto& Actor : Actors) { TArray<UAudioLoomComponent*> Found; Actor->GetComponents(Found); Components.Append(Found); }
    Matrix.Capture(Components,Cache.Devices);
    if (Grid) Grid->Invalidate(EInvalidateWidgetReason::Paint);
}
void SAudioRoutingPanel::Apply(const FString& Id, int32 Channel)
{
    AudioRouting::Assign(Selected.Get(),Id,Channel,Message); Capture();
}
namespace
{
using FInterfaceChoices = TArray<TPair<FString,FString>>;
// Bound every popup level, including scenes with many distinct unknown authored IDs.
void AddInterfaceChoices(FMenuBuilder& Menu, TSharedRef<FInterfaceChoices> Choices,
    int32 First, int32 End, TFunction<void(const FString&)> Choose)
{
    if (End-First <= 64)
    {
        for (int32 I=First; I<End; ++I)
        {
            const auto Choice=(*Choices)[I];
            Menu.AddMenuEntry(FText::FromString(Choice.Value),FText::FromString(Choice.Key),FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([Choose,Choice]() { Choose(Choice.Key); })));
        }
        return;
    }
    int32 Chunk=64;
    while (FMath::DivideAndRoundUp(End-First,Chunk) > 64) Chunk*=64;
    for (int32 Start=First; Start<End; Start+=Chunk)
    {
        const int32 Last=FMath::Min(Start+Chunk,End);
        Menu.AddSubMenu(FText::FromString(FString::Printf(TEXT("Interfaces %d–%d"),Start+1,Last)),FText(),
            FNewMenuDelegate::CreateLambda([Choices,Start,Last,Choose](FMenuBuilder& Sub) { AddInterfaceChoices(Sub,Choices,Start,Last,Choose); }));
    }
}
}
TSharedRef<SWidget> SAudioRoutingPanel::InterfaceMenu()
{
    FMenuBuilder Menu(true,nullptr);
    const TWeakObjectPtr<UAudioLoomComponent> Target = Selected;
    const TWeakPtr<SAudioRoutingPanel> Panel=SharedThis(this);
    auto Choices=MakeShared<FInterfaceChoices>();
    for (const auto& G : Matrix.Groups) Choices->Emplace(G.Id,G.Title);
    AddInterfaceChoices(Menu,Choices,0,Choices->Num(),[Panel,Target](const FString& Id)
    { if (auto P=Panel.Pin(); P && Target.IsValid()) { P->Selected=Target; P->Apply(Id,Target->OutputChannel); } });
    return Menu.MakeWidget();
}
TSharedRef<SWidget> SAudioRoutingPanel::ChannelMenu()
{
    // A lazy submenu per 64 destinations keeps popup widget count bounded even at 4096 channels.
    FMenuBuilder Menu(true,nullptr);
    const TWeakObjectPtr<UAudioLoomComponent> Target = Selected;
    const auto* Group = Matrix.Groups.FindByPredicate([&](const AudioRouting::FGroup& G) { return Target.IsValid() && AudioRouting::SameId(G.Id,Target->DeviceId); });
    if (!Group) return Menu.MakeWidget();
    const TWeakPtr<SAudioRoutingPanel> Panel=SharedThis(this);
    auto Add = [Panel,Target](FMenuBuilder& M, int32 Ch)
    {
        M.AddMenuEntry(FText::FromString(Ch == 0 ? TEXT("0 — corresponding source channels") : FString::Printf(TEXT("%d — first source → output"),Ch)), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Panel,Target,Ch]()
        { if (auto P=Panel.Pin(); P && Target.IsValid()) { P->Selected=Target; P->Apply(Target->DeviceId,Ch); } })));
    };
    Add(Menu,0);
    for (int32 First=1; First<=Group->Capacity; First+=64)
    {
        const int32 Last=FMath::Min(First+63,Group->Capacity);
        Menu.AddSubMenu(FText::FromString(FString::Printf(TEXT("Outputs %d–%d"),First,Last)),FText(),FNewMenuDelegate::CreateLambda([Add,First,Last](FMenuBuilder& M) { for (int32 Ch=First; Ch<=Last; ++Ch) Add(M,Ch); }));
    }
    if (Target.IsValid() && Target->OutputChannel > Group->Capacity) Add(Menu,Target->OutputChannel);
    return Menu.MakeWidget();
}
