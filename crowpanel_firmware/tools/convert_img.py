import sys
# pyrefly: ignore [missing-import]
from PIL import Image

def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def convert_image(input_path, output_path, var_name, max_width, max_height):
    img = Image.open(input_path).convert("RGBA")
    
    # Calculate scale maintaining aspect ratio
    scale = min(max_width / img.width, max_height / img.height)
    new_w = int(img.width * scale)
    new_h = int(img.height * scale)
    
    img = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
    
    with open(output_path, "w") as f:
        f.write("#include <lvgl.h>\n\n")
        f.write(f"const uint8_t {var_name}_map[] = {{\n")
        
        pixels = list(img.getdata())
        for i, p in enumerate(pixels):
            r, g, b, a = p
            c565 = rgb565(r, g, b)
            b0 = c565 & 0xFF
            b1 = (c565 >> 8) & 0xFF
            f.write(f"0x{b0:02x}, 0x{b1:02x}, 0x{a:02x}, ")
            if (i + 1) % 12 == 0:
                f.write("\n")
                
        f.write("\n};\n\n")
        
        f.write(f"const lv_img_dsc_t {var_name} = {{\n")
        f.write("  {\n")
        f.write("    LV_IMG_CF_TRUE_COLOR_ALPHA,\n")
        f.write("    0, 0,\n")
        f.write(f"    {new_w}, {new_h}\n")
        f.write("  },\n")
        f.write(f"  {new_w * new_h * 3},\n")
        f.write(f"  {var_name}_map\n")
        f.write("};\n")
    
    print(f"Generated {output_path} ({new_w}x{new_h})")

if __name__ == "__main__":
    convert_image(
        r"c:\Users\User\BIOMETRICSMANPRO\data\ManPro (100 x 50 px) (1000 x 400 px).png", 
        r"c:\Users\User\BIOMETRICSMANPRO\crowpanel_firmware\manpro_logo.c",
        "manpro_logo",
        300, 120
    )
    
    convert_image(
        r"c:\Users\User\BIOMETRICSMANPRO\data\icons\people.png", 
        r"c:\Users\User\BIOMETRICSMANPRO\crowpanel_firmware\icon_people.c",
        "icon_people",
        120, 120
    )
    
    convert_image(
        r"c:\Users\User\BIOMETRICSMANPRO\data\icons\people.png", 
        r"c:\Users\User\BIOMETRICSMANPRO\crowpanel_firmware\icon_people_small.c",
        "icon_people_small",
        36, 36
    )
    
    convert_image(
        r"c:\Users\User\BIOMETRICSMANPRO\data\icons\schedule.png", 
        r"c:\Users\User\BIOMETRICSMANPRO\crowpanel_firmware\icon_schedule.c",
        "icon_schedule",
        120, 120
    )
    
    convert_image(
        r"c:\Users\User\BIOMETRICSMANPRO\data\icons\settings.png", 
        r"c:\Users\User\BIOMETRICSMANPRO\crowpanel_firmware\icon_settings.c",
        "icon_settings",
        120, 120
    )

    convert_image(
        r"c:\Users\User\BIOMETRICSMANPRO\data\manpro_icon.png", 
        r"c:\Users\User\BIOMETRICSMANPRO\crowpanel_firmware\icon_manpro.c",
        "icon_manpro",
        250, 80
    )