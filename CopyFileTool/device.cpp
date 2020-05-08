#include "pch.h"

#include "device.h"
#include "SCSI_IO.h"

// The scsi capability of USB2.0
#define SCSI_CAPABILITY_USB2 65536

Device::Device() {
	this->ident = NULL;
	capacity_sec = 0;
	byte_per_sec = 0;
	max_transf_len = 0;
}

Device::Device(char ident) {
	this->ident = ident;
	this->initCapacity();
	this->initMaxTransfLen();
}

char Device::getIdent() {
	return this->ident;
}

DWORD Device::getCapacity() {
	return this->capacity_sec;
}

DWORD Device::getMaxTransfLen() {
	return this->max_transf_len;
}

BOOL Device::isValid()
{
	return this->byte_per_sec <= 4096;
}

HANDLE Device::openDevice() {
	char device_path[10];

	// initial handle of USB
	sprintf_s(device_path, "\\\\.\\%c:", this->ident);
	HANDLE hDevice = CreateFileA(device_path,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		TRACE("\n[Error] Open %c: fail. Error Code = %u\n", this->ident, GetLastError());
	}

	return hDevice;
}

void Device::initCapacity() {
	BYTE capacity_buf[8];
	HANDLE hDevice = this->openDevice();

	// get capacity
	int ret = SCSIReadCapacity(hDevice, capacity_buf);
	if (!ret) {
		TRACE("\n[Error] Read capacity fail. Error Code = % u\n", GetLastError());
		this->capacity_sec = 0;
		CloseHandle(hDevice);
		return;
	}

	// RETURNED LOGICAL BLOCK ADDRESS
	this->capacity_sec = capacity_buf[0] * (1 << 24) + capacity_buf[1] * (1 << 16)
		+ capacity_buf[2] * (1 << 8) + capacity_buf[3] + 1;
	// BLOCK LENGTH IN BYTES
	this->byte_per_sec = capacity_buf[4] * (1 << 24) + capacity_buf[5] * (1 << 16)
		+ capacity_buf[6] * (1 << 8) + capacity_buf[7];

	if (this->byte_per_sec != PHYSICAL_SECTOR_SIZE) {
		TRACE("\n[Warn] PHYSICAL_SECTOR_SIZE is not equal to block length!\n");
	}

	CloseHandle(hDevice);
}


// Obtain maximum transfer length
void Device::initMaxTransfLen() {
	HANDLE hDevice = this->openDevice();
	DWORD bytesReturned = 0;				// Number of bytes returned
	IO_SCSI_CAPABILITIES scap = { 0 };		// Used to determine the maximum SCSI transfer length

	int retVal = DeviceIoControl(hDevice, IOCTL_SCSI_GET_CAPABILITIES, NULL, 0, &scap, sizeof(scap), &bytesReturned, NULL);
	if (!retVal) {
		TRACE("\n[Warn] Cannot get SCSI capabilities of %c:. Error code = %u.\n", this->ident, GetLastError());
		this->max_transf_len = SCSI_CAPABILITY_USB2;
	}
	else {
		this->max_transf_len = scap.MaximumTransferLength;
		//TRACE("\n[Msg] SCSI capabilities: %u Bytes.\n", maxTransfLen);
	}

	CloseHandle(hDevice);
}

CString Device::showText() {
	CString text;
	DWORD capacity_MB = (this->capacity_sec >> 20) * this->byte_per_sec;
	if (capacity_MB > 1024) {
		double capacity_GB = (double)capacity_MB / 1024;
		text.Format(_T("%c: (%.1f GB)"), this->ident, capacity_GB);
	}
	else {
		text.Format(_T("%c: (%u MB)"), this->ident, capacity_MB);
	}

	return text;
}