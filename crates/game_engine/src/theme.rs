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
    fn build(&self, app: &mut App) { app.add_systems(Update, hover); }
}

fn hover(mut q: Query<(&Interaction, &Primary, &mut BorderColor), Changed<Interaction>>) {
    for (i, p, mut b) in &mut q {
        b.0 = match (i, p.0) { (Interaction::None, true) => EMBER, (Interaction::None, false) => LINE, _ => LINE_HI };
    }
}
