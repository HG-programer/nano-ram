import os
import trimesh
import numpy as np

batch2_dir = r"C:\Users\Haru\Desktop\Batch-2-Staged"
hi3d_dir = r"C:\Users\Haru\Desktop\hi3d-ram-production-stls"

thickened_shells = {
    '01': ('01_head_front_artistic_fused_thickened.stl', os.path.join(batch2_dir, '01_head_front_artistic_fused_thickened.stl')),
    '02': ('02_head_rear_artistic_fused_thickened.stl', os.path.join(batch2_dir, '02_head_rear_artistic_fused_thickened.stl')),
    '04': ('04_torso_front_structural_thickened.stl', os.path.join(batch2_dir, '04_torso_front_structural_thickened.stl')),
    '05': ('05_torso_rear_structural_thickened.stl', os.path.join(batch2_dir, '05_torso_rear_structural_thickened.stl')),
    '10': ('10_skirt_base_structural_thickened.stl', os.path.join(batch2_dir, '10_skirt_base_structural_thickened.stl')),
}

orig_shells = {
    '01': ('01_head_front_artistic_fused.stl', os.path.join(hi3d_dir, '01_head_front_artistic_fused.stl')),
    '02': ('02_head_rear_artistic_fused.stl', os.path.join(hi3d_dir, '02_head_rear_artistic_fused.stl')),
    '04': ('04_torso_front_structural.stl', os.path.join(hi3d_dir, '04_torso_front_structural.stl')),
    '05': ('05_torso_rear_structural.stl', os.path.join(hi3d_dir, '05_torso_rear_structural.stl')),
    '10': ('10_skirt_base_structural.stl', os.path.join(hi3d_dir, '10_skirt_base_structural.stl')),
}

internal_parts = [
    '12_neck_bearing_sleeve.stl',
    '13_yaw_carrier_and_pitch_frame.stl',
    '14_head_pitch_idler_cap.stl',
    '15_neck_outer_race_shim.stl',
    '16_neck_hollow_post.stl',
    '17_yaw_fixed_keyed_horn_coupler.stl',
    '18_waist_idler_cap.stl',
    '19_speaker_chamber_cover.stl',
    '20_buck_electronics_tray.stl',
    '21_pwm_electronics_tray.stl'
]

print("Loading internal skeleton meshes...")
internal_meshes = {}
for fname in internal_parts:
    p = os.path.join(hi3d_dir, fname)
    m = trimesh.load(p, force='mesh')
    internal_meshes[fname] = m

print("Loading thickened shells...")
thick_meshes = {}
for k, (fname, p) in thickened_shells.items():
    thick_meshes[k] = trimesh.load(p, force='mesh')

print("Loading original shells...")
orig_meshes = {}
for k, (fname, p) in orig_shells.items():
    orig_meshes[k] = trimesh.load(p, force='mesh')

print("\n--- AABB OVERLAP CHECK ---")
for int_name, m_int in internal_meshes.items():
    int_b = m_int.bounds
    for shell_k, m_thick in thick_meshes.items():
        sh_b = m_thick.bounds
        # Check AABB overlap
        overlap = not (np.any(int_b[0] > sh_b[1]) or np.any(int_b[1] < sh_b[0]))
        if overlap:
            print(f"AABB candidate: {int_name} overlaps AABB with shell {shell_k} ({thickened_shells[shell_k][0]})")

print("\n--- RIGOROUS DISTANCE / INTERSECTION ANALYSIS ---")
# For candidate pairs, compute proximity and check if internal vertices are inside the shell
for int_name, m_int in internal_meshes.items():
    int_b = m_int.bounds
    for shell_k, m_thick in thick_meshes.items():
        sh_b = m_thick.bounds
        if np.any(int_b[0] > sh_b[1]) or np.any(int_b[1] < sh_b[0]):
            continue
        
        # Check collision via trimesh collision manager or signed distance / contains
        # Sample points on internal mesh or check vertices
        # Let's check contains:
        pts = m_int.vertices
        # Check if points of internal mesh are contained in thickened shell
        # Note: contains check tests if point is inside watertight mesh
        if m_thick.is_watertight:
            inside_thick = m_thick.contains(pts)
            inside_count_thick = np.sum(inside_thick)
        else:
            inside_count_thick = -1
            
        m_orig = orig_meshes[shell_k]
        if m_orig.is_watertight:
            inside_orig = m_orig.contains(pts)
            inside_count_orig = np.sum(inside_orig)
        else:
            inside_count_orig = -1
            
        # Distance calculation
        # Proximity between m_int and m_thick
        # Distance from int vertices to thick surface
        # Sample 1000 points if int mesh is large
        sample_pts = pts if len(pts) < 2000 else pts[np.random.choice(len(pts), 2000, replace=False)]
        
        d_thick = trimesh.proximity.ProximityQuery(m_thick).signed_distance(sample_pts)
        d_orig = trimesh.proximity.ProximityQuery(m_orig).signed_distance(sample_pts)
        
        # In signed_distance: negative is outside, positive is inside (or vice versa in trimesh convention)
        # Let's check min absolute distance
        min_abs_d_thick = np.min(np.abs(d_thick))
        min_abs_d_orig = np.min(np.abs(d_orig))
        
        # Check how many points penetrated inside (positive signed distance)
        penetrating_thick = np.sum(d_thick > 0.05)
        penetrating_orig = np.sum(d_orig > 0.05)
        
        print(f"\nInternal Part: {int_name} vs Shell {shell_k}:")
        print(f"  Thickened: contains {inside_count_thick}/{len(pts)} vertices inside cavity wall.")
        print(f"  Original:  contains {inside_count_orig}/{len(pts)} vertices inside cavity wall.")
        print(f"  Min surface distance: Thickened={min_abs_d_thick:.3f} mm, Original={min_abs_d_orig:.3f} mm")
        print(f"  Max penetration into shell solid wall: Thickened={np.max(d_thick):.3f} mm, Original={np.max(d_orig):.3f} mm")
