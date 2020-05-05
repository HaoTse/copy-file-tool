#pragma once

#include "vector"
#include "device.h"

using namespace std;

char* cstr2str(CString cstr);

wchar_t* cstr2strW(CString str);

void SetDropDownHeight(CComboBox* pMyComboBox, int itemsToShow);

int enumUsbDisk(vector<Device>& device_list, int cnt);

BOOL directoryExists(CString path);

BOOL fileExists(CString path);