#ifndef __BSP_H_
#define __BSP_H_

#ifdef __cplusplus
extern "C" {
#endif


/***************************** Include Files *********************************/

/************************** Constant Definitions *****************************/
#define INTC_DEVICE_ID      XPAR_SCUGIC_SINGLE_DEVICE_ID
#define GTC_CLK_FREQ_HZ		(XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ / 2)

/************************** Function Prototypes ******************************/
void bsp_init();

#ifdef __cplusplus
}
#endif

#endif /* __BSP_H_ */
