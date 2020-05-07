/*++
AccessMBR

This module sends a SCSI Passthrough command to send a read or write command directly to the disk.
Bypassing an MJ_READ/WRITE filter.

Written by Andrea Allievi, Cisco Talos
Copyright (C) 2016 Cisco Systems Inc
--*/

#include "pch.h"

#include "SCSI_IO.h"
#include "device.h"

BOOL SCSIReadCapacity(HANDLE hDevice, BYTE* capacityBuf) {

	SCSI_PASS_THROUGH_DIRECT sptd;
	int retVal;
	DWORD bytesReturn;

	// read_capacity(10)
	memset(&sptd, 0, sizeof(sptd));
	sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
	sptd.CdbLength = 10;
	sptd.DataIn = SCSI_IOCTL_DATA_IN;
	sptd.DataTransferLength = 8; // read_capacity(10) scsi transfer 8 bytes
	sptd.TimeOutValue = SCSI_TIMEOUT;
	sptd.DataBuffer = capacityBuf;
	sptd.Cdb[0] = 0x25; // read capacity(10) op_code is 0x25
	retVal = DeviceIoControl(hDevice,
		IOCTL_SCSI_PASS_THROUGH_DIRECT,
		&sptd,
		sizeof(sptd),
		NULL, 0,
		&bytesReturn,
		NULL);

	if (retVal == 0)
		return FALSE;

	return TRUE;
}

// SCSI Read/Write Sector
BOOL SCSISectorIO(HANDLE hDrive, DWORD maxTransfLen, ULONGLONG offset, LPBYTE buffer, UINT buffSize, BOOLEAN write) {
	SCSI_PASS_THROUGH_DIRECT srb = { 0 };	// SCSI Request Block Structure
	DWORD bytesReturned = 0;				// Number of bytes returned
	DWORD curSize = buffSize;				// Current Transfer Size

	BOOL retVal = 0;
	DWORD lastErr = 0;
	if (!buffer || !buffSize) {
		TRACE("\n[Error] Buffer setup error.\n");
		return FALSE;
	}

	// Inizialize common SCSI_PASS_THROUGH_DIRECT members 
	RtlZeroMemory(&srb, sizeof(SCSI_PASS_THROUGH_DIRECT));
	srb.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
	srb.CdbLength = 0xa;
	srb.SenseInfoLength = 0;
	srb.SenseInfoOffset = sizeof(SCSI_PASS_THROUGH_DIRECT);
	if (write)
		srb.DataIn = SCSI_IOCTL_DATA_OUT;
	else
		srb.DataIn = SCSI_IOCTL_DATA_IN;
	srb.TimeOutValue = SCSI_TIMEOUT;

	while (curSize) {
		if (curSize > maxTransfLen)
			srb.DataTransferLength = maxTransfLen;
		else {
			// Check buffer alignment
			if ((curSize % PHYSICAL_SECTOR_SIZE) != 0)
				// This operation below is so hazardous BUT with VirtualAlloc I'm sure that every memory 
				// allocation is PAGE_ALIGNED
				curSize = curSize + (PHYSICAL_SECTOR_SIZE - (curSize % PHYSICAL_SECTOR_SIZE));
			srb.DataTransferLength = curSize;
		}

		srb.DataBuffer = buffer;
		retVal = SCSIBuild10CDB(&srb, offset, srb.DataTransferLength, write);
		retVal = DeviceIoControl(hDrive, IOCTL_SCSI_PASS_THROUGH_DIRECT, (LPVOID)&srb, sizeof(SCSI_PASS_THROUGH_DIRECT),
			NULL, 0, &bytesReturned, NULL);
		lastErr = GetLastError();			// 87 = Error Invalid Parameter
											// 1117 = ERROR_IO_DEVICE
		if (!retVal) break;
		else lastErr = 0;
		buffer += srb.DataTransferLength;
		curSize -= srb.DataTransferLength;
		offset += srb.DataTransferLength;
	}

	if (lastErr != ERROR_SUCCESS) {
		// Errore 1: ERROR_INVALID_FUNCTION
		TRACE("\n[Error] SCSI Read/Write command fail. Error code = %u.\n", lastErr);
		return FALSE;
	}
	else
		return TRUE;
}

// Build the 10-bytes SCSI command descriptor block
BOOL SCSIBuild10CDB(PSCSI_PASS_THROUGH_DIRECT srb, ULONGLONG offset, ULONG length, BOOLEAN Write) {
	if (!srb || length < 1)	
		return FALSE;				
	LPBYTE cdb = srb->Cdb;
	if (Write == FALSE) {
		cdb[0] = SCSIOP_READ;				// READ (10) opcode
		cdb[1] = 0;
	}
	else {
		cdb[0] = SCSIOP_WRITE;				// WRITE (10) opcode
		cdb[1] = 0;
	}
	DWORD LBA = (DWORD)(offset / PHYSICAL_SECTOR_SIZE);
	cdb[2] = ((LPBYTE)&LBA)[3];		
	cdb[3] = ((LPBYTE)&LBA)[2];
	cdb[4] = ((LPBYTE)&LBA)[1];
	cdb[5] = ((LPBYTE)&LBA)[0];		
	cdb[6] = 0x00;

	WORD CDBTLen = (WORD)(length / PHYSICAL_SECTOR_SIZE);		
	cdb[7] = ((LPBYTE)&CDBTLen)[1];	
	cdb[8] = ((LPBYTE)&CDBTLen)[0];
	cdb[9] = 0x00;

	return TRUE;
}
