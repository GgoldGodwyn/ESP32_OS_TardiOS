# TardiOS app SDK

Build an app as a relocatable ELF that TardiOS loads at runtime.

## Build
    idf.py set-target esp32s3    # or esp32p4 for the P4 build
    idf.py build
The relocatable ELF is emitted by `project_elf()` under the build dir.

## Install (sideload)
Copy the ELF to the device filesystem (internal storage or a mounted microSD
card) at:
    /storage/apps/<your-app-name>/app.elf
    /sdcard/apps/<your-app-name>/app.elf     (SD overrides an internal app of the same name)

TardiOS discovers every app.elf under both roots at boot and lists them in
the launcher — no hardcoded path, no rebuild of the OS needed to add an app.

Optionally add a manifest alongside it to control the display name shown in
the launcher:
    /storage/apps/<your-app-name>/manifest.json
```json
{
    "name": "Human-Readable Name"
}
```
Without a manifest, the launcher just shows the directory name.

## The contract
`main/os_api.h` is the ABI. It is a vendored copy of
`tardi/components/os_abi/include/os_api.h` — keep the two in sync.
Your app reaches the OS only through the `os_api_t*` passed as `argv[0]`.
