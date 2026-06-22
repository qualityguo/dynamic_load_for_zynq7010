#include "fsbl.h"
#include "qspi.h"
#include "sd.h"
#include "pcap.h"
#include "image_mover.h"
#include "xparameters.h"
#include "xil_cache.h"
#include "xil_exception.h"
#include "xstatus.h"
#include "fsbl_hooks.h"
#include "xtime_l.h"
#include "xuartps_hw.h"

extern ImageMoverType MoveImage;
extern u8 BitstreamFlag;

u8 LinearBootDeviceFlag=0;
u32 FlashReadBaseAddress = 0;


extern int ps7_init();
extern int ps7_post_config();
static void RegisterHandlers(void);
static void Undef_Handler (void);
static void SVC_Handler (void);
static void PreFetch_Abort_Handler (void);
static void Data_Abort_Handler (void);
static void IRQ_Handler (void);
static void FIQ_Handler (void);
static void RegisterHandlers(void);
u32 DDRInitCheck(void);



int main(void)
{
    u32 BootModeRegister = 0;
    u32 HandoffAddress = 0;
    u32 Status = XST_SUCCESS;
 
    /* 1. PS initialization (clk/mio/ddr) */
    Status = ps7_init();
    if (Status != FSBL_PS7_INIT_SUCCESS) {
    	OutputStatus(PS7_INIT_FAIL);
        FsblHookFallback();
    }
    
    SlcrUnlock();

    /* 2. Clear D-Cache */
    Xil_DCacheFlush();
    Xil_DCacheDisable();

    /* 3. register exception handlers */
    RegisterHandlers();

    /* 4. Clean banner */
    xil_printf("\n\rFSBL (slim)\r\n");

    /* 5. DDR read and write test */
    Status = DDRInitCheck();
    if (Status != XST_SUCCESS) {
    	OutputStatus(DDR_INIT_FAIL);
        FsblHookFallback();
    }

    /* 6. devcfg initialization */
    Status = InitPcap();
    if (Status != XST_SUCCESS) {
    	OutputStatus(PCAP_INIT_FAIL);
        FsblHookFallback(); 
    }

    /* 7. read boot mode register */
    BootModeRegister = Xil_In32(BOOT_MODE_REG) & BOOT_MODES_MASK;

    if(BootModeRegister == SD_MODE) {
        Status = InitSD("BOOT.BIN");
        MoveImage = SDAccess;
    }
#ifdef XPAR_PS7_QSPI_LINEAR_0_S_AXI_BASEADDR
    else if (BootModeRegister == QSPI_MODE) {
        InitQspi();
        MoveImage = QspiAccess;
    }
#endif
    else {
        OutputStatus(ILLEGAL_BOOT_MODE);
        FsblHookFallback();
    }

    /* 8. load ssbl */
    HandoffAddress = LoadBootImage();

    /* 9. handoff to ssbl */
    FsblHandoff(HandoffAddress);

    return XST_SUCCESS;

}

void FsblHandoff(u32 FsblStartAddr)
{
	u32 Status;

	/*
	 * Enable level shifters when a bitstream has been loaded into PL
	 */
	if(BitstreamFlag) {
		ps7_post_config();
		SlcrUnlock();
	}

	/*
	 * FSBL user hook call before handoff to the application
	 */
	Status = FsblHookBeforeHandoff();
	if (Status != XST_SUCCESS) {
		fsbl_printf(DEBUG_GENERAL,"FSBL_HANDOFF_HOOK_FAIL\r\n");
 		OutputStatus(FSBL_HANDOFF_HOOK_FAIL);
		FsblFallback();
	}

	/*
	 * Clear our mark in reboot status register
	 */
	ClearFSBLIn();

	fsbl_printf(DEBUG_GENERAL,"SUCCESSFUL_HANDOFF\r\n");
	OutputStatus(SUCCESSFUL_HANDOFF);
	FsblHandoffExit(FsblStartAddr);

	OutputStatus(ILLEGAL_RETURN);

	FsblFallback();
}


void OutputStatus(u32 State)
{
#ifdef STDOUT_BASEADDRESS
#ifdef XPAR_XUARTPS_0_BASEADDR
	u32 UartReg = 0;
#endif

	xil_printf("FSBL Status = 0x%.4lx\r\n", State);
	/*
	 * The TX buffer needs to be flushed out
	 * If this is not done some of the prints will not appear on the
	 * serial output
	 */
#ifdef XPAR_XUARTPS_0_BASEADDR
	UartReg = Xil_In32(STDOUT_BASEADDRESS + XUARTPS_SR_OFFSET);
	while ((UartReg & XUARTPS_SR_TXEMPTY) != XUARTPS_SR_TXEMPTY) {
		UartReg = Xil_In32(STDOUT_BASEADDRESS + XUARTPS_SR_OFFSET);
	}
#endif
#endif
}

void FsblFallback(void)
{
	fsbl_printf(DEBUG_GENERAL, "enter FsblFallback!\r\n");
	while(1);
}


void ErrorLockdown(u32 State)
{
	OutputStatus(State);
	FsblFallback();
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
	fsbl_printf(DEBUG_GENERAL, "UNDEFINED_HANDLER\r\n");
	ErrorLockdown (EXCEPTION_ID_UNDEFINED_INT);
}

static void SVC_Handler (void)
{
	fsbl_printf(DEBUG_GENERAL, "SVC_HANDLER \r\n");
	ErrorLockdown (EXCEPTION_ID_SWI_INT);
}

static void PreFetch_Abort_Handler (void)
{
	fsbl_printf(DEBUG_GENERAL, "PREFETCH_ABORT_HANDLER \r\n");
	ErrorLockdown (EXCEPTION_ID_PREFETCH_ABORT_INT);
}

static void Data_Abort_Handler (void)
{
	fsbl_printf(DEBUG_GENERAL, "DATA_ABORT_HANDLER \r\n");
	ErrorLockdown (EXCEPTION_ID_DATA_ABORT_INT);
}

static void IRQ_Handler (void)
{
	fsbl_printf(DEBUG_GENERAL, "IRQ_HANDLER \r\n");
	ErrorLockdown (EXCEPTION_ID_IRQ_INT);
}

static void FIQ_Handler (void)
{
	fsbl_printf(DEBUG_GENERAL, "FIQ_HANDLER \r\n");
	ErrorLockdown (EXCEPTION_ID_FIQ_INT);
}


u32 DDRInitCheck(void)
{
	u32 ReadVal;

	/*
	 * Write and Read from the DDR location for sanity checks
	 */
	Xil_Out32(DDR_START_ADDR, DDR_TEST_PATTERN);
	ReadVal = Xil_In32(DDR_START_ADDR);
	if (ReadVal != DDR_TEST_PATTERN) {
		return XST_FAILURE;
	}

	/*
	 * Write and Read from the DDR location for sanity checks
	 */
	Xil_Out32(DDR_START_ADDR + DDR_TEST_OFFSET, DDR_TEST_PATTERN);
	ReadVal = Xil_In32(DDR_START_ADDR + DDR_TEST_OFFSET);
	if (ReadVal != DDR_TEST_PATTERN) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

char *strcpy_rom(char *Dest, const char *Src)
{
	unsigned i;
	for (i=0; Src[i] != '\0'; ++i)
		Dest[i] = Src[i];
	Dest[i] = '\0';
	return Dest;
}

void ClearFSBLIn(void)
{
	Xil_Out32(REBOOT_STATUS_REG,
		(Xil_In32(REBOOT_STATUS_REG)) & ~(FSBL_FAIL_MASK));
}
