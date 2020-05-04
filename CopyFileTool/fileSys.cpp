
#include "pch.h"
#include "fileSys.h"
#include "SCSI_IO.h"

FileSys::FileSys() {
	memset(DBR_buf, 0, sizeof(DBR_buf));
	memset(FAT_1st_sec_buf, 0, sizeof(FAT_1st_sec_buf));
}

FileSys::~FileSys() {
	// reset vector
	for (vector<FileInfo>::iterator iter = file_info.begin(); iter != file_info.end(); ) {
		iter = file_info.erase(iter);
	}
	vector<FileInfo>().swap(file_info);
}

BOOL FileSys::checkIfDBR(HANDLE hDevice, DWORD max_transf_len, BYTE* buf) {
	// find out offset
	DWORD tmp_hid_sec_num, tmp_rsvd_sec_num;
	ULONGLONG tmp_FAT_offset;
	BYTE tmp_FAT_buf[PHYSICAL_SECTOR_SIZE];

	tmp_hid_sec_num = (buf[0x1C]) | (buf[0x1D] << 8) | (buf[0x1E] << 16) | (buf[0x1F] << 24);
	tmp_rsvd_sec_num = (buf[0x0E]) | (buf[0x0F] << 8);
	tmp_FAT_offset = (ULONGLONG)tmp_hid_sec_num * PHYSICAL_SECTOR_SIZE + (ULONGLONG)tmp_rsvd_sec_num * PHYSICAL_SECTOR_SIZE;

	// read FAT
	if (!SCSISectorIO(hDevice, max_transf_len, tmp_FAT_offset, tmp_FAT_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
		TRACE("\n[Error] Read FAT failed.\n");
		return FALSE;
	}
	if (tmp_FAT_buf[0] == 0xF8 && tmp_FAT_buf[1] == 0xFF && tmp_FAT_buf[2] == 0xFF && tmp_FAT_buf[3] == 0x0F) {
		// store FAT 1st sector
		memcpy(this->FAT_1st_sec_buf, tmp_FAT_buf, PHYSICAL_SECTOR_SIZE);
		return TRUE;
	}

	return FALSE;
}

BOOL FileSys::getDBR(HANDLE hDevice, DWORD max_transf_len) {
	BYTE read_buf[PHYSICAL_SECTOR_SIZE];

	// read first sector
	if (!SCSISectorIO(hDevice, max_transf_len, 0, read_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
		TRACE("\n[Error] Read first sector failed.\n");
		return FALSE;
	}

	// read_buf is DBR
	if (checkIfDBR(hDevice, max_transf_len, read_buf)) {
		memcpy(this->DBR_buf, read_buf, PHYSICAL_SECTOR_SIZE);
		return TRUE;
	}
	// read_buf is MBR
	DWORD partition_addr = (read_buf[0x1C6]) | (read_buf[0x1C7] << 8) | (read_buf[0x1C8] << 16) | (read_buf[0x1C9] << 24);
	ULONGLONG partition_offset = (ULONGLONG)partition_addr * PHYSICAL_SECTOR_SIZE;
	if (!SCSISectorIO(hDevice, max_transf_len, partition_offset, read_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
		TRACE("\n[Error] Read DBR with MBR failed.\n");
		return FALSE;
	}
	if (checkIfDBR(hDevice, max_transf_len, read_buf)) {
		memcpy(this->DBR_buf, read_buf, PHYSICAL_SECTOR_SIZE);
		return TRUE;
	}

	TRACE("\n[Error] Can't find DBR.\n");
	return FALSE;
}

void FileSys::initFileSys() {
	// reset vector
	for (vector<FileInfo>::iterator iter = file_info.begin(); iter != file_info.end(); ) {
		iter = file_info.erase(iter);
	}
	vector<FileInfo>().swap(file_info);

	memset(DBR_buf, 0, sizeof(DBR_buf));
	memset(FAT_1st_sec_buf, 0, sizeof(FAT_1st_sec_buf));
}

int FileSys::getFileList(Device cur_device) {
	/*
	 * return value: file number (-1 is read fail)
	 */
	DWORD hid_sec_num, rsvd_sec_num, root_begin_clu;
	DWORD file_cnt = 0;
	vector<DWORD> root_clu_chain;

	HANDLE hDevice = cur_device.openDevice();
	DWORD max_transf_len = cur_device.getMaxTransfLen();

	// get DBR
	if (!getDBR(hDevice, max_transf_len)) {
		TRACE(_T("\n[Error] Get DBR failed.\n"));
		CloseHandle(hDevice);
		return -1;
	}

	this->sec_per_clu = this->DBR_buf[0x0D];
	this->FAT_num = this->DBR_buf[0x10];
	this->sec_per_FAT = (this->DBR_buf[0x24]) | (this->DBR_buf[0x25] << 8) 
						| (this->DBR_buf[0x26] << 16) | (this->DBR_buf[0x27] << 24);

	hid_sec_num = (this->DBR_buf[0x1C]) | (this->DBR_buf[0x1D] << 8)
				| (this->DBR_buf[0x1E] << 16) | (this->DBR_buf[0x1F] << 24);
	rsvd_sec_num = (this->DBR_buf[0x0E]) | (this->DBR_buf[0x0F] << 8);
	root_begin_clu = (this->DBR_buf[0x2C]) | (this->DBR_buf[0x2D] << 8)
					| (this->DBR_buf[0x2E] << 16) | (this->DBR_buf[0x2F] << 24);

	// get FAT
	this->FAT_offset = ((ULONGLONG)hid_sec_num + rsvd_sec_num) * PHYSICAL_SECTOR_SIZE;
	if (!this->FAT_1st_sec_buf) {
		if (!SCSISectorIO(hDevice, max_transf_len, this->FAT_offset, this->FAT_1st_sec_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
			TRACE("\n[Error] Read FAT failed.\n");
			CloseHandle(hDevice);
			return -1;
		}
	}

	// get root cluster chain
	DWORD cur_clu_idx = root_begin_clu, clu_entry_idx = root_begin_clu << 2;
	DWORD nxt_clu_idx = (this->FAT_1st_sec_buf[clu_entry_idx + 0]) | (this->FAT_1st_sec_buf[clu_entry_idx + 1] << 8)
						| (this->FAT_1st_sec_buf[clu_entry_idx + 2] << 16) | (this->FAT_1st_sec_buf[clu_entry_idx + 3] << 24);
	root_clu_chain.push_back(cur_clu_idx);
	while (nxt_clu_idx != 0x0FFFFFFF)
	{
		cur_clu_idx = nxt_clu_idx;
		clu_entry_idx = cur_clu_idx << 2;
		root_clu_chain.push_back(cur_clu_idx);
		nxt_clu_idx = (this->FAT_1st_sec_buf[clu_entry_idx + 0]) | (this->FAT_1st_sec_buf[clu_entry_idx + 1] << 8)
					| (this->FAT_1st_sec_buf[clu_entry_idx + 2] << 16) | (this->FAT_1st_sec_buf[clu_entry_idx + 3] << 24);
	}

	// get directory entries in root
	this->heap_offset = this->FAT_offset + ((ULONGLONG)this->sec_per_FAT * this->FAT_num) * PHYSICAL_SECTOR_SIZE;
	DWORD entry_num_per_clu = (this->sec_per_clu * PHYSICAL_SECTOR_SIZE) >> 5;
	CString LFN_content = _T("");

	for (DWORD cur_clu_idx : root_clu_chain) {
		// read cluster
		BYTE* cur_clu = new BYTE[this->sec_per_clu * PHYSICAL_SECTOR_SIZE];
		ULONGLONG cur_clu_offset = this->heap_offset + ((ULONGLONG)cur_clu_idx - 2) * this->sec_per_clu * PHYSICAL_SECTOR_SIZE;
		if (!SCSISectorIO(hDevice, max_transf_len, cur_clu_offset, cur_clu, this->sec_per_clu * PHYSICAL_SECTOR_SIZE, FALSE)) {
			TRACE("\n[Error] Read root cluster failed.\n");
			delete[] cur_clu;
			CloseHandle(hDevice);
			return -1;
		}

		// read directory entry
		for (DWORD i = 0; i < entry_num_per_clu; i++) {
			BYTE entry_tmp[32];
			memcpy(entry_tmp, cur_clu + 32 * i, 32);

			if (entry_tmp[0] == 0xE5)
				continue;
			if (entry_tmp[0] == 0x00)
				break;

			//LFN
			if (entry_tmp[0x0B] == 0x0F) {
				CString tmp_LFN = _T("");
				for (DWORD j = 0x1; j <= 0xA; j += 2) {
					wchar_t cur_unicode = entry_tmp[j] | entry_tmp[j + 1] << 8;
					tmp_LFN += CString(cur_unicode);
				}
				for (DWORD j = 0xE; j <= 0x19; j += 2) {
					wchar_t cur_unicode = entry_tmp[j] | entry_tmp[j + 1] << 8;
					tmp_LFN += CString(cur_unicode);
				}
				for (DWORD j = 0x1C; j <= 0x1F; j += 2) {
					wchar_t cur_unicode = entry_tmp[j] | entry_tmp[j + 1] << 8;
					tmp_LFN += CString(cur_unicode);
				}
				LFN_content = tmp_LFN + LFN_content;
			}
			else if (entry_tmp[0x0B] < 0x20 || (entry_tmp[0x0B] & 0x02) > 0) { // skip directory and hidden files
				LFN_content.Empty();
				continue;
			}
			else {
				DWORD tmp_addr, tmp_size;
				tmp_addr = entry_tmp[0x14] | entry_tmp[0x15] << 8;
				tmp_addr = tmp_addr << 16 | entry_tmp[0x1A] | entry_tmp[0x1B] << 8;
				tmp_size = entry_tmp[0x1C] | (entry_tmp[0x1D] << 8) | (entry_tmp[0x1E] << 16) | (entry_tmp[0x1F] << 24);

				FileInfo cur_file;
				cur_file.file_name = LFN_content;
				cur_file.file_addr = tmp_addr;
				cur_file.file_size = tmp_size;
				file_info.push_back(cur_file);

				file_cnt++;
				
				LFN_content.Empty();
			}
		}

		delete[] cur_clu;
	}

	CloseHandle(hDevice);
	return file_cnt;
}