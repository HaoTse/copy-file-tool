#pragma once

#include <vector>
#include "device.h"

using namespace std;

typedef struct FileInfo
{
	CString file_name;
	ULONGLONG file_addr, file_size;
} FileInfo;

class FileSys
{
private:
	BYTE DBR_buf[PHYSICAL_SECTOR_SIZE], FAT_1st_sec_buf[PHYSICAL_SECTOR_SIZE];
	BYTE sec_per_clu;
	DWORD sec_per_FAT, FAT_num;
	ULONGLONG FAT_offset, heap_offset;

	BOOL checkIfDBR(HANDLE hDevice, DWORD max_transf_len, BYTE* buf);
	BOOL getDBR(HANDLE hDevice, DWORD max_transf_len);

public:
	FileSys();
	~FileSys();
	vector<FileInfo> file_info;
	void initFileSys();
	int getFileList(Device cur_device);
};