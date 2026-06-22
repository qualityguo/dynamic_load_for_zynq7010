/******************************************************************************
* Copyright (c) 2011 - 2020 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
*
* @file image_mover.c
*
* Move partitions to either DDR to execute or to program FPGA.
* It performs partition walk.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver	Who	Date		Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a jz	05/24/11	Initial release
* 2.00a jz	06/30/11	Updated partition header defs for 64-byte
*			 			alignment change in data2mem tool
* 2.00a mb	05/25/12	Updated for standalone based bsp FSBL
* 			 			Nand/SD encryption and review comments
* 3.00a np	08/30/12	Added FSBL user hook calls
* 						(before and after bitstream download.)
* 4.00a sgd	02/28/13	Fix for CR#691148 Secure bootmode error in devcfg test
*						Fix for CR#695578 FSBL failed to load standalone 
*						application in secure bootmode
*
* 4.00a sgd	04/23/13	Fix for CR#710128 FSBL failed to load standalone 
*						application in secure bootmode
* 5.00a kc	07/30/13	Fix for CR#724165 Partition Header used by FSBL 
*						is not authenticated
* 						Fix for CR#724166 FSBL doesn�t use PPK authenticated 
*						by Boot ROM for authenticating the Partition images 
* 						Fix for CR#732062 FSBL fails to build if UART not 
*						available 
* 7.00a kc  10/30/13    Fix for CR#755245 FSBL does not load partition
*                       if eMMC has only one partition
* 8.00a kc  01/16/13    Fix for CR#767798  FSBL MD5 Checksum failure
* 						for encrypted images
*						Fix for CR#761895 FSBL should authenticate image
*						only if partition owner was not set to u-boot
* 9.00a kc  04/16/14    Fix for CR#785778  FSBL takes 8 seconds to 
* 						authenticate (RSA) a bitstream on zc706
* 10.00a kc 07/15/14	Fix for CR#804595 Zynq FSBL - Issues with
* 						fallback image offset handling using MD5
* 						Fix for PR#782309 Fallback support for AES
* 						encryption with E-Fuse - Enhancement
* 11.00a ka 10/12/18    Fix for CR#1006294 Zynq FSBL - Zynq FSBL does not check
* 						USE_AES_ONLY eFuse
*
* </pre>
*
* @note
*	A partition is either an executable or a bitstream to program FPGA
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "fsbl.h"
#include "image_mover.h"
#include "xil_printf.h"
#include "xreg_cortexa9.h"
#include "pcap.h"
#include "fsbl_hooks.h"

/************************** Constant Definitions *****************************/

/* We are 32-bit machine */
#define MAXIMUM_IMAGE_WORD_LEN 0x40000000

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/
/*
 * Partition information flags
 */
u8 EncryptedPartitionFlag;
u8 PLPartitionFlag;
u8 PSPartitionFlag;
u8 SignedPartitionFlag;
u8 PartitionChecksumFlag;
u8 BitstreamFlag;

ImageMoverType MoveImage;

/*
 * Header array
 */
PartHeader PartitionHeader[MAX_PARTITION_NUMBER];
u32 PartitionCount;
u32 FsblLength;

extern u32 FlashReadBaseAddress;
extern u8 LinearBootDeviceFlag;
extern XDcfg *DcfgInstPtr;

