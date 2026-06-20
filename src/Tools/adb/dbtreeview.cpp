//--------------------------------------------------------------------------//
//                                                                          //
//                               dbtreeview.cpp    							//
//                                                                          //
//                  COPYRIGHT (C) 2003 Fever Pitch Studios, INC.            //
//                                                                          //
//--------------------------------------------------------------------------//
/*
	$Header: /Libs/Src/Tools/adb/dbtreeview.cpp 7     11/14/03 2:54p Ajackson $

	$Author: Ajackson $
*/
//--------------------------------------------------------------------------

#include <windows.h>
#include <commctrl.h>
#include <malloc.h>
#include <string>
#include <vector>

#include "dbTreeView.h"

//--------------------------------------------------------------------------

#define APSTUDIO_INVOKED
	#include "resource.h"
	#define IDD_TREE_DLG _APS_NEXT_RESOURCE_VALUE
#undef APSTUDIO_INVOKED

extern HWND hMainWindow;

//-------------------------------------------------------------------------------------

namespace
{
	std::string GetTreeItemText(HWND tree, HTREEITEM item)
	{
		char buffer[512] = {};
		TVITEM tvi = {};
		tvi.mask = TVIF_HANDLE | TVIF_TEXT;
		tvi.hItem = item;
		tvi.pszText = buffer;
		tvi.cchTextMax = sizeof(buffer);
		TreeView_GetItem(tree, &tvi);
		return buffer;
	}

	bool SetTreeItemData(HWND tree, HTREEITEM item, DWORD data)
	{
		TVITEM tvi = {};
		tvi.mask = TVIF_HANDLE | TVIF_PARAM;
		tvi.hItem = item;
		tvi.lParam = data;
		return TreeView_SetItem(tree, &tvi) != FALSE;
	}

	DWORD GetTreeItemData(HWND tree, HTREEITEM item)
	{
		TVITEM tvi = {};
		tvi.mask = TVIF_HANDLE | TVIF_PARAM;
		tvi.hItem = item;
		TreeView_GetItem(tree, &tvi);
		return static_cast<DWORD>(tvi.lParam);
	}
}

//--------------------------------------------------------------------------

DbTreeView::DbTreeView()
{
	m_TreeCtrl = NULL;
}

//--------------------------------------------------------------------------

DbTreeView::~DbTreeView()
{
	if (m_TreeCtrl)
	{
		DestroyWindow(m_TreeCtrl);
	}
	m_TreeCtrl = NULL;
}

//--------------------------------------------------------------------------

bool DbTreeView::Init( HWND _hwnd )
{
	RECT r;
	WINDOWPLACEMENT winPlace;

	::GetClientRect( _hwnd, &r );
	::ShowWindow( _hwnd, SW_HIDE );
	::GetWindowPlacement( _hwnd, &winPlace );

	INITCOMMONCONTROLSEX icc = {};
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_TREEVIEW_CLASSES;
	InitCommonControlsEx(&icc);

	DWORD dwStyle = TVS_LINESATROOT | TVS_HASLINES | TVS_NOTOOLTIPS | WS_CHILD | WS_BORDER | TVS_HASBUTTONS;
	
	m_TreeCtrl = CreateWindowEx(
		0,
		WC_TREEVIEW,
		"",
		dwStyle,
		r.left,
		r.top,
		r.right - r.left,
		r.bottom - r.top,
		::GetParent(_hwnd),
		reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDD_TREE_DLG)),
		GetModuleHandle(NULL),
		NULL);

	if (!m_TreeCtrl)
	{
		return false;
	}

	::ShowWindow( m_TreeCtrl, SW_NORMAL );
	::SetWindowPos( m_TreeCtrl, NULL, winPlace.rcNormalPosition.left, winPlace.rcNormalPosition.top, 0, 0, SWP_NOSIZE );

	return true;
}

//--------------------------------------------------------------------------

bool DbTreeView::CreateBranch( DWORD _data, char* _label, ... )
{
	if( !m_TreeCtrl ) return false;

	HTREEITEM item = TVI_ROOT;

	char* label = _label;

	va_list marker;
	va_start( marker, _label );
	while( label != NULL )
	{
		item  = InsertBranch( label, item );
		label = va_arg( marker, char*);
	}
	va_end( marker );

	if( item != TVI_ROOT )
	{
		return SetTreeItemData(m_TreeCtrl, item, _data);
	}

	return false;
}

//--------------------------------------------------------------------------

bool DbTreeView::Clear()
{
	if( !m_TreeCtrl ) return false;
	return( TreeView_DeleteAllItems(m_TreeCtrl) != false );
}

//--------------------------------------------------------------------------

