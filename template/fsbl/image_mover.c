/***************************** Include Files *********************************/
#include "fsbl.h"
#include "image_mover.h"

/************************** Constant Definitions *****************************/
/* We are 32-bit machine */
#define MAXIMUM_IMAGE_WORD_LEN 0x40000000

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/
u32 GetPartitionHeaderInfo(u32 ImageBaseAddress);
u32 ValidateHeader(PartHeader *Header);
u32 PartitionMove(u32 ImageBaseAddress, PartHeader *Header);
u32 GetPartitionHeaderStartAddr(u32 ImageAddress, u32 *Offset);
u32 LoadPartitionsHeaderInfo(u32 PartHeaderOffset,  PartHeader *Header);
u32 IsEmptyHeader(struct HeaderArray *H);
u32 ValidatePartitionHeaderChecksum(struct HeaderArray *H);

/************************** Variable Definitions *****************************/
ImageMoverType MoveImage = NULL;
PartHeader PartitionHeader[MAX_PARTITION_NUMBER];
extern u8 LinearBootDeviceFlag;
extern u32 FlashReadBaseAddress;


u32 LoadBootImage(void)
{
	u32 Status;
	u32 ImageStartAddress = 0;
	PartHeader *HeaderPtr;
	u32 PartitionAttr;
	u32 PartitionDataLength;
	u32 PartitionImageLength;
	u32 PartitionLoadAddr;
	/*
	 * 假设MultiBoot寄存器必定等于0
	 * 同时值存在一个镜像保存在起始地址
	 * 不支持fallback
	 */

	// 1. 获得起始地址为0的分区头
    Status = GetPartitionHeaderInfo(ImageStartAddress);
    if (Status != XST_SUCCESS) {
        fsbl_printf(LOG_ERR, "Partition Header Load Failed.\r\n");
		while(1);
	}

	// 2. 获得SSBL的分区头（固定为分区1）
	HeaderPtr = &PartitionHeader[1];

	// 4. 校验分区头
	Status = ValidateHeader(HeaderPtr);
	if (Status != XST_SUCCESS) {
		fsbl_printf(LOG_ERR, "INVALID_HEADER_FAIL.\r\n");
		while(1);
	}

	// 5. 获得分区属性并校验
	PartitionDataLength = HeaderPtr->DataWordLen;
	PartitionImageLength = HeaderPtr->ImageWordLen;
	PartitionAttr = HeaderPtr->PartitionAttr;
	PartitionLoadAddr = HeaderPtr->LoadAddr;

	fsbl_printf(LOG_INFO, "Partition 1 info:\r\n");
	fsbl_printf(LOG_INFO, "  LoadAddr  : 0x%.8lx\r\n", PartitionLoadAddr);
	fsbl_printf(LOG_INFO, "  ExecAddr  : 0x%.8lx\r\n", HeaderPtr->ExecAddr);
	fsbl_printf(LOG_INFO, "  DataLen   : 0x%.8lx words (%lu bytes)\r\n",
			PartitionDataLength, PartitionDataLength << WORD_LENGTH_SHIFT);
	fsbl_printf(LOG_INFO, "  SrcOffset : 0x%.8lx\r\n", HeaderPtr->PartitionStart);

    // 非加密: 镜像长度=数据长度
    if(PartitionDataLength != PartitionImageLength) {
        fsbl_printf(LOG_ERR, "Partition is encrypted.\r\n");
		while(1);
	}

    // 属性必须是PS镜像
    if (!(PartitionAttr & ATTRIBUTE_PS_IMAGE_MASK)) {
        fsbl_printf(LOG_ERR, "Partition is not PS App.\r\n");
		while(1);
	}

    // 属性必须满足DDR空间
    if((PartitionLoadAddr < DDR_START_ADDR) || (PartitionLoadAddr > DDR_END_ADDR)) {
        fsbl_printf(LOG_ERR, "Partition is not valid region.\r\n");
		while(1);
	}

    // 6. 代码搬到DDR
    fsbl_printf(LOG_INFO, "Loading SSBL to DDR...\r\n");
    Status = PartitionMove(ImageStartAddress, HeaderPtr);
    if (Status != XST_SUCCESS) {
        fsbl_printf(LOG_ERR, "Partion move failed.\r\n");
		while(1);
	}
	fsbl_printf(LOG_INFO, "SSBL loaded (%lu bytes)\r\n",
			PartitionDataLength << WORD_LENGTH_SHIFT);

	return HeaderPtr->ExecAddr;
}


