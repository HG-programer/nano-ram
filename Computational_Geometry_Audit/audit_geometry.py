import os
import struct
import numpy as np

def read_stl(file_path):
    with open(file_path, 'rb') as f:
        header = f.read(80)
        num_triangles = struct.unpack('<I', f.read(4))[0]
        # Check file size
        expected_size = 84 + num_triangles * 50
        actual_size = os.path.getsize(file_path)
        is_valid_size = (expected_size == actual_size)
        
        # Read all faces
        record_dtype = np.dtype([
            ('normal', np.float32, (3,)),
            ('v1', np.float32, (3,)),
            ('v2', np.float32, (3,)),
            ('v3', np.float32, (3,)),
            ('attr', np.uint16)
        ])
        data = np.fromfile(f, dtype=record_dtype, count=num_triangles)
        
    v1 = data['v1']
    v2 = data['v2']
    v3 = data['v3']
    normals = data['normal']
    vertices = np.vstack([v1, v2, v3])
    
    return {
        'num_triangles': num_triangles,
        'is_valid_size': is_valid_size,
        'vertices': vertices,
        'v1': v1,
        'v2': v2,
        'v3': v3,
        'normals': normals,
        'file_size': actual_size
    }

if __name__ == "__main__":
    print("STL Reader ready.")
