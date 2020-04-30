
#include "pch.h"
#include "utils.h"

char* cstr2str(CString cstr) {
	const size_t newsizew = (cstr.GetLength() + 1) * 2;
	char* nstringw = new char[newsizew];
	size_t convertedCharsw = 0;
	wcstombs_s(&convertedCharsw, nstringw, newsizew, cstr, _TRUNCATE);

	return nstringw;
}

void SetDropDownHeight(CComboBox* pMyComboBox, int itemsToShow) {
	// Get rectangles    
	CRect rctComboBox, rctDropDown;
	pMyComboBox->GetClientRect(&rctComboBox); // Combo rect    
	pMyComboBox->GetDroppedControlRect(&rctDropDown); // DropDownList rect   
	int itemHeight = pMyComboBox->GetItemHeight(-1); // Get Item height   
	pMyComboBox->GetParent()->ScreenToClient(&rctDropDown); // Converts coordinates    
	rctDropDown.bottom = rctDropDown.top + rctComboBox.Height() + itemHeight * itemsToShow; // Set height   
	pMyComboBox->MoveWindow(&rctDropDown); // enable changes  
}