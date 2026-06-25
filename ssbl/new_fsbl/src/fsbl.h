#ifndef __FSBL_H_
#define __FSBL_H_

#ifdef __cplusplus
extern "C" {
#endif
/***************************** Include Files *********************************/
#include "xparameters.h"
#include "xil_types.h"
#include "xstatus.h"
#include "xil_io.h"
#include "fsbl_debug.h"
#include "ps7_init.h"

/************************** Constant Definitions *****************************/
#define WORD_LENGTH_SHIFT	2

/*
 * Backward compatibility for ps7_init
 */
#ifdef NEW_PS7_ERR_CODE
#define FSBL_PS7_INIT_SUCCESS	PS7_INIT_SUCCESS
#else
#define FSBL_PS7_INIT_SUCCESS	(1)
#endif

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/
/*
 * SLCR Registers
 */
#define PS_RST_CTRL_REG			(XPS_SYS_CTRL_BASEADDR + 0x200)
#define FPGA_RESET_REG			(XPS_SYS_CTRL_BASEADDR + 0x240)
#define RESET_REASON_REG		(XPS_SYS_CTRL_BASEADDR + 0x250)
#define RESET_REASON_CLR		(XPS_SYS_CTRL_BASEADDR + 0x254)
#define REBOOT_STATUS_REG		(XPS_SYS_CTRL_BASEADDR + 0x258)
#define BOOT_MODE_REG			(XPS_SYS_CTRL_BASEADDR + 0x25C)
#define PS_LVL_SHFTR_EN			(XPS_SYS_CTRL_BASEADDR + 0x900)

/*
 * SLCR BOOT Mode Register defines
 */
#define BOOT_MODES_MASK			0x00000007 /**< FLASH types */

/*
 * Boot Modes
 */
#define JTAG_MODE			0x00000000 /**< JTAG Boot Mode */
#define QSPI_MODE			0x00000001 /**< QSPI Boot Mode */
#define SD_MODE				0x00000005 /**< SD Boot Mode */
#define MMC_MODE			0x00000006 /**< MMC Boot Device */

/*
 * DDR start address for storing the data temporarily(1M)
 * Need to finalize correct logic
 */
#ifdef XPAR_PS7_DDR_0_S_AXI_BASEADDR
#define DDR_START_ADDR 	XPAR_PS7_DDR_0_S_AXI_BASEADDR
#define DDR_END_ADDR	XPAR_PS7_DDR_0_S_AXI_HIGHADDR
#else
/*
 * In case of PL DDR, this macros defined based PL DDR address
 */
#define DDR_START_ADDR 	0x00
#define DDR_END_ADDR	0x00
#endif

#define DDR_TEMP_START_ADDR 	DDR_START_ADDR

/*
 * DDR test pattern
 */
#define DDR_TEST_PATTERN	0xAA55AA55
#define DDR_TEST_OFFSET		0x100000

/*
 * These are the SLCR lock and unlock macros
 */
#define SlcrUnlock()	Xil_Out32(XPS_SYS_CTRL_BASEADDR + 0x08, 0xDF0DDF0D)
#define SlcrLock()		Xil_Out32(XPS_SYS_CTRL_BASEADDR + 0x04, 0x767B767B)

#define IMAGE_HEADER_CHECKSUM_COUNT 10

/* Boot ROM Image defines */
#define IMAGE_WIDTH_CHECK_OFFSET        (0x020)	/**< 0xaa995566 Width Detection word */
#define IMAGE_IDENT_OFFSET              (0x024) /**< 0x584C4E58 "XLNX" */
#define IMAGE_ENC_FLAG_OFFSET           (0x028) /**< 0xA5C3C5A3 */
#define IMAGE_USR_DEF_OFFSET            (0x02C)	/**< undefined  could be used as  */
#define IMAGE_SOURCE_ADDR_OFFSET        (0x030)	/**< start address of image  */
#define IMAGE_BYTE_LEN_OFFSET           (0x034)	/**< length of image> in bytes  */
#define IMAGE_DEST_ADDR_OFFSET          (0x038)	/**< destination address in OCM */
#define IMAGE_EXECUTE_ADDR_OFFSET       (0x03c)	/**< address to start executing at */
#define IMAGE_TOT_BYTE_LEN_OFFSET       (0x040)	/**< total length of image in bytes */
#define IMAGE_QSPI_CFG_WORD_OFFSET      (0x044)	/**< QSPI configuration data */
#define IMAGE_CHECKSUM_OFFSET           (0x048) /**< Header Checksum offset */
#define IMAGE_IDENT                     (0x584C4E58) /**< XLNX pattern */



/************************** Function Prototypes ******************************/
char *strcpy_rom(char *Dest, const char *Src);
void FsblHandoffExit(u32 FsblStartAddr);

/************************** Variable Definitions *****************************/


#ifdef __cplusplus
}
#endif

#endif /* __FSBL_H_ */
