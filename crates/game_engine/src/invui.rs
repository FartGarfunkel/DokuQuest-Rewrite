//! Inventory, crafting table and furnace screens (ui/invui.js) in the 0.9 slot style.
//! Left click: pick up / drop / merge / swap. Right click: half / one. Result slot crafts.
use crate::hud::Icons;
use crate::player::{Player, Ui};
use crate::theme::*;
use crate::AppState;
use bevy::prelude::*;
use bevy::window::PrimaryWindow;
use common_server::items::{click, consume, craft, reg, Slot, Stack};
use std::collections::HashMap;

#[derive(Component, Clone, Copy, PartialEq, Eq, Debug)]
pub enum SlotRef {
    Inv(usize),
    Craft(usize),
    Result,
    FIn,
    FFuel,
    FOut,
}

#[derive(Component)]
struct InvRoot;
#[derive(Component)]
struct SlotIcon(SlotRef);
#[derive(Component)]
struct SlotCount(SlotRef);
#[derive(Component)]
struct CursorIcon;
#[derive(Component)]
struct FurnaceText;

#[derive(Default, Clone)]
pub struct Furnace {
    pub input: Slot,
    pub fuel: Slot,
    pub out: Slot,
    pub burn: f32,
    pub prog: f32,
}

#[derive(Resource, Default)]
pub struct InvState {
    pub cursor: Slot,
    pub craft: [Slot; 9],
    pub furnaces: HashMap<IVec3, Furnace>,
}

pub struct InvUiPlugin;
impl Plugin for InvUiPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<InvState>()
            .add_systems(Update, (rebuild, clicks, refresh, follow_cursor, smelt).chain().run_if(in_state(AppState::Playing)))
            .add_systems(OnExit(AppState::Playing), close_all);
    }
}

fn grid_w(ui: Ui) -> usize { if ui == Ui::Crafting { 3 } else { 2 } }

fn slot(r: SlotRef) -> impl Bundle {
    (
        Button,
        r,
        Node { width: Val::Px(44.0), height: Val::Px(44.0), border: UiRect::all(Val::Px(2.0)), align_items: AlignItems::Center, justify_content: JustifyContent::Center, ..default() },
        BackgroundColor(SLOT),
        BorderColor(LINE),
        Primary(false),
        bevy::ecs::children![
            (SlotIcon(r), ImageNode::default(), Node { width: Val::Px(32.0), height: Val::Px(32.0), ..default() }, Visibility::Hidden),
            (SlotCount(r), text("", 13.0, TEXT), Node { position_type: PositionType::Absolute, right: Val::Px(2.0), bottom: Val::Px(0.0), ..default() }),
        ],
    )
}

fn row() -> Node { Node { column_gap: Val::Px(2.0), align_items: AlignItems::Center, ..default() } }

fn put_back(st: &mut InvState, p: &mut Player) {
    let InvState { cursor, craft, .. } = st;
    for s in craft.iter_mut().chain(std::iter::once(cursor)) {
        if let Some(x) = s.take() { let _ = p.inv.add(x); }
    }
}

fn close_all(mut commands: Commands, roots: Query<Entity, With<InvRoot>>, mut st: ResMut<InvState>, mut p: ResMut<Player>) {
    for e in &roots { commands.entity(e).despawn(); }
    put_back(&mut st, &mut p);
}

