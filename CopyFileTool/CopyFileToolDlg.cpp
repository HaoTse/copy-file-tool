
// CopyFileToolDlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "CopyFileTool.h"
#include "CopyFileToolDlg.h"
#include "afxdialogex.h"
#include <vector>

#include "device.h"
#include "utils.h"
#include "file_op.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace std;

// CCopyFileToolDlg dialog

CCopyFileToolDlg::CCopyFileToolDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_COPYFILETOOL_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CCopyFileToolDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_DEST_EDIT, dest_ctrl);
	DDX_Control(pDX, IDC_DEVICE_COMBO, device_ctrl);
	DDX_Control(pDX, IDC_FILE_LIST, file_list_ctrl);
}

BEGIN_MESSAGE_MAP(CCopyFileToolDlg, CDialogEx)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_CBN_SELCHANGE(IDC_DEVICE_COMBO, &CCopyFileToolDlg::OnCbnSelchangeDeviceCombo)
	ON_CBN_DROPDOWN(IDC_DEVICE_COMBO, &CCopyFileToolDlg::OnCbnDropdownDeviceCombo)
END_MESSAGE_MAP()


// CCopyFileToolDlg message handlers

BOOL CCopyFileToolDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// TODO: Add extra initialization here
	// initial file list
	file_list_ctrl.SetExtendedStyle(file_list_ctrl.GetExtendedStyle() |
									LVS_EX_GRIDLINES |
									LVS_EX_FULLROWSELECT |
									LVS_EX_HEADERDRAGDROP);
	file_list_ctrl.InsertColumn(0, _T("Name"), LVCFMT_LEFT, 250, 0);
	file_list_ctrl.InsertColumn(1, _T("Size (Bytes)"), LVCFMT_LEFT, 90, 1);
	file_list_ctrl.InsertColumn(2, _T("First Cluster"), LVCFMT_LEFT, 90, 2);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CCopyFileToolDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CCopyFileToolDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}



void CCopyFileToolDlg::OnCbnSelchangeDeviceCombo()
{
	// reset file list
	file_list_ctrl.DeleteAllItems();

	// get selected device
	CString text;
	char device_name;
	int device_idx = device_ctrl.GetCurSel();
	
	char* device_name_w_capacity;
	device_ctrl.GetLBText(device_idx, text);
	device_name_w_capacity = cstr2str(text);
	device_name = device_name_w_capacity[0]; // only store the identifier
	delete[] device_name_w_capacity;

	TRACE(_T("\n[Msg] Device selected: %c:\n"), device_name);

	// get file list
	HANDLE hDevice = getHandle(device_name);
	int file_num;
	vector<CString> file_name, file_type;
	vector<ULONGLONG> file_addr, file_size;
	file_num = getFileList(hDevice, file_name, file_addr, file_size);
	if (file_num < 0) {
		MessageBox(_T("Get file list failed."), _T("Error"), MB_ICONERROR);
		return;
	}

	for (int i = 0; i < file_num; i++) {
		CString text;
		int nRow = file_list_ctrl.InsertItem(0, file_name.at(i));
		text.Format(_T("%llu"), file_size.at(i));
		file_list_ctrl.SetItemText(nRow, 1, text);
		text.Format(_T("%llu"), file_addr.at(i));
		file_list_ctrl.SetItemText(nRow, 2, text);
	}

	CloseHandle(hDevice);
}


void CCopyFileToolDlg::OnCbnDropdownDeviceCombo()
{
	// empty options
	device_ctrl.ResetContent();

	// set device combo box
	char usb_volume[8] = { 0 };
	DWORD usb_capacity_sec[8];
	int usb_cnt = enumUsbDisk(usb_volume, usb_capacity_sec, 8);
	if (usb_cnt == -1) {
		MessageBox(_T("Enumerate usb disk failed."), _T("Error"), MB_ICONERROR);
	}
	else {
		for (int i = 0; i < usb_cnt; i++) {
			CString text;
			DWORD capacity_MB = (usb_capacity_sec[i] >> 20) * PHYSICAL_SECTOR_SIZE;
			if (capacity_MB > 1024) {
				double capacity_GB = (double)capacity_MB / 1024;
				text.Format(_T("%c: (%.1f GB)"), usb_volume[i], capacity_GB);
			}
			else {
				text.Format(_T("%c: (%u MB)"), usb_volume[i], capacity_MB);
			}
			device_ctrl.InsertString(i, text);
		}
	}
	SetDropDownHeight(&device_ctrl, usb_cnt);
}
