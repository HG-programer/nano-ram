import os
import trimesh
import numpy as np

batch2_dir = r"C:\Users\Haru\Desktop\Batch-2-Staged"
batch3_dir = r"C:\Users\Haru\Desktop\Batch-3-Staged"
hi3d_dir = r"C:\Users\Haru\Desktop\hi3d-ram-production-stls"

staged_files = [
    (batch2_dir, "01_head_front_artistic_fused_thickened.stl"),
    (batch2_dir, "02_head_rear_artistic_fused_thickened.stl"),
    (batch2_dir, "04_torso_front_structural_thickened.stl"),
    (batch2_dir, "05_torso_rear_structural_thickened.stl"),
    (batch2_dir, "10_skirt_base_structural_thickened.stl"),
    (batch3_dir, "03_maid_collar_ruffles_thickened.stl"),
    (batch3_dir, "06_left_arm_maid_sleeve_thickened.stl"),
    (batch3_dir, "07_right_arm_maid_sleeve_thickened.stl"),
    (batch3_dir, "08_maid_apron_front_thickened.stl"),
    (batch3_dir, "09a_skirt_hem_ruffles_upper_thickened.stl"),
    (batch3_dir, "09b_skirt_lower_ruffles_thickened.stl"),
    (batch3_dir, "11_skirt_bottom_led_lid.stl"),
]

print(f"{'Filename':<45} | {'Xmin':>7} {'Xmax':>7} | {'Ymin':>7} {'Ymax':>7} | {'Zmin':>7} {'Zmax':>7} | {'dX':>7} {'dY':>7} {'dZ':>7}")
print("-" * 125)

for folder, fname in staged_files:
    p = os.path.join(folder, fname)
    m = trimesh.load(p, force='mesh')
    bounds = m.bounds
    extents = m.extents
    print(f"{fname:<45} | {bounds[0][0]:7.2f} {bounds[1][0]:7.2f} | {bounds[0][1]:7.2f} {bounds[1][1]:7.2f} | {bounds[0][2]:7.2f} {bounds[1][2]:7.2f} | {extents[0]:7.2f} {extents[1]:7.2f} {extents[2]:7.2f}")

print("\n--- Original hi3d meshes bounding boxes ---")
orig_files = [
    "01_head_front_artistic_fused.stl",
    "02_head_rear_artistic_fused.stl",
    "03_maid_collar_ruffles.stl",
    "04_torso_front_structural.stl",
    "05_torso_rear_structural.stl",
    "06_left_arm_maid_sleeve.stl",
    "07_right_arm_maid_sleeve.stl",
    "08_maid_apron_front.stl",
    "09_skirt_hem_ruffles.stl",
    "10_skirt_base_structural.stl",
    "11_skirt_bottom_led_lid.stl",
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

for fname in orig_files:
    p = os.path.join(hi3d_dir, fname)
    if os.path.exists(p):
        m = trimesh.load(p, force='mesh')
        bounds = m.bounds
        extents = m.extents
        print(f"{fname:<45} | {bounds[0][0]:7.2f} {bounds[1][0]:7.2f} | {bounds[0][1]:7.2f} {bounds[1][1]:7.2f} | {bounds[0][2]:7.2f} {bounds[1][2]:7.2f} | {extents[0]:7.2f} {extents[1]:7.2f} {extents[2]:7.2f}")
