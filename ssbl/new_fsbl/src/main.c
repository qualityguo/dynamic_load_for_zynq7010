/***************************** Include Files *********************************/
#include "fsbl.h"
#include "fsbl_debug.h"
#include "image_mover.h"
#include "qspi.h"
#include "sd.h"
#include "xparameters.h"
#include "xil_cache.h"
#include "xil_exception.h"

/************************** Function Prototypes ******************************/
extern 	int  ps7_init();
static 	void Undef_Handler (void);
static 	void SVC_Handler (void);
static 	void PreFetch_Abort_Handler (void);
static 	void Data_Abort_Handler (void);
static 	void IRQ_Handler (void);
static 	void FIQ_Handler (void);
static 	void RegisterHandlers(void);
static	u32  DDRInitCheck(void);
static	void FsblHandoff(u32 FsblStartAddr);

/************************** Variable Definitions *****************************/
extern ImageMoverType MoveImage;
u8 LinearBootDeviceFlag = 0;
u32 FlashReadBaseAddress = 0;

/************************** Function Implations *****************************/
int main(void)
{
    u32 BootModeRegister = 0;
    u32 HandoffAddress = 0;
    u32 Status = XST_SUCCESS;

    // 1. PS initialization (clk/mio/ddr)
    Status = ps7_init();
    if (Status != FSBL_PS7_INIT_SUCCESS) {
        fsbl_printf(LOG_ERR, "PS7 initialization failed with status: 0x%.4lx\r\n", Status);
        while(1);
    }
    fsbl_printf(LOG_INFO, "[1/5] ps7_init() OK\r\n");

    // 2. 刷D-Cache
    Xil_DCacheFlush();
    Xil_DCacheDisable();

    // 3. 注册异常处理
    RegisterHandlers();
    fsbl_printf(LOG_INFO, "[2/5] Exception handlers registered\r\n");

    // 4. DDR读写测试
    Status = DDRInitCheck();
    if (Status != XST_SUCCESS) {
        fsbl_printf(LOG_ERR, "DDR initialization check failed with status: 0x%.4lx\r\n", Status);
        while(1);
    }
    fsbl_printf(LOG_INFO, "[3/5] DDR test OK (0x%.8lx)\r\n", DDR_START_ADDR);

    // 5. 读取BootMode寄存器
    BootModeRegister = Xil_In32(BOOT_MODE_REG) & BOOT_MODES_MASK;

    // 6. 根据BootMode寄存器的值执行相应的操作
    switch (BootModeRegister) {
        case SD_MODE:
            fsbl_printf(LOG_INFO, "[4/5] Boot mode: SD\r\n");
            Status = InitSD("BOOT.BIN");
            if (Status != XST_SUCCESS) {
                fsbl_printf(LOG_ERR, "SD initialization failed with status: 0x%.4lx\r\n", Status);
                while(1);
            }
            MoveImage = SDAccess;
            break;
#ifdef XPAR_PS7_QSPI_LINEAR_0_S_AXI_BASEADDR
        case QSPI_MODE:
            fsbl_printf(LOG_INFO, "[4/5] Boot mode: QSPI\r\n");
            Status = InitQspi();
            if (Status != XST_SUCCESS) {
                fsbl_printf(LOG_ERR, "QSPI initialization failed with status: 0x%.4lx\r\n", Status);
                while(1);
            }
            MoveImage = QspiAccess;
            break;
#endif
        default:
            fsbl_printf(LOG_ERR, "Unsupported boot mode: 0x%.4lx\r\n", BootModeRegister);
            while(1);
    }

    // 7. 搬移SSBL到DDR
    HandoffAddress = LoadBootImage();
    if (HandoffAddress == 0) {
        fsbl_printf(LOG_ERR, "Failed to load boot image.\r\n");
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
        fsbl_printf(LOG_ERR, "DDR test failed at address 0x%.8lx: wrote 0x%.8lx, read 0x%.8lx\r\n", DDR_START_ADDR, DDR_TEST_PATTERN, ReadValue);
        return XST_FAILURE;
    }

    // 读写测试2
    Xil_Out32(DDR_START_ADDR + DDR_TEST_OFFSET, DDR_TEST_PATTERN);
    ReadValue = Xil_In32(DDR_START_ADDR + DDR_TEST_OFFSET);
    if(ReadValue != DDR_TEST_PATTERN) {
        fsbl_printf(LOG_ERR, "DDR test failed at address 0x%.8lx: wrote 0x%.8lx, read 0x%.8lx\r\n", DDR_START_ADDR + DDR_TEST_OFFSET, DDR_TEST_PATTERN, ReadValue);
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}


void FsblHandoff(u32 FsblStartAddr)
{
	fsbl_printf(LOG_INFO, "[5/5] Handoff to 0x%.8lx\r\n", FsblStartAddr);
	fsbl_printf(LOG_INFO, "SUCCESSFUL_HANDOFF\r\n");

	FsblHandoffExit(FsblStartAddr);

	fsbl_printf(LOG_ERR, "Handoff failed - should not reach here\r\n");

	while(1);
}

char *strcpy_rom(char *Dest, const char *Src)
{
	unsigned i;
	for (i=0; Src[i] != '\0'; ++i)
		Dest[i] = Src[i];
	Dest[i] = '\0';
	return Dest;
}

static void RegisterHandlers(void)
{
	Xil_ExceptionInit();

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_UNDEFINED_INT,
					(Xil_ExceptionHandler)Undef_Handler,
					(void *) 0);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_SWI_INT,
					(Xil_ExceptionHandler)SVC_Handler,
					(void *) 0);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_PREFETCH_ABORT_INT,
				(Xil_ExceptionHandler)PreFetch_Abort_Handler,
				(void *) 0);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_DATA_ABORT_INT,
				(Xil_ExceptionHandler)Data_Abort_Handler,
				(void *) 0);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
				(Xil_ExceptionHandler)IRQ_Handler,(void *) 0);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_FIQ_INT,
			(Xil_ExceptionHandler)FIQ_Handler,(void *) 0);

	Xil_ExceptionEnable();

}

static void Undef_Handler (void)
{
	fsbl_printf(LOG_ERR, "UNDEFINED_HANDLER\r\n");
	while(1);
}

static void SVC_Handler (void)
{
	fsbl_printf(LOG_ERR, "SVC_HANDLER \r\n");
	while(1);
}

static void PreFetch_Abort_Handler (void)
{
	fsbl_printf(LOG_ERR, "PREFETCH_ABORT_HANDLER \r\n");
	while(1);
}

static void Data_Abort_Handler (void)
{
	fsbl_printf(LOG_ERR, "DATA_ABORT_HANDLER \r\n");
	while(1);
}

static void IRQ_Handler (void)
{
	fsbl_printf(LOG_ERR, "IRQ_HANDLER \r\n");
	while(1);
}

static void FIQ_Handler (void)
{
	fsbl_printf(LOG_ERR, "FIQ_HANDLER \r\n");
	while(1);
}
