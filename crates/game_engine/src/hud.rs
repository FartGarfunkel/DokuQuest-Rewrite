//! In-game HUD: crosshair, hotbar, info line, dimension toast.
use crate::player::{Player, HOTBAR};
use crate::AppState;
use bevy::prelude::*;
use common_server::{blocks, dims};

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

#[derive(Component)]
struct HudRoot;
#[derive(Component)]
struct HotbarText;
#[derive(Component)]
struct InfoText;
#[derive(Component)]
struct ToastText;

pub struct HudPlugin;
impl Plugin for HudPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Toast>()
            .add_systems(OnEnter(AppState::Playing), spawn)
            .add_systems(OnExit(AppState::Playing), despawn)
            .add_systems(Update, update.run_if(in_state(AppState::Playing)));
    }
}

fn txt(s: &str, size: f32) -> (Text, TextFont, TextColor) { (Text::new(s), TextFont { font_size: size, ..default() }, TextColor(Color::WHITE)) }

fn spawn(mut commands: Commands) {
    let abs = |top: Option<f32>, bottom: Option<f32>| Node {
        position_type: PositionType::Absolute,
        width: Val::Percent(100.0),
        justify_content: JustifyContent::Center,
        top: top.map(Val::Px).unwrap_or(Val::Auto),
        bottom: bottom.map(Val::Px).unwrap_or(Val::Auto),
        ..default()
    };
    let full = Node { position_type: PositionType::Absolute, width: Val::Percent(100.0), height: Val::Percent(100.0), align_items: AlignItems::Center, justify_content: JustifyContent::Center, ..default() };
    commands.spawn((HudRoot, full)).with_children(|p| { p.spawn(txt("+", 28.0)); });
    commands.spawn((HudRoot, abs(None, Some(16.0)))).with_children(|p| { p.spawn((HotbarText, txt("", 20.0))); });
    commands.spawn((HudRoot, abs(Some(70.0), None))).with_children(|p| { p.spawn((ToastText, txt("", 34.0))); });
    commands
        .spawn((HudRoot, Node { position_type: PositionType::Absolute, top: Val::Px(8.0), left: Val::Px(10.0), ..default() }))
        .with_children(|p| { p.spawn((InfoText, txt("", 15.0))); });
}

fn despawn(mut commands: Commands, q: Query<Entity, With<HudRoot>>) {
    for e in &q { commands.entity(e).despawn(); }
}

#[allow(clippy::type_complexity)]
fn update(
    time: Res<Time>,
    p: Res<Player>,
    mut toast: ResMut<Toast>,
    mut hb: Query<&mut Text, (With<HotbarText>, Without<InfoText>, Without<ToastText>)>,
    mut info: Query<&mut Text, (With<InfoText>, Without<HotbarText>, Without<ToastText>)>,
    mut tt: Query<(&mut Text, &mut TextColor), (With<ToastText>, Without<HotbarText>, Without<InfoText>)>,
) {
    if let Ok(mut t) = hb.single_mut() {
        t.0 = HOTBAR
            .iter()
            .enumerate()
            .map(|(i, b)| {
                let n = if *b == blocks::id::AIR { "Sword" } else { blocks::def(*b).name };
                if i == p.slot { format!("[{} {}]", i + 1, n) } else { format!(" {} {} ", i + 1, n) }
            })
            .collect::<Vec<_>>()
            .join(" ");
    }
    if let Ok(mut t) = info.single_mut() {
        let band = dims::band_at(p.pos.y.floor() as i32).map(|b| b.name).unwrap_or("the Void");
        t.0 = format!(
            "{:.0} {:.0} {:.0} · {} · {} · {:.0} fps\nWASD move · Space jump · F fly · Ctrl sprint · LMB break · RMB place · 1-9 / wheel · Esc save & title",
            p.pos.x, p.pos.y, p.pos.z, band, if p.flying { "flying" } else { "walking" }, 1.0 / time.delta_secs().max(1e-4)
        );
    }
    toast.time -= time.delta_secs();
    if let Ok((mut t, mut c)) = tt.single_mut() {
        t.0 = toast.text.clone();
        c.0 = Color::srgba(1.0, 0.92, 0.7, toast.time.clamp(0.0, 1.0));
    }
}
