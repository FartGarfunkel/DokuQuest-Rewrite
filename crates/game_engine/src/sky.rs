//! Camera, sun, day/night cycle, fog and per-dimension skies (the sky follows whichever stacked
//! dimension the camera is in). Gradient-sky shader (TanTan's bevy_sky_gradient idea) comes later.
use crate::world::{VoxelWorld, RENDER_DIST};
use bevy::pbr::{CascadeShadowConfigBuilder, DistanceFog, FogFalloff};
use bevy::prelude::*;
use common_server::dims;

#[derive(Component)]
pub struct MainCamera;

#[derive(Component)]
struct Sun;

/// 0..1 through the day (0 = sunrise, 0.25 = noon, 0.5 = sunset).
#[derive(Resource)]
pub struct DayTime(pub f32);
const DAY_SECS: f32 = 1200.0;

pub struct SkyPlugin;
impl Plugin for SkyPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(DayTime(0.12))
            .insert_resource(ClearColor(Color::srgb(0.5, 0.7, 0.95)))
            .add_systems(Startup, setup)
            .add_systems(Update, cycle);
    }
}

fn setup(mut commands: Commands, w: Res<VoxelWorld>) {
    commands.spawn((
        MainCamera,
        Camera3d::default(),
        Projection::Perspective(PerspectiveProjection { fov: 70f32.to_radians(), far: 2000.0, ..default() }),
        Transform::from_translation(w.focus + Vec3::Y * 1.62),
        DistanceFog { color: Color::srgb(0.5, 0.7, 0.95), falloff: FogFalloff::Linear { start: 60.0, end: 130.0 }, ..default() },
    ));
    commands.spawn((
        Sun,
        DirectionalLight { illuminance: 9000.0, shadows_enabled: true, ..default() },
        Transform::default(),
        CascadeShadowConfigBuilder { num_cascades: 3, first_cascade_far_bound: 18.0, maximum_distance: 110.0, ..default() }.build(),
    ));
    commands.insert_resource(AmbientLight { color: Color::WHITE, brightness: 500.0, ..default() });
}

fn smooth(a: f32, b: f32, x: f32) -> f32 {
    let t = ((x - a) / (b - a)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

#[allow(clippy::type_complexity)]
fn cycle(
    time: Res<Time>,
    mut day: ResMut<DayTime>,
    mut clear: ResMut<ClearColor>,
    mut amb: ResMut<AmbientLight>,
    mut sun: Query<(&mut Transform, &mut DirectionalLight), With<Sun>>,
    mut fog: Query<(&GlobalTransform, &mut DistanceFog), With<MainCamera>>,
) {
    day.0 = (day.0 + time.delta_secs() / DAY_SECS).fract();
    let a = day.0 * std::f32::consts::TAU;
    let dir = Vec3::new(a.cos(), a.sin(), 0.35).normalize(); // towards the sun
    let elev = dir.y;
    let dayf = smooth(-0.08, 0.3, elev);
    let Ok((cam, mut f)) = fog.single_mut() else { return };
    let band = dims::band_at(cam.translation().y.floor() as i32).map(|b| b.key).unwrap_or("overworld");
    let (night, noon) = (Vec3::new(0.015, 0.02, 0.05), Vec3::new(0.48, 0.70, 0.95));
    let dusk = Vec3::new(0.95, 0.5, 0.25) * (1.0 - (elev.abs() * 4.0).min(1.0)) * 0.6;
    let far = (RENDER_DIST * 16) as f32;
    let (sky, lux, ambient, fs, fe) = match band {
        "nether" => (Vec3::new(0.22, 0.05, 0.03), 0.0, 700.0, 8.0, 90.0),
        "end" => (Vec3::new(0.06, 0.03, 0.08), 0.0, 350.0, 30.0, 170.0),
        k => {
            let base = night.lerp(noon, dayf) + dusk;
            (if k == "skylands" { base * 1.12 } else { base }, 9000.0 * dayf + 30.0, 90.0 + 650.0 * dayf, far * 0.5, far * 0.95)
        }
    };
    let c = Color::srgb(sky.x.min(1.0), sky.y.min(1.0), sky.z.min(1.0));
    clear.0 = c;
    f.color = c;
    f.falloff = FogFalloff::Linear { start: fs, end: fe };
    amb.brightness = ambient;
    if let Ok((mut t, mut l)) = sun.single_mut() {
        *t = Transform::IDENTITY.looking_to(-dir, Vec3::Y);
        l.illuminance = lux;
        l.shadows_enabled = lux > 200.0;
    }
}
