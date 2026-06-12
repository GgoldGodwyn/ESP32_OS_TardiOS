# NOTE: managed-component require names use the double-underscore form.
# espressif/elf_loader -> espressif__elf_loader
idf_component_register(SRCS "app_loader.c"
                       INCLUDE_DIRS "include"
                       REQUIRES os_abi
                       PRIV_REQUIRES espressif__elf_loader)
