"""
Pack app ELF files into a LittleFS image for the TardiOS storage partition.

Usage:
    python make_storage.py

Outputs: storage.bin  (flash to 0x310000)

Partition config (matches partitions.csv):
    offset 0x310000, size 0xCF0000, block_size 4096
"""
import sys
import os
from pathlib import Path
import littlefs

BLOCK_SIZE  = 4096
BLOCK_COUNT = 0x4F0000 // BLOCK_SIZE   # 1264 blocks (~4.9 MB)
OUT_IMAGE   = "storage.bin"

# Maps source file path -> destination path on the LittleFS volume
APP_FILES = {
    "app-sdk/build/hello.elf": "/apps/hello/app.elf",
}

def main():
    fs = littlefs.LittleFS(block_size=BLOCK_SIZE, block_count=BLOCK_COUNT)

    for src, dst in APP_FILES.items():
        src_path = Path(src)
        if not src_path.exists():
            print(f"ERROR: {src} not found — run build_app.bat first", file=sys.stderr)
            sys.exit(1)

        # Create parent directories
        parts = Path(dst).parts[1:]   # strip leading /
        for i in range(1, len(parts)):
            d = "/" + "/".join(parts[:i])
            try:
                fs.mkdir(d)
            except littlefs.errors.LittleFSError:
                pass   # already exists

        data = src_path.read_bytes()
        with fs.open(dst, "wb") as f:
            f.write(data)
        print(f"  packed {src}  ->  {dst}  ({len(data):,} bytes)")

    image = bytes(fs.context.buffer)
    Path(OUT_IMAGE).write_bytes(image)
    print(f"\nWrote {OUT_IMAGE} ({len(image):,} bytes)")
    print(f"Flash with:  esptool.py --port COMX write_flash 0x310000 {OUT_IMAGE}")

if __name__ == "__main__":
    main()
