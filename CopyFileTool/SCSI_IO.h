/*++
AccessMBR

This module sends a SCSI Passthrough command to send a read or write command directly to the disk.
Bypassing an MJ_READ/WRITE filter.

Written by Andrea Allievi, Cisco Talos
Copyright (C) 2016 Cisco Systems Inc
--*/

#ifndef SCSI_IO
#define SCSI_IO

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinIoCtl.h>
#include <Ntddscsi.h>

#define SCSIOP_WRITE 0x2A
#define SCSIOP_READ  0x28
#define SCSI_TIMEOUT 0xFFFF

// Command to get capacity of volume
BOOL SCSIReadCapacity(HANDLE hDevice, BYTE* capacityBuf);

// SCSI Read/Write Sector
BOOL SCSISectorIO(HANDLE hDrive, DWORD maxTransfLen, ULONGLONG offset, LPBYTE buffer, UINT buffSize, BOOLEAN write);

// Compila il Command Descriptor Block della richiesta di lettura o scrittura a 10 Bytes
BOOL SCSIBuild10CDB(PSCSI_PASS_THROUGH_DIRECT srb, ULONGLONG offset, ULONG length, BOOLEAN Write);

#endif //SCSI_IO