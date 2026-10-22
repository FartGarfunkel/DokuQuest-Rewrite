//! HUD (ui/hud.js): crosshair, 9-slot hotbar with item icons, health & hunger bars, mining
//! progress, toasts and an info line. `Icons` turns JS tiles into UI images (shared with invui).
use crate::player::Player;
use crate::theme::*;
use crate::AppState;
use bevy::image::ImageSampler;
use bevy::prelude::*;
use bevy::render::render_resource::{Extent3d, TextureDimension, TextureFormat};
use common_server::{dims, items::reg, tiles};
use std::collections::HashMap;

#[derive(Resource, Default)]
pub struct Toast {
    text: String,
    time: f32,
}
impl Toast {
    pub fn show(&mut self, t: String) {
        self.text = t;
        self.time = 3.5;
    }
}

/// Tile → 16×16 nearest-filtered UI image.
#[derive(Resource, Default)]
pub struct Icons(HashMap<u16, Handle<Image>>);
impl Icons {
    pub fn get(&mut self, tile: u16, images: &mut Assets<Image>) -> Handle<Image> {
        self.0
            .entry(tile)
            .or_insert_with(|| {
                let mut data = Vec::with_capacity(1024);
                for y in 0..16 { for x in 0..16 { data.extend_from_slice(&tiles::px(tile, x, y)); } }
                let mut img = Image::new(Extent3d { width: 16, height: 16, depth_or_array_layers: 1 }, TextureDimension::D2, data, TextureFormat::Rgba8UnormSrgb, Default::default());
                img.sampler = ImageSampler::nearest();
                images.add(img)
            })
            .clone()
    }
}

#[derive(Component)]
struct HudRoot;
#[derive(Component)]
struct HotSlot(usize);
#[derive(Component)]
struct HotIcon(usize);
#[derive(Component)]
struct HotCount(usize);
#[derive(Component)]
struct HealthFill;
#[derive(Component)]
struct FoodFill;
#[derive(Component)]
struct BreakFill;
#[derive(Component)]
struct InfoText;
#[derive(Component)]
struct ToastText;

pub struct HudPlugin;
impl Plugin for HudPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Toast>()
            .init_resource::<Icons>()
            .add_systems(OnEnter(AppState::Playing), spawn)
            .add_systems(OnExit(AppState::Playing), despawn)
            .add_systems(Update, update.run_if(in_state(AppState::Playing)));
    }
}

fn bar<T: Component>(tag: T, colour: Color) -> impl Bundle {
    (
        Node { width: Val::Px(180.0), height: Val::Px(10.0), border: UiRect::all(Val::Px(2.0)), ..default() },
        BackgroundColor(SLOT),
        BorderColor(LINE),
        bevy::ecs::children![(tag, Node { width: Val::Percent(100.0), height: Val::Percent(100.0), ..default() }, BackgroundColor(colour))],
    )
}

fn spawn(mut commands: Commands) {
    let full = Node { position_type: PositionType::Absolute, width: Val::Percent(100.0), height: Val::Percent(100.0), flex_direction: FlexDirection::Column, align_items: AlignItems::Center, justify_content: JustifyContent::Center, ..default() };
    commands.spawn((HudRoot, full)).with_children(|p| {
        p.spawn(text("+", 28.0, TEXT));
        p.spawn((Node { width: Val::Px(60.0), height: Val::Px(5.0), margin: UiRect::top(Val::Px(14.0)), ..default() }, BackgroundColor(SLOT)))
            .with_children(|b| { b.spawn((BreakFill, Node { width: Val::Percent(0.0), height: Val::Percent(100.0), ..default() }, BackgroundColor(EMBER))); });
    });
    commands
        .spawn((HudRoot, Node { position_type: PositionType::Absolute, bottom: Val::Px(10.0), width: Val::Percent(100.0), flex_direction: FlexDirection::Column, align_items: AlignItems::Center, row_gap: Val::Px(6.0), ..default() }))
        .with_children(|p| {
            p.spawn(Node { column_gap: Val::Px(40.0), ..default() }).with_children(|b| {
                b.spawn(bar(HealthFill, HEART));
                b.spawn(bar(FoodFill, FOOD));
            });
            p.spawn(Node { column_gap: Val::Px(2.0), ..default() }).with_children(|row| {
                for i in 0..9 {
                    row.spawn((
                        HotSlot(i),
                        Node { width: Val::Px(46.0), height: Val::Px(46.0), border: UiRect::all(Val::Px(2.0)), align_items: AlignItems::Center, justify_content: JustifyContent::Center, ..default() },
                        BackgroundColor(SLOT.with_alpha(0.85)),
                        BorderColor(LINE),
                    ))
                    .with_children(|s| {
                        s.spawn((HotIcon(i), ImageNode::default(), Node { width: Val::Px(32.0), height: Val::Px(32.0), ..default() }, Visibility::Hidden));
                        s.spawn((HotCount(i), text("", 13.0, TEXT), Node { position_type: PositionType::Absolute, right: Val::Px(2.0), bottom: Val::Px(0.0), ..default() }));
                    });
                }
            });
        });
    commands
        .spawn((HudRoot, Node { position_type: PositionType::Absolute, top: Val::Px(70.0), width: Val::Percent(100.0), justify_content: JustifyContent::Center, ..default() }))
        .with_children(|p| { p.spawn((ToastText, text("", 30.0, TEXT))); });
    commands
        .spawn((HudRoot, Node { position_type: PositionType::Absolute, top: Val::Px(8.0), left: Val::Px(10.0), ..default() }))
        .with_children(|p| { p.spawn((InfoText, text("", 14.0, MUTED))); });
}

