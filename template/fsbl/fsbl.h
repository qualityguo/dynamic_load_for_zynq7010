#include "xparameters.h"


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
 * These are the SLCR lock and unlock macros
 */
#define SlcrUnlock()	Xil_Out32(XPS_SYS_CTRL_BASEADDR + 0x08, 0xDF0DDF0D)
#define SlcrLock()		Xil_Out32(XPS_SYS_CTRL_BASEADDR + 0x04, 0x767B767B)