import os
import trimesh
import numpy as np
from scipy.spatial import cKDTree

batch2_dir = r"C:\Users\Haru\Desktop\Batch-2-Staged"
batch3_dir = r"C:\Users\Haru\Desktop\Batch-3-Staged"
hi3d_dir = r"C:\Users\Haru\Desktop\hi3d-ram-production-stls"

# Load Batch 2 meshes
m04 = trimesh.load(os.path.join(batch2_dir, "04_torso_front_structural_thickened.stl"), force='mesh')
m05 = trimesh.load(os.path.join(batch2_dir, "05_torso_rear_structural_thickened.stl"), force='mesh')
m10 = trimesh.load(os.path.join(batch2_dir, "10_skirt_base_structural_thickened.stl"), force='mesh')

# Combined torso
torso_verts = np.vstack([m04.vertices, m05.vertices])
tree_torso = cKDTree(torso_verts)
tree_10 = cKDTree(m10.vertices)

# Load neck hollow post (part 16)
m16 = trimesh.load(os.path.join(hi3d_dir, "16_neck_hollow_post.stl"), force='mesh')
tree_16 = cKDTree(m16.vertices)

batch3_files = {
    '03_collar': "03_maid_collar_ruffles_thickened.stl",
    '06_left_sleeve': "06_left_arm_maid_sleeve_thickened.stl",
    '07_right_sleeve': "07_right_arm_maid_sleeve_thickened.stl",
    '08_apron': "08_maid_apron_front_thickened.stl",
    '09a_ruffles_upper': "09a_skirt_hem_ruffles_upper_thickened.stl",
    '09b_ruffles_lower': "09b_skirt_lower_ruffles_thickened.stl",
}

print("=== CHECK 4: BATCH 2 EXTERIOR VS BATCH 3 COSMETIC ARMOR FIT ===")

for name, fname in batch3_files.items():
    p = os.path.join(batch3_dir, fname)
    m = trimesh.load(p, force='mesh')
    print(f"\n--- {name} ({fname}) ---")
    print(f"  Bounds: X[{m.bounds[0,0]:.2f}, {m.bounds[1,0]:.2f}] Y[{m.bounds[0,1]:.2f}, {m.bounds[1,1]:.2f}] Z[{m.bounds[0,2]:.2f}, {m.bounds[1,2]:.2f}]")
    print(f"  Dimensions: dX={m.extents[0]:.2f}, dY={m.extents[1]:.2f}, dZ={m.extents[2]:.2f} mm")
    
    # 03 Collar: check fit to neck post 16 and torso top
    if '03' in name:
        d_to_16, _ = tree_16.query(m.vertices)
        d_to_torso, _ = tree_torso.query(m.vertices)
        print(f"  Distance to Neck Hollow Post (16): min={np.min(d_to_16):.3f} mm, mean={np.mean(d_to_16):.3f} mm")
        print(f"  Distance to Torso (04/05): min={np.min(d_to_torso):.3f} mm, mean={np.mean(d_to_torso):.3f} mm")
        # Check inner neck hole of collar
        # Filter vertices at the inner neck ring (near Z ~ 100-105, small radius)
        r_collar = np.sqrt(m.vertices[:,0]**2 + m.vertices[:,1]**2)
        inner_collar_mask = (m.vertices[:,2] >= 96.0) & (m.vertices[:,2] <= 104.0) & (r_collar < 15.0)
        if np.any(inner_collar_mask):
            inner_r = r_collar[inner_collar_mask]
            print(f"  Collar inner neck aperture: min R = {np.min(inner_r):.2f} mm (Dia = {np.min(inner_r)*2:.2f} mm), mean R = {np.mean(inner_r):.2f} mm")
            # Neck post outer radius:
            r_16 = np.sqrt(m16.vertices[:,0]**2 + m16.vertices[:,1]**2)
            print(f"  Neck post outer radius: max R = {np.max(r_16):.2f} mm (Dia = {np.max(r_16)*2:.2f} mm)")
            clearance = np.min(inner_r) - np.max(r_16)
            print(f"  Radial neck post clearance: {clearance:.2f} mm")
            
    # 06 / 07 Sleeves: check fit to torso shoulder sockets
    if 'sleeve' in name:
        d_to_torso, _ = tree_torso.query(m.vertices)
        print(f"  Distance to Torso (04/05): min={np.min(d_to_torso):.3f} mm, mean={np.mean(d_to_torso):.3f} mm")
        # Contact vertices within 1.0 mm
        contact_pts = np.sum(d_to_torso < 1.0)
        print(f"  Shoulder socket interface vertices (< 1.0 mm to Torso): {contact_pts} / {len(m.vertices)} ({contact_pts/len(m.vertices)*100:.1f}%)")
        if contact_pts > 0:
            socket_dists = d_to_torso[d_to_torso < 1.0]
            print(f"  Socket interface gap: min={np.min(socket_dists):.3f} mm, median={np.median(socket_dists):.3f} mm, mean={np.mean(socket_dists):.3f} mm")
            
    # 08 Apron: check fit to torso and skirt base
    if '08' in name:
        d_to_torso, _ = tree_torso.query(m.vertices)
        d_to_10, _ = tree_10.query(m.vertices)
        print(f"  Distance to Torso (04/05): min={np.min(d_to_torso):.3f} mm, contact (<1mm): {np.sum(d_to_torso < 1.0)}")
        print(f"  Distance to Skirt Base (10): min={np.min(d_to_10):.3f} mm")
        
    # 09a / 09b Ruffles: check fit to skirt base 10 and original 09
    if 'ruffles' in name:
        d_to_10, _ = tree_10.query(m.vertices)
        print(f"  Distance to Skirt Base (10): min={np.min(d_to_10):.3f} mm")
        r_ruffle = np.sqrt(m.vertices[:,0]**2 + m.vertices[:,1]**2)
        print(f"  Ruffle Radial range: R_min={np.min(r_ruffle):.2f} mm, R_max={np.max(r_ruffle):.2f} mm")
        # Skirt base radial range at matching Z:
        z_match = (m10.vertices[:,2] >= m.bounds[0,2]) & (m10.vertices[:,2] <= m.bounds[1,2])
        if np.any(z_match):
            r_10_match = np.sqrt(m10.vertices[z_match, 0]**2 + m10.vertices[z_match, 1]**2)
            print(f"  Skirt Base (10) Radial range at matching Z: R_min={np.min(r_10_match):.2f} mm, R_max={np.max(r_10_match):.2f} mm")