/*****************************************************************************/
/**
*
* This function
*
* @param
*
* @return
*
*
* @note		None
*
****************************************************************************/
u32 LoadBootImage(void)
{
	u32 MultiBootReg = 0;
	u32 ImageStartAddress = 0;
	u32 PartitionDataLength;
	u32 PartitionImageLength;
	u32 PartitionAttr;
	u32 PartitionLoadAddr;
	u32 PartitionStartAddr;
	u32 Status;
	PartHeader *HeaderPtr;
#ifndef FORCE_USE_AES_EXCLUDE
	u32 EncOnly;
#endif

	/*
	 * read the multiboot register and compute image start address
	 */
	MultiBootReg = XDcfg_ReadReg(DcfgInstPtr->Config.BaseAddr,
			XDCFG_MULTIBOOT_ADDR_OFFSET);
	ImageStartAddress = (MultiBootReg & PCAP_MBOOT_REG_REBOOT_OFFSET_MASK)
								* GOLDEN_IMAGE_OFFSET;

	fsbl_printf(DEBUG_INFO,"Multiboot Register: 0x%08lx\r\n",MultiBootReg);
	fsbl_printf(DEBUG_INFO,"Image Start Address: 0x%08lx\r\n",ImageStartAddress);

	/*
	 * Get partitions header information
	 */
	Status = GetPartitionHeaderInfo(ImageStartAddress);
	if (Status != XST_SUCCESS) {
		fsbl_printf(DEBUG_GENERAL, "Partition Header Load Failed\r\n");
		ErrorLockdown(GET_HEADER_INFO_FAIL);
	}

	/*
	 * Load only the first partition (partition[1], right after FSBL's own)
	 */
	HeaderPtr = &PartitionHeader[1];

	fsbl_printf(DEBUG_INFO, "Loading partition 1\r\n");
	HeaderDump(HeaderPtr);

	/*
	 * Validate partition header
	 */
	Status = ValidateHeader(HeaderPtr);
	if (Status != XST_SUCCESS) {
		fsbl_printf(DEBUG_GENERAL, "INVALID_HEADER_FAIL\r\n");
		ErrorLockdown(INVALID_HEADER_FAIL);
	}

	/*
	 * Load partition header information in to local variables
	 */
	PartitionDataLength = HeaderPtr->DataWordLen;
	PartitionImageLength = HeaderPtr->ImageWordLen;
	PartitionAttr = HeaderPtr->PartitionAttr;
	PartitionLoadAddr = HeaderPtr->LoadAddr;

	/*
	 * Determine partition type (PL bitstream or PS application)
	 */
	if (PartitionAttr & ATTRIBUTE_PL_IMAGE_MASK) {
		fsbl_printf(DEBUG_INFO, "Bitstream\r\n");
		PLPartitionFlag = 1;
		BitstreamFlag = 1;
	}

	if (PartitionAttr & ATTRIBUTE_PS_IMAGE_MASK) {
		fsbl_printf(DEBUG_INFO, "Application\r\n");
		PSPartitionFlag = 1;
	}

	/*
	 * Encrypted partition will have different value
	 * for Image length and data length
	 */
	if (PartitionDataLength != PartitionImageLength) {
		fsbl_printf(DEBUG_INFO, "Encrypted\r\n");
		EncryptedPartitionFlag = 1;
	}

#ifndef FORCE_USE_AES_EXCLUDE
	EncOnly = XDcfg_ReadReg(DcfgInstPtr->Config.BaseAddr,
	                        XDCFG_STATUS_OFFSET) &
			XDCFG_STATUS_EFUSE_SEC_EN_MASK;
	if ((EncOnly != 0) && (EncryptedPartitionFlag == 0)) {
		fsbl_printf(DEBUG_GENERAL,"EFUSE_SEC_EN bit is set,"
	                                " Encryption is mandatory\r\n");
		ErrorLockdown(PARTITION_LOAD_FAIL);
	}
#endif

	/*
	 * Check for partition checksum check
	 */
	if (PartitionAttr & ATTRIBUTE_CHECKSUM_TYPE_MASK) {
		PartitionChecksumFlag = 1;
	}

	/*
	 * Load address sanity check for PS partition
	 */
	if (PSPartitionFlag &&
			((PartitionLoadAddr < DDR_START_ADDR) ||
			 (PartitionLoadAddr > DDR_END_ADDR))) {
		fsbl_printf(DEBUG_GENERAL, "INVALID_LOAD_ADDRESS_FAIL\r\n");
		ErrorLockdown(INVALID_LOAD_ADDRESS_FAIL);
	}

	/*
	 * FSBL user hook call before bitstream download
	 */
	if (PLPartitionFlag) {
		Status = FsblHookBeforeBitstreamDload();
		if (Status != XST_SUCCESS) {
			fsbl_printf(DEBUG_GENERAL,"FSBL_BEFORE_BSTREAM_HOOK_FAIL\r\n");
			ErrorLockdown(FSBL_BEFORE_BSTREAM_HOOK_FAIL);
		}
	}

	/*
	 * Move partition from boot device
	 */
	Status = PartitionMove(ImageStartAddress, HeaderPtr);
	if (Status != XST_SUCCESS) {
		fsbl_printf(DEBUG_GENERAL,"PARTITION_MOVE_FAIL\r\n");
		ErrorLockdown(PARTITION_MOVE_FAIL);
	}

	if ((SignedPartitionFlag) || (PartitionChecksumFlag)) {
		if(PLPartitionFlag) {
			/*
			 * PL partition loaded in to DDR temporary address
			 */
			PartitionStartAddr = DDR_TEMP_START_ADDR;
		} else {
			PartitionStartAddr = PartitionLoadAddr;
		}

		/*
		 * Decrypt PS partition
		 */
		if (EncryptedPartitionFlag && PSPartitionFlag) {
			Status = DecryptPartition(PartitionStartAddr,
					PartitionDataLength,
					PartitionImageLength);
			if (Status != XST_SUCCESS) {
				fsbl_printf(DEBUG_GENERAL,"DECRYPTION_FAIL\r\n");
				ErrorLockdown(DECRYPTION_FAIL);
			}
		}

		/*
		 * Load Signed/Checksummed PL partition in Fabric
		 */
		if (PLPartitionFlag) {
			Status = PcapLoadPartition((u32*)PartitionStartAddr,
					(u32*)PartitionLoadAddr,
					PartitionImageLength,
					PartitionDataLength,
					EncryptedPartitionFlag);
			if (Status != XST_SUCCESS) {
				fsbl_printf(DEBUG_GENERAL,"BITSTREAM_DOWNLOAD_FAIL\r\n");
				ErrorLockdown(BITSTREAM_DOWNLOAD_FAIL);
			}
		}
	}

	/*
	 * FSBL user hook call after bitstream download
	 */
	if (PLPartitionFlag) {
		Status = FsblHookAfterBitstreamDload();
		if (Status != XST_SUCCESS) {
			fsbl_printf(DEBUG_GENERAL,"FSBL_AFTER_BSTREAM_HOOK_FAIL\r\n");
			ErrorLockdown(FSBL_AFTER_BSTREAM_HOOK_FAIL);
		}
	}

	return HeaderPtr->ExecAddr;
}

