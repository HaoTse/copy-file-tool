#include "pch.h"

#include "device.h"
#include "SCSI_IO.h"

// The scsi capability of USB2.0
#define SCSI_CAPABILITY_USB2 8192

HANDLE getHandle(char device_name) {
	char device_path[10];
	HANDLE hDevice;

	// initial handle of USB
	sprintf_s(device_path, "\\\\.\\%c:", device_name);
	hDevice = CreateFileA(device_path,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		TRACE("\n[Error] Open fail. Error Code = %u\n", GetLastError());
	}

	return hDevice;
}

DWORD getCapacity(HANDLE hDevice) {
	DWORD sectors_num, bytesPerSecotr; // volume information
	BYTE capacity_buf[8];

	// get capacity
	int ret = SCSIReadCapacity(hDevice, capacity_buf);
	if (!ret) {
		TRACE("\n[Error] Read capacity fail. Error Code = % u\n", GetLastError());
		return 0;
	}

	// RETURNED LOGICAL BLOCK ADDRESS
	sectors_num = capacity_buf[0] * (1 << 24) + capacity_buf[1] * (1 << 16)
		+ capacity_buf[2] * (1 << 8) + capacity_buf[3] + 1;
	// BLOCK LENGTH IN BYTES
	bytesPerSecotr = capacity_buf[4] * (1 << 24) + capacity_buf[5] * (1 << 16)
		+ capacity_buf[6] * (1 << 8) + capacity_buf[7];

	if (bytesPerSecotr != PHYSICAL_SECTOR_SIZE) {
		TRACE("\n[Warn] PHYSICAL_SECTOR_SIZE is not equal to block length!\n");
	}

	return sectors_num;
}

int enumUsbDisk(char usb_paths[], DWORD usb_capacity[], int cnt)
{
	int usb_disk_cnt = 0;

	char disk_path[5] = { 0 };
	DWORD all_disk = GetLogicalDrives();

	int i = 0;
	DWORD bytes_returned = 0;
	while (all_disk && usb_disk_cnt < cnt)
	{
		if ((all_disk & 0x1) == 1)
		{
			sprintf_s(disk_path, "%c:", 'A' + i);


			if (GetDriveTypeA(disk_path) == DRIVE_REMOVABLE)
			{
				// get device capacity
				HANDLE hDevice = getHandle('A' + i);
				if (hDevice == INVALID_HANDLE_VALUE) {
					TRACE("Open %s failed.", disk_path);
					CloseHandle(hDevice);
					return -1;
				}
				DWORD capacity_sec = getCapacity(hDevice);
				if (capacity_sec == 0) {
					CloseHandle(hDevice);
					continue; // skip invalid device (include card reader)
				}

				usb_paths[usb_disk_cnt] = 'A' + i;
				usb_capacity[usb_disk_cnt++] = capacity_sec;

				CloseHandle(hDevice);
			}
		}
		all_disk = all_disk >> 1;
		i++;
	}

	return usb_disk_cnt;
}

// Obtain maximum transfer length
DWORD getMaxTransfLen(HANDLE hDevice) {
	DWORD bytesReturned = 0;				// Number of bytes returned
	IO_SCSI_CAPABILITIES scap = { 0 };		// Used to determine the maximum SCSI transfer length
	DWORD maxTransfLen;						// Maximum Transfer Length

	int retVal = DeviceIoControl(hDevice, IOCTL_SCSI_GET_CAPABILITIES, NULL, 0, &scap, sizeof(scap), &bytesReturned, NULL);
	if (!retVal) {
		//TRACE("\n[Warn] Cannot get SCSI capabilities. Error code = %u.\n", GetLastError());
		maxTransfLen = SCSI_CAPABILITY_USB2;
	}
	else {
		maxTransfLen = scap.MaximumTransferLength;
		//TRACE("\n[Msg] SCSI capabilities: %u Bytes.\n", maxTransfLen);
	}

	return maxTransfLen;
}

BOOL checkIfDBR(HANDLE hDevice, BYTE* buf) {
	// find out offset
	DWORD hid_sec_num, rsvd_sec_num;
	ULONGLONG FAT_offset;
	BYTE FAT_buf[PHYSICAL_SECTOR_SIZE];

	hid_sec_num = (buf[0x1C]) | (buf[0x1D] << 8) | (buf[0x1E] << 16) | (buf[0x1F] << 24);
	rsvd_sec_num = (buf[0x0E]) | (buf[0x0F] << 8);
	FAT_offset = (ULONGLONG)hid_sec_num * PHYSICAL_SECTOR_SIZE + (ULONGLONG)rsvd_sec_num * PHYSICAL_SECTOR_SIZE;

	// read FAT
	if (!SCSISectorIO(hDevice, FAT_offset, FAT_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
		TRACE("\n[Error] Read FAT failed.\n");
		return FALSE;
	}
	if (FAT_buf[0] == 0xF8 && FAT_buf[1] == 0xFF && FAT_buf[2] == 0xFF && FAT_buf[3] == 0x0F) {
		return TRUE;
	}

	return FALSE;
}

BOOL getDBR(HANDLE hDevice, BYTE* buf) {
	BYTE read_buf[PHYSICAL_SECTOR_SIZE];

	// read first sector
	if (!SCSISectorIO(hDevice, 0, read_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
		TRACE("\n[Error] Read first sector failed.\n");
		return FALSE;
	}

	// read_buf is DBR
	if (checkIfDBR(hDevice, read_buf)) {
		memcpy(buf, read_buf, PHYSICAL_SECTOR_SIZE);
		return TRUE;
	}
	// read_buf is MBR
	DWORD partition_addr = (read_buf[0x1C6]) | (read_buf[0x1C7] << 8) | (read_buf[0x1C8] << 16) | (read_buf[0x1C9] << 24);
	ULONGLONG partition_offset = (ULONGLONG)partition_addr * PHYSICAL_SECTOR_SIZE;
	if (!SCSISectorIO(hDevice, partition_offset, read_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
		TRACE("\n[Error] Read DBR with MBR failed.\n");
		return FALSE;
	}
	if (checkIfDBR(hDevice, read_buf)) {
		memcpy(buf, read_buf, PHYSICAL_SECTOR_SIZE);
		return TRUE;
	}

	TRACE("\n[Error] Can't find DBR.\n");
	return FALSE;
}