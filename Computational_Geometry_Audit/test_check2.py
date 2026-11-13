import os
import trimesh
import numpy as np

p10 = r"C:\Users\Haru\Desktop\Batch-2-Staged\10_skirt_base_structural_thickened.stl"
p11 = r"C:\Users\Haru\Desktop\Batch-3-Staged\11_skirt_bottom_led_lid.stl"

m10 = trimesh.load(p10, force='mesh')
m11 = trimesh.load(p11, force='mesh')

print("=== CHECK 2: SKIRT BASE (10) VS BOTTOM LED LID (11) ===")
print("Part 10 bounds:", m10.bounds[0].round(2), "to", m10.bounds[1].round(2))
print("Part 11 bounds:", m11.bounds[0].round(2), "to", m11.bounds[1].round(2))

# Let's inspect Part 11 radii at different Z levels
v11 = m11.vertices
r11 = np.sqrt(v11[:, 0]**2 + v11[:, 1]**2)
z11 = v11[:, 2]

print("\nPart 11 (LED Lid) profile by Z height:")
for z_val in np.unique(np.round(z11, 2)):
    mask = np.abs(z11 - z_val) < 0.05
    radii = r11[mask]
    if len(radii) > 0:
        print(f"  Z = {z_val:5.2f} mm | R_min = {np.min(radii):5.2f} mm (Dia = {np.min(radii)*2:5.2f}), R_max = {np.max(radii):5.2f} mm (Dia = {np.max(radii)*2:5.2f})")

# Let's inspect Part 10 (Skirt base) bottom inner opening
# Bottom is at Z = 3.00 mm
v10 = m10.vertices
r10 = np.sqrt(v10[:, 0]**2 + v10[:, 1]**2)
z10 = v10[:, 2]

print("\nPart 10 (Skirt Base) profile near base (Z = 3.00 to 5.00 mm):")
for z_thresh in [3.00, 3.05, 3.10, 3.15, 3.20, 3.50, 4.00]:
    mask = np.abs(z10 - z_thresh) < 0.05
    radii = r10[mask]
    if len(radii) > 0:
        # Find the different distinct radii (inner bore vs outer shell)
        sorted_r = np.sort(radii)
        print(f"  Z = {z_thresh:5.2f} mm ({len(radii)} verts) | R_min = {np.min(radii):5.2f} mm (Dia = {np.min(radii)*2:5.2f}), R_max = {np.max(radii):5.2f} mm (Dia = {np.max(radii)*2:5.2f})")

# Cross-section slice at Z = 3.05 mm and Z = 3.15 mm
slice_305 = m10.section(plane_origin=[0, 0, 3.05], plane_normal=[0, 0, 1])
if slice_305:
    # 2D paths
    path, _ = slice_305.to_2D()
    print("\nSlice at Z = 3.05 mm entities:")
    for entity in path.entities:
        pts = path.vertices[entity.points]
        r = np.sqrt(pts[:, 0]**2 + pts[:, 1]**2)
        print(f"  Entity {type(entity).__name__}: mean R = {np.mean(r):.2f} mm (Dia = {np.mean(r)*2:.2f} mm), min R = {np.min(r):.2f}, max R = {np.max(r):.2f}")
