cmake_minimum_required(VERSION 3.1)

set(ADF_DEVICE_TREE_FILE bspm_P8_adf-00A0.dts)
set(ADF_DEVICE_TREE_BINARY bspm_P8_adf-00A0.dtbo)
set(ADF_PRU_FIRMWARE_SOURCE_FILE pru.c)
set(ADF_PRU_FIRMWARE_OBJECT_FILE pru.o)
set(ADF_PRU_FIRMWARE_BINARY_FILE pru.bin)
set(ADF_PRU_FIRMWARE_LINKER_COMMANDS AM335x_PRU.cmd)
set(ADF_PRU_FIRMWARE_FILE am335x-pru0-adf-fw)

set(PRU_CGT /usr/share/ti/cgt-pru)
set(PRU_SUPPORT /usr/lib/ti/pru-software-support-package)

add_custom_target(
    device_tree ALL
    COMMAND dtc -O dtb -o ${ADF_DEVICE_TREE_BINARY}
                -b 0 -@ ${CMAKE_CURRENT_SOURCE_DIR}/${ADF_DEVICE_TREE_FILE}
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${ADF_DEVICE_TREE_FILE}
    VERBATIM)

add_custom_target(
    pru_firmware ALL
    COMMAND clpru --define=DECAY_RATE=95
              --include_path=${PRU_CGT}/include
              --include_path=${PRU_SUPPORT}/include
              --include_path=${PRU_SUPPORT}/include/am335x
              -o2
              --display_error_number --asm_listing --c_src_interlist --c++03
              --output_file ${ADF_PRU_FIRMWARE_OBJECT_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${ADF_PRU_FIRMWARE_SOURCE_FILE}
    COMMAND lnkpru
               /usr/lib/ti/pru-software-support-package/labs/lab_2/AM335x_PRU.cmd
               -o ${ADF_PRU_FIRMWARE_BINARY_FILE}
               ${CMAKE_CURRENT_BINARY_DIR}/${ADF_PRU_FIRMWARE_OBJECT_FILE}
    COMMAND cp ${CMAKE_CURRENT_BINARY_DIR}/${ADF_PRU_FIRMWARE_BINARY_FILE} /lib/firmware/${ADF_PRU_FIRMWARE_FILE}
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${ADF_PRU_FIRMWARE_SOURCE_FILE}
    VERBATIM)
