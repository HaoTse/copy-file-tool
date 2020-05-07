
#include "pch.h"
#include "fileSys.h"
#include "SCSI_IO.h"
#include "utils.h"

FileSys::~FileSys() {
	// reset vector
	for (vector<FileInfo>::iterator iter = file_info.begin(); iter != file_info.end(); ) {
		iter = file_info.erase(iter);
	}
	vector<FileInfo>().swap(file_info);
}

BOOL FileSys::checkIfDBR(HANDLE hDevice, DWORD max_transf_len, BYTE* buf) {
	/*
	 * Use two way to double check:
	 *	1. Check jumpBoot and signature value.
	 *	2. Check FAT entries.
	 */

	// check jumpBoot and signature value
	if (buf[0x0] != 0xEB || buf[0x1] != 0x58 || buf[0x2] != 0x90) {
		return FALSE;
	}
	if (buf[0x1FE] != 0x55 || buf[0x1FF] != 0xAA) {
		return FALSE;
	}

	// check FAT entries
	DWORD tmp_hid_sec_num, tmp_rsvd_sec_num;
	ULONGLONG tmp_FAT_offset;
	BYTE tmp_FAT_buf[PHYSICAL_SECTOR_SIZE];

	tmp_hid_sec_num = (buf[0x1C]) | (buf[0x1D] << 8) | (buf[0x1E] << 16) | (buf[0x1F] << 24);
	tmp_rsvd_sec_num = (buf[0x0E]) | (buf[0x0F] << 8);
	tmp_FAT_offset = (ULONGLONG)tmp_hid_sec_num * PHYSICAL_SECTOR_SIZE + (ULONGLONG)tmp_rsvd_sec_num * PHYSICAL_SECTOR_SIZE;

	// read FAT
	if (!SCSISectorIO(hDevice, max_transf_len, tmp_FAT_offset, tmp_FAT_buf, PHYSICAL_SECTOR_SIZE, FALSE)) {
		TRACE(_T("\n[Error] Read FAT failed. Error Code = %u.\n"), GetLastError());
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
		TRACE(_T("\n[Error] Read first sector failed. Error Code = %u.\n"), GetLastError());
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
		TRACE(_T("\n[Error] Read DBR with MBR failed. Error Code = %u.\n"), GetLastError());
		return FALSE;
	}
	if (checkIfDBR(hDevice, max_transf_len, read_buf)) {
		memcpy(DBR_buf, read_buf, PHYSICAL_SECTOR_SIZE);
		return TRUE;
	}

	TRACE(_T("\n[Error] Can't find DBR. Error Code = %u.\n"), GetLastError());
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

BOOL FileSys::getCluChain(HANDLE hDevice, DWORD max_transf_len, DWORD begin_clu_idx, vector<DWORD>& clu_chain)
{
	DWORD cur_clu_idx = 0, nxt_clu_idx = begin_clu_idx, clu_entry_idx;
	BYTE cur_FAT_buf[PHYSICAL_SECTOR_SIZE];

	// link cluster chain
	do {
		// find the sector of needed FAT entry
		clu_entry_idx = findFATEntrySec(hDevice, max_transf_len, cur_clu_idx, nxt_clu_idx, cur_FAT_buf);
		if (clu_entry_idx < 0) {
			TRACE(_T("\n[Error] Read FAT entry failed. Error Code = %u.\n"), GetLastError());
			return FALSE;
		}
		cur_clu_idx = nxt_clu_idx;

		clu_chain.push_back(cur_clu_idx);
		nxt_clu_idx = (cur_FAT_buf[clu_entry_idx + 0]) | (cur_FAT_buf[clu_entry_idx + 1] << 8)
			| (cur_FAT_buf[clu_entry_idx + 2] << 16) | (cur_FAT_buf[clu_entry_idx + 3] << 24);
	} while (nxt_clu_idx != 0x0FFFFFFF);

	return TRUE;
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
	if (hDevice == INVALID_HANDLE_VALUE) {
		TRACE(_T("\n[Error] Open usb device failed. Error Code = %u.\n"), GetLastError());
		CloseHandle(hDevice);
		return -1;
	}

	// get DBR
	if (!getDBR(hDevice, max_transf_len, DBR_buf)) {
		TRACE(_T("\n[Error] Get DBR failed. Error Code = %u.\n"), GetLastError());
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
	if (!getCluChain(hDevice, max_transf_len, root_begin_clu, root_clu_chain)) {
		TRACE(_T("\n[Error] Get ROOT cluster chain failed. Error Code = %u.\n"), GetLastError());
		CloseHandle(hDevice);
		return -1;
	}

	// get directory entries in root
	this->heap_offset = this->FAT_offset + ((ULONGLONG)this->sec_per_FAT * this->FAT_num) * PHYSICAL_SECTOR_SIZE;
	DWORD entry_num_per_clu = (this->sec_per_clu * PHYSICAL_SECTOR_SIZE) >> 5;
	wchar_t remove_word = 0xFFFF, end_word = 0x0000;
	CString LFN_content = _T("");

	for (DWORD cur_clu_idx : root_clu_chain) {
		// read cluster
		BYTE* cur_clu = new BYTE[this->sec_per_clu * PHYSICAL_SECTOR_SIZE];
		ULONGLONG cur_clu_offset = this->heap_offset + ((ULONGLONG)cur_clu_idx - 2) * this->sec_per_clu * PHYSICAL_SECTOR_SIZE;
		if (!SCSISectorIO(hDevice, max_transf_len, cur_clu_offset, cur_clu, this->sec_per_clu * PHYSICAL_SECTOR_SIZE, FALSE)) {
			TRACE(_T("\n[Error] Read root cluster failed. Error Code = %u.\n"), GetLastError());
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

				// get file name
				if (LFN_content == _T("")) {
					CString SFN_name = _T(""), ext_name = _T("");
					for (DWORD j = 0x00; j <= 0x07; j++) {
						if (entry_tmp[j] == 0x20)
							break;
						CString cur_ascii;
						cur_ascii.Format(_T("%c"), entry_tmp[j]);
						SFN_name += cur_ascii;
					}
					for (DWORD j = 0x08; j <= 0x0A; j++) {
						if (entry_tmp[j] == 0x20)
							break;
						CString cur_ascii;
						cur_ascii.Format(_T("%c"), entry_tmp[j]);
						ext_name += cur_ascii;
					}
					if (ext_name != _T("")) {
						SFN_name += _T(".") + ext_name;
					}
					cur_file.file_name = SFN_name;
				}
				else {
					LFN_content.TrimRight(remove_word);
					LFN_content.TrimRight(end_word);
					cur_file.file_name = LFN_content;
				}

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

BOOL FileSys::copyfile(Device cur_device, CString dest_path, FileInfo source_file) {
	// open destination file
	wchar_t* dest_pathW = cstr2strW(dest_path);
	HANDLE hDest = CreateFile(dest_pathW,
							GENERIC_WRITE,
							0,
							NULL,
							CREATE_NEW,
							FILE_ATTRIBUTE_NORMAL,
							NULL);
	delete[] dest_pathW;
	if (hDest == INVALID_HANDLE_VALUE) {
		TRACE(_T("\n[Error] Open destination file failed. Error Code = %u.\n"), GetLastError());
		CloseHandle(hDest);
		return FALSE;
	}

	// check if empty file
	if (source_file.file_size == 0 && source_file.file_addr == 0) {
		TRACE(_T("\n[Info] Copy an empty file. Error Code = %u.\n"), GetLastError());
		CloseHandle(hDest);
		return TRUE;
	}

	// device information
	HANDLE hDevice = cur_device.openDevice();
	DWORD max_transf_len = cur_device.getMaxTransfLen();
	if (hDevice == INVALID_HANDLE_VALUE) {
		TRACE(_T("\n[Error] Open usb device failed. Error Code = %u.\n"), GetLastError());
		CloseHandle(hDest);
		CloseHandle(hDevice);
		return FALSE;
	}

	// get cluster chain
	vector<DWORD> clu_chain;
	DWORD file_begin_clu = source_file.file_addr;
	if (!getCluChain(hDevice, max_transf_len, file_begin_clu, clu_chain)) {
		TRACE(_T("\n[Error] Get cluster chain failed. Error Code = %u.\n"), GetLastError());
		CloseHandle(hDest);
		CloseHandle(hDevice);
		return FALSE;
	}

	// read file content
	DWORD dw_bytes_to_write = source_file.file_size, total_bytes_write = 0, dw_bytes_write;
	DWORD bytes_per_clu = this->sec_per_clu * PHYSICAL_SECTOR_SIZE;
	for (DWORD cur_clu_idx : clu_chain) {
		// read cluster
		BYTE* cur_clu = new BYTE[bytes_per_clu];
		ULONGLONG cur_clu_offset = this->heap_offset + ((ULONGLONG)cur_clu_idx - 2) * bytes_per_clu;
		if (!SCSISectorIO(hDevice, max_transf_len, cur_clu_offset, cur_clu, bytes_per_clu, FALSE)) {
			TRACE(_T("\n[Error] Read file content failed. Error Code = %u.\n"), GetLastError());
			delete[] cur_clu;
			CloseHandle(hDest);
			CloseHandle(hDevice);
			return FALSE;
		}

		// write file
		DWORD cur_bytes_to_write = (dw_bytes_to_write > bytes_per_clu) ? bytes_per_clu : dw_bytes_to_write;
		if (!WriteFile(hDest, cur_clu, cur_bytes_to_write, &dw_bytes_write, NULL)) {
			TRACE(_T("\n[Error] Write file failed. Error Code = %u.\n"), GetLastError());
			delete[] cur_clu;
			CloseHandle(hDest);
			CloseHandle(hDevice);
			return FALSE;
		}

		dw_bytes_to_write -= dw_bytes_write;
		total_bytes_write += dw_bytes_write;

		delete[] cur_clu;
	}
	if (total_bytes_write != source_file.file_size) {
		TRACE(_T("\n[Warn] The written file size isn't identical. Write bytes: %lu; File size: %lu.\n"),
				total_bytes_write, source_file.file_size);
	}

	CloseHandle(hDest);
	CloseHandle(hDevice);

	return TRUE;
}

BOOL FileSys::copyfileByAPI(Device cur_device, CString dest_path, FileInfo source_file)
{
	// get source file path
	char ident = cur_device.getIdent();
	CString source_path;
	source_path.Format(_T("%c:\\%s"), ident, source_file.file_name);
	
	if (!CopyFile(source_path, dest_path, TRUE)) {
		TRACE(_T("\n[Error] Copy file by API failed. Error Code = %u.\n"), GetLastError());
		return FALSE;
	}

	return TRUE;
}
