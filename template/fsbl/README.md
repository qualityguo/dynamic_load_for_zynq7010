## FSBL代码说明

### 一、main函数初始化流程

- `ps7_init`，初始化MIO、PLL、CLK、DDR、Peripherals
- `Xil_DCacheFlush`+`Xil_DCacheDisable`：刷一下D-Cache
- `DDRInitCheck`：写一个地址然后读取，比较
- 读寄存器`BOOT_MODE_REG`，根据启动方式分流
  - SD卡，初始化SD卡，并设置`MoveImage`函数指针
  - QSPIFlash，初始化Flash，并设置`MoveImage`函数指针
- 调用`LoadBootImage`，实现代码搬移，并返回入口地址`HandoffAddress`
- 调用`FsblHandoff`跳转执行

### 二、SD卡路径-初始化

- 函数主体：`InitSD("BOOT.BIN");`

  - ```c
    // 挂载文件系统
    FATFS fatfs;
    FIL fil;
    TCHAR *path = "0:/";
    char *filename = "BOOT.BIN";
    f_mount(&fatfs, path, 0);
    // 拷贝名字-为了安全
    strcpy_rom(boot_file, filename);
    // 设置BOOT.BIN的加载地址
    FlashReadBaseAddress = XPAR_PS7_SD_0_S_AXI_BASEADDR;
    // 打开文件
    f_open(&fil, boot_file, FA_READ);
    ```

### 三、SD卡路径-代码搬移

- 实际就是找到分区头，最终调用`MoveImage`函数

### 四、Flash路径-初始化

- 函数主体：`InitQspi();`

  - ```
    // 设置BOOT.BIN的加载地址
    FlashReadBaseAddress = XPS_QSPI_LINEAR_BASEADDR;
    // 初始化QSPI控制器
    XQspiPs QspiInstance;
    XQspiPs *QspiInstancePtr;
    XQspiPs_Config *QspiConfig;
    QspiConfig = XQspiPs_LookupConfig(QSPI_DEVICE_ID);
    XQspiPs_CfgInitialize(QspiInstancePtr, QspiConfig,QspiConfig->BaseAddress);
    // 配置基本参数-强制片选、驱动HOLD_B、设置时钟分频
    XQspiPs_SetOptions(QspiInstancePtr, XQSPIPS_FORCE_SSELECT_OPTION | XQSPIPS_HOLD_B_DRIVE_OPTION);
    XQspiPs_SetClkPrescaler(QspiInstancePtr, XQSPIPS_CLK_PRESCALE_8);
    XQspiPs_SetSlaveSelect(QspiInstancePtr);
    // 读取Flash-ID
    FlashReadID();
    // 根据连接模式配置
    // 情况一：单闪存，小于16MB，线性模式，支持直接内存访问
    XQspiPs_SetOptions(QspiInstancePtr,  XQSPIPS_LQSPI_MODE_OPTION | XQSPIPS_HOLD_B_DRIVE_OPTION);
    // 设置线性模式
    XQspiPs_SetLqspiConfigReg(QspiInstancePtr, ConfigCmd);
    // 使能控制器
    XQspiPs_Enable(QspiInstancePtr);
    // 情况二：单闪存，大于16MB，IO模式
    // 设置IO模式
    XQspiPs_SetLqspiConfigReg(QspiInstancePtr, ConfigCmd);
    // 使能控制器
    XQspiPs_Enable(QspiInstancePtr);
    // 情况三：双并，小于16MB，线性模式
    XQspiPs_SetOptions(QspiInstancePtr,  XQSPIPS_LQSPI_MODE_OPTION | XQSPIPS_HOLD_B_DRIVE_OPTION);
    XQspiPs_SetLqspiConfigReg(QspiInstancePtr, DUAL_QSPI_CONFIG_FAST_QUAD_READ);
    XQspiPs_Enable(QspiInstancePtr);
    // 情况四：双并，大于16MB，IO模式
    XQspiPs_SetLqspiConfigReg(QspiInstancePtr, DUAL_QSPI_IO_CONFIG_FAST_QUAD_READ);
    XQspiPs_Enable(QspiInstancePtr);
    // 情况五：双叠
    XQspiPs_SetLqspiConfigReg(QspiInstancePtr, ConfigCmd);
    ```

### 五、Flash路径-代码搬移

- 实际就是找到分区头，最终调用`MoveImage`函数

