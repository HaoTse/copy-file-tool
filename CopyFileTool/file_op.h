#pragma once

#include <vector>

using namespace std;

int getFileList(HANDLE hDevice, vector<CString> &file_name, vector<ULONGLONG> &file_addr, vector<ULONGLONG> &file_size);