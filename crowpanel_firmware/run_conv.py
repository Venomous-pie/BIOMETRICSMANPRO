import sys
import os

sys.path.append(r"c:\Users\alain\BIOMETRICSMANPRO\crowpanel_firmware")
from convert_img import convert_image

convert_image(
    r"c:\Users\alain\BIOMETRICSMANPRO\data\icons\icon_clock_network.png", 
    r"c:\Users\alain\BIOMETRICSMANPRO\crowpanel_firmware\icon_clock_network.c",
    "icon_clock_network",
    96, 96
)
convert_image(
    r"c:\Users\alain\BIOMETRICSMANPRO\data\icons\icon_device_info.png", 
    r"c:\Users\alain\BIOMETRICSMANPRO\crowpanel_firmware\icon_device_info.c",
    "icon_device_info",
    96, 96
)
convert_image(
    r"c:\Users\alain\BIOMETRICSMANPRO\data\icons\icon_server_device.png", 
    r"c:\Users\alain\BIOMETRICSMANPRO\crowpanel_firmware\icon_server_device.c",
    "icon_server_device",
    96, 96
)
convert_image(
    r"c:\Users\alain\BIOMETRICSMANPRO\data\icons\icon_user_a.png", 
    r"c:\Users\alain\BIOMETRICSMANPRO\crowpanel_firmware\icon_user_a.c",
    "icon_user_a",
    128, 128
)
