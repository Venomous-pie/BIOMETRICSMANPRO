import random
import string
import time
import sys

def print_slow(text, delay=0.03):
    for char in text:
        sys.stdout.write(char)
        sys.stdout.flush()
        time.sleep(delay)
    print()

def generate_activation_code():
    # The firmware currently accepts any 12-character uppercase alpha string
    return ''.join(random.choices(string.ascii_uppercase, k=12))

def main():
    print("========================================")
    print("   COMPANY MAIN SYSTEM API - ADMIN UI   ")
    print("========================================")
    print()
    
    hw_code = input("Enter the 8-character Hardware Code shown on the device: ").strip()
    
    if len(hw_code) != 8:
        print("Error: Hardware code must be exactly 8 characters.")
        return
        
    print_slow("\nVerifying hardware code against company registry...", 0.05)
    time.sleep(1)
    
    # Mocking registry verification
    print(f"[SUCCESS] Hardware '{hw_code}' recognized as a valid deployment device.")
    
    confirm = input("Do you want to authorize this device and generate an activation code? (y/n): ").strip().lower()
    
    if confirm == 'y':
        print_slow("Generating activation code...", 0.05)
        time.sleep(1)
        
        activation_code = generate_activation_code()
        
        print("\n========================================")
        print("             DEVICE AUTHORIZED            ")
        print("========================================")
        print(f"Hardware Code   : {hw_code}")
        print(f"ACTIVATION CODE : {activation_code}")
        print("========================================")
        print("\nPlease type this 12-character code into the device to unlock its features.")
    else:
        print("Authorization cancelled.")

if __name__ == "__main__":
    main()
