//! The 0.9.0.90 look (styles.css :root palette) + small UI builders shared by every screen.
use bevy::ecs::children;
use bevy::prelude::*;

pub const fn rgba(h: u32, a: f32) -> Color {
    Color::srgba(((h >> 16) & 255) as f32 / 255.0, ((h >> 8) & 255) as f32 / 255.0, (h & 255) as f32 / 255.0, a)
}
pub const INK: Color = rgba(0x0f0c0a, 1.0);
pub const PANEL: Color = rgba(0x1b1612, 0.95);
pub const SLOT: Color = rgba(0x221c17, 1.0);
pub const SLOT_HI: Color = rgba(0x3a3027, 1.0);
pub const LINE: Color = rgba(0x4d3f33, 1.0);
pub const LINE_HI: Color = rgba(0x7c6650, 1.0);
pub const TEXT: Color = rgba(0xefe4cc, 1.0);
pub const MUTED: Color = rgba(0xb3a48c, 1.0);
pub const EMBER: Color = rgba(0xff8a3d, 1.0);
pub const EMBER_DEEP: Color = rgba(0xc4521b, 1.0);
pub const HEART: Color = rgba(0xe5484d, 1.0);
pub const FOOD: Color = rgba(0xd99a4e, 1.0);

pub fn text(s: impl Into<String>, size: f32, c: Color) -> (Text, TextFont, TextColor) {
    (Text::new(s), TextFont { font_size: size, ..default() }, TextColor(c))
}

/// Marks ember (primary) buttons so hover can restore their border.
#[derive(Component)]
pub struct Primary(pub bool);

/// 0.9-style button: bordered dark box, or ember when `primary`.
pub fn button<T: Component>(tag: T, label: &str, width: f32, primary: bool) -> impl Bundle {
    (
        tag,
        Button,
        Primary(primary),
        Node { width: Val::Px(width), height: Val::Px(44.0), justify_content: JustifyContent::Center, align_items: AlignItems::Center, border: UiRect::all(Val::Px(2.0)), ..default() },
        BackgroundColor(if primary { EMBER_DEEP } else { SLOT }),
        BorderColor(if primary { EMBER } else { LINE }),
        children![text(label.to_string(), 20.0, if primary { INK } else { TEXT })],
    )
}

/// Dark bordered panel (column).
pub fn panel_node(gap: f32) -> (Node, BackgroundColor, BorderColor) {
    (
        Node { flex_direction: FlexDirection::Column, align_items: AlignItems::Center, padding: UiRect::all(Val::Px(14.0)), row_gap: Val::Px(gap), border: UiRect::all(Val::Px(2.0)), ..default() },
        BackgroundColor(PANEL),
        BorderColor(LINE),
    )
}

pub struct ThemePlugin;
impl Plugin for ThemePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Focus>().init_resource::<PadClick>().add_systems(Update, hover).add_systems(PreUpdate, nav.after(bevy::ui::UiSystem::Focus));
    }
}

fn hover(mut q: Query<(&Interaction, &Primary, &mut BorderColor), Changed<Interaction>>) {
    for (i, p, mut b) in &mut q {
        b.0 = match (i, p.0) { (Interaction::None, true) => EMBER, (Interaction::None, false) => LINE, _ => LINE_HI };
    }
}

/// The button the keyboard / controller is on (D-pad, left stick or arrows move; A / Enter press).
#[derive(Resource, Default)]
pub struct Focus(pub Option<Entity>, f32);
/// A / X on an inventory slot = left / right click (read by invui).
#[derive(Resource, Default)]
pub struct PadClick {
    pub target: Option<Entity>,
    pub right: bool,
}

#[allow(clippy::too_many_arguments)]
fn nav(
    time: Res<Time>,
    pads: Query<&Gamepad>,
    keys: Res<ButtonInput<KeyCode>>,
    buttons: Query<(Entity, &GlobalTransform, &InheritedVisibility), With<Button>>,
    mut focus: ResMut<Focus>,
    mut inter: Query<&mut Interaction>,
    mut borders: Query<(&mut BorderColor, &Primary)>,
    mut click: ResMut<PadClick>,
) {
    *click = PadClick::default();
    let live: Vec<(Entity, Vec2)> = buttons.iter().filter(|(_, _, v)| v.get()).map(|(e, t, _)| (e, t.translation().truncate())).collect();
    if live.is_empty() { focus.0 = None; return; }
    let mut dir = Vec2::ZERO;
    for (k, d) in [(KeyCode::ArrowUp, Vec2::NEG_Y), (KeyCode::ArrowDown, Vec2::Y), (KeyCode::ArrowLeft, Vec2::NEG_X), (KeyCode::ArrowRight, Vec2::X)] {
        if keys.just_pressed(k) { dir += d; }
    }
    let (mut ok, mut alt, mut any_pad) = (keys.just_pressed(KeyCode::Enter), false, false);
    focus.1 -= time.delta_secs();
    for g in &pads {
        any_pad = true;
        for (b, d) in [(GamepadButton::DPadUp, Vec2::NEG_Y), (GamepadButton::DPadDown, Vec2::Y), (GamepadButton::DPadLeft, Vec2::NEG_X), (GamepadButton::DPadRight, Vec2::X)] {
            if g.just_pressed(b) { dir += d; }
        }
        let s = g.left_stick();
        if s.length() > 0.6 && focus.1 <= 0.0 { dir += Vec2::new(s.x, -s.y); focus.1 = 0.22; }
        ok |= g.just_pressed(GamepadButton::South);
        alt |= g.just_pressed(GamepadButton::West);
    }
    let mut cur = focus.0.filter(|e| live.iter().any(|(x, _)| x == e));
    if cur.is_none() {
        if any_pad || dir != Vec2::ZERO || ok {
            cur = live.iter().min_by(|a, b| (a.1.y, a.1.x).partial_cmp(&(b.1.y, b.1.x)).unwrap_or(std::cmp::Ordering::Equal)).map(|x| x.0);
        }
    } else if dir != Vec2::ZERO {
        let c = cur.unwrap_or(Entity::PLACEHOLDER);
        let cp = live.iter().find(|x| x.0 == c).map(|x| x.1).unwrap_or_default();
        let d = dir.normalize_or_zero();
        let best = live.iter().filter(|x| x.0 != c).filter_map(|x| {
            let v = x.1 - cp;
            let along = v.dot(d);
            (along > 4.0).then(|| (x.0, along + (v - d * along).length() * 2.5))
        }).min_by(|a, b| a.1.partial_cmp(&b.1).unwrap_or(std::cmp::Ordering::Equal));
        if let Some(n) = best { cur = Some(n.0); }
    }
    if cur != focus.0 {
        for (e, on) in [(focus.0, false), (cur, true)] {
            if let Some(e) = e {
                if let Ok((mut b, p)) = borders.get_mut(e) { b.0 = if on { if p.0 { TEXT } else { EMBER } } else if p.0 { EMBER } else { LINE }; }
            }
        }
    }
    focus.0 = cur;
    if let Some(e) = cur {
        if ok || alt { *click = PadClick { target: Some(e), right: alt }; }
        if ok { if let Ok(mut i) = inter.get_mut(e) { *i = Interaction::Pressed; } }
    }
}
