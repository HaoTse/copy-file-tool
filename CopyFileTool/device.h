#pragma once

// The physical sector size in bytes
#define PHYSICAL_SECTOR_SIZE 512

class Device
{
private:
	char ident;
	DWORD capacity_sec, byte_per_sec, max_transf_len;

	void initCapacity();
	void initMaxTransfLen();
public:
	Device(char ident);
	HANDLE openDevice();
	char getIdent();
	DWORD getCapacity();
	DWORD getMaxTransfLen();
	CString showText();
};