/*****************************************************************************/
/**
*
* This function loads all partition header information in global array
*
* @param	ImageAddress is the start address of the image
*
* @return	- XST_SUCCESS if Get partition Header information successful
*			- XST_FAILURE if Get Partition Header information failed
*
* @note		None
*
****************************************************************************/
u32 GetPartitionHeaderInfo(u32 ImageBaseAddress)
{
    u32 PartitionHeaderOffset;
    u32 Status;


    /*
     * Get the length of the FSBL from BootHeader
     */
    Status = GetFsblLength(ImageBaseAddress, &FsblLength);
    if (Status != XST_SUCCESS) {
    	fsbl_printf(DEBUG_GENERAL, "Get Header Start Address Failed\r\n");
    	return XST_FAILURE;
    }

    /*
    * Get the start address of the partition header table
    */
    Status = GetPartitionHeaderStartAddr(ImageBaseAddress,
    				&PartitionHeaderOffset);
    if (Status != XST_SUCCESS) {
    	fsbl_printf(DEBUG_GENERAL, "Get Header Start Address Failed\r\n");
    	return XST_FAILURE;
    }

    /*
     * Header offset on flash
     */
    PartitionHeaderOffset += ImageBaseAddress;

    fsbl_printf(DEBUG_INFO,"Partition Header Offset:0x%08lx\r\n",
    		PartitionHeaderOffset);

    /*
     * Load all partitions header data in to global variable
     */
    Status = LoadPartitionsHeaderInfo(PartitionHeaderOffset,
    				&PartitionHeader[0]);
    if (Status != XST_SUCCESS) {
    	fsbl_printf(DEBUG_GENERAL, "Header Information Load Failed\r\n");
    	return XST_FAILURE;
    }

    /*
     * Get partitions count from partitions header information
     */
	PartitionCount = GetPartitionCount(&PartitionHeader[0]);

    fsbl_printf(DEBUG_INFO, "Partition Count: %lu\r\n", PartitionCount);

    /*
     * Partition Count check
     */
    if (PartitionCount >= MAX_PARTITION_NUMBER) {
        fsbl_printf(DEBUG_GENERAL, "Invalid Partition Count\r\n");
		return XST_FAILURE;
#ifndef MMC_SUPPORT
    } else if (PartitionCount <= 1) {
        fsbl_printf(DEBUG_GENERAL, "There is no partition to load\r\n");
		return XST_FAILURE;
#endif
	}

    return XST_SUCCESS;
}


