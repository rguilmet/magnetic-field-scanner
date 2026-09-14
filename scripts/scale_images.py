import os
import shutil
from PIL import Image

MAX_SIZE = 1200
THRESHOLD_BYTES = 1.5 * 1024 * 1024 # 1.5 MB

images_dir = "docs/images"
full_res_dir = os.path.join(images_dir, "full_res")

if not os.path.exists(full_res_dir):
    os.makedirs(full_res_dir)

count = 0
for filename in os.listdir(images_dir):
    filepath = os.path.join(images_dir, filename)
    
    if not os.path.isfile(filepath):
        continue
        
    if not (filename.lower().endswith(".png") or filename.lower().endswith(".jpg")):
        continue
        
    size_bytes = os.path.getsize(filepath)
    if size_bytes > THRESHOLD_BYTES:
        print(f"Processing {filename} ({size_bytes / 1024 / 1024:.1f} MB)...")
        
        full_res_path = os.path.join(full_res_dir, filename)
        
        # Copy to full_res first
        if not os.path.exists(full_res_path):
            shutil.copy2(filepath, full_res_path)
            
        # Now compress and replace the original in-place
        with Image.open(full_res_path) as img:
            # Calculate new size preserving aspect ratio
            width, height = img.size
            if width > MAX_SIZE or height > MAX_SIZE:
                if width > height:
                    new_width = MAX_SIZE
                    new_height = int((MAX_SIZE / width) * height)
                else:
                    new_height = MAX_SIZE
                    new_width = int((MAX_SIZE / height) * width)
                
                print(f"  Scaling from {width}x{height} to {new_width}x{new_height}")
                img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)
            
            # Save it back (always to JPEG to save space, but if it's a PNG we save it as a scaled PNG to not break links)
            if filename.lower().endswith(".png"):
                img.save(filepath, "PNG", optimize=True)
            else:
                img.save(filepath, "JPEG", quality=85, optimize=True)
                
        count += 1
        
print(f"Done. Scaled {count} large images.")
