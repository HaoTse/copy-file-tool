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
	BYTE sec_per_clu;
	DWORD sec_per_FAT, FAT_num; // store for backup FAT
	ULONGLONG FAT_offset, heap_offset;

	BOOL checkIfDBR(HANDLE hDevice, DWORD max_transf_len, BYTE* buf);
	BOOL getDBR(HANDLE hDevice, DWORD max_transf_len, BYTE* DBR_buf);
	int findFATEntrySec(HANDLE hDevice, DWORD max_transf_len, DWORD last_clu_idx, DWORD clu_idx, BYTE* FAT_buf);

public:
	~FileSys();
	vector<FileInfo> file_info;
	void initFileSys();
	int getFileList(Device cur_device);
};