fn despawn(mut commands: Commands, q: Query<Entity, With<HudRoot>>) {
    for e in &q { commands.entity(e).despawn(); }
}

#[allow(clippy::type_complexity, clippy::too_many_arguments)]
fn update(
    time: Res<Time>,
    p: Res<Player>,
    mut toast: ResMut<Toast>,
    mut icons: ResMut<Icons>,
    mut images: ResMut<Assets<Image>>,
    mut slots: Query<(&HotSlot, &mut BorderColor)>,
    mut hicons: Query<(&HotIcon, &mut ImageNode, &mut Visibility)>,
    mut counts: Query<(&HotCount, &mut Text), (Without<InfoText>, Without<ToastText>)>,
    mut fills: Query<(&mut Node, Has<HealthFill>, Has<FoodFill>), Or<(With<HealthFill>, With<FoodFill>, With<BreakFill>)>>,
    mut info: Query<&mut Text, (With<InfoText>, Without<HotCount>, Without<ToastText>)>,
    mut tt: Query<(&mut Text, &mut TextColor), (With<ToastText>, Without<HotCount>, Without<InfoText>)>,
) {
    for (s, mut b) in &mut slots { b.0 = if s.0 == p.slot { EMBER } else { LINE }; }
    for (i, mut img, mut vis) in &mut hicons {
        match p.inv.slots[i.0] {
            Some(st) => {
                img.image = icons.get(reg().item(st.item).icon, &mut images);
                *vis = Visibility::Inherited;
            }
            None => *vis = Visibility::Hidden,
        }
    }
    for (c, mut t) in &mut counts {
        let s = p.inv.slots[c.0].filter(|s| s.count > 1).map(|s| s.count.to_string()).unwrap_or_default();
        if t.0 != s { t.0 = s; }
    }
    let brk = p.breaking.map(|b| b.1).unwrap_or(0.0);
    for (mut n, hp, food) in &mut fills {
        let v = if hp { p.health / 20.0 } else if food { p.hunger / 20.0 } else { brk };
        n.width = Val::Percent((v * 100.0).clamp(0.0, 100.0));
    }
    if let Ok(mut t) = info.single_mut() {
        let band = dims::band_at(p.pos.y.floor() as i32).map(|b| b.name).unwrap_or("the Void");
        let held = p.held().map(|s| reg().item(s.item).name.as_str()).unwrap_or("Empty hand");
        t.0 = format!(
            "{:.0} {:.0} {:.0} · {} · {}{} · {:.0} fps\n{}",
            p.pos.x, p.pos.y, p.pos.z, band, if p.creative { "creative" } else { "survival" }, if p.flying { " (flying)" } else { "" },
            1.0 / time.delta_secs().max(1e-4), held
        );
    }
    toast.time -= time.delta_secs();
    if let Ok((mut t, mut c)) = tt.single_mut() {
        if t.0 != toast.text { t.0 = toast.text.clone(); }
        c.0 = TEXT.with_alpha(toast.time.clamp(0.0, 1.0));
    }
}
