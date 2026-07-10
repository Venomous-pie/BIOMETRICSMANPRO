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
    # Format as XXXX-XXXX-XXXX
    s1 = ''.join(random.choices(string.ascii_uppercase, k=4))
    s2 = ''.join(random.choices(string.ascii_uppercase, k=4))
    s3 = ''.join(random.choices(string.ascii_uppercase, k=4))
    return f"{s1}-{s2}-{s3}"

def main():
    print("========================================")
    print("   COMPANY MAIN SYSTEM API - ADMIN UI   ")
    print("========================================")
    print()
    
    hw_code = input("Enter the 9-character Hardware Code (XXXX-XXXX) shown on the device: ").strip()
    
    if len(hw_code) != 9:
        print("Error: Hardware code must be exactly 9 characters.")
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
        print("\nPlease type this 14-character code into the device to unlock its features.")
    else:
        print("Authorization cancelled.")

if __name__ == "__main__":
    main()
