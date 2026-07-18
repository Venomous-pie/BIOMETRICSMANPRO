import sys
import os
import argparse

sys.path.append(r"c:\Users\alain\BIOMETRICSMANPRO\crowpanel_firmware")
import convert_img

def main():
    parser = argparse.ArgumentParser(description="Convert PNG to LVGL C array")
    parser.add_argument("input_path", help="Path to input PNG file")
    parser.add_argument("array_name", help="Name of the LVGL array (e.g., icon_calendar)")
    parser.add_argument("width", type=int, help="Target width")
    parser.add_argument("height", type=int, help="Target height")
    
    args = parser.parse_args()
    
    out_path = f"c:\\Users\\alain\\BIOMETRICSMANPRO\\crowpanel_firmware\\{args.array_name}.c"
    print(f"Converting {args.input_path} -> {out_path} ({args.width}x{args.height})...")
    convert_img.convert_image(args.input_path, out_path, args.array_name, args.width, args.height)
    print(f"Done {args.array_name}.")

if __name__ == "__main__":
    main()
