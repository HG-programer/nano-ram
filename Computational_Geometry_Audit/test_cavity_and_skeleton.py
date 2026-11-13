import os
import trimesh
import numpy as np
from scipy.spatial import cKDTree

batch2_dir = r"C:\Users\Haru\Desktop\Batch-2-Staged"
hi3d_dir = r"C:\Users\Haru\Desktop\hi3d-ram-production-stls"

shells = [
    ('01', '01_head_front_artistic_fused.stl', '01_head_front_artistic_fused_thickened.stl'),
    ('02', '02_head_rear_artistic_fused.stl', '02_head_rear_artistic_fused_thickened.stl'),
    ('04', '04_torso_front_structural.stl', '04_torso_front_structural_thickened.stl'),
    ('05', '05_torso_rear_structural.stl', '05_torso_rear_structural_thickened.stl'),
    ('10', '10_skirt_base_structural.stl', '10_skirt_base_structural_thickened.stl'),
]

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

print("=== DETAILED SKELETON CLEARANCE AUDIT ===")

# Load all internal parts
loaded_internals = {}
for ip in internal_parts:
    p = os.path.join(hi3d_dir, ip)
    m = trimesh.load(p, force='mesh')
    loaded_internals[ip] = {
        'mesh': m,
        'tree': cKDTree(m.vertices),
        'bounds': m.bounds,
        'name': ip
    }

for code, orig_f, thick_f in shells:
    p_orig = os.path.join(hi3d_dir, orig_f)
    p_thick = os.path.join(batch2_dir, thick_f)
    
    m_orig = trimesh.load(p_orig, force='mesh')
    m_thick = trimesh.load(p_thick, force='mesh')
    
    tree_orig = cKDTree(m_orig.vertices)
    tree_thick = cKDTree(m_thick.vertices)
    
    print(f"\n==================================================================")
    print(f"SHELL {code}: {thick_f}")
    print(f"Bounds Orig:  X[{m_orig.bounds[0,0]:.2f}, {m_orig.bounds[1,0]:.2f}] Y[{m_orig.bounds[0,1]:.2f}, {m_orig.bounds[1,1]:.2f}] Z[{m_orig.bounds[0,2]:.2f}, {m_orig.bounds[1,2]:.2f}]")
    print(f"Bounds Thick: X[{m_thick.bounds[0,0]:.2f}, {m_thick.bounds[1,0]:.2f}] Y[{m_thick.bounds[0,1]:.2f}, {m_thick.bounds[1,1]:.2f}] Z[{m_thick.bounds[0,2]:.2f}, {m_thick.bounds[1,2]:.2f}]")
    
    # Check bounding box deltas
    # Negative dmin means expanded outwards in negative direction
    # Positive dmax means expanded outwards in positive direction
    dmin = m_thick.bounds[0] - m_orig.bounds[0]
    dmax = m_thick.bounds[1] - m_orig.bounds[1]
    print(f"Outer envelope expansion: dMin={dmin.round(3)} mm, dMax={dmax.round(3)} mm")
    
    # Check clearance against each internal skeleton part
    sh_bounds = m_thick.bounds
    for ip, idata in loaded_internals.items():
        ib = idata['bounds']
        # Check AABB overlap or close proximity (< 10mm)
        margin = 5.0
        overlap = not (
            ib[0, 0] > sh_bounds[1, 0] + margin or ib[1, 0] < sh_bounds[0, 0] - margin or
            ib[0, 1] > sh_bounds[1, 1] + margin or ib[1, 1] < sh_bounds[0, 1] - margin or
            ib[0, 2] > sh_bounds[1, 2] + margin or ib[1, 2] < sh_bounds[0, 2] - margin
        )
        if not overlap:
            continue
            
        m_int = idata['mesh']
        # Compute nearest distance from internal mesh vertices to shell surface
        # Using KDTree of shell vertices
        dists_to_thick, _ = tree_thick.query(m_int.vertices)
        dists_to_orig, _ = tree_orig.query(m_int.vertices)
        
        min_d_thick = np.min(dists_to_thick)
        min_d_orig = np.min(dists_to_orig)
        avg_d_thick = np.mean(dists_to_thick)
        avg_d_orig = np.mean(dists_to_orig)
        
        # Check whether any vertex penetrates (using signed distance on candidate nearest points)
        # We can test if points are inside the shell mesh using trimesh.collision or raycast
        print(f"  -> Skeleton: {ip}")
        print(f"     Min vertex distance: Thickened = {min_d_thick:.3f} mm | Original = {min_d_orig:.3f} mm (Delta = {min_d_thick - min_d_orig:+.3f} mm)")
        print(f"     Mean vertex distance: Thickened = {avg_d_thick:.3f} mm | Original = {avg_d_orig:.3f} mm")
        
        # If min distance is very small (< 1.5mm), check the closest vertices
        close_idx = np.where(dists_to_thick < 1.0)[0]
        if len(close_idx) > 0:
            print(f"     [!] {len(close_idx)} vertices of {ip} are within 1.0 mm of Shell {code}")
            # Check signed distance of these close points
            # Test with raycast to see if inside
            closest_pts = m_int.vertices[close_idx[:min(len(close_idx), 100)]]
            # Raycast test
            inside = m_thick.contains(closest_pts)
            inside_cnt = np.sum(inside)
            print(f"     [!] Of sample {len(closest_pts)} closest vertices, {inside_cnt} are strictly INSIDE the shell solid volume.")
