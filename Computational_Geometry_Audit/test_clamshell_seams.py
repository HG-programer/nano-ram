import os
import trimesh
import numpy as np
from scipy.spatial import cKDTree

batch2_dir = r"C:\Users\Haru\Desktop\Batch-2-Staged"

def audit_clamshell(nameA, fileA, nameB, fileB, seam_y_range=(-2.0, 2.0)):
    mA = trimesh.load(os.path.join(batch2_dir, fileA), force='mesh')
    mB = trimesh.load(os.path.join(batch2_dir, fileB), force='mesh')
    
    print(f"\n=======================================================")
    print(f"AUDITING CLAMSHELL SEAM: {nameA} vs {nameB}")
    print(f"  {nameA}: {len(mA.vertices)} verts, Bounds Y: [{mA.bounds[0,1]:.2f}, {mA.bounds[1,1]:.2f}]")
    print(f"  {nameB}: {len(mB.vertices)} verts, Bounds Y: [{mB.bounds[0,1]:.2f}, {mB.bounds[1,1]:.2f}]")
    
    # Filter vertices near the split plane
    maskA = (mA.vertices[:, 1] >= seam_y_range[0]) & (mA.vertices[:, 1] <= seam_y_range[1])
    maskB = (mB.vertices[:, 1] >= seam_y_range[0]) & (mB.vertices[:, 1] <= seam_y_range[1])
    
    vertsA = mA.vertices[maskA]
    vertsB = mB.vertices[maskB]
    
    print(f"  Seam candidate vertices in Y range {seam_y_range}: {len(vertsA)} in {nameA}, {len(vertsB)} in {nameB}")
    
    # Check minimum distance from A to B
    treeB = cKDTree(mB.vertices)
    distsA, idxB = treeB.query(vertsA)
    
    # Also check from B to A
    treeA = cKDTree(mA.vertices)
    distsB, idxA = treeA.query(vertsB)
    
    # Seam contact vertices (distance < 0.5 mm)
    contactA = distsA[distsA < 0.5]
    contactB = distsB[distsB < 0.5]
    
    print(f"  Contact vertices (<0.5mm): {len(contactA)} in {nameA}, {len(contactB)} in {nameB}")
    if len(contactA) > 0:
        print(f"  Min clearance along seam: {np.min(distsA):.4f} mm")
        print(f"  Median contact gap: {np.median(contactA):.4f} mm")
        print(f"  Mean contact gap: {np.mean(contactA):.4f} mm")
    
    # Penetration / Interference check:
    # Check if candidate seam vertices on part A penetrate past the mating face into part B
    # For vertices in contact (< 0.5mm), check if their vector to nearest neighbor opposes the normal
    # Or check if Y of front part penetrates into rear part
    print(f"  Split-plane overlap zone: Head Front reaches Y = {np.max(vertsA[:, 1]):.3f} mm, Head Rear reaches Y = {np.min(vertsB[:, 1]):.3f} mm")
    # Check if there are alignment pins / steps
    print(f"  Mating interface has step/lip extending into Y range [{np.min(vertsB[:,1]):.2f}, {np.max(vertsA[:,1]):.2f}] mm")

            
audit_clamshell("01_Head_Front", "01_head_front_artistic_fused_thickened.stl",
                "02_Head_Rear", "02_head_rear_artistic_fused_thickened.stl")

audit_clamshell("04_Torso_Front", "04_torso_front_structural_thickened.stl",
                "05_Torso_Rear", "05_torso_rear_structural_thickened.stl")
