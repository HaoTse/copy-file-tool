
// CopyFileToolDlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "CopyFileTool.h"
#include "CopyFileToolDlg.h"
#include "afxdialogex.h"

#include "utils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

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

	// reset file system object
	file_sys.initFileSys();

	// get selected device
	int device_idx = device_ctrl.GetCurSel();
	Device cur_device = device_list.at(device_idx);

	TRACE(_T("\n[Msg] Device selected: %c:\n"), cur_device.getIdent());

	// get file list
	int file_num;
	file_num = file_sys.getFileList(cur_device);
	if (file_num < 0) {
		MessageBox(_T("Get file list failed."), _T("Error"), MB_ICONERROR);
		return;
	}

	for (int i = 0; i < file_num; i++) {
		CString text;
		FileInfo cur_file = file_sys.file_info.at(i);
		int nRow = file_list_ctrl.InsertItem(0, cur_file.file_name);
		text.Format(_T("%llu"), cur_file.file_size);
		file_list_ctrl.SetItemText(nRow, 1, text);
		text.Format(_T("%llu"), cur_file.file_addr);
		file_list_ctrl.SetItemText(nRow, 2, text);
	}
}


void CCopyFileToolDlg::OnCbnDropdownDeviceCombo()
{
	// empty options
	device_ctrl.ResetContent();

	// reset device_list
	for (vector<Device>::iterator iter = device_list.begin(); iter != device_list.end(); ) {
		iter = device_list.erase(iter);
	}
	vector<Device>().swap(device_list);

	// set device combo box
	int usb_cnt = enumUsbDisk(device_list, 8);
	if (usb_cnt == -1) {
		MessageBox(_T("Enumerate usb disk failed."), _T("Error"), MB_ICONERROR);
	}
	else {
		for (int i = 0; i < usb_cnt; i++) {
			Device cur_device = device_list.at(i);
			device_ctrl.InsertString(i, cur_device.showText());
		}
	}
	SetDropDownHeight(&device_ctrl, usb_cnt);
}
