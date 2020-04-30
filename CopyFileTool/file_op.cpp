
#include "pch.h"
#include "file_op.h"
#include "device.h"
#include "SCSI_IO.h"

int getFileList(HANDLE hDevice, vector<CString> &file_name, vector<ULONGLONG> &file_addr, vector<ULONGLONG> &file_size) {
	/*
	 * return value: file number (-1 is read fail)
	 */
	BYTE DBR_buf[PHYSICAL_SECTOR_SIZE], FAT_buf[PHYSICAL_SECTOR_SIZE];
	BYTE sec_per_clu, FAT_num;
	DWORD hid_sec_num, rsvd_sec_num, sec_per_FAT, root_begin_clu;
	DWORD file_cnt = 0;
	ULONGLONG FAT_offset;
	vector<DWORD> root_clu_chain;

	// get DBR
	if (!getDBR(hDevice, DBR_buf)) {
		TRACE(_T("\n[Error] Get DBR failed.\n"));
		return -1;
	}
	sec_per_clu = DBR_buf[0x0D];
	FAT_num = DBR_buf[0x10];
	hid_sec_num = (DBR_buf[0x1C]) | (DBR_buf[0x1D] << 8) | (DBR_buf[0x1E] << 16) | (DBR_buf[0x1F] << 24);
	rsvd_sec_num = (DBR_buf[0x0E]) | (DBR_buf[0x0F] << 8);
	sec_per_FAT = (DBR_buf[0x24]) | (DBR_buf[0x25] << 8) | (DBR_buf[0x26] << 16) | (DBR_buf[0x27] << 24);
	root_begin_clu = (DBR_buf[0x2C]) | (DBR_buf[0x2D] << 8) | (DBR_buf[0x2E] << 16) | (DBR_buf[0x2F] << 24);
	
	// get FAT
	FAT_offset = ((ULONGLONG)hid_sec_num + rsvd_sec_num) * PHYSICAL_SECTOR_SIZE;
	if (!SCSISectorIO(hDevice, FAT_offset, FAT_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
		TRACE("\n[Error] Read FAT failed.\n");
		return -1;
	}

	// get root cluster chain
	DWORD cur_clu_idx = root_begin_clu, clu_entry_idx = root_begin_clu << 2;
	DWORD nxt_clu_idx = (FAT_buf[clu_entry_idx + 0]) | (FAT_buf[clu_entry_idx + 1] << 8) 
						| (FAT_buf[clu_entry_idx + 2] << 16) | (FAT_buf[clu_entry_idx + 3] << 24);
	root_clu_chain.push_back(cur_clu_idx);
	while (nxt_clu_idx != 0x0FFFFFFF)
	{
		cur_clu_idx = nxt_clu_idx;
		clu_entry_idx = cur_clu_idx << 2;
		root_clu_chain.push_back(cur_clu_idx);
		nxt_clu_idx = (FAT_buf[clu_entry_idx + 0]) | (FAT_buf[clu_entry_idx + 1] << 8)
					| (FAT_buf[clu_entry_idx + 2] << 16) | (FAT_buf[clu_entry_idx + 3] << 24);
	}

	// get directory entries in root
	ULONGLONG heap_offset = FAT_offset + ((ULONGLONG)sec_per_FAT * FAT_num) * PHYSICAL_SECTOR_SIZE;
	DWORD entry_num_per_clu = sec_per_clu * PHYSICAL_SECTOR_SIZE * 32;
	CString LFN_content = _T("");

	for (DWORD cur_clu_idx : root_clu_chain) {
		// read cluster
		BYTE* cur_clu = new BYTE[sec_per_clu * PHYSICAL_SECTOR_SIZE];
		ULONGLONG cur_clu_offset = heap_offset + ((ULONGLONG)cur_clu_idx - 2) * sec_per_clu * PHYSICAL_SECTOR_SIZE;
		if (!SCSISectorIO(hDevice, cur_clu_offset, cur_clu, sec_per_clu * PHYSICAL_SECTOR_SIZE, FALSE)) {
			TRACE("\n[Error] Read root cluster failed.\n");
			delete[] cur_clu;
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

				file_name.push_back(LFN_content);
				file_addr.push_back(tmp_addr);
				file_size.push_back(tmp_size);
				file_cnt++;
				
				LFN_content.Empty();
			}
		}

		delete[] cur_clu;
	}

	return file_cnt;
}