fn rebuild(ui: Res<Ui>, mut commands: Commands, roots: Query<Entity, With<InvRoot>>, mut st: ResMut<InvState>, mut p: ResMut<Player>) {
    if !ui.is_changed() { return; }
    for e in &roots { commands.entity(e).despawn(); }
    let mode = *ui;
    if !matches!(mode, Ui::Inventory | Ui::Crafting | Ui::Furnace(_)) {
        put_back(&mut st, &mut p);
        return;
    }
    let title = match mode { Ui::Crafting => "Crafting Table", Ui::Furnace(_) => "Furnace", _ => "Inventory" };
    let full = Node { position_type: PositionType::Absolute, width: Val::Percent(100.0), height: Val::Percent(100.0), align_items: AlignItems::Center, justify_content: JustifyContent::Center, ..default() };
    commands.spawn((InvRoot, full, BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.45)))).with_children(|root| {
        root.spawn(panel_node(8.0)).with_children(|pn| {
            pn.spawn(text(title, 20.0, TEXT));
            pn.spawn(Node { column_gap: Val::Px(18.0), align_items: AlignItems::Center, ..default() }).with_children(|top| {
                match mode {
                    Ui::Furnace(_) => {
                        top.spawn(Node { flex_direction: FlexDirection::Column, row_gap: Val::Px(6.0), ..default() }).with_children(|c| {
                            c.spawn(slot(SlotRef::FIn));
                            c.spawn(slot(SlotRef::FFuel));
                        });
                        top.spawn((FurnaceText, text("→", 22.0, MUTED)));
                        top.spawn(slot(SlotRef::FOut));
                    }
                    _ => {
                        if mode == Ui::Inventory {
                            top.spawn(Node { flex_direction: FlexDirection::Column, row_gap: Val::Px(2.0), ..default() }).with_children(|c| {
                                for i in 36..40 { c.spawn(slot(SlotRef::Inv(i))); }
                            });
                        }
                        let w = grid_w(mode);
                        top.spawn(Node { flex_direction: FlexDirection::Column, row_gap: Val::Px(2.0), ..default() }).with_children(|g| {
                            for y in 0..w {
                                g.spawn(row()).with_children(|r| { for x in 0..w { r.spawn(slot(SlotRef::Craft(y * w + x))); } });
                            }
                        });
                        top.spawn(text("→", 22.0, MUTED));
                        top.spawn(slot(SlotRef::Result));
                    }
                }
            });
            pn.spawn(Node { flex_direction: FlexDirection::Column, row_gap: Val::Px(2.0), margin: UiRect::top(Val::Px(8.0)), ..default() }).with_children(|g| {
                for y in 0..3 {
                    g.spawn(row()).with_children(|r| { for x in 0..9 { r.spawn(slot(SlotRef::Inv(9 + y * 9 + x))); } });
                }
            });
            pn.spawn(Node { margin: UiRect::top(Val::Px(6.0)), ..row() }).with_children(|r| { for i in 0..9 { r.spawn(slot(SlotRef::Inv(i))); } });
        });
        root.spawn((CursorIcon, ImageNode::default(), Node { position_type: PositionType::Absolute, width: Val::Px(32.0), height: Val::Px(32.0), ..default() }, Visibility::Hidden));
    });
}

fn clicks(mouse: Res<ButtonInput<MouseButton>>, slots: Query<(&Interaction, &SlotRef)>, ui: Res<Ui>, mut st: ResMut<InvState>, mut p: ResMut<Player>) {
    let (l, r) = (mouse.just_pressed(MouseButton::Left), mouse.just_pressed(MouseButton::Right));
    if !l && !r { return; }
    let Some(sr) = slots.iter().find(|(i, _)| **i != Interaction::None).map(|(_, s)| *s) else { return };
    let st = &mut *st;
    let w = grid_w(*ui);
    let n = w * w;
    match sr {
        SlotRef::Inv(i) => click(&mut p.inv.slots[i], &mut st.cursor, r),
        SlotRef::Craft(i) => click(&mut st.craft[i], &mut st.cursor, r),
        SlotRef::Result => {
            if let Some((_, out)) = craft(&st.craft[..n], w) {
                match &mut st.cursor {
                    None => {
                        st.cursor = Some(out);
                        consume(&mut st.craft[..n]);
                    }
                    Some(c) if c.item == out.item && c.count + out.count <= reg().item(c.item).max_stack => {
                        c.count += out.count;
                        consume(&mut st.craft[..n]);
                    }
                    _ => {}
                }
            }
        }
        SlotRef::FIn | SlotRef::FFuel | SlotRef::FOut => {
            let Ui::Furnace(pos) = *ui else { return };
            let f = st.furnaces.entry(pos).or_default();
            match sr {
                SlotRef::FIn => click(&mut f.input, &mut st.cursor, r),
                SlotRef::FFuel => click(&mut f.fuel, &mut st.cursor, r),
                _ => {
                    if let Some(o) = f.out {
                        match &mut st.cursor {
                            None => st.cursor = f.out.take(),
                            Some(c) if c.item == o.item && c.count + o.count <= reg().item(o.item).max_stack => {
                                c.count += o.count;
                                f.out = None;
                            }
                            _ => {}
                        }
                    }
                }
            }
        }
    }
}

