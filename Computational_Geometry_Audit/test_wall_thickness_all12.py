import os
import trimesh
import numpy as np

batch2_dir = r"C:\Users\Haru\Desktop\Batch-2-Staged"
batch3_dir = r"C:\Users\Haru\Desktop\Batch-3-Staged"

files = [
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

print("=== AUDITING WALL THICKNESS & SLICER PRE-FLIGHT ACROSS ALL 12 STAGED FILES ===")

results = []

for folder, fname in files:
    p = os.path.join(folder, fname)
    m = trimesh.load(p, force='mesh')
    
    # 1. Binary STL check
    # Check if file has standard binary STL header + triangle count
    with open(p, 'rb') as f:
        header = f.read(80)
        tri_count = int.from_bytes(f.read(4), byteorder='little')
        expected_size = 84 + tri_count * 50
        actual_size = os.path.getsize(p)
        is_valid_binary = (expected_size == actual_size)
    
    # 2. Watertight & Disconnected shells check
    # trimesh.graph.connected_components
    components = m.split(only_watertight=False)
    num_components = len(components)
    
    # 3. Raycast Wall Thickness Check
    # Sample up to 1000 faces evenly
    num_faces = len(m.faces)
    sample_stride = max(1, num_faces // 1000)
    sample_indices = np.arange(0, num_faces, sample_stride)[:1000]
    
    face_centers = m.triangles_center[sample_indices]
    face_normals = m.face_normals[sample_indices]
    
    # Cast ray inward: start slightly inside surface (0.01mm inward) along -normal
    ray_origins = face_centers - face_normals * 0.01
    ray_directions = -face_normals
    
    # Intersect with mesh
    # Use ray intersector
    locations, index_ray, index_tri = m.ray.intersects_location(
        ray_origins=ray_origins,
        ray_directions=ray_directions,
        multiple_hits=False
    )
    
    if len(index_ray) > 0:
        # Distance from origin to hit location
        hit_dists = np.linalg.norm(locations - ray_origins[index_ray], axis=1) + 0.01
        
        # Filter out extreme self-hits or outer rays:
        # Keep rays with dist < bounding box diagonal
        diag = np.linalg.norm(m.extents)
        valid = hit_dists < diag
        dists = hit_dists[valid]
        
        min_thick = np.min(dists) if len(dists) > 0 else 0.0
        p05_thick = np.percentile(dists, 5) if len(dists) > 0 else 0.0
        median_thick = np.median(dists) if len(dists) > 0 else 0.0
        mean_thick = np.mean(dists) if len(dists) > 0 else 0.0
        pct_ge_1mm = (np.sum(dists >= 1.0) / len(dists) * 100.0) if len(dists) > 0 else 0.0
    else:
        min_thick = p05_thick = median_thick = mean_thick = pct_ge_1mm = 0.0

    print(f"\n[{fname}]")
    print(f"  Valid Binary STL: {is_valid_binary} (Header=80B, Tris={tri_count:,}, FileSize={actual_size:,} bytes)")
    print(f"  Watertight: {m.is_watertight} | Connected Bodies: {num_components} (Floating components: {num_components - 1})")
    print(f"  Bounds: X[{m.bounds[0,0]:.1f}, {m.bounds[1,0]:.1f}] Y[{m.bounds[0,1]:.1f}, {m.bounds[1,1]:.1f}] Z[{m.bounds[0,2]:.1f}, {m.bounds[1,2]:.1f}]")
    print(f"  Thickness (Raycast N={len(index_ray)}):")
    print(f"    Min: {min_thick:.2f} mm | 5th-pct: {p05_thick:.2f} mm | Median: {median_thick:.2f} mm | Mean: {mean_thick:.2f} mm")
    print(f"    % >= 1.0 mm: {pct_ge_1mm:.1f}%")