bool DbTreeView::CreateBranchByParsing( char* _label, const char* _parser, DWORD _data )
{
	if( !m_TreeCtrl ) return false;

	HTREEITEM hItem = TVI_ROOT;

	char* pBuffer = (char*) _alloca( strlen(_label) + 1 );
	strcpy( pBuffer, _label );

	char* pLabel = pBuffer;
	char* pLabelNext = strstr(pLabel, _parser);

	while( pLabel )
	{
		if( pLabelNext )
		{
			*pLabelNext = 0;
			pLabelNext += strlen(_parser);
		}

		hItem = InsertBranch( pLabel, hItem );

		pLabel = pLabelNext;
		if( pLabel )
			pLabelNext = strstr(pLabel, _parser);
	}

	if( hItem != TVI_ROOT )
	{
		return SetTreeItemData(m_TreeCtrl, hItem, _data);
	}

	return false;
}

//--------------------------------------------------------------------------

bool DbTreeView::RenameLabel( char* _oldLabel, char* _newLabel )
{
	if( !m_TreeCtrl ) return false;

	return 0;
}

//--------------------------------------------------------------------------

bool DbTreeView::OnNotify( HWND _hwnd, WPARAM _wParam, LPARAM _lParam )
{
	if( !m_TreeCtrl ) return false;

	int idCtrl   = (int) _wParam; 
	LPNMHDR pnmh = (LPNMHDR) _lParam; 

	switch( pnmh->code )
	{
		case NM_DBLCLK:
		{
			LPNMHDR lpnmh = (LPNMHDR)_lParam; 
			PostMessage(hMainWindow,WM_COMMAND,MAKEWPARAM(ID_EDIT,0),0);
			break;
		}
		case NM_RETURN:
		{
			// why is this never thown?
			PostMessage(hMainWindow,WM_COMMAND,MAKEWPARAM(ID_EDIT,0),0);
			return 0;
		}
		case TVN_SELCHANGED:
		{
			LPNMTREEVIEW tv = (LPNMTREEVIEW)pnmh;
			break;
		}
		case TVN_ITEMEXPANDING:
		{
			LPNMTREEVIEW lpnmtv = (LPNMTREEVIEW)_lParam;
			TreeView_SortChildren( m_TreeCtrl, lpnmtv->itemNew.hItem, 0 );
			break;
		}
		case NM_RCLICK:
		{
			HTREEITEM hCurItem = TreeView_GetDropHilight(m_TreeCtrl);
			if (hCurItem == NULL)
			{
				hCurItem = TreeView_GetSelection(m_TreeCtrl);
				if (hCurItem == NULL)
				{
					return false;
				}
			}

			// setting up the location for the pop-up menu
			TreeView_Select(m_TreeCtrl, hCurItem, TVGN_CARET);

			RECT rectSel;
			TreeView_GetItemRect(m_TreeCtrl, hCurItem, &rectSel, TRUE);
			MapWindowPoints(m_TreeCtrl, HWND_DESKTOP, reinterpret_cast<POINT*>(&rectSel), 2);

			rectSel.top  += 10;
			rectSel.left += 10;
			 
			// Load the menu resource. 	
			HMENU menu = LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_FILEPOPUP));
			HMENU subMenu = menu ? GetSubMenu(menu, 0) : NULL;

			BOOL ret = subMenu ? TrackPopupMenu(subMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, rectSel.left, rectSel.top, 0, m_TreeCtrl, NULL) : 0;
			if (menu)
			{
				DestroyMenu(menu);
			}

			if( ret == IDM_UNDOCHECKOUT )
			{
				ret++;
			}
			else if( ret == IDM_CHECKOUT )
			{
				ret++;
			}
			else if( ret == IDM_CHECKIN )
			{
				ret++;
			}
		}
	}

	if( pnmh->code == TVN_KEYDOWN )
	{
		LPNMTVKEYDOWN ptvkd = (LPNMTVKEYDOWN) _lParam;

		switch( ptvkd->wVKey )
		{
			case VK_DELETE:
				PostMessage(hMainWindow, WM_COMMAND, MAKELONG(IDC_DELETE, 0), (LONG)_hwnd);
				return 0;
				
			case VK_INSERT:
				PostMessage(hMainWindow, WM_COMMAND, MAKELONG(IDC_CLONE, 0), (LONG)_hwnd);
				return 0;

			case VK_SPACE:
				PostMessage(hMainWindow,WM_COMMAND,MAKEWPARAM(ID_EDIT,0),0);
				return 0;
		}
	}
	return 0;
}

//--------------------------------------------------------------------------

int DbTreeView::GetCurrentSelectionData( void )
{
	if( !m_TreeCtrl ) return false;

	HTREEITEM hitem = TreeView_GetSelection(m_TreeCtrl);

	if( !hitem )
	{
		return -1;
	}

	return (int)GetTreeItemData( m_TreeCtrl, hitem );
}

