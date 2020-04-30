#pragma once

// The physical sector size in bytes
#define PHYSICAL_SECTOR_SIZE 512

HANDLE getHandle(char device_name);

DWORD getCapacity(HANDLE hDevice);

int enumUsbDisk(char usb_paths[], DWORD usb_capacity[], int cnt);

DWORD getMaxTransfLen(HANDLE hDrive);