u32 GetPartitionHeaderInfo(u32 ImageBaseAddress)
{
    u32 PartitionHeaderOffset;
    u32 Status;

    // 获得分区头表的起始地址
    Status = GetPartitionHeaderStartAddr(ImageBaseAddress, &PartitionHeaderOffset);
    if (Status != XST_SUCCESS) {
    	fsbl_printf(LOG_ERR, "Get Header Start Address Failed\r\n");
    	return XST_FAILURE;
    }

    // 计算得到分区头表的绝对地址
    PartitionHeaderOffset += ImageBaseAddress;
    fsbl_printf(LOG_INFO,"Partition Header Offset:0x%08lx\r\n", PartitionHeaderOffset);

    // 加载分区头表信息
    Status = LoadPartitionsHeaderInfo(PartitionHeaderOffset, &PartitionHeader[0]);
    if (Status != XST_SUCCESS) {
    	fsbl_printf(LOG_ERR, "Header Information Load Failed\r\n");
    	return XST_FAILURE;
    }

    return XST_SUCCESS;
}

u32 ValidateHeader(PartHeader *Header)
{
	struct HeaderArray *Hap;

    Hap = (struct HeaderArray *)Header;

    // 空表是不可以的，必须存在SSBL
    if (IsEmptyHeader(Hap) == XST_SUCCESS) {
        fsbl_printf(LOG_ERR, "IMAGE_HAS_NO_PARTITIONS\r\n");
	    return XST_FAILURE;
	}

    // 校验分区头
    if (ValidatePartitionHeaderChecksum(Hap) != XST_SUCCESS) {
        fsbl_printf(LOG_ERR, "PARTITION_HEADER_CORRUPTION\r\n");
		return XST_FAILURE;
	}

    // 校验镜像大小
    if (Header->ImageWordLen > MAXIMUM_IMAGE_WORD_LEN) {
        fsbl_printf(LOG_ERR, "INVALID_PARTITION_LENGTH\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}


u32 PartitionMove(u32 ImageBaseAddress, PartHeader *Header)
{
    u32 SourceAddr;
    u32 LoadAddr;
    u32 ImageWordLen;
    u32 DataWordLen;

	SourceAddr = ImageBaseAddress;
	SourceAddr += Header->PartitionStart<<WORD_LENGTH_SHIFT;
	LoadAddr = Header->LoadAddr;
	ImageWordLen = Header->ImageWordLen;
	DataWordLen = Header->DataWordLen;

	// 直接调用函数
	return MoveImage(SourceAddr, LoadAddr, (DataWordLen << WORD_LENGTH_SHIFT));
}

u32 GetPartitionHeaderStartAddr(u32 ImageAddress, u32 *Offset)
{
	u32 Status;

    Status = MoveImage(ImageAddress + IMAGE_PHDR_OFFSET, (u32)Offset, 4);
    if (Status != XST_SUCCESS) {
        fsbl_printf(LOG_ERR,"Move Image failed\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

u32 LoadPartitionsHeaderInfo(u32 PartHeaderOffset,  PartHeader *Header)
{
	u32 Status;

    Status = MoveImage(PartHeaderOffset, (u32)Header, sizeof(PartHeader)*MAX_PARTITION_NUMBER);
    if (Status != XST_SUCCESS) {
        fsbl_printf(LOG_ERR,"Move Image failed\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

u32 IsEmptyHeader(struct HeaderArray *H)
{
	int Index;

	for (Index = 0; Index < PARTITION_HDR_WORD_COUNT; Index++) {
		if (H->Fields[Index] != 0x0) {
			return XST_FAILURE;
		}
	}

	return XST_SUCCESS;
}

u32 ValidatePartitionHeaderChecksum(struct HeaderArray *H)
{
	u32 Checksum;
	u32 Count;

	Checksum = 0;

	for (Count = 0; Count < PARTITION_HDR_CHECKSUM_WORD_COUNT; Count++) {
		/*
		 * Read the word from the header
		 */
		Checksum += H->Fields[Count];
	}

	/*
	 * Invert checksum, last bit of error checking
	 */
	Checksum ^= 0xFFFFFFFF;

    /*
     * Validate the checksum
     */
    if (H->Fields[PARTITION_HDR_CHECKSUM_WORD_COUNT] != Checksum) {
        fsbl_printf(LOG_ERR, "Error: Checksum 0x%8.8lx != 0x%8.8lx\r\n",
			Checksum, H->Fields[PARTITION_HDR_CHECKSUM_WORD_COUNT]);
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}