//--------------------------------------------------------------------------

bool DbTreeView::GetRootToSelected( char* _rootLabel, int _rootLabelMax, const char* _seperator )
{
	_rootLabel[0] = 0;

	if( !m_TreeCtrl ) return false;

	HTREEITEM hitem = TreeView_GetSelection(m_TreeCtrl);

	if( !hitem )
		return false;

	std::vector<std::string> stringArray;

	if( GetTreeItemData(m_TreeCtrl, hitem) != 0xFFFFFFFF )
	{
		hitem = TreeView_GetParent(m_TreeCtrl, hitem);
	}

	while( hitem )
	{
		stringArray.push_back( GetTreeItemText(m_TreeCtrl, hitem) );
		hitem = TreeView_GetParent(m_TreeCtrl, hitem);
	}

	for( int i = static_cast<int>(stringArray.size())-1; i >= 0; i-- )
	{
		strcat( _rootLabel, stringArray[i].c_str() );
		strcat( _rootLabel, _seperator );
	}
	
	return 0;
}

//--------------------------------------------------------------------------

void DbTreeView::SavePlace()
{
	m_CurrentItemName[0] = 0;

	if( m_TreeCtrl )
	{
		std::vector<std::string> stringArray;
		HTREEITEM item = TreeView_GetSelection(m_TreeCtrl);
		while( item )
		{
			stringArray.push_back( GetTreeItemText(m_TreeCtrl, item) );
			item = TreeView_GetParent(m_TreeCtrl, item);
		}

		for( int i = static_cast<int>(stringArray.size())-1; i >= 0; i-- )
		{
			strcat( m_CurrentItemName, stringArray[i].c_str() );

			if( i )
				strcat( m_CurrentItemName, "!!" );
		}
	}
}

//--------------------------------------------------------------------------

HTREEITEM DbTreeView::FindPlace( HTREEITEM _item, const char* _archname, int _archindex )
{
	if( !_item )
	{
		return NULL;
	}

	std::string mask(_archname, _archindex);
	
	size_t nPrev = mask.find("!!");
	while( nPrev != std::string::npos && nPrev > 0 )
	{
		mask = mask.substr( nPrev + 2 );
		nPrev = mask.find("!!");
	}

	HTREEITEM item = _item;

	while( item )
	{
		std::string label = GetTreeItemText(m_TreeCtrl, item);
		if( label == mask )
		{
			break;
		}
		item = TreeView_GetNextSibling(m_TreeCtrl, item);
	}

	if( item && _archindex < (int)strlen(_archname) )
	{
		mask = _archname;
		size_t nextIndex = mask.find("!!", _archindex + 2);

		if( nextIndex == std::string::npos )
		{
			nextIndex = mask.length();
		}
		else
			mask = mask.substr( 0, _archindex + nextIndex );

		return FindPlace( TreeView_GetChild(m_TreeCtrl, item), _archname, static_cast<int>(nextIndex) );
	}

	return item;
}

//--------------------------------------------------------------------------

void DbTreeView::RestorePlace()
{
	if( m_TreeCtrl )
	{
		std::string string = m_CurrentItemName;
		size_t index = string.find("!!");

		if( index != std::string::npos )
		{
			string = string.substr( 0, index );
			HTREEITEM item = FindPlace( TreeView_GetRoot(m_TreeCtrl), m_CurrentItemName, static_cast<int>(index) );
			if( item )
			{
				TreeView_SelectItem(m_TreeCtrl, item);
				TreeView_EnsureVisible(m_TreeCtrl, item);
			}
		}
	}
}

//--------------------------------------------------------------------------

HTREEITEM DbTreeView::InsertBranch( char* _label, HTREEITEM _item )
{
	HTREEITEM hItem = _item;

	if( hItem == TVI_ROOT )
	{
		hItem = TreeView_GetRoot(m_TreeCtrl);
	}
	else
	{
		hItem = TreeView_GetChild(m_TreeCtrl, _item);
	}

	while( hItem != NULL )
	{
		std::string s = GetTreeItemText( m_TreeCtrl, hItem );

		if( s == _label )
		{
			return hItem;
		}

		hItem = TreeView_GetNextSibling(m_TreeCtrl, hItem);
	}
	
	TVINSERTSTRUCT insert = {};
	insert.hParent = _item;
	insert.hInsertAfter = TVI_SORT;
	insert.item.mask = TVIF_TEXT | TVIF_PARAM;
	insert.item.pszText = _label;
	insert.item.lParam = (DWORD)-1;

	hItem = TreeView_InsertItem( m_TreeCtrl, &insert );
	SetTreeItemData( m_TreeCtrl, hItem, (DWORD)-1 );

	return hItem;
}
