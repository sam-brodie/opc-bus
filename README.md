# Prerequisites:
Kvaser CAN lib is required from [here](https://kvaser.com/canlib-webhelp/section_install_windows.htm) (can work with the virtual CAN bus or physical hardware). OpenSSL is required (bundled with git or [here](https://slproweb.com/products/Win32OpenSSL.html))
- Kvaser CAN Lib is in C:/Program Files (x86)/Kvaser/Canlib/INC
- Opne SSL (C:/Program Files/OpenSSL-Win64/)

# Cloning the repo
clone with 'git clone --recursive https://github.com/sam-brodie/opc-bus.git'
The '--recursive' is required to also download the open62541 library with the correct version

# After cloning
After clone generate the certificates (run: testCerts/make_the_certs.bat). Click on the CA certificate and install it so the client and server certificates are trusted.

# Building
open CMake.txt with Visual Studio (open Visual Studio -> click continue without code -> file -> open -> CMake Project)
Generate the cache and click build all

# Running
The main exes are ClientCanEncryption.exe and ServerCanEncryption.exe
