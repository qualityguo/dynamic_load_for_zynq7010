#include "fsbl.h"
#include "fsbl_debug.h"
#include "qspi.h"
#include "sd.h"
#include "xparameters.h"
#include "xil_cache.h"

extern ImageMoverType MoveImage;


extern ps7_init();
u32 DDRInitCheck(void);



int main(void)
{
    u32 BootModeRegister = 0;
    u32 HandoffAddress = 0;
    u32 Status = XST_SUCCESS;

    // 1. PS initialization (clk/mio/ddr)
    Status = ps7_init();
    if (Status != FSBL_PS7_INIT_SUCCESS) {
        fsbl_printf(LOG_DEBUG, "PS7 initialization failed with status: 0x%.4lx\r\n", Status); 
        while(1);
    }
    fsbl_printf(LOG_INFO, "ps7_init() completed successfully.\r\n");

    // 2. 解锁SLCR寄存器
    SlcrUnlock();
    fsbl_printf(LOG_INFO, "SLCR unlocked.\r\n");

    // 3. 刷D-Cache
    Xil_DCacheFlush();
    Xil_DCacheDisable();
    fsbl_printf(LOG_INFO, "D-Cache flushed and disabled.\r\n");

    // 4. DDR读写测试
    Status = DDRInitCheck();
    if (Status != XST_SUCCESS) {
        fsbl_printf(LOG_DEBUG, "DDR initialization check failed with status: 0x%.4lx\r\n", Status);
        while(1);
    }
    fsbl_printf(LOG_INFO, "DDR initialization check completed successfully.\r\n");

    // 5. 读取BootMode寄存器
    BootModeRegister = Xil_In32(BOOT_MODE_REG) & BOOT_MODES_MASK;
    fsbl_printf(LOG_INFO, "BootMode register value: 0x%.4lx\r\n", BootModeRegister);

    // 6. 根据BootMode寄存器的值执行相应的操作
    switch (BootModeRegister) {
        case SD_MODE:
            fsbl_printf(LOG_INFO, "Boot mode: SD\r\n");
            Status = InitSD("BOOT.BIN");
            if (Status != XST_SUCCESS) {
                fsbl_printf(LOG_DEBUG, "SD initialization failed with status: 0x%.4lx\r\n", Status);
                while(1);
            }
            MoveImage = SDAccess;
            break;
#ifdef XPAR_PS7_QSPI_LINEAR_0_S_AXI_BASEADDR
        case QSPI_MODE:
            fsbl_printf(LOG_INFO, "Boot mode: QSPI\r\n");
            Status = InitQspi();
            if (Status != XST_SUCCESS) {   
                fsbl_printf(LOG_DEBUG, "QSPI initialization failed with status: 0x%.4lx\r\n", Status);
                while(1);
            }
            MoveImage = QspiAccess;
            break;
#endif
        default:
            fsbl_printf(LOG_DEBUG, "Unsupported boot mode: 0x%.4lx\r\n", BootModeRegister);
            while(1);
    }

    // 7. 搬移SSBL到DDR
    HandoffAddress = LoadBootImage();
    if (HandoffAddress == 0) {  
        fsbl_printf(LOG_DEBUG, "Failed to load boot image.\r\n");
        while(1);
    }

    // 8. 跳转到SSBL
    FsblHandoff(HandoffAddress);

    return XST_SUCCESS;
}


u32 DDRInitCheck(void)
{
    u32 ReadValue = 0;

    // 读写测试1
    Xil_Out32(DDR_START_ADDR, DDR_TEST_PATTERN);
    ReadValue = Xil_In32(DDR_START_ADDR);
    if(ReadValue != DDR_TEST_PATTERN) {
        fsbl_printf(LOG_DEBUG, "DDR test failed at address 0x%.8lx: wrote 0x%.8lx, read 0x%.8lx\r\n", DDR_START_ADDR, DDR_TEST_PATTERN, ReadValue);
        return XST_FAILURE;
    }

    // 读写测试2
    Xil_Out32(DDR_START_ADDR + DDR_TEST_OFFSET, DDR_TEST_PATTERN);
    ReadValue = Xil_In32(DDR_START_ADDR + DDR_TEST_OFFSET);
    if(ReadValue != DDR_TEST_PATTERN) {
        fsbl_printf(LOG_DEBUG, "DDR test failed at address 0x%.8lx: wrote 0x%.8lx, read 0x%.8lx\r\n", DDR_START_ADDR + DDR_TEST_OFFSET, DDR_TEST_PATTERN, ReadValue);
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}