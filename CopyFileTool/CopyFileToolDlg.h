
// CopyFileToolDlg.h : header file
//

#pragma once


// CCopyFileToolDlg dialog
class CCopyFileToolDlg : public CDialogEx
{
// Construction
public:
	CCopyFileToolDlg(CWnd* pParent = nullptr);	// standard constructor

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_COPYFILETOOL_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	CEdit dest_ctrl;
	CComboBox device_ctrl;
	CListCtrl file_list_ctrl;
	afx_msg void OnCbnSelchangeDeviceCombo();
	afx_msg void OnCbnDropdownDeviceCombo();
};