/*****************************************************************************/
/**
*
* This function goes to the partition header of the specified partition
*
* @param	ImageAddress is the start address of the image
*
* @return	Offset Partition header address of the image
*
* @return	- XST_SUCCESS if Get Partition Header start address successful
* 			- XST_FAILURE if Get Partition Header start address failed
*
* @note		None
*
****************************************************************************/
u32 GetPartitionHeaderStartAddr(u32 ImageAddress, u32 *Offset)
{
	u32 Status;

	Status = MoveImage(ImageAddress + IMAGE_PHDR_OFFSET, (u32)Offset, 4);
	if (Status != XST_SUCCESS) {
		fsbl_printf(DEBUG_GENERAL,"Move Image failed\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function gets the length of the FSBL
*
* @param	ImageAddress is the start address of the image
*
* @return	FsblLength is the length of the fsbl
*
* @return	- XST_SUCCESS if fsbl length reading is successful
* 			- XST_FAILURE if fsbl length reading failed
*
* @note		None
*
****************************************************************************/
u32 GetFsblLength(u32 ImageAddress, u32 *FsblLength)
{
	u32 Status;

	Status = MoveImage(ImageAddress + IMAGE_TOT_BYTE_LEN_OFFSET,
							(u32)FsblLength, 4);
	if (Status != XST_SUCCESS) {
		fsbl_printf(DEBUG_GENERAL,"Move Image failed reading FsblLength\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function get the header information of the all the partitions and load into
* global array
*
* @param	PartHeaderOffset Offset address where the header information present
*
* @param	Header Partition header pointer
*
* @return	- XST_SUCCESS if Load Partitions Header information successful
*			- XST_FAILURE if Load Partitions Header information failed
*
* @note		None
*
****************************************************************************/
u32 LoadPartitionsHeaderInfo(u32 PartHeaderOffset,  PartHeader *Header)
{
	u32 Status;

	Status = MoveImage(PartHeaderOffset, (u32)Header, sizeof(PartHeader)*MAX_PARTITION_NUMBER);
	if (Status != XST_SUCCESS) {
		fsbl_printf(DEBUG_GENERAL,"Move Image failed\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}


/*****************************************************************************/
/**
*
* This function dumps the partition header.
*
* @param	Header Partition header pointer
*
* @return	None
*
* @note		None
*
******************************************************************************/
void HeaderDump(PartHeader *Header)
{
	fsbl_printf(DEBUG_INFO, "Header Dump\r\n");
	fsbl_printf(DEBUG_INFO, "Image Word Len: 0x%08lx\r\n",
									Header->ImageWordLen);
	fsbl_printf(DEBUG_INFO, "Data Word Len: 0x%08lx\r\n",
									Header->DataWordLen);
	fsbl_printf(DEBUG_INFO, "Partition Word Len:0x%08lx\r\n",
									Header->PartitionWordLen);
	fsbl_printf(DEBUG_INFO, "Load Addr: 0x%08lx\r\n",
									Header->LoadAddr);
	fsbl_printf(DEBUG_INFO, "Exec Addr: 0x%08lx\r\n",
									Header->ExecAddr);
	fsbl_printf(DEBUG_INFO, "Partition Start: 0x%08lx\r\n",
									Header->PartitionStart);
	fsbl_printf(DEBUG_INFO, "Partition Attr: 0x%08lx\r\n",
									Header->PartitionAttr);
	fsbl_printf(DEBUG_INFO, "Partition Checksum Offset: 0x%08lx\r\n",
										Header->CheckSumOffset);
	fsbl_printf(DEBUG_INFO, "Section Count: 0x%08lx\r\n",
									Header->SectionCount);
	fsbl_printf(DEBUG_INFO, "Checksum: 0x%08lx\r\n",
									Header->CheckSum);
}


/******************************************************************************/
/**
*
* This function calculates the partitions count from header information
*
* @param	Header Partition header pointer
*
* @return	Count Partition count
*
* @note		None
*
*******************************************************************************/
u32 GetPartitionCount(PartHeader *Header)
{
    u32 Count=0;
    struct HeaderArray *Hap;

    for(Count = 0; Count < MAX_PARTITION_NUMBER; Count++) {
        Hap = (struct HeaderArray *)&Header[Count];
        if(IsLastPartition(Hap)!=XST_FAILURE)
            break;
    }

	return Count;
}

/******************************************************************************/
/**
* This function check whether the current partition is the end of partitions
*
* The partition is the end of the partitions if it looks like this:
*	0x00000000
*	0x00000000
*	....
*	0x00000000
*	0x00000000
*	0xFFFFFFFF
*
* @param	H is a pointer to struct HeaderArray
*
* @return
*		- XST_SUCCESS if it is the last partition
*		- XST_FAILURE if it is not last partition
*
****************************************************************************/
u32 IsLastPartition(struct HeaderArray *H)
{
	int Index;

	if (H->Fields[PARTITION_HDR_CHECKSUM_WORD_COUNT] != 0xFFFFFFFF) {
		return	XST_FAILURE;
	}

	for (Index = 0; Index < PARTITION_HDR_WORD_COUNT - 1; Index++) {

        if (H->Fields[Index] != 0x0) {
			return XST_FAILURE;
		}
	}

    return XST_SUCCESS;
}


/******************************************************************************/
/**
*
* This function validates the partition header.
*
* @param	Header Partition header pointer
*
* @return
*		- XST_FAILURE if bad header.
* 		- XST_SUCCESS if successful.
*
* @note		None
*
*******************************************************************************/
u32 ValidateHeader(PartHeader *Header)
{
	struct HeaderArray *Hap;

    Hap = (struct HeaderArray *)Header;

	/*
	 * If there are no partitions to load, fail
	 */
	if (IsEmptyHeader(Hap) == XST_SUCCESS) {
		fsbl_printf(DEBUG_GENERAL, "IMAGE_HAS_NO_PARTITIONS\r\n");
	    return XST_FAILURE;
	}

	/*
	 * Validate partition header checksum
	 */
	if (ValidatePartitionHeaderChecksum(Hap) != XST_SUCCESS) {
		fsbl_printf(DEBUG_GENERAL, "PARTITION_HEADER_CORRUPTION\r\n");
		return XST_FAILURE;
	}

    /*
     * Validate partition data size
     */
	if (Header->ImageWordLen > MAXIMUM_IMAGE_WORD_LEN) {
		fsbl_printf(DEBUG_GENERAL, "INVALID_PARTITION_LENGTH\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}


/******************************************************************************/
/**
* This function check whether the current partition header is empty.
* A partition header is considered empty if image word length is 0 and the
* last word is 0.
*
* @param	H is a pointer to struct HeaderArray
*
* @return
*		- XST_SUCCESS , If the partition header is empty
*		- XST_FAILURE , If the partition header is NOT empty
*
* @note		Caller is responsible to make sure the address is valid.
*
*
****************************************************************************/
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


/******************************************************************************/
/**
*
* This function checks the header checksum If the header checksum is not valid
* XST_FAILURE is returned.
*
* @param	H is a pointer to struct HeaderArray
*
* @return
*		- XST_SUCCESS is header checksum is ok
*		- XST_FAILURE if the header checksum is not correct
*
* @note		None.
*
****************************************************************************/
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
	    fsbl_printf(DEBUG_GENERAL, "Error: Checksum 0x%8.8lx != 0x%8.8lx\r\n",
			Checksum, H->Fields[PARTITION_HDR_CHECKSUM_WORD_COUNT]);
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}


/******************************************************************************/
/**
*
* This function load the partition from boot device
*
* @param	ImageBaseAddress Base address on flash
* @param	Header Partition header pointer
*
* @return
*		- XST_SUCCESS if partition move successful
*		- XST_FAILURE if check failed move failed
*
* @note		None
*
*******************************************************************************/
u32 PartitionMove(u32 ImageBaseAddress, PartHeader *Header)
{
    u32 SourceAddr;
    u32 Status;
    u8 SecureTransferFlag = 0;
    u32 LoadAddr;
    u32 ImageWordLen;
    u32 DataWordLen;

	SourceAddr = ImageBaseAddress;
	SourceAddr += Header->PartitionStart<<WORD_LENGTH_SHIFT;
	LoadAddr = Header->LoadAddr;
	ImageWordLen = Header->ImageWordLen;
	DataWordLen = Header->DataWordLen;

	/*
	 * Add flash base address for linear boot devices
	 */
	if (LinearBootDeviceFlag) {
		SourceAddr += FlashReadBaseAddress;
	}

	/*
	 * Partition encrypted
	 */
	if(EncryptedPartitionFlag) {
		SecureTransferFlag = 1;
	}

	/*
	 * For Signed or checksum enabled partition, 
	 * Total partition image need to copied to DDR
	 */
	if (SignedPartitionFlag || PartitionChecksumFlag) {
		ImageWordLen = Header->PartitionWordLen;
		DataWordLen = Header->PartitionWordLen;
	}

	/*
	 * Encrypted and Signed PS partition need to be loaded on to DDR
	 * without decryption
	 */
	if (PSPartitionFlag &&
			(SignedPartitionFlag || PartitionChecksumFlag) &&
			EncryptedPartitionFlag) {
		SecureTransferFlag = 0;
	}

	/*
	 * CPU is used for data transfer in case of non-linear
	 * boot device
	 */
	if (!LinearBootDeviceFlag) {
		/*
		 * PL partition copied to DDR temporary location
		 */
		if (PLPartitionFlag) {
			LoadAddr = DDR_TEMP_START_ADDR;
		}

		Status = MoveImage(SourceAddr,
						LoadAddr,
						(ImageWordLen << WORD_LENGTH_SHIFT));
		if(Status != XST_SUCCESS) {
			fsbl_printf(DEBUG_GENERAL, "Move Image Failed\r\n");
			return XST_FAILURE;
		}

		/*
		 * As image present at load address
		 */
		SourceAddr = LoadAddr;
	}

	if ((LinearBootDeviceFlag && PLPartitionFlag &&
			(SignedPartitionFlag || PartitionChecksumFlag)) ||
				(LinearBootDeviceFlag && PSPartitionFlag) ||
				((!LinearBootDeviceFlag) && PSPartitionFlag && SecureTransferFlag)) {
		/*
		 * PL signed partition copied to DDR temporary location
		 * using non-secure PCAP for linear boot device
		 */
		if(PLPartitionFlag){
			SecureTransferFlag = 0;
			LoadAddr = DDR_TEMP_START_ADDR;
		}

		/*
		 * Data transfer using PCAP
		 */
		Status = PcapDataTransfer((u32*)SourceAddr,
						(u32*)LoadAddr,
						ImageWordLen,
						DataWordLen,
						SecureTransferFlag);
		if(Status != XST_SUCCESS) {
			fsbl_printf(DEBUG_GENERAL, "PCAP Data Transfer Failed\r\n");
			return XST_FAILURE;
		}

		/*
		 * As image present at load address
		 */
		SourceAddr = LoadAddr;
	}

	/*
	 * Load Bitstream partition in to fabric only
	 * if checksum and authentication bits are not set
	 */
	if (PLPartitionFlag && (!(SignedPartitionFlag || PartitionChecksumFlag))) {
		Status = PcapLoadPartition((u32*)SourceAddr,
					(u32*)Header->LoadAddr,
					Header->ImageWordLen,
					Header->DataWordLen,
					EncryptedPartitionFlag);
		if(Status != XST_SUCCESS) {
			fsbl_printf(DEBUG_GENERAL, "PCAP Bitstream Download Failed\r\n");
			return XST_FAILURE;
		}
	}

	return XST_SUCCESS;
}


/******************************************************************************/
/**
*
* This function load the decrypts partition
*
* @param	StartAddr Source start address
* @param	DataLength Data length in words
* @param	ImageLength Image length in words
*
* @return
*		- XST_SUCCESS if decryption successful
*		- XST_FAILURE if decryption failed
*
* @note		None
*
*******************************************************************************/
u32 DecryptPartition(u32 StartAddr, u32 DataLength, u32 ImageLength)
{
	u32 Status;
	u8 SecureTransferFlag =1;

	/*
	 * Data transfer using PCAP
	 */
	Status = PcapDataTransfer((u32*)StartAddr,
					(u32*)StartAddr,
					ImageLength,
					DataLength,
					SecureTransferFlag);
	if (Status != XST_SUCCESS) {
		fsbl_printf(DEBUG_GENERAL,"PCAP Data Transfer failed \r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

