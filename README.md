# RISC-V + LiteX + WolfSSL/WolfCrypt Development Environment Setup

This guide provides a step-by-step procedure to set up a RISC-V embedded development environment using LiteX, LiteX Simulation (litex_sim), and WolfSSL/WolfCrypt, along with the necessary RISC-V toolchain and dependencies.

This setup enables developers to simulate a constraint-based embedded system environment for firmware development, bare-metal applications, and cryptographic experiments.
Follow the steps to setup development environment of embdedded system (RISC-V + LiteX + WolfSSL/WolfCRYPT). Execute the commands step-wise.

## Prerequisites

Ensure your system has the following installed:
- Python 3.8+
- Git
- Build tools: build-essential, cmake, etc.
- Linux environment (Ubuntu recommended)

## 1. Clone the Repository
Clone the project repository containing the LiteX configuration and simulation modules:
```
git clone https://github.com/QTrino-Labs-Pvt-Ltd/Constraint_Env_Sim.git
cd Constraint_Env_Sim
```
## 2. Create and Activate Python Virtual Environment
It is recommended to isolate LiteX and Python dependencies using a virtual environment.
```
python3 -m venv litex-env
source litex-env/bin/activate
```
Your shell should now indicate the active environment (litex-env).
## 3. Run LiteX Setup Script
Make the setup script executable and initialize the LiteX environment.
```
chmod +x litex_setup.py
./litex_setup.py --init --install
```
Install additional Python dependencies:
```
pip3 install meson ninja
```
## 4. Install RISC-V Toolchain and Required Packages
Install the RISC-V GCC toolchain using the LiteX setup utility:
Note: sudo is required because the toolchain installs system-wide.

```
sudo ./litex_setup.py --gcc=riscv
```
Install system dependencies:
```
sudo apt install libevent-dev libjson-c-dev verilator
```
These packages enable simulation and LiteX SoC generation.
## 5. Run LiteX Simulation Environment
Launch the LiteX simulation with a RISC-V VexRiscv CPU core:
```
litex_sim --csr-json csr.json --cpu-type=vexriscv --cpu-variant=full --integrated-main-ram-size=0x06400000
```
Use CTRL + C to exit the simulation.
## 6. Build and Run the LiteX Bare-Metal Demo
Generate the bare-metal demo software:
```
litex_bare_metal_demo --build-path=build/sim
```
Run simulation with the generated boot binary:
```
litex_sim --csr-json csr.json --cpu-type=vexriscv --cpu-variant=full --integrated-main-ram-size=0x06400000 --ram-init=boot.bin
```
## 7. Modify Firmware (boot Directory)

All embedded C source files reside in the boot/ directory.  
To update the firmware:  
1. Modify or add C files under boot/.  
2. Rebuild and test via LiteX simulation.  
3. Re-run the simulation with your updated firmware:  

```
litex_bare_metal_demo --build-path=build/sim
litex_sim --csr-json csr.json --cpu-type=vexriscv --cpu-variant=full --integrated-main-ram-size=0x06400000 --ram-init=boot.bin
```
