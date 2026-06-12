# TardiOS app SDK

Build an app as a relocatable ELF that TardiOS loads at runtime.

## Build
    idf.py set-target esp32s3    # or esp32p4 for the P4 build
    idf.py build
The relocatable ELF is emitted by `project_elf()` under the build dir.

## Install (Phase 1, sideload)
Copy the ELF to the device filesystem at:
    /storage/apps/hello/app.elf
On boot, TardiOS launches it. (Phase 2 replaces this with the launcher + package manager.)

## The contract
`main/os_api.h` is the ABI. It is a vendored copy of
`tardi/components/os_abi/include/os_api.h` — keep the two in sync.
Your app reaches the OS only through the `os_api_t*` passed as `argv[0]`.
