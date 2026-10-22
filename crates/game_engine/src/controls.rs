//! Input → one `Controls` snapshot per frame from keyboard + mouse and gamepads
//! (input/controls.js, gamepad.js). Gameplay code only reads `Controls`.
//! Pad: L-stick move, R-stick look, A jump, B sneak/down, RT mine/attack, LT use/place,
//! LB/RB hotbar, Y inventory, Start pause, D-pad up pick block, L3 sprint.
use crate::menu::Settings;
use bevy::input::mouse::{AccumulatedMouseMotion, AccumulatedMouseScroll};
use bevy::input::InputSystem;
use bevy::prelude::*;

#[derive(Resource, Default)]
pub struct Controls {
    pub mv: Vec2,
    /// (yaw, pitch) change this frame, radians.
    pub look: Vec2,
    pub jump: bool,
    pub down: bool,
    pub sprint: bool,
    pub attack: bool,
    pub attack_pressed: bool,
    pub use_pressed: bool,
    pub pick: bool,
    pub slot_delta: i32,
    pub slot_set: Option<usize>,
    pub inventory: bool,
    pub pause: bool,
    pub fly: bool,
    pub creative: bool,
}

pub struct ControlsPlugin;
impl Plugin for ControlsPlugin {
    fn build(&self, app: &mut App) { app.init_resource::<Controls>().add_systems(PreUpdate, gather.after(InputSystem)); }
}

const DIGITS: [KeyCode; 9] = [
    KeyCode::Digit1, KeyCode::Digit2, KeyCode::Digit3, KeyCode::Digit4, KeyCode::Digit5,
    KeyCode::Digit6, KeyCode::Digit7, KeyCode::Digit8, KeyCode::Digit9,
];

#[allow(clippy::too_many_arguments)]
pub fn gather(
    keys: Res<ButtonInput<KeyCode>>,
    mouse: Res<ButtonInput<MouseButton>>,
    mm: Res<AccumulatedMouseMotion>,
    scroll: Res<AccumulatedMouseScroll>,
    pads: Query<&Gamepad>,
    settings: Res<Settings>,
    time: Res<Time>,
    mut c: ResMut<Controls>,
) {
    let k = |code: KeyCode| keys.pressed(code);
    let mut mv = Vec2::ZERO;
    if k(KeyCode::KeyW) { mv.y += 1.0; }
    if k(KeyCode::KeyS) { mv.y -= 1.0; }
    if k(KeyCode::KeyD) { mv.x += 1.0; }
    if k(KeyCode::KeyA) { mv.x -= 1.0; }
    let mut look = -mm.delta * 0.0022 * settings.sens;
    let (mut jump, mut down, mut sprint) = (k(KeyCode::Space), k(KeyCode::ShiftLeft), k(KeyCode::ControlLeft));
    let (mut attack, mut attack_pressed) = (mouse.pressed(MouseButton::Left), mouse.just_pressed(MouseButton::Left));
    let (mut use_pressed, mut pick) = (mouse.just_pressed(MouseButton::Right), mouse.just_pressed(MouseButton::Middle));
    let mut slot_delta = if scroll.delta.y > 0.0 { -1 } else if scroll.delta.y < 0.0 { 1 } else { 0 };
    let (mut inventory, mut pause) = (keys.just_pressed(KeyCode::KeyE), keys.just_pressed(KeyCode::Escape));
    for g in &pads {
        let ls = g.left_stick();
        if ls.length() > 0.15 { mv += ls; }
        let rs = g.right_stick();
        if rs.length() > 0.12 { look += Vec2::new(-rs.x, rs.y) * 2.6 * settings.sens * time.delta_secs(); }
        jump |= g.pressed(GamepadButton::South);
        down |= g.pressed(GamepadButton::East);
        sprint |= g.pressed(GamepadButton::LeftThumb);
        attack |= g.pressed(GamepadButton::RightTrigger2);
        attack_pressed |= g.just_pressed(GamepadButton::RightTrigger2);
        use_pressed |= g.just_pressed(GamepadButton::LeftTrigger2);
        pick |= g.just_pressed(GamepadButton::DPadUp);
        if g.just_pressed(GamepadButton::RightTrigger) { slot_delta += 1; }
        if g.just_pressed(GamepadButton::LeftTrigger) { slot_delta -= 1; }
        inventory |= g.just_pressed(GamepadButton::North);
        pause |= g.just_pressed(GamepadButton::Start);
    }
    *c = Controls {
        mv: mv.clamp_length_max(1.0),
        look,
        jump,
        down,
        sprint,
        attack,
        attack_pressed,
        use_pressed,
        pick,
        slot_delta,
        slot_set: DIGITS.iter().position(|d| keys.just_pressed(*d)),
        inventory,
        pause,
        fly: keys.just_pressed(KeyCode::KeyF),
        creative: keys.just_pressed(KeyCode::F4),
    };
}
