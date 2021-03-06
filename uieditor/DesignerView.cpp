﻿#include "stdafx.h"
#include "DesignerView.h"
#include "helper\SplitString.h"
#include "Dialog/DlgSkinSelect.h"
#include "Dialog/DlgStyleManage.h"
#include "Dialog/DlgFontSelect.h"
#include "core/SWnd.h"
#include "MainDlg.h"
#include "adapter.h"
#include "Global.h"
#include "pugixml_write.h"
#include "SysdataMgr.h"
#include <helper/SAppDir.h>

//编辑界面时XML窗口只显示选择控件的XML文本
//#define  ONLYSHOWSELXML

#define  MARGIN 20
extern CMainDlg* g_pMainDlg;

extern BOOL g_bHookCreateWnd;	//是否拦截窗口的建立
extern CSysDataMgr g_SysDataMgr;
static pugi::xml_node emptyNode;

namespace SOUI{

BOOL SDesignerView::NewLayout(SStringT strResName, SStringT strPath)
{
	SStringT strShortPath = strPath.Mid(m_strProPath.GetLength() + 1);

	pugi::xml_node xmlNode = m_xmlDocUiRes.child(_T("resource")).child(_T("LAYOUT"));

	if (xmlNode)
	{
		pugi::xml_node Child = xmlNode.append_child(_T("file"));
		Child.append_attribute(_T("name")).set_value(strResName);
		Child.append_attribute(_T("path")).set_value(strShortPath);

		m_xmlDocUiRes.save_file(m_strUIResFile);
	}

	return TRUE;
}


SDesignerView::SDesignerView(SHostDialog *pMainHost,  STreeCtrl *pTreeXmlStruct)
{
	m_nSciCaretPos = 0;
	m_nState = 0;
	m_pMainHost = pMainHost;
	m_treeXmlStruct = pTreeXmlStruct;
	m_ndata = 0;
	m_CurSelCtrlIndex = -1;
	m_CurSelCtrlItem = NULL;
	m_pPropgrid = NULL;

	((SouiEditorApp*)SApplication::getSingletonPtr())->InitEnv();

	m_bXmlResLoadOK = false;
	m_treeXmlStruct->GetEventSet()->subscribeEvent(EVT_TC_SELCHANGED, Subscriber(&SDesignerView::OnTCSelChanged, this));

	pugi::xml_document doc;
	BOOL result = LoadConfig(doc,_T("Config\\Ctrl.xml"));
	if (!result)
	{
		Debug(_T("加载Ctrl.xml失败\n 请将 demos/SouiEditor 下的 Config 文件夹拷贝到本程序所在目录."));
		return;
	}

	pugi::xml_node node = doc.child(_T("root")).child(_T("容器控件")).first_child();
	while (node)
	{
		m_lstContainerCtrl.AddTail(node.name());
		node = node.next_sibling();
	}

	m_bXmlResLoadOK = true;
}

BOOL SDesignerView::OpenProject(SStringT strFileName)
{
	m_xmlDocUiRes.load_file(strFileName, pugi::parse_full);

	m_strUIResFile = strFileName;
	TCHAR *s = strFileName.GetBuffer(strFileName.GetLength());

	PathRemoveFileSpec(s);
	SStringT strTemp(s);
	m_strProPath = strTemp;

/*
	CAutoRefPtr<IResProvider> pResProvider;
	CreateResProvider(RES_FILE, (IObjRef**)&pResProvider);
	if (!pResProvider->Init((LPARAM)s, 0))
	{
		Debug(_T("CreateResProvider失败"));
		return FALSE;
	}*/

	SStringT strXMLInit;
	pugi::xml_node xmlNode = m_xmlDocUiRes.child(_T("resource")).child(_T("UIDEF")).child(_T("file"));

	if (xmlNode)
	{
		strXMLInit = xmlNode.attribute(_T("name")).as_string();
	}

	if (strXMLInit.IsEmpty())
	{
		strXMLInit = _T("xml_init");
	}

	return TRUE;
}

BOOL SDesignerView::CloseProject()
{
	m_strCurLayoutXmlFile.Empty();
	m_strCurFileEditor.Empty();
	m_strProPath = m_strUIResFile = L"";
	m_xmlDocUiRes.reset();
	UseEditorUIDef(true);
	m_textCtrlTypename->SetWindowText(L"");
	((CMainDlg*)m_pMainHost)->m_textNodenum->SetWindowText(L"");

	m_pScintillaWnd->SendEditor(SCI_CLEARALL);
	m_treeXmlStruct->RemoveAllItems();

	if(m_pPropgrid)
	{
		m_pPropertyContainer->DestroyChild(m_pPropgrid);
		m_pPropgrid = NULL;
	}

	m_nState = 0;
	m_ndata = 0;
	m_nSciCaretPos = 0;
	m_curPropertyXmlNode = emptyNode;

	ShowNoteInSciwnd();

	return TRUE;
}

BOOL SDesignerView::InsertLayoutToMap(SStringT strFileName)
{
	SStringT FullFileName = m_strProPath + _T("\\") + strFileName;

	pugi::xml_document *xmlDoc1 = new pugi::xml_document();

	if (!xmlDoc1->load_file(FullFileName, pugi::parse_full))
		return FALSE;

	m_mapLayoutFile[strFileName] = xmlDoc1;
	m_mapIncludeReplace[strFileName] = new SMap<int, SStringT>;

	SStringT tmpFilename = m_strCurLayoutXmlFile;
	m_strCurLayoutXmlFile = strFileName;
	//RenameChildeWnd(xmlDoc1->first_child());
	RenameChildeWnd(xmlDoc1->root());
	m_strCurLayoutXmlFile = tmpFilename;
	return TRUE;
}

void SDesignerView::StartPreviewProcess()
{
	SAppDir appdir(NULL);
	SStringT binDir = appdir.AppDir();
#ifdef _DEBUG
	SStringT strPreviewExePath = binDir + _T("\\uiviewerd.exe");
#else
	SStringT strPreviewExePath = binDir + _T("\\uiviewer.exe");
#endif

	TCHAR buffer[32] = { 0 };
	SStringT strCommandLine = strPreviewExePath + _T(" ");
	strCommandLine += _T("\"");
	strCommandLine += m_strProPath;
	strCommandLine += _T("\" LAYOUT:");
	strCommandLine += m_strCurLayoutName;
	strCommandLine += _T(" ");
	strCommandLine += _ltot((long)g_pMainDlg->m_hWnd, buffer, 10);
	strCommandLine += _T(" ");
	strCommandLine += _ltot((long)g_pMainDlg->m_pLayoutContainer->GetRealHwnd(), buffer, 10);
	
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	::ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(STARTUPINFO);
	si.wShowWindow = SW_SHOWNORMAL;
	si.dwFlags = STARTF_USESHOWWINDOW;

	if (!CreateProcess(NULL, (LPTSTR)strCommandLine.c_str(), 
		NULL, NULL, TRUE, NULL, NULL, NULL, &si, &pi))
	{
		int err = GetLastError();
		return;
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}
	
BOOL SDesignerView::LoadLayout(SStringT strFileName, SStringT layoutName)
{
	m_nSciCaretPos = 0;
	m_CurSelCtrlIndex = 0;
	g_pMainDlg->SendMsgToViewer(exitviewer_id, NULL, 0);
	
	m_defFont = SFontPool::getSingleton().GetFont(FF_DEFAULTFONT, 100);
	m_strCurLayoutXmlFile = strFileName;
	m_strCurLayoutName = layoutName;
	SMap<SStringT, pugi::xml_document*>::CPair *p = m_mapLayoutFile.Lookup(strFileName);
	if (!p)
	{
		m_pScintillaWnd->SendMessage(SCI_SETREADONLY, 0, 0);
		SStringT strPath = m_strProPath + _T("\\") + strFileName;
		m_pScintillaWnd->OpenFile(strPath);

		int n = m_pScintillaWnd->SendEditor(SCI_GETTEXT, 0, 0);
		if (n > 0)
		{
			char *chText = new char[n];
			m_pScintillaWnd->SendEditor(SCI_GETTEXT, n, (LPARAM)chText);
			SStringW sText = S_CA2W(chText, CP_UTF8);
			delete chText;

			pugi::xml_document xmlDoc;
			pugi::xml_parse_result res = xmlDoc.load_buffer(sText.c_str(), sText.GetLength(), pugi::parse_full);

			int linecount = 0;
			SStringW subText = sText.Mid(0, res.offset);
			//MessageBoxW(0, subText.c_str(), L"", 0);
			for (int i = 0; i < subText.GetLength(); i++)
			{
				if (subText[i] == '\n')
					linecount++;
			}
			m_pScintillaWnd->SetFocus();
			m_pScintillaWnd->SendEditor(SCI_GOTOLINE, linecount, 0);
			m_pScintillaWnd->SendEditor(SCI_LINESCROLL, 0, linecount);
			
			SStringT strNote;
			strNote.Format(_T("%s 的源文件在 %d 行附近存在语法错误"), strFileName, linecount);

			SMessageBox(NULL, strNote, _T("提示"), MB_OK);
		}

		return FALSE;
	}

	pugi::xml_node xmlroot = p->m_value->document_element();
	m_CurrentLayoutNode = xmlroot;
	ReloadLayout(TRUE);

	m_nState = 0;
	SelectCtrlByIndex(0);	
	m_pScintillaWnd->ResetRedo();

	StartPreviewProcess();

	return TRUE;
}

BOOL SDesignerView::ReloadLayout(BOOL bClearSel)
{
	if (bClearSel)
	{
		m_CurSelCtrlIndex = 0;
		m_curPropertyXmlNode = emptyNode;
	}

	pugi::xml_node xmlnode;
	BOOL bIsInclude = FALSE;

	m_ndata = 0;
	m_CurSelCtrlItem = NULL;
	
	if (m_CurrentLayoutNode == NULL)
		return TRUE;

	if (S_CW2T(m_CurrentLayoutNode.name()) != _T("SOUI"))
	{
		//include文件
		xmlnode = m_CurrentLayoutNode;
		bIsInclude = TRUE;
	}
	else
	{
		xmlnode = m_CurrentLayoutNode.child(L"root", false);
	}
	if (!xmlnode) return FALSE;

	//m_pContainer->SSendMessage(WM_DESTROY);

	SStringW s1, s2;
	if (!bIsInclude)
	{
		int nWidth, nHeight;
		SStringT strSize(_T("size"));
		SStringT strWidth(_T("width"));
		SStringT strHeight(_T("height"));
		SStringT strMargin(_T("margin"));
		BOOL bHasSize = FALSE;

		pugi::xml_attribute attr = m_CurrentLayoutNode.first_attribute();
		while (attr)
		{
			// width height单独处理，解决margin的问题
			if (strSize.CompareNoCase(attr.name()) == 0)
			{
				//size属性
				SStringT strVal = attr.value();
				swscanf(strVal, L"%d,%d", &nWidth, &nHeight);

				bHasSize = TRUE;
			}
			else if (strWidth.CompareNoCase(attr.name()) == 0)
			{
				//width属性
				::StrToIntExW(attr.value(), STIF_SUPPORT_HEX, &nWidth);
			}
			else if (strHeight.CompareNoCase(attr.name()) == 0)
			{
				//height属性
				::StrToIntExW(attr.value(), STIF_SUPPORT_HEX, &nHeight);
			}
			else if (strMargin.CompareNoCase(attr.name()) == 0)
			{
				//忽略margin属性
			}
			else
			{
				s1.Format(L" %s=\"%s\" ", attr.name(), attr.value());
				s2 = s2 + s1;
			}
			attr = attr.next_attribute();
		}

		//删除size 改成width height
		if (bHasSize)
		{
			m_CurrentLayoutNode.remove_attribute(_T("size"));

			pugi::xml_attribute attrWorH = m_CurrentLayoutNode.attribute(_T("width"));
			if (attrWorH)
			{
				attrWorH.set_value(nWidth);
			}

			attrWorH = m_CurrentLayoutNode.attribute(_T("height"));
			if (attrWorH)
			{
				attrWorH.set_value(nHeight);
			}
		}
	}
	else
	{
		//include文件
		int nWidth, nHeight;
		pugi::xml_attribute attrWorH = m_CurrentLayoutNode.attribute(_T("width"));

		if (attrWorH)
		{
			::StrToIntExW(attrWorH.value(), STIF_SUPPORT_HEX, &nWidth);
		}
		else
		{
			nWidth = 500;
			m_CurrentLayoutNode.append_attribute(_T("width")).set_value(nWidth);
		}

		attrWorH = m_CurrentLayoutNode.attribute(_T("height"));

		if (attrWorH)
		{
			::StrToIntExW(attrWorH.value(), STIF_SUPPORT_HEX, &nHeight);
		}
		else
		{
			nHeight = 500;
			m_CurrentLayoutNode.append_attribute(_T("height")).set_value(nHeight);
		}
	}

	SStringA strNoteTag;
	m_treeXmlStruct->RemoveAllItems();
	m_rootItem = NULL;
	InitXMLStruct(m_CurrentLayoutNode, STVI_ROOT);
	if(m_rootItem==NULL)
	{
		m_rootItem = m_treeXmlStruct->GetRootItem();
	}
	return FALSE;
}
	
void SDesignerView::SelectCtrlByIndex(int index, bool bReCreatePropGrid)
{
	SLOG_INFO("SelectCtrlByIndex,index="<<index);
	if (index != 0)
	{
		SStringT s;
		//long data = m_pRealWnd->GetUserData();
		s.Format(_T("%d"), index);
		SetCurrentCtrl(FindNodeByAttr(m_CurrentLayoutNode, L"data", s), index);
		if (bReCreatePropGrid)
		{
			CreatePropGrid(m_curSelXmlNode.name());
			UpdatePropGrid(m_curSelXmlNode);
		}
	}
	else
	{
		SetCurrentCtrl(m_CurrentLayoutNode, index);
		if (bReCreatePropGrid)
		{
			CreatePropGrid(_T("hostwnd"));
			UpdatePropGrid(m_curSelXmlNode);
		}
	}

	AddCodeToEditor(NULL);
}
	
/*
void SDesignerView::CreateAllChildWnd(SUIWindow *pRealWnd, SMoveWnd *pMoveWnd)
{
	//view系列加上适配器
	if (pRealWnd->IsClass(SMCListView::GetClassNameW()))
	{
		CBaseMcAdapterFix *mcAdapter = new CBaseMcAdapterFix();
		((SMCListView*)pRealWnd)->SetAdapter(mcAdapter);
		mcAdapter->Release();
	}
	//listview(flex)需要重新处理，有空再来
	if (pRealWnd->IsClass(SListView::GetClassNameW()))
	{
		CBaseAdapterFix *listAdapter = new CBaseAdapterFix();
		((SListView*)pRealWnd)->SetAdapter(listAdapter);
		listAdapter->Release();
	}
	if (pRealWnd->IsClass(STileView::GetClassNameW()))
	{
		CBaseAdapterFix *listAdapter = new CBaseAdapterFix();
		((STileView*)pRealWnd)->SetAdapter(listAdapter);
		listAdapter->Release();
	}
	////得到第一个子窗口
	SUIWindow *pSibReal = (SUIWindow*)pRealWnd->GetWindow(GSW_FIRSTCHILD);
	for (; pSibReal; pSibReal = (SUIWindow*)pSibReal->GetWindow(GSW_NEXTSIBLING))
	{
		const wchar_t *s1 = L"<movewnd pos=\"0,0,@100,@100\" ></movewnd>";
		//创建布局窗口的根窗口
		SMoveWnd *pSibMove = (SMoveWnd *)pMoveWnd->CreateChildren(s1);
		pSibMove->m_pRealWnd = pSibReal;
		pSibMove->SetVisible(pSibReal->IsVisible());
		m_mapMoveRealWnd[pSibReal] = pSibMove;
		pSibMove->m_Desiner = this;

		CreateAllChildWnd(pSibReal, pSibMove);
	}
}*/

SDesignerView::~SDesignerView()
{

}

BOOL SDesignerView::SaveAll()
{
	SMap<SStringT, pugi::xml_document*>::CPair *p;
	SStringT strFileName;
	SStringT FullFileName;
	pugi::xml_document *doc;
	pugi::xml_document DocSave;
	bool bRet = true;

	SPOSITION pos = m_mapLayoutFile.GetStartPosition();

	while (pos)
	{
		p = m_mapLayoutFile.GetNext(pos);
		strFileName = p->m_key;
		doc = p->m_value;

		DocSave.reset();
		DocSave.append_copy(doc->document_element());

		pugi::xml_node NodeSave = DocSave.root();
		TrimXmlNodeTextBlank(DocSave.document_element());
		RemoveWndName(NodeSave, FALSE, strFileName);

		FullFileName = m_strProPath + _T("\\") + strFileName;
		if (!DocSave.save_file(FullFileName, L"\t", pugi::format_default, pugi::encoding_utf8))
		{
			Debug(_T("保存文件失败：") + FullFileName);
			bRet = false;
		}
	}

	if (!m_xmlDocUiRes.save_file(m_strUIResFile))
	{
		Debug(_T("保存文件失败：") + m_strUIResFile);
		bRet = false;
	}

	if (bRet)
	{
		Debug(_T("保存成功"));
	}
	else
	{
		Debug(_T("保存失败"));
	}

	return TRUE;
}

//保存当前打开的布局文件
bool SDesignerView::SaveLayoutFile()
{
	if (m_strCurLayoutXmlFile.IsEmpty())
	{
		return false;
	}
	bool bRet = false;
	SStringT strFile = m_strCurLayoutXmlFile;
	SStringT strFileName;
	SStringT FullFileName;

	SMap<SStringT, pugi::xml_document*>::CPair *p = m_mapLayoutFile.Lookup(strFile);
	strFileName = p->m_key;
	pugi::xml_document *doc = p->m_value;

	pugi::xml_document DocSave;
	DocSave.append_copy(doc->document_element());

	pugi::xml_node NodeSave = DocSave.root();
	TrimXmlNodeTextBlank(DocSave.document_element());
	RemoveWndName(NodeSave, FALSE);
	FullFileName = m_strProPath + _T("\\") + strFileName;
	bRet = DocSave.save_file(FullFileName, L"\t", pugi::format_default, pugi::encoding_utf8);
	if (!bRet)
	{
		Debug(_T("保存文件失败：") + FullFileName);
	}

	if (bRet)
	{
		bRet = m_xmlDocUiRes.save_file(m_strUIResFile);
		if (!bRet)
		{
			Debug(_T("保存失败:") + m_strUIResFile);
		}
	}
	return bRet;
}

//关闭当前打开的布局文件
BOOL SDesignerView::CloseLayoutFile()
{
	return TRUE;
}

/*
//创建窗口
SMoveWnd* SDesignerView::CreateWnd(SUIWindow *pContainer, LPCWSTR pszXml)
{
	SWindow *pChild = pContainer->CreateChildren(pszXml);
	((SMoveWnd*)pChild)->m_Desiner = this;
	m_CurSelCtrl = (SMoveWnd*)pChild;
	return (SMoveWnd*)pChild;
}*/

void SDesignerView::RenameWnd(pugi::xml_node xmlNode, BOOL force)
{
	if (xmlNode.type() != pugi::node_element)
	{
		return;
	}

	pugi::xml_attribute xmlAttr = xmlNode.attribute(L"data", false);
	pugi::xml_attribute xmlAttr1 = xmlNode.attribute(L"uidesiner_data", false);

	SStringT strName = _T("item"); //不处理item节点
	if (strName.CompareNoCase(xmlNode.name()) == 0)
	{
		return;
	}

	if (!xmlAttr)
	{
		xmlNode.append_attribute(L"data").set_value(GetIndexData());
		xmlNode.append_attribute(L"uidesiner_data").set_value(_T(""));
	}
	else
	{
		int data = xmlAttr.as_int();

		if (!xmlAttr1)
		{
			xmlNode.append_attribute(L"uidesiner_data").set_value(data);
		}
		else
		{
			xmlAttr1.set_value(data);
		}

		xmlAttr.set_value(GetIndexData());
	}
	//}
}

// 还原替换的include为原始内容
void SDesignerView::RemoveWndName(pugi::xml_node xmlNode, BOOL bClear, SStringT strFileName)
{
	pugi::xml_node NodeChild = xmlNode.first_child();

	pugi::xml_attribute attr, attr1;
	pugi::xml_document doc;

	while (NodeChild)
	{
		if (NodeChild.type() != pugi::node_element)
		{
			NodeChild = NodeChild.next_sibling();
			continue;
		}

		attr = NodeChild.attribute(L"uidesiner_data", false);
		attr1 = NodeChild.attribute(L"data", false);

		if (strFileName.IsEmpty())
		{
			strFileName = m_strCurLayoutXmlFile;
		}

		SMap<SStringT, SMap<int, SStringT>* >::CPair *p = m_mapIncludeReplace.Lookup(strFileName);
		SMap<int, SStringT>* pMap;
		SMap<int, SStringT>::CPair *p1;
		if (p)
		{
			pMap = p->m_value;
			p1 = pMap->Lookup(attr1.as_int());
		}
		else
		{
			Debug(_T("替换include出错"));
		}

		if (p1)
		{	// 如果这个控件是include
			if (!doc.load_buffer(p1->m_value, wcslen(p1->m_value) * sizeof(wchar_t), pugi::parse_default, pugi::encoding_utf16))
			{
				Debug(_T("RemoveWndName出错了"));
			}
			else
			{
				pugi::xml_node nodeNew;
				SStringT oldname = NodeChild.name();
				
 				nodeNew = NodeChild.parent().insert_copy_after(doc.first_child(), NodeChild);
				nodeNew.set_name(oldname);
 				NodeChild.parent().remove_child(NodeChild);
				NodeChild = nodeNew.next_sibling();
				if (bClear)
				{
					pMap->RemoveKey(attr1.as_int());
				}
			}
		}
		else
		{
			if (attr && _wcsicmp(NodeChild.name(), L"item") != 0)
			{
				SStringT str = attr.value();
				if (str.IsEmpty())
				{
					NodeChild.remove_attribute(L"uidesiner_data");
					NodeChild.remove_attribute(L"data");
				}
				else
				{
					attr1.set_value(str);
					NodeChild.remove_attribute(L"uidesiner_data");
				}
			}

			if (_wcsicmp(NodeChild.name(), L"item") != 0)
			{
				RemoveWndName(NodeChild, bClear, strFileName);
			}

			NodeChild = NodeChild.next_sibling();
		}
	}

}

void SDesignerView::RenameChildeWnd(pugi::xml_node xmlNode)
{
	pugi::xml_node NodeChild = xmlNode.first_child();

	while (NodeChild)
	{
		if (NodeChild.type() != pugi::node_element)
		{
			NodeChild = NodeChild.next_sibling();
			continue;
		}

		//替换Include 成一个window
		if (_wcsicmp(NodeChild.name(), L"include") == 0 && NodeChild.attribute(L"src"))
		{
			SStringT strInclude = NodeToStr(NodeChild);
			//NodeChild.set_name(L"window");
			NodeChild.remove_attribute(L"src");
			NodeChild.append_attribute(L"pos").set_value(L"10,10,-10,-10");
			NodeChild.append_attribute(L"colorBkgnd").set_value(L"RGB(191,141,255)");
			NodeChild.text().set(strInclude);

			RenameWnd(NodeChild);
			
			SMap<SStringT, SMap<int, SStringT>* >::CPair *p = m_mapIncludeReplace.Lookup(m_strCurLayoutXmlFile);
			if (p)
			{
				SMap<int, SStringT>* pMap = p->m_value;
				(*pMap)[NodeChild.attribute(L"data").as_int()] = strInclude;
			}
			else
			{
				Debug(_T("替换include出错"));
			}

			NodeChild = NodeChild.next_sibling();

			continue;
		}

		//将<button data = "1"/> 修改为
		//  <button data = "xxx" uidesiner_data = "1"/> 
		RenameWnd(NodeChild);
		RenameChildeWnd(NodeChild);

		NodeChild = NodeChild.next_sibling();
	}
}

void SDesignerView::RenameAllLayoutWnd()
{
	SPOSITION pos = m_mapLayoutFile.GetStartPosition();
	while (pos)
	{
		SMap<SStringT, pugi::xml_document*>::CPair *p = m_mapLayoutFile.GetNext(pos);
		RenameChildeWnd(p->m_value->document_element());
	}
}

pugi::xml_node SDesignerView::FindNodeByAttr(pugi::xml_node NodeRoot, SStringT attrName, SStringT attrValue)
{
	pugi::xml_node NodeChild = NodeRoot.first_child();

	pugi::xml_attribute attr;
	pugi::xml_node NodeResult;

	while (NodeChild)
	{
		if (NodeChild.type() != pugi::node_element)
		{
			NodeChild = NodeChild.next_sibling();
			continue;
		}

		if (_wcsicmp(NodeChild.name(), _T("item")) == 0)
		{
			NodeChild = NodeChild.next_sibling();
			continue;
		}

		attr = NodeChild.attribute(attrName, false);
		if (attr)
		{
			if (0 == attrValue.CompareNoCase(attr.value()))
			{
				return NodeChild;
			}
		}

		NodeResult = FindNodeByAttr(NodeChild, attrName, attrValue);
		if (NodeResult)
		{
			return NodeResult;
		}

		NodeChild = NodeChild.next_sibling();
	}

	return NodeResult;
}


void SDesignerView::Debug(pugi::xml_node xmlNode)
{
	pugi::xml_writer_buff writer;
	xmlNode.print(writer, L"\t", pugi::format_default, pugi::encoding_utf16);
	SStringW *strDebug = new SStringW(writer.buffer(), writer.size());
	SMessageBox(g_pMainDlg->m_hWnd, *strDebug, _T(""), MB_OK);
	delete strDebug;
}

void SDesignerView::Debug(SStringT str)
{
	SMessageBox(g_pMainDlg->m_hWnd, str, _T(""), MB_OK);
}

SStringT SDesignerView::Debug1(pugi::xml_node xmlNode)
{
	pugi::xml_writer_buff writer;
	xmlNode.print(writer, L"\t", pugi::format_default, pugi::encoding_utf16);
	SStringW *strDebug = new SStringW(writer.buffer(), writer.size());
	SStringT strtemp = *strDebug;
	delete strDebug;
	return strtemp;
}

SStringT SDesignerView::NodeToStr(pugi::xml_node xmlNode)
{
	SStringT writer_buf;
	myxml_writer_stream writer(writer_buf);
	xmlNode.print(writer, L"\t", pugi::format_default, pugi::encoding_utf16);
	return writer_buf;
}

// 响应各事件, 选中相应元素
void SDesignerView::SetCurrentCtrl(pugi::xml_node xmlNode, long index)
{
	m_curSelXmlNode = xmlNode;
	m_CurSelCtrlIndex = index;
	//m_CurSelCtrl = pWnd;

	//m_pContainer->Invalidate();

	m_treeXmlStruct->GetEventSet()->unsubscribeEvent(EVT_TC_SELCHANGED, Subscriber(&SDesignerView::OnTCSelChanged, this));
	GoToXmlStructItem(index, m_treeXmlStruct->GetRootItem());
	m_treeXmlStruct->GetEventSet()->subscribeEvent(EVT_TC_SELCHANGED, Subscriber(&SDesignerView::OnTCSelChanged, this));
}
/*

void SDesignerView::UpdatePosToXmlNode(SUIWindow *pRealWnd, SMoveWnd* pMoveWnd)
{
	if (m_CurSelCtrl == m_pMoveWndRoot)
	{
		SouiLayoutParam *pSouiLayoutParam = pRealWnd->GetLayoutParamT<SouiLayoutParam>();

		CRect r;
		pMoveWnd->GetWindowRect(r);
		m_CurrentLayoutNode.attribute(_T("height")).set_value((int)pSouiLayoutParam->GetSpecifiedSize(Vert).fSize - MARGIN * 2);
		m_CurrentLayoutNode.attribute(_T("width")).set_value((int)pSouiLayoutParam->GetSpecifiedSize(Horz).fSize - MARGIN * 2);

		return;
	}

	SStringT s;
	s.Format(_T("%d"), pRealWnd->GetUserData());
	pugi::xml_node xmlNode = FindNodeByAttr(m_CurrentLayoutNode, L"data", s);
	if (!xmlNode)
	{
		return;
	}

	pugi::xml_attribute attrPos, attrSize, attrOffset, attrPos2type, attrWidth, attrHeight;

	SStringW strTemp;
	attrPos = xmlNode.attribute(L"pos");
	attrSize = xmlNode.attribute(L"size");
	attrOffset = xmlNode.attribute(L"offset");
	attrPos2type = xmlNode.attribute(L"pos2type");
	attrWidth = xmlNode.attribute(L"width");
	attrHeight = xmlNode.attribute(L"height");

	if (pRealWnd->GetLayoutParam()->IsClass(SouiLayoutParam::GetClassName()))
	{
		SouiLayoutParam *pSouiLayoutParam = pRealWnd->GetLayoutParamT<SouiLayoutParam>();
		SouiLayoutParamStruct *pSouiLayoutParamStruct = (SouiLayoutParamStruct*)pSouiLayoutParam->GetRawData();

		if (attrSize)
		{
			strTemp.Format(_T("%d%s, %d%s"),
				(int)pSouiLayoutParam->GetSpecifiedSize(Horz).fSize,
				UnitToStr(pSouiLayoutParam->GetSpecifiedSize(Horz).unit),
				(int)pSouiLayoutParam->GetSpecifiedSize(Vert).fSize,
				UnitToStr(pSouiLayoutParam->GetSpecifiedSize(Vert).unit));

			attrSize.set_value(strTemp);
		}

		if (attrPos2type)
		{
			//删除Pos2Type,改成attrOffset
			xmlNode.remove_attribute(L"pos2type");
			if (!attrOffset)
			{
				xmlNode.append_attribute(L"offset");
				attrOffset = xmlNode.attribute(L"offset");
			}
		}

		if (attrOffset)
		{
			strTemp.Format(_T("%g, %g"), pSouiLayoutParamStruct->fOffsetX, pSouiLayoutParamStruct->fOffsetY);
			attrOffset.set_value(strTemp);
		}

		if (attrPos)
		{
			if (pSouiLayoutParamStruct->nCount == 2)
			{
				strTemp = GetPosFromLayout(pSouiLayoutParam, 0) + _T(",");
				strTemp = strTemp + GetPosFromLayout(pSouiLayoutParam, 1);
				attrPos.set_value(strTemp);
			}
			else if (pSouiLayoutParamStruct->nCount == 4)
			{
				strTemp = GetPosFromLayout(pSouiLayoutParam, 0) + _T(",");
				strTemp = strTemp + GetPosFromLayout(pSouiLayoutParam, 1) + _T(",");
				strTemp = strTemp + GetPosFromLayout(pSouiLayoutParam, 2) + _T(",");
				strTemp = strTemp + GetPosFromLayout(pSouiLayoutParam, 3);
				attrPos.set_value(strTemp);
			}
		}

	}
	else
	{
		SLinearLayoutParam *pSLinearLayoutParam = pRealWnd->GetLayoutParamT<SLinearLayoutParam>();
		if (pSLinearLayoutParam)
		{
			if (attrSize)
			{
				strTemp.Format(_T("%d%s, %d%s"),
					(int)pSLinearLayoutParam->GetSpecifiedSize(Horz).fSize,
					UnitToStr(pSLinearLayoutParam->GetSpecifiedSize(Horz).unit),
					(int)pSLinearLayoutParam->GetSpecifiedSize(Vert).fSize,
					UnitToStr(pSLinearLayoutParam->GetSpecifiedSize(Vert).unit));
				attrSize.set_value(strTemp);
			}

			if (attrWidth)
			{
				strTemp.Format(_T("%d%s"),
					(int)pSLinearLayoutParam->GetSpecifiedSize(Horz).fSize,
					UnitToStr(pSLinearLayoutParam->GetSpecifiedSize(Horz).unit));
				attrWidth.set_value(strTemp);
			}
			if (attrHeight)
			{
				strTemp.Format(_T("%d"),
					(int)pSLinearLayoutParam->GetSpecifiedSize(Vert).fSize,
					UnitToStr(pSLinearLayoutParam->GetSpecifiedSize(Vert).unit));
				attrHeight.set_value(strTemp);
			}
		}	
	}

	SetCurrentCtrl(xmlNode, 0);
}*/

SStringW SDesignerView::GetPosFromLayout(SouiLayoutParam *pLayoutParam, INT nPosIndex)
{
	SouiLayoutParamStruct *pSouiLayoutParamStruct = (SouiLayoutParamStruct*)pLayoutParam->GetRawData();

	POS_INFO Pi;

	switch (nPosIndex)
	{
	case 0:
		Pi = pSouiLayoutParamStruct->posLeft;
		break;
	case 1:
		Pi = pSouiLayoutParamStruct->posTop;
		break;
	case 2:
		Pi = pSouiLayoutParamStruct->posRight;
		break;
	case 3:
	default:
		Pi = pSouiLayoutParamStruct->posBottom;
	}

	SStringW strPos;
	switch (Pi.pit)
	{
	case PIT_NULL:
		strPos = L"";        //无效定义
		break;
	case PIT_NORMAL:        //锚点坐标
		strPos = L"";
		break;
	case PIT_CENTER:        //参考父窗口中心点,以"|"开始
		strPos = L"|";
		break;
	case PIT_PERCENT:       //指定在父窗口坐标的中的百分比,以"%"开始
		strPos = L"%";
		break;
	case PIT_PREV_NEAR:     //参考前一个兄弟窗口与自己近的边,以"["开始
		strPos = L"[";
		break;
	case PIT_NEXT_NEAR:     //参考下一个兄弟窗口与自己近的边,以"]"开始
		strPos = L"]";
		break;
	case PIT_PREV_FAR:     //参考前一个兄弟窗口与自己远的边,以"{"开始
		strPos = L"{";
		break;
	case PIT_NEXT_FAR:      //参考下一个兄弟窗口与自己远的边,以"}"开始
		strPos = L"}";
		break;
	case PIT_SIZE:          //指定窗口的宽或者高,以"@"开始
		strPos = L"@";
		break;
	case PIT_SIB_LEFT:      //兄弟结点的left,用于X
		if (0 == nPosIndex)
		{
			strPos = strPos.Format(L"sib.left@%d:", Pi.nRefID);
		}
		else
		{
			strPos = strPos.Format(L"sib.top@%d:", Pi.nRefID);
		}
		break;

		//case PIT_SIB_TOP:      //兄弟结点的top，与left相同，用于Y
		//	break;

	case PIT_SIB_RIGHT:      //兄弟结点的right,用于X 
		if (2 == nPosIndex)
		{
			strPos = strPos.Format(L"sib.right@%d:", Pi.nRefID);
		}
		else
		{
			strPos = strPos.Format(L"sib.bottom@%d:", Pi.nRefID);
		}
		break;

		//case PIT_SIB_BOTTOM:      //兄弟结点的bottom,与right相同,用于Y 
		//	break;

	default:
		break;
	}

	if (Pi.cMinus == -1)
	{
		strPos = strPos + L"-";
	}
	SStringW strTemp;
	int n = (int)Pi.nPos.fSize;
	strTemp.Format(L"%d%s", n, UnitToStr(Pi.nPos.unit));
	strPos = strPos + strTemp;
	return strPos;
}

void SDesignerView::BindXmlcodeWnd(SWindow *pXmlTextCtrl)
{
	m_pScintillaWnd = (CScintillaWnd*)pXmlTextCtrl->GetUserData();
	if (m_pScintillaWnd)
	{
		m_pScintillaWnd->SetSaveCallback((SCIWND_FN_CALLBACK)&CMainDlg::OnScintillaSave);

		ShowNoteInSciwnd();
	}
}

void SDesignerView::ShowNoteInSciwnd()
{
	SStringW strDebug = L"\r\n\r\n\r\n\t\t\t修改代码后按Ctrl+S可在窗体看到变化";

	SStringA str = S_CW2A(strDebug, CP_UTF8);
	m_pScintillaWnd->SendMessage(SCI_ADDTEXT, str.GetLength(),
		reinterpret_cast<LPARAM>((LPCSTR)str));
	m_pScintillaWnd->SetDirty(false);
	m_pScintillaWnd->SendMessage(SCI_SETREADONLY, 1, 0);
}


void SDesignerView::InitProperty(SStatic* textCtrl, SWindow *pPropertyContainer)   //初始化属性列表
{
	m_textCtrlTypename = textCtrl;
	m_pPropertyContainer = pPropertyContainer;
	/*

	<通用样式>
		<id style="proptext" name ="窗口ID(id)" value="" />
		<name style="proptext" name ="窗口名称(name)" value="" />
		<skin style="proptext" name ="皮肤(skin)" value="" />
	</通用样式>

	<Button>
		<分组 name="基本">
		<id/>
		<name/>
		<skin/>
		<pos/>
		<size/>
		<offset/>
		</分组>

		<分组 name="拓展">
		<accel style="proptext" name ="快捷键(accel)" value="ctrl+alt+f9" />
		<animate style="propoption" name ="动画(animate)" value="0" options="无(0)|有(1)"/>
		</分组>

	</Button>
	*/

	SStringW s = L"<propgrid name=\"NAME_UIDESIGNER_PROPGRID_MAIN\" pos=\"0, 0, -4, -4\" sbSkin=\"skin_bb_scrollbar\" switchSkin=\"skin_prop_switch\"                \
		nameWidth=\"150\" orderType=\"group\" itemHeight=\"26\" ColorGroup=\"RGB(96,112,138)\"  ColorBorder=\"#FFFFFF50\"                                        \
		ColorItemSel=\"rgb(234,128,16)\" colorItemSelText=\"#FF0000\" EditBkgndColor=\"rgb(87,104,132)\" class=\"ue_cls_text\"     \
		autoWordSel=\"1\"> <cmdbtnstyle skin=\"_skin.sys.btn.normal\" colorText=\"RGB(96,112,138)\">...</cmdbtnstyle> </propgrid>";

	pugi::xml_document xmlDocProperty;
	xmlDocProperty.append_copy(g_SysDataMgr.m_xmlDocProperty.document_element());
	pugi::xml_node NodeCom = xmlDocProperty.child(L"root").child(L"通用样式");
	pugi::xml_node NodeCtrlList = xmlDocProperty.child(L"root").child(L"属性列表");

	//hostwnd节点处理
	pugi::xml_node NodeCtrl = NodeCtrlList.child(_T("hostwnd")).first_child();
	m_lstSouiProperty.RemoveAll();
	pugi::xml_node NodeCtrlChild = NodeCtrl.first_child();
	while (NodeCtrlChild)
	{
		m_lstSouiProperty.AddTail(NodeCtrlChild.name());
		NodeCtrlChild = NodeCtrlChild.next_sibling();
	}

	NodeCtrl = NodeCtrl.next_sibling();
	m_lstRootProperty.RemoveAll();
	NodeCtrlChild = NodeCtrl.first_child();
	while (NodeCtrlChild)
	{
		m_lstRootProperty.AddTail(NodeCtrlChild.name());
		NodeCtrlChild = NodeCtrlChild.next_sibling();
	}

	NodeCtrl = NodeCtrlList.first_child();  //NodeCtrl = Button节点
	while (NodeCtrl)
	{
		InitCtrlProperty(NodeCom, NodeCtrl);

		SStringT strName = NodeCtrl.name();
		NodeCtrl.set_name(L"groups");

		pugi::xml_document *doc = new pugi::xml_document();

		if (!doc->load_buffer(s, wcslen(s) * sizeof(wchar_t), pugi::parse_default, pugi::encoding_utf16))
		{
			Debug(_T("InitProperty失败1"));
		}

		doc->child(L"propgrid").append_copy(NodeCtrl);

		m_mapCtrlProperty[strName.MakeLower()] = doc;

		NodeCtrl = NodeCtrl.next_sibling();
	}
}


void SDesignerView::InitCtrlProperty(pugi::xml_node NodeCom, pugi::xml_node NodeCtrl)
{
	/*
	<通用样式>
		<id style="proptext" name ="窗口ID(id)" value="" />
		<name style="proptext" name ="窗口名称(name)" value="" />
		<skin style="proptext" name ="皮肤(skin)" value="" />
	</通用样式>

	<Button>
		<分组 name="基本">
			<id/>
			<name/>
			<skin/>
			<pos/>
			<size/>
			<offset/>
		</分组>

		<分组 name="拓展">
			<accel style="proptext" name ="快捷键(accel)" value="ctrl+alt+f9" />
			<animate style="propoption" name ="动画(animate)" value="0" options="无(0)|有(1)"/>
		</分组>

	</Button>

	<propgroup name="group1" description="desc of group1">
	<proptext name="text1.1" value="value 1.1">

	*/


	pugi::xml_node NodeChild = NodeCtrl.first_child();

	while (NodeChild)
	{
		if (_wcsicmp(NodeChild.name(), L"分组") == 0)
		{
			SStringT nameAttr = NodeChild.attribute(L"name").as_string();
			if (nameAttr.CompareNoCase(L"基本样式") == 0)
			{
				pugi::xml_document NodeComStyle; 
				NodeComStyle.append_copy(g_SysDataMgr.m_xmlDocProperty.child(L"root").child(nameAttr));
				pugi::xml_node parentNode = NodeChild.parent();
				pugi::xml_node nodeCopy = parentNode.insert_copy_after(NodeComStyle.document_element(), NodeChild);
				parentNode.remove_child(NodeChild);
				NodeChild = nodeCopy;
			}
			else if (nameAttr.CompareNoCase(L"ColorMask") == 0)
			{
				pugi::xml_document NodeComStyle;
				NodeComStyle.append_copy(g_SysDataMgr.m_xmlDocProperty.child(L"root").child(nameAttr));
				pugi::xml_node parentNode = NodeChild.parent();
				pugi::xml_node nodeCopy = parentNode.insert_copy_after(NodeComStyle.document_element(), NodeChild);
				parentNode.remove_child(NodeChild);
				NodeChild = nodeCopy;
			}
			NodeChild.set_name(L"propgroup");
			InitCtrlProperty(NodeCom, NodeChild);
		}
		else
		{
			if (!NodeChild.attribute(L"style"))
			{
				SStringT strName = NodeChild.name();
				pugi::xml_node N = NodeCom.child(strName);
				if (N)
				{	// 用通用属性进行替换
					pugi::xml_node NodeNew;
					NodeNew = NodeChild.parent().insert_copy_before(N, NodeChild);
					NodeChild.parent().remove_child(NodeChild);
					NodeChild = NodeNew;
				}
				else
				{
					NodeChild.append_attribute(L"style").set_value(L"proptext");
					NodeChild.append_attribute(L"name").set_value(strName);
				}
			}
			NodeChild.append_attribute(L"name2").set_value(NodeChild.name());

			NodeChild.set_name(NodeChild.attribute(L"style").value());
			NodeChild.remove_attribute(L"style");
		}

		NodeChild = NodeChild.next_sibling();
	}

}

void SDesignerView::CreatePropGrid(SStringT strCtrlType)
{
	if (m_curPropertyXmlNode == m_curSelXmlNode)
		return;
	
	m_curPropertyXmlNode = m_curSelXmlNode;
	m_pPropgrid = (SPropertyGrid *)m_pPropertyContainer->GetWindow(GSW_FIRSTCHILD);
	if (m_pPropgrid)
	{
		//这是一个坑啊，要先这样才不报错，因为点按钮的时候，PropGrid并没有失去焦点，
		//没有执行Killfocus操作销毁Edit,在DestroyChild以后PropGrid已经销毁了，这时候在执行PropGrid中edit的killfocus会报错		
		m_pPropgrid->GetEventSet()->unsubscribeEvent(EventPropGridValueChanged::EventID, Subscriber(&SDesignerView::OnPropGridValueChanged, this));
		m_pPropgrid->SetFocus();

		m_pPropertyContainer->DestroyChild(m_pPropgrid);
		m_pPropgrid = NULL;
	}

	SStringT strTemp;
	strTemp = m_CurrentLayoutNode.name();

	if (strCtrlType.CompareNoCase(_T("hostwnd")) == 0 && strTemp.CompareNoCase(_T("SOUI")) != 0)
	{   //include 文件
		strCtrlType = _T("include");
		//return;
	}

	m_textCtrlTypename->SetWindowText(_T("控件类型: ") + strCtrlType);
	SMap<SStringT, pugi::xml_document*>::CPair *p = m_mapCtrlProperty.Lookup(strCtrlType.MakeLower());
	if (!p)
		p = m_mapCtrlProperty.Lookup(_T("window"));

	if (p)
	{
		pugi::xml_document *tempDoc = p->m_value;

		m_pPropertyContainer->CreateChildren(tempDoc->root());

		m_pPropgrid = (SPropertyGrid *)m_pPropertyContainer->GetWindow(GSW_FIRSTCHILD);
		m_pPropertyContainer->Invalidate();

		m_pPropgrid->GetEventSet()->subscribeEvent(EventPropGridValueChanged::EventID, Subscriber(&SDesignerView::OnPropGridValueChanged, this));
		m_pPropgrid->GetEventSet()->subscribeEvent(EventPropGridItemClick::EventID, Subscriber(&SDesignerView::OnPropGridItemClick, this));
		m_pPropgrid->GetEventSet()->subscribeEvent(EventPropGridItemActive::EventID, Subscriber(&SDesignerView::OnPropGridItemActive, this));
	}

	m_strCurrentCtrlType = strCtrlType;

	((CMainDlg*)m_pMainHost)->m_edtDesc->SetWindowText(_T(""));
}

void SDesignerView::UpdatePropGrid(pugi::xml_node xmlNode)
{
	if (m_pPropgrid == NULL)
	{
		return;
	}

	m_pPropgrid->ClearAllGridItemValue();

	if (xmlNode == m_CurrentLayoutNode && _wcsicmp(xmlNode.name(), _T("SOUI")) == 0)
	{
		pugi::xml_attribute xmlAttr = xmlNode.first_attribute();

		while (xmlAttr)
		{
			SStringT str = xmlAttr.name();
			IPropertyItem *pItem = m_pPropgrid->GetGridItem(str.MakeLower());
			if (pItem)
			{
				if (str.CompareNoCase(_T("data")) == 0)
				{
					pItem->SetStringOnly(xmlNode.attribute(L"uidesiner_data").value());
				}
				else
				{
					SStringT s = xmlAttr.value();
					pItem->SetStringOnly(xmlAttr.value());
				}
			}

			xmlAttr = xmlAttr.next_attribute();
		}

		xmlAttr = xmlNode.child(_T("root")).first_attribute();
		while (xmlAttr)
		{
			SStringT str = xmlAttr.name();
			IPropertyItem *pItem = m_pPropgrid->GetGridItem(str.MakeLower());
			if (pItem)
			{
				if (str.CompareNoCase(_T("data")) == 0)
				{
					pItem->SetStringOnly(xmlNode.attribute(L"uidesiner_data").value());
				}
				else
				{
					pItem->SetStringOnly(xmlAttr.value());
				}
			}
			xmlAttr = xmlAttr.next_attribute();
		}
	}
	else
	{
		pugi::xml_attribute xmlAttr = xmlNode.first_attribute();

		IPropertyItem *pItem = m_pPropgrid->GetGridItem(uiedit_SpecAttr);
		if (pItem)
		{
			SStringT strTemp = xmlNode.text().get();
			strTemp.TrimBlank();
			pItem->SetStringOnly(strTemp);
		};

		while (xmlAttr)
		{
			SStringT str = xmlAttr.name();
			IPropertyItem *pItem = m_pPropgrid->GetGridItem(str.MakeLower());
			if (pItem)
			{
				if (str.CompareNoCase(_T("data")) == 0)
				{
					pItem->SetStringOnly(xmlNode.attribute(L"uidesiner_data").value());
				}
				else
				{
					pItem->SetStringOnly(xmlAttr.value());
				}
			}

			xmlAttr = xmlAttr.next_attribute();
		}
	}

	m_pPropgrid->Invalidate();
}

// 通过属性窗口调整窗口
bool SDesignerView::OnPropGridValueChanged(EventArgs *pEvt)
{
	pugi::xml_node xmlNode;
	BOOL bRoot = FALSE;

	IPropertyItem* pItem = ((EventPropGridValueChanged*)pEvt)->pItem;
	SStringT attr_name = pItem->GetName2();  //属性名：pos skin name id 等等
	SStringT attr_value = pItem->GetString();   //属性的值

	if (attr_name.IsEmpty())
	{
		return false;
	}

	//如果当前选择的是布局根窗口，需要特殊处理
	//if (m_CurSelCtrl == m_pMoveWndRoot)
	if (m_CurSelCtrlIndex == 0)
	{
		SPOSITION pos = m_lstRootProperty.GetHeadPosition();
		while (pos)
		{
			SStringT strTemp = m_lstRootProperty.GetNext(pos);
			if (strTemp.CompareNoCase(attr_name) == 0)
			{
				xmlNode = m_curSelXmlNode.child(_T("root"));
				bRoot = TRUE;
				break;
			}
		}

		if (!bRoot)
		{
			SPOSITION pos = m_lstSouiProperty.GetHeadPosition();
			while (pos)
			{
				SStringT strTemp = m_lstSouiProperty.GetNext(pos);
				if (strTemp.CompareNoCase(attr_name) == 0)
				{
					xmlNode = m_curSelXmlNode;
					bRoot = TRUE;
					break;
				}
			}
		}
	}
	else
	{
		xmlNode = m_curSelXmlNode;
	}

	if (attr_name.CompareNoCase(uiedit_SpecAttr) == 0)
	{
		xmlNode.text().set(attr_value);
	}

	//SWindow * pWnd = ((SMoveWnd*)m_CurSelCtrl)->m_pRealWnd->GetParent();
	pugi::xml_attribute attr = xmlNode.attribute(attr_name);
	if (attr)
	{
		if (attr_value.IsEmpty())
		{
			if (attr_name.CompareNoCase(_T("data")) == 0)
			{
				xmlNode.attribute(_T("uidesiner_data")).set_value(_T(""));
			}
			else
			{
				xmlNode.remove_attribute(attr_name);
			}
		}
		else
		{
			if (attr_name.CompareNoCase(_T("data")) == 0)
			{
				xmlNode.attribute(_T("uidesiner_data")).set_value(attr_value);
			}
			else
			{
				attr.set_value(attr_value);
			}
		}
	}
	else
	{
		if ((!attr_value.IsEmpty()) && (attr_name.CompareNoCase(uiedit_SpecAttr) != 0))
		{
			xmlNode.append_attribute(attr_name).set_value(attr_value);
		}
	}

	// 先记下原来选的控件是第几个顺序的控件, 再进行重布局
	int data = m_CurSelCtrlIndex;
	ReloadLayout(FALSE);
	SelectCtrlByIndex(data, false);

	GetCodeFromEditor(NULL);
	return true;
}

void SDesignerView::RefreshRes()
{
	m_xmlDocUiRes.load_file(m_strUIResFile, pugi::parse_full);

	CAutoRefPtr<IResProvider> pResProvider;
	TCHAR *s = m_strProPath.GetBuffer(m_strProPath.GetLength());

	//IResProvider* pResProvider = SApplication::getSingletonPtr()->GetMatchResProvider(_T("UIDEF"), _T("XML_INIT"));

	/*SApplication::getSingletonPtr()->RemoveResProvider(m_pWsResProvider);

	CreateResProvider(RES_FILE, (IObjRef**)&pResProvider);
	if (!pResProvider->Init((LPARAM)s, 0))
	{
		Debug(_T("ResProvider初始化失败"));
		return;
	}
	m_pWsResProvider = pResProvider;
	SApplication::getSingletonPtr()->AddResProvider(pResProvider, NULL);*/

	SStringT strXMLInit;
	pugi::xml_node xmlNode = m_xmlDocUiRes.child(_T("resource")).child(_T("UIDEF")).child(_T("file"));

	if (xmlNode)
	{
		strXMLInit = xmlNode.attribute(_T("name")).as_string();
	}

	if (strXMLInit.IsEmpty())
	{
		strXMLInit = _T("xml_init");
	}

	//将皮肤中的uidef保存起来.
	//m_pUiDef.Attach(SUiDef::getSingleton().CreateUiDefInfo(pResProvider, _T("uidef:") + strXMLInit));
}

bool SDesignerView::OnPropGridItemClick(EventArgs *pEvt)
{
	EventPropGridItemClick *pEvent = (EventPropGridItemClick*)pEvt;
	IPropertyItem* pItem = pEvent->pItem;
	SStringT strType = pEvent->strType;

	if (strType.CompareNoCase(_T("skin")) == 0)
	{
		SDlgSkinSelect DlgSkin(_T("layout:UIDESIGNER_XML_SKIN_SELECT"), pItem->GetString(), m_strUIResFile);
		DlgSkin.m_pResFileManger = &((CMainDlg*)m_pMainHost)->m_UIResFileMgr;
		if (DlgSkin.DoModal(m_pMainHost->m_hWnd) == IDOK)
		{
			SStringT s1 = pItem->GetString();   //属性的值

			if (s1.CompareNoCase(DlgSkin.m_strSkinName) != 0)
			{
				((CMainDlg*)m_pMainHost)->ReloadWorkspaceUIRes();
				RefreshRes();
				pItem->SetString(DlgSkin.m_strSkinName);
				m_pPropgrid->Invalidate();
				//ReLoadLayout();
			}
		}
		//调用皮肤对话框
	}
	else if (strType.CompareNoCase(_T("font")) == 0)
	{
		//调用字体对话框
		SDlgFontSelect DlgFont(pItem->GetString(), this);
		if (DlgFont.DoModal(m_pMainHost->m_hWnd) == IDOK)
		{
			pItem->SetString(DlgFont.m_strFont);
			m_pPropgrid->Invalidate();
		}
	}
	else if (strType.CompareNoCase(_T("class")) == 0)
	{

	}

	return true;
}

BOOL SDesignerView::bIsContainerCtrl(SStringT strCtrlName) //判断控件是否是容器控件
{
	SStringT s;
	SPOSITION pos = m_lstContainerCtrl.GetHeadPosition();
	while (pos)
	{
		s = m_lstContainerCtrl.GetNext(pos);
		if (s.CompareNoCase(strCtrlName) == 0)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void SDesignerView::SaveEditorCaretPos()
{
	m_nSciCaretPos = m_pScintillaWnd->SendMessage(SCI_GETCURRENTPOS);
}

void SDesignerView::RestoreEditorCaretPos()
{
	m_pScintillaWnd->SendMessage(SCI_GOTOPOS, m_nSciCaretPos);
	m_pScintillaWnd->GotoFoundLine();
}

void SDesignerView::LocateControlXML()
{
	CScintillaWnd* pSciWnd = m_pScintillaWnd;
	m_strCurFileEditor = m_strCurLayoutXmlFile;

	pugi::xml_document doc, doc_shadow;

	if (m_curSelXmlNode != m_CurrentLayoutNode)
	{
		SStringT tmpName = m_curSelXmlNode.name();
		m_curSelXmlNode.set_name(L"use_forfind");
		doc_shadow.append_copy(m_CurrentLayoutNode);
		m_curSelXmlNode.set_name(tmpName);

		RemoveWndName(doc_shadow.root(), FALSE);
		TrimXmlNodeTextBlank(doc_shadow.document_element());

		SStringT writer_buf;
		myxml_writer_stream writer(writer_buf);
		doc_shadow.document_element().print(writer, L"\t", pugi::format_default, pugi::encoding_utf16);

		SStringA str;
		str = S_CW2A(writer_buf, CP_UTF8);
		int pos = str.Find("use_forfind");
		if (pos != -1)
			m_nSciCaretPos = pos;
		else
			return;
	}

	doc.append_copy(m_CurrentLayoutNode);

	RemoveWndName(doc.root(), FALSE);
	//Debug(doc.root());
	TrimXmlNodeTextBlank(doc.document_element());

	SStringT writer_buf;
	myxml_writer_stream writer(writer_buf);
	doc.document_element().print(writer, L"\t", pugi::format_default, pugi::encoding_utf16);

	SStringA str;
	str = S_CW2A(writer_buf, CP_UTF8);
	pSciWnd->SendMessage(SCI_SETUNDOCOLLECTION, 0, 0);

	pSciWnd->SendMessage(SCI_SETREADONLY, 0, 0);
	pSciWnd->SendMessage(SCI_CLEARALL, 0, 0);
	pSciWnd->SendMessage(SCI_ADDTEXT, str.GetLength(),
		reinterpret_cast<LPARAM>((LPCSTR)str));
	pSciWnd->SetDirty(false);

	pSciWnd->SendMessage(SCI_SETUNDOCOLLECTION, 1, 0);
	//pSciWnd->SetFocus();		//在响应属性修改时, 这将引发异常
	RestoreEditorCaretPos();
}

void SDesignerView::AddCodeToEditor(CScintillaWnd* _pSciWnd)  //复制xml代码到代码编辑器
{
#ifndef ONLYSHOWSELXML
	LocateControlXML();
#else
	CScintillaWnd* pSciWnd = m_pScintillaWnd;
	if (_pSciWnd)
		pSciWnd = _pSciWnd;

	m_strCurFileEditor = m_strCurLayoutXmlFile;

	pugi::xml_document doc;
	SStringT strInclude(_T("include"));

	if (m_curSelXmlNode == m_CurrentLayoutNode && strInclude.CompareNoCase(m_curSelXmlNode.name()) == 0)
	{
		doc.append_copy(m_curSelXmlNode);
	}
	else
		if (m_curSelXmlNode == m_CurrentLayoutNode)
		{
			doc.append_copy(m_curSelXmlNode.child(_T("root")));
		}
		else
		{
			doc.append_copy(m_curSelXmlNode);
		}


	RemoveWndName(doc.root(), FALSE);
	//Debug(doc.root());
	TrimXmlNodeTextBlank(doc.document_element());

	SStringT writer_buf;
	myxml_writer_stream writer(writer_buf);
	doc.document_element().print(writer, L"\t", pugi::format_default, pugi::encoding_utf16);

	SStringA str;
	str = S_CW2A(writer_buf, CP_UTF8);
	pSciWnd->SendMessage(SCI_SETREADONLY, 0, 0);
	pSciWnd->SendMessage(SCI_CLEARALL, 0, 0);
	pSciWnd->SendMessage(SCI_ADDTEXT, str.GetLength(),
		reinterpret_cast<LPARAM>((LPCSTR)str));
	pSciWnd->SetDirty(false);
	//pSciWnd->SetFocus();		//在响应属性修改时, 这将引发异常
	RestoreEditorCaretPos();
#endif // e
}

// 把代码编辑器修改的结果重新加载, 更新布局窗口
void SDesignerView::GetCodeFromEditor()
{
	if (m_strCurLayoutXmlFile.IsEmpty())
	{
		return;
	}

	if (m_strCurFileEditor.CompareNoCase(m_strCurLayoutXmlFile) != 0)
	{
		return;
	}

	CScintillaWnd* pSciWnd = m_pScintillaWnd;

	int n = pSciWnd->SendMessage(SCI_GETTEXT, 0, 0);
	if (n == 0)
	{
		return;
	}

	char *chText = new char[n];

	pSciWnd->SendMessage(SCI_GETTEXT, n, (LPARAM)chText);

	SStringA s(chText);
	SStringW s1 = S_CA2W(s, CP_UTF8);
	delete chText;

	pugi::xml_document doc;
	if (!doc.load_buffer(s1, wcslen(s1) * sizeof(wchar_t), pugi::parse_full, pugi::encoding_utf16))
	{
		g_pMainDlg->SendMessage(WM_MSG_SHOWBOX, (WPARAM)_T("XML有语法错误！"), (LPARAM)_T(""));
		return;
	}

	g_pMainDlg->SendMsgToViewer(kcds_id, (void*)s.c_str(), s.GetLength());
	HSTREEITEM rootitem = m_treeXmlStruct->GetRootItem();
	if (rootitem && m_CurSelCtrlItem)
	{
		SStringA strTag = "0,";
		GetTC_CtrlNodeTag(rootitem, m_CurSelCtrlItem, strTag);
		g_pMainDlg->SendMsgToViewer(selctrl_id, (void*)strTag.c_str(), strTag.GetLength());
	}

	SaveEditorCaretPos();

	RenameChildeWnd(doc.root());
	TrimXmlNodeTextBlank(doc.document_element());

	SMap<SStringT, pugi::xml_document*>::CPair *p = m_mapLayoutFile.Lookup(m_strCurLayoutXmlFile);
	p->m_value->reset();
	p->m_value->append_copy(doc.document_element());
	pugi::xml_node xmlroot = p->m_value->document_element();

	m_CurrentLayoutNode = xmlroot;

	// 先记下原来选的控件是第几个顺序的控件, 再进行重布局
	int data = m_CurSelCtrlIndex;
	ReloadLayout();
	SelectCtrlByIndex(data);
}

// 把代码编辑器修改的结果重新加载, 更新布局窗口
void SDesignerView::GetCodeFromEditor(CScintillaWnd* _pSciWnd)//从代码编辑器获取xml代码
{
#ifndef ONLYSHOWSELXML
	GetCodeFromEditor();
#else
	if (m_strCurLayoutXmlFile.IsEmpty())
	{
		return;
	}

	if (m_strCurFileEditor.CompareNoCase(m_strCurLayoutXmlFile) != 0)
	{
		return;
	}

	CScintillaWnd* pSciWnd = m_pScintillaWnd;
	if (_pSciWnd)
		pSciWnd = _pSciWnd;

	int n = pSciWnd->SendMessage(SCI_GETTEXT, 0, 0);
	if (n == 0)
	{
		return;
	}

	char *chText = new char[n];

	pSciWnd->SendMessage(SCI_GETTEXT, n, (LPARAM)chText);

	SStringA s(chText);
	SStringW s1 = S_CA2W(s, CP_UTF8);
	delete chText;

	pugi::xml_document doc;
	if (!doc.load_buffer(s1, wcslen(s1) * sizeof(wchar_t), pugi::parse_full, pugi::encoding_utf16))
	{
		Debug(_T("XML有语法错误！"));
		return;
	}

	SaveEditorCaretPos();

	RenameChildeWnd(doc.root());
	TrimXmlNodeTextBlank(doc.document_element());

	BOOL bRoot = FALSE;

	SStringT strInclude(_T("include"));

	if (m_curSelXmlNode == m_CurrentLayoutNode && strInclude.CompareNoCase(m_curSelXmlNode.name()) == 0)
	{
		pugi::xml_node xmlPNode;
		xmlPNode = m_curSelXmlNode.parent();
		pugi::xml_node xmlNode = xmlPNode.insert_copy_after(doc.root().first_child(), m_curSelXmlNode);

		xmlPNode.remove_child(m_curSelXmlNode);
		m_curSelXmlNode = xmlNode;

		m_CurrentLayoutNode = m_curSelXmlNode;
	}
	else
		if (m_curSelXmlNode == m_CurrentLayoutNode && strInclude.CompareNoCase(m_curSelXmlNode.name()) != 0)
		{
			pugi::xml_node fristNode = m_curSelXmlNode.child(_T("root"));
			pugi::xml_node xmlNode = m_curSelXmlNode.insert_copy_after(doc.root().first_child(), fristNode);
			m_curSelXmlNode.remove_child(fristNode);
			bRoot = TRUE;
		}
		else
		{
			pugi::xml_node xmlPNode;
			xmlPNode = m_curSelXmlNode.parent();
			pugi::xml_node xmlNode = xmlPNode.insert_copy_after(doc.root().first_child(), m_curSelXmlNode);

			xmlPNode.remove_child(m_curSelXmlNode);
			m_curSelXmlNode = xmlNode;
		}


	// 	ReLoadLayout(FALSE);
	// 	m_pMoveWndRoot->Click(0, CPoint(0, 0));

	// 先记下原来选的控件是第几个顺序的控件, 再进行重布局
	int data = GetWindowUserData(m_CurSelCtrl);
	ReLoadLayout();

	if (bRoot)
	{
		m_CurSelCtrl = m_pMoveWndRoot;
	}
	else
	{
		SMoveWnd *pMoveWnd = (SMoveWnd*)FindChildByUserData(m_pMoveWndRoot, data);

		if (pMoveWnd)
		{
			m_CurSelCtrl = pMoveWnd;
			AddCodeToEditor(NULL);
		}
		else
		{
			m_pMoveWndRoot->Click(0, CPoint(0, 0));
		}
	}
#endif
}

void SDesignerView::SetSelCtrlNode(pugi::xml_node xmlNode)
{
	m_nState = 1;

	SStringW writer_buf;
	myxml_writer_stream writer(writer_buf);
	xmlNode.print(writer, L"\t", pugi::format_default, pugi::encoding_utf16);

	if (m_xmlSelCtrlDoc.load_buffer(writer_buf, wcslen(writer_buf) * sizeof(wchar_t), pugi::parse_default, pugi::encoding_utf16))
	{
		m_xmlSelCtrlNode = m_xmlSelCtrlDoc.first_child();
	}
	else
	{
		Debug(_T("选择控件异常"));
	}

	return;
}

int SDesignerView::InitXMLStruct(pugi::xml_node xmlNode, HSTREEITEM item)
{
	if (!xmlNode)
	{
		return 0;
	}
	int count = 0;

	pugi::xml_node NodeSib = xmlNode;
	while (NodeSib)
	{
		if (NodeSib.type() != pugi::node_element)
		{
			NodeSib = NodeSib.next_sibling();
			continue;
		}

		int data = 0;
		if (NodeSib.attribute(_T("data")))
		{
			data = NodeSib.attribute(_T("data")).as_int();
		}

		SStringT strNodeName = NodeSib.name();
		if (strNodeName.IsEmpty())
		{
			NodeSib = NodeSib.next_sibling();
			continue;
		}

		count++;

		HSTREEITEM itemChild = m_treeXmlStruct->InsertItem(strNodeName, item);
		if(strNodeName.CompareNoCase(_T("root"))==0)
		{
			m_rootItem = itemChild;//save root item.
		}
		m_treeXmlStruct->SetItemData(itemChild, data);
		
		count += InitXMLStruct(NodeSib.first_child(), itemChild);
		NodeSib = NodeSib.next_sibling();
	}
	if (item == STVI_ROOT)
	{
		SStringT tmpstr;
		tmpstr.Format(_T("窗口总数: %d"), count);
		((CMainDlg*)m_pMainHost)->m_textNodenum->SetWindowText(tmpstr);
	}
	m_treeXmlStruct->Invalidate();
	return count;
}

BOOL SDesignerView::GoToXmlStructItem(int data, HSTREEITEM item)
{
	HSTREEITEM SibItem = item;

	while (SibItem)
	{
		int data1 = m_treeXmlStruct->GetItemData(SibItem);

		if (data1 == data)
		{
			m_CurSelCtrlItem = SibItem;
			m_treeXmlStruct->SelectItem(SibItem);
			m_treeXmlStruct->Invalidate();
			return TRUE;
		}

		HSTREEITEM ChildItem = m_treeXmlStruct->GetChildItem(SibItem);

		BOOL Result = GoToXmlStructItem(data, ChildItem);
		if (Result)
		{
			return TRUE;
		}

		SibItem = m_treeXmlStruct->GetNextSiblingItem(SibItem);
	}
	return FALSE;
}

BOOL SDesignerView::GetTC_CtrlNodeTag(HSTREEITEM fromItem, HSTREEITEM findItem, SStringA& strTag)
{
	int idx = 0;
	SStringA curTag = strTag, backupTag;

	HSTREEITEM hChild = m_treeXmlStruct->GetChildItem(fromItem);
	while (hChild)
	{
		strTag = curTag + SStringA(("")).Format(("%d"), idx);
		if (hChild == findItem)
		{
			return TRUE;
		}
		idx++;

		backupTag = strTag;
		strTag += ",";
		if (GetTC_CtrlNodeTag(hChild, findItem, strTag))
			return TRUE;
		
		strTag = backupTag;
		hChild = m_treeXmlStruct->GetNextSiblingItem(hChild);
	}
	
	return FALSE;
}
	
// 响应窗口结构中点击选中界面元素
bool SDesignerView::OnTCSelChanged(EventArgs *pEvt)
{
	EventTCSelChanged *evt = (EventTCSelChanged*)pEvt;
	HSTREEITEM item = m_treeXmlStruct->GetSelectedItem();
	m_CurSelCtrlItem = item;
	
	int data = m_treeXmlStruct->GetItemData(item);

	SStringT s;
	s.Format(_T("%d"), data);
	pugi::xml_node xmlNode = FindNodeByAttr(m_CurrentLayoutNode, L"data", s);

	if (!xmlNode)
	{
		return true;
	}

	SelectCtrlByIndex(data);
	HSTREEITEM rootitem = m_treeXmlStruct->GetRootItem();
	if (rootitem)
	{
		SStringA strTag = "0,";
		GetTC_CtrlNodeTag(rootitem, item, strTag);
		g_pMainDlg->SendMsgToViewer(selctrl_id, (void*)strTag.c_str(), strTag.GetLength());		
	}
	
	return true;
}


void SDesignerView::DeleteCtrl()
{
	if (m_curSelXmlNode == m_CurrentLayoutNode)
	{
		return;
	}
	else
	{
		m_curSelXmlNode.parent().remove_child(m_curSelXmlNode);

		//Debug(m_CurrentLayoutNode);
		//GetMoveWndRoot()->Click(0, CPoint(0, 0));
		ReloadLayout();
		m_nState = 0;

	}
}

void SDesignerView::Preview()
{
	//SMap<SWindow*, SMoveWnd*>::CPair *p = m_mapMoveRealWnd.GetNext();

	//SMoveWnd *wnd;

/*
	m_pMoveWndRoot->SetVisible(FALSE);

	m_pMoveWndRoot->GetParent()->Invalidate();*/

	//SPOSITION pos = m_mapMoveRealWnd.GetStartPosition();
	//while (pos)
	//{
	//	SMap<SWindow*, SMoveWnd*>::CPair *p = m_mapMoveRealWnd.GetNext(pos);
	//	wnd = p->m_value;
	//	wnd->SetVisible(FALSE);
	//}

}

void SDesignerView::unPreview()
{
	ReloadLayout();
	m_nState = 0;
/*
	GetMoveWndRoot()->Click(0, CPoint(0, 0));
	m_pMoveWndRoot->GetParent()->Invalidate();*/
}

void SDesignerView::ShowZYGLDlg()
{
	if (m_strUIResFile.IsEmpty())
	{
		return;
	}
	SDlgSkinSelect DlgSkin(_T("layout:UIDESIGNER_XML_SKIN_SELECT"), _T(""), m_strUIResFile, FALSE);
	DlgSkin.m_pResFileManger = &((CMainDlg*)m_pMainHost)->m_UIResFileMgr;
	if (DlgSkin.DoModal(m_pMainHost->m_hWnd) == IDOK)
	{
		((CMainDlg*)m_pMainHost)->ReloadWorkspaceUIRes();
		RefreshRes();
	}
}

void SDesignerView::ShowYSGLDlg()
{
	if (m_strUIResFile.IsEmpty())
	{
		return;
	}
	SDlgStyleManage dlg(_T(""), m_strUIResFile, FALSE);
	dlg.m_pResFileManger = &((CMainDlg*)m_pMainHost)->m_UIResFileMgr;
	if (dlg.DoModal(m_pMainHost->m_hWnd) == IDOK)
	{
		((CMainDlg*)m_pMainHost)->ReloadWorkspaceUIRes();
		RefreshRes();
	}
}
/*

void SDesignerView::ShowMovWndChild(BOOL bShow, SMoveWnd* pMovWnd)
{
	if (bShow)
	{
		for (; pMovWnd; pMovWnd = (SMoveWnd*)pMovWnd->GetWindow(GSW_NEXTSIBLING))
		{
			SWindow* pRealWnd = pMovWnd->m_pRealWnd;
			pMovWnd->SetVisible(pRealWnd->IsVisible());
			ShowMovWndChild(bShow, (SMoveWnd*)pMovWnd->GetWindow(GSW_FIRSTCHILD));
		}
	}
	else
	{
		for (; pMovWnd; pMovWnd = (SMoveWnd*)pMovWnd->GetWindow(GSW_NEXTSIBLING))
		{
			pMovWnd->SetVisible(FALSE);
			ShowMovWndChild(bShow, (SMoveWnd*)pMovWnd->GetWindow(GSW_FIRSTCHILD));
		}
	}
}*/

int SDesignerView::GetIndexData()
{
	m_ndata = m_ndata + 1;
	return m_ndata;
}

SWindow* SDesignerView::FindChildByUserData(SWindow* pWnd, int data)
{
	SWindow *pChild = pWnd->GetWindow(GSW_FIRSTCHILD);
	while (pChild)
	{
		int child_data = GetWindowUserData(pChild);

		if (child_data == data)
			return pChild;
		pChild = pChild->GetWindow(GSW_NEXTSIBLING);
	}

	pChild = pWnd->GetWindow(GSW_FIRSTCHILD);
	while (pChild)
	{
		SWindow *pChildFind = FindChildByUserData(pChild, data);
		if (pChildFind) return pChildFind;
		pChild = pChild->GetWindow(GSW_NEXTSIBLING);
	}

	return NULL;
}


void SDesignerView::TrimXmlNodeTextBlank(pugi::xml_node xmlNode)
{
	if (!xmlNode)
	{
		return;
	}

	pugi::xml_node NodeSib = xmlNode;
	while (NodeSib)
	{
		if (NodeSib.type() != pugi::node_element)
		{
			NodeSib = NodeSib.next_sibling();
			continue;
		}

		SStringT strText = NodeSib.text().get();
		strText.TrimBlank();
		if (!strText.IsEmpty())
		{
			NodeSib.text().set(strText);
		}

		TrimXmlNodeTextBlank(NodeSib.first_child());
		NodeSib = NodeSib.next_sibling();
	}
}

bool SDesignerView::OnPropGridItemActive(EventArgs *pEvt)
{
	EventPropGridItemActive *pEvent = (EventPropGridItemActive*)pEvt;
	IPropertyItem* pItem = pEvent->pItem;

	SStringT strDesc = pItem->GetDescription();
	SStringT strName = pItem->GetName1();

	if (strDesc.IsEmpty())
	{
		((CMainDlg*)m_pMainHost)->m_edtDesc->SetWindowText(strName);
	}
	else
	{
		((CMainDlg*)m_pMainHost)->m_edtDesc->SetWindowText(strDesc);
	}

	return true;
}

void SDesignerView::UseEditorUIDef(bool bYes) //使用编辑器自身的UIDef还是使用所打开的工程的UIDef
{
	if (bYes)
	{
		//SUiDef::getSingleton().SetUiDef(m_pOldUiDef, true);
	}
	else
	{
		//SUiDef::getSingleton().SetUiDef(m_pUiDef, true);
	}
}

SStringT SDesignerView::UnitToStr(int nUnit)
{
	//	px=0,dp,dip,sp
	switch (nUnit)
	{
	case 0:
		return _T("");
	case 1:
		return _T("dp");
	case 2:
		return _T("dip");
	case 3:
		return _T("sp");
	default:
		return _T("");
	}
}

BOOL SDesignerView::LoadConfig(pugi::xml_document &doc,const SStringT & cfgFile)
{
	pugi::xml_parse_result result = doc.load_file(g_CurDir + cfgFile);

	if (!result)
	{
		TCHAR szBuf[255+1];
		int nRet = GetEnvironmentVariable(_T("SOUIPATH"),szBuf,255);
		SStringT path = SStringT(szBuf) + L"\\demos\\souieditor\\";
		if(nRet>0 && nRet<255)
		{
			result = doc.load_file(path+cfgFile);
		}
		if(result)
		{
			g_CurDir = path;
		}
	}
	g_SysDataMgr.LoadSysData(g_CurDir + L"Config");
	return result;
}

long SDesignerView::GetWindowUserData(SWindow *pWnd)
{
	return ((SouiEditorApp*)SApplication::getSingletonPtr())->GetWindowIndex(pWnd);
}

void SDesignerView::SelectCtrlByOrder(int *pOrder,int nLen,HSTREEITEM hFrom)
{
	if (nLen == 0)
		return;
	
	SASSERT(m_rootItem);
	int iItem = pOrder[0];
	if (hFrom == 0)
	{
		hFrom = m_rootItem;
		if (nLen > 1)
			SelectCtrlByOrder(pOrder + 1, nLen - 1, hFrom);
		return;
	}
	HSTREEITEM hChild = m_treeXmlStruct->GetChildItem(hFrom);
	for (int i = 0; i < iItem && hChild; i++)
	{
		hChild = m_treeXmlStruct->GetNextSiblingItem(hChild);
	}
	if (hChild)
	{
		if (nLen > 1)
		{
			SelectCtrlByOrder(pOrder + 1, nLen - 1, hChild);
		}
		else
		{
			int iIndex = (int)m_treeXmlStruct->GetItemData(hChild);
			SelectCtrlByIndex(iIndex);
		}
	}else
	{//指定的窗口没有找到，选中父窗口
		int iIndex = (int)m_treeXmlStruct->GetItemData(hFrom);
		SelectCtrlByIndex(iIndex);
	}
}

}
