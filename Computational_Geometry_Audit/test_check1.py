import os
import trimesh
import numpy as np
from scipy.spatial import cKDTree

batch2_dir = r"C:\Users\Haru\Desktop\Batch-2-Staged"
hi3d_dir = r"C:\Users\Haru\Desktop\hi3d-ram-production-stls"

pairs = [
    ("01_head_front_artistic_fused", "01_head_front_artistic_fused_thickened"),
    ("02_head_rear_artistic_fused", "02_head_rear_artistic_fused_thickened"),
    ("04_torso_front_structural", "04_torso_front_structural_thickened"),
    ("05_torso_rear_structural", "05_torso_rear_structural_thickened"),
    ("10_skirt_base_structural", "10_skirt_base_structural_thickened"),
]

internal_parts = [
    "12_neck_bearing_sleeve.stl",
    "13_yaw_carrier_and_pitch_frame.stl",
    "14_head_pitch_idler_cap.stl",
    "15_neck_outer_race_shim.stl",
    "16_neck_hollow_post.stl",
    "17_yaw_fixed_keyed_horn_coupler.stl",
    "18_waist_idler_cap.stl",
    "19_speaker_chamber_cover.stl",
    "20_buck_electronics_tray.stl",
    "21_pwm_electronics_tray.stl",
]

print("=== CHECK 1: INWARD VS OUTWARD THICKENING DELTA ===")
for orig_name, thick_name in pairs:
    orig_path = os.path.join(hi3d_dir, orig_name + ".stl")
    thick_path = os.path.join(batch2_dir, thick_name + ".stl")
    
    m_orig = trimesh.load(orig_path, force='mesh')
    m_thick = trimesh.load(thick_path, force='mesh')
    
    print(f"\n--- Comparing {orig_name} vs {thick_name} ---")
    print(f"Orig: {len(m_orig.vertices)} verts, {len(m_orig.faces)} faces, watertight={m_orig.is_watertight}")
    print(f"Thick: {len(m_thick.vertices)} verts, {len(m_thick.faces)} faces, watertight={m_thick.is_watertight}")
    print(f"Orig bounds: {m_orig.bounds[0].round(2)} to {m_orig.bounds[1].round(2)}")
    print(f"Thick bounds: {m_thick.bounds[0].round(2)} to {m_thick.bounds[1].round(2)}")
    delta_min = m_thick.bounds[0] - m_orig.bounds[0]
    delta_max = m_thick.bounds[1] - m_orig.bounds[1]
    print(f"Bounding Box Expansion (Thick - Orig):")
    print(f"  Min delta (negative means expanded outward): {delta_min.round(3)}")
    print(f"  Max delta (positive means expanded outward): {delta_max.round(3)}")

