
#include "pch.h"
#include "fileSys.h"
#include "SCSI_IO.h"

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
		return TRUE;
	}

	return FALSE;
}

BOOL FileSys::getDBR(HANDLE hDevice, DWORD max_transf_len, BYTE* DBR_buf) {
	BYTE read_buf[PHYSICAL_SECTOR_SIZE];

	// read first sector
	if (!SCSISectorIO(hDevice, max_transf_len, 0, read_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
		TRACE("\n[Error] Read first sector failed.\n");
		return FALSE;
	}

	// read_buf is DBR
	if (checkIfDBR(hDevice, max_transf_len, read_buf)) {
		memcpy(DBR_buf, read_buf, PHYSICAL_SECTOR_SIZE);
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
		memcpy(DBR_buf, read_buf, PHYSICAL_SECTOR_SIZE);
		return TRUE;
	}

	TRACE("\n[Error] Can't find DBR.\n");
	return FALSE;
}

int FileSys::findFATEntrySec(HANDLE hDevice, DWORD max_transf_len, DWORD last_clu_idx, DWORD clu_idx, BYTE* FAT_buf) {
	DWORD entry_num_per_sec = PHYSICAL_SECTOR_SIZE >> 2;
	DWORD entry_sec_idx = clu_idx / entry_num_per_sec, ralative_offset = clu_idx % entry_num_per_sec;
	ULONGLONG entry_sec_offset = this->FAT_offset + ((ULONGLONG)entry_sec_idx * PHYSICAL_SECTOR_SIZE);

	if (last_clu_idx == 0 || entry_sec_idx != (last_clu_idx / entry_num_per_sec)) {
		if (!SCSISectorIO(hDevice, max_transf_len, entry_sec_offset, FAT_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
			return -1;
		}
	}

	return ralative_offset << 2;
}

void FileSys::initFileSys() {
	// reset vector
	for (vector<FileInfo>::iterator iter = file_info.begin(); iter != file_info.end(); ) {
		iter = file_info.erase(iter);
	}
	vector<FileInfo>().swap(file_info);
}

int FileSys::getFileList(Device cur_device) {
	/*
	 * return value: file number (-1 is read fail)
	 */
	BYTE DBR_buf[PHYSICAL_SECTOR_SIZE];
	DWORD hid_sec_num, rsvd_sec_num, root_begin_clu;
	ULONGLONG byte_per_FAT;
	DWORD file_cnt = 0;
	vector<DWORD> root_clu_chain;

	HANDLE hDevice = cur_device.openDevice();
	DWORD max_transf_len = cur_device.getMaxTransfLen();

	// get DBR
	if (!getDBR(hDevice, max_transf_len, DBR_buf)) {
		TRACE(_T("\n[Error] Get DBR failed.\n"));
		CloseHandle(hDevice);
		return -1;
	}

	this->sec_per_clu = DBR_buf[0x0D];
	this->FAT_num = DBR_buf[0x10];
	this->sec_per_FAT = (DBR_buf[0x24]) | (DBR_buf[0x25] << 8) 
						| (DBR_buf[0x26] << 16) | (DBR_buf[0x27] << 24);

	byte_per_FAT = (ULONGLONG)this->sec_per_FAT * PHYSICAL_SECTOR_SIZE;

	hid_sec_num = (DBR_buf[0x1C]) | (DBR_buf[0x1D] << 8)
				| (DBR_buf[0x1E] << 16) | (DBR_buf[0x1F] << 24);
	rsvd_sec_num = (DBR_buf[0x0E]) | (DBR_buf[0x0F] << 8);
	root_begin_clu = (DBR_buf[0x2C]) | (DBR_buf[0x2D] << 8)
					| (DBR_buf[0x2E] << 16) | (DBR_buf[0x2F] << 24);

	this->FAT_offset = ((ULONGLONG)hid_sec_num + rsvd_sec_num) * PHYSICAL_SECTOR_SIZE;

	// get root cluster chain
	DWORD cur_clu_idx = root_begin_clu, clu_entry_idx;
	BYTE cur_FAT_buf[PHYSICAL_SECTOR_SIZE];
	
	// find the sector of needed FAT entry
	clu_entry_idx = findFATEntrySec(hDevice, max_transf_len, 0, root_begin_clu, cur_FAT_buf);
	if (clu_entry_idx < 0) {
		TRACE("\n[Error] Read ROOT FAT entry failed.\n");
		CloseHandle(hDevice);
		return -1;
	}

	DWORD nxt_clu_idx = (cur_FAT_buf[clu_entry_idx + 0]) | (cur_FAT_buf[clu_entry_idx + 1] << 8)
						| (cur_FAT_buf[clu_entry_idx + 2] << 16) | (cur_FAT_buf[clu_entry_idx + 3] << 24);
	root_clu_chain.push_back(cur_clu_idx);
	
	while (nxt_clu_idx != 0x0FFFFFFF)
	{
		clu_entry_idx = findFATEntrySec(hDevice, max_transf_len, cur_clu_idx, nxt_clu_idx, cur_FAT_buf);
		cur_clu_idx = nxt_clu_idx;
		
		root_clu_chain.push_back(cur_clu_idx);
		nxt_clu_idx = (cur_FAT_buf[clu_entry_idx + 0]) | (cur_FAT_buf[clu_entry_idx + 1] << 8)
					| (cur_FAT_buf[clu_entry_idx + 2] << 16) | (cur_FAT_buf[clu_entry_idx + 3] << 24);
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