#[allow(clippy::type_complexity, clippy::too_many_arguments)]
fn refresh(
    ui: Res<Ui>,
    st: Res<InvState>,
    p: Res<Player>,
    mut icons: ResMut<Icons>,
    mut images: ResMut<Assets<Image>>,
    mut q_icon: Query<(&SlotIcon, &mut ImageNode, &mut Visibility), Without<CursorIcon>>,
    mut q_cnt: Query<(&SlotCount, &mut Text), Without<FurnaceText>>,
    mut ft: Query<&mut Text, (With<FurnaceText>, Without<SlotCount>)>,
) {
    if !matches!(*ui, Ui::Inventory | Ui::Crafting | Ui::Furnace(_)) { return; }
    let w = grid_w(*ui);
    let fur = if let Ui::Furnace(pos) = *ui { st.furnaces.get(&pos).cloned().unwrap_or_default() } else { Furnace::default() };
    let result = craft(&st.craft[..w * w], w).map(|x| x.1);
    let get = |r: SlotRef| -> Slot {
        match r {
            SlotRef::Inv(i) => p.inv.slots[i],
            SlotRef::Craft(i) => st.craft[i],
            SlotRef::Result => result,
            SlotRef::FIn => fur.input,
            SlotRef::FFuel => fur.fuel,
            SlotRef::FOut => fur.out,
        }
    };
    for (s, mut img, mut vis) in &mut q_icon {
        match get(s.0) {
            Some(st) => {
                img.image = icons.get(reg().item(st.item).icon, &mut images);
                *vis = Visibility::Inherited;
            }
            None => *vis = Visibility::Hidden,
        }
    }
    for (s, mut t) in &mut q_cnt {
        let v = get(s.0).filter(|s| s.count > 1).map(|s| s.count.to_string()).unwrap_or_default();
        if t.0 != v { t.0 = v; }
    }
    if let Ok(mut t) = ft.single_mut() {
        t.0 = format!("{} {:>3.0}%", if fur.burn > 0.0 { "🔥" } else { "→" }, fur.prog * 100.0);
    }
}

fn follow_cursor(st: Res<InvState>, win: Query<&Window, With<PrimaryWindow>>, mut icons: ResMut<Icons>, mut images: ResMut<Assets<Image>>, mut q: Query<(&mut Node, &mut ImageNode, &mut Visibility), With<CursorIcon>>) {
    let Ok((mut n, mut img, mut vis)) = q.single_mut() else { return };
    let (Some(s), Some(pos)) = (st.cursor, win.single().ok().and_then(|w| w.cursor_position())) else {
        *vis = Visibility::Hidden;
        return;
    };
    img.image = icons.get(reg().item(s.item).icon, &mut images);
    n.left = Val::Px(pos.x - 16.0);
    n.top = Val::Px(pos.y - 16.0);
    *vis = Visibility::Inherited;
}

fn fuel_secs(item: u16) -> f32 {
    let k = reg().item(item).key.as_str();
    if k.contains("coal") { 80.0 } else if k.contains("log") || k.ends_with("_wood") || k.contains("planks") { 15.0 } else if k == "stick" { 5.0 } else if k.contains("lava_bucket") { 1000.0 } else { 0.0 }
}

/// Furnaces keep smelting while their screen is closed (5 s per item).
fn smelt(time: Res<Time>, mut st: ResMut<InvState>) {
    let dt = time.delta_secs();
    for f in st.furnaces.values_mut() {
        let target = f.input.and_then(|s| reg().smelt(s.item));
        let fits = target.is_some_and(|t| f.out.is_none_or(|o| o.item == t && o.count < reg().item(t).max_stack));
        if f.burn <= 0.0 && fits {
            if let Some(fu) = f.fuel.as_mut() {
                let secs = fuel_secs(fu.item);
                if secs > 0.0 {
                    f.burn = secs;
                    fu.count -= 1;
                    if fu.count == 0 { f.fuel = None; }
                }
            }
        }
        if f.burn > 0.0 {
            f.burn -= dt;
            if fits {
                f.prog += dt / 5.0;
                if f.prog >= 1.0 {
                    f.prog = 0.0;
                    let t = target.unwrap_or(0);
                    if let Some(i) = f.input.as_mut() {
                        i.count -= 1;
                        if i.count == 0 { f.input = None; }
                    }
                    match f.out.as_mut() { Some(o) => o.count += 1, None => f.out = Some(Stack { item: t, count: 1 }) }
                }
            } else {
                f.prog = 0.0;
            }
        } else {
            f.prog = (f.prog - dt).max(0.0);
        }
    }
}
