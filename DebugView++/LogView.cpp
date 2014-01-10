// (C) Copyright Gert-Jan de Vos and Jan Wilmans 2013.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)

// Repository at: https://github.com/djeedjay/DebugViewPP/

#include "stdafx.h"
#include <iomanip>
#include <array>
#include <regex>
#include <boost/algorithm/string.hpp>
#include "dbgstream.h"
#include "Win32Lib.h"
#include "Utilities.h"
#include "Resource.h"
#include "MainFrame.h"
#include "LogView.h"

namespace fusion {
namespace debugviewpp {

SelectionInfo::SelectionInfo() :
	beginLine(0), endLine(0), count(0)
{
}

SelectionInfo::SelectionInfo(int beginLine, int endLine, int count) :
	beginLine(beginLine), endLine(endLine), count(count)
{
}

TextColor::TextColor(COLORREF back, COLORREF fore) :
	back(back), fore(fore)
{
}

Highlight::Highlight(int id, int begin, int end, const TextColor& color) :
	id(id), begin(begin), end(end), color(color)
{
}

LogLine::LogLine(int line) :
	bookmark(false), line(line)
{
}

ItemData::ItemData() :
	color(GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT))
{
}

BEGIN_MSG_MAP_TRY(CLogView)
	MSG_WM_CREATE(OnCreate)
	MSG_WM_CONTEXTMENU(OnContextMenu)
	MSG_WM_SETCURSOR(OnSetCursor)
	MSG_WM_MOUSEMOVE(OnMouseMove)
	MSG_WM_LBUTTONUP(OnLButtonUp)
	REFLECTED_NOTIFY_CODE_HANDLER_EX(NM_CLICK, OnClick)
	REFLECTED_NOTIFY_CODE_HANDLER_EX(NM_DBLCLK, OnDblClick)
	REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_ITEMCHANGED, OnItemChanged)
	REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_GETDISPINFO, OnGetDispInfo)
	REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_ODSTATECHANGED, OnOdStateChanged)
	REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_INCREMENTALSEARCH, OnIncrementalSearch)
	REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_ODCACHEHINT, OnOdCacheHint)
	REFLECTED_NOTIFY_CODE_HANDLER_EX(LVN_BEGINDRAG, OnBeginDrag)
	COMMAND_ID_HANDLER_EX(ID_VIEW_CLEAR, OnViewClear)
	COMMAND_ID_HANDLER_EX(ID_VIEW_SELECTALL, OnViewSelectAll)
	COMMAND_ID_HANDLER_EX(ID_VIEW_COPY, OnViewCopy)
	COMMAND_ID_HANDLER_EX(ID_VIEW_SCROLL, OnViewScroll)
	COMMAND_ID_HANDLER_EX(ID_VIEW_TIME, OnViewTime)
	COMMAND_ID_HANDLER_EX(ID_VIEW_HIDE_HIGHLIGHT, OnViewHideHighlight)
	COMMAND_ID_HANDLER_EX(ID_VIEW_FIND_NEXT, OnViewFindNext)
	COMMAND_ID_HANDLER_EX(ID_VIEW_FIND_PREVIOUS, OnViewFindPrevious)
	COMMAND_ID_HANDLER_EX(ID_VIEW_NEXT_PROCESS, OnViewNextProcess)
	COMMAND_ID_HANDLER_EX(ID_VIEW_PREVIOUS_PROCESS, OnViewPreviousProcess)
	COMMAND_ID_HANDLER_EX(ID_VIEW_PROCESS_HIGHLIGHT, OnViewProcessHighlight)
	COMMAND_ID_HANDLER_EX(ID_VIEW_PROCESS_EXCLUDE, OnViewProcessExclude)
	COMMAND_ID_HANDLER_EX(ID_VIEW_PROCESS_TOKEN, OnViewProcessToken)
	COMMAND_ID_HANDLER_EX(ID_VIEW_PROCESS_TRACK, OnViewProcessTrack)
	COMMAND_ID_HANDLER_EX(ID_VIEW_PROCESS_ONCE, OnViewProcessOnce)
	COMMAND_ID_HANDLER_EX(ID_VIEW_FILTER_HIGHLIGHT, OnViewFilterHighlight)
	COMMAND_ID_HANDLER_EX(ID_VIEW_FILTER_EXCLUDE, OnViewFilterExclude)
	COMMAND_ID_HANDLER_EX(ID_VIEW_FILTER_TOKEN, OnViewFilterToken)
	COMMAND_ID_HANDLER_EX(ID_VIEW_FILTER_TRACK, OnViewFilterTrack)
	COMMAND_ID_HANDLER_EX(ID_VIEW_FILTER_ONCE, OnViewFilterOnce)
	COMMAND_ID_HANDLER_EX(ID_VIEW_BOOKMARK, OnViewBookmark)
	COMMAND_ID_HANDLER_EX(ID_VIEW_NEXT_BOOKMARK, OnViewNextBookmark)
	COMMAND_ID_HANDLER_EX(ID_VIEW_PREVIOUS_BOOKMARK, OnViewPreviousBookmark)
	COMMAND_ID_HANDLER_EX(ID_VIEW_CLEAR_BOOKMARKS, OnViewClearBookmarks)
	COMMAND_RANGE_HANDLER_EX(ID_VIEW_COLUMN_FIRST, ID_VIEW_COLUMN_LAST, OnViewColumn)
	CHAIN_MSG_MAP_ALT(COwnerDraw<CLogView>, 1)
	CHAIN_MSG_MAP(COffscreenPaint<CLogView>)
	DEFAULT_REFLECTION_HANDLER()
END_MSG_MAP_CATCH(ExceptionHandler)

bool CLogView::IsColumnViewed(int nID) const
{
	return m_columns[nID - ID_VIEW_COLUMN_FIRST].enable;
}

void CLogView::OnViewColumn(UINT /*uNotifyCode*/, int nID, CWindow /*wndCtl*/)
{
	UpdateColumnInfo();

	auto& column = m_columns[nID - ID_VIEW_COLUMN_FIRST];
	column.enable = !column.enable;

	int delta = column.enable ? +1 : -1;
	for (auto it = m_columns.begin(); it != m_columns.end(); ++it)
		if (it->column.iSubItem != column.column.iSubItem && it->column.iOrder >= column.column.iOrder)
			it->column.iOrder += delta;

	UpdateColumns();
}

CLogView::CLogView(const std::wstring& name, CMainFrame& mainFrame, LogFile& logFile, LogFilter filter) :
	m_name(name),
	m_mainFrame(mainFrame),
	m_logFile(logFile),
	m_filter(std::move(filter)),
	m_firstLine(0),
	m_clockTime(false),
	m_autoScrollDown(true),
	m_dirty(false),
	m_hBookmarkIcon(static_cast<HICON>(LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_BOOKMARK), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR))),
	m_hBeamCursor(LoadCursor(nullptr, IDC_IBEAM)),
	m_dragStart(0, 0),
	m_dragEnd(0, 0)
{
}

void CLogView::ExceptionHandler()
{
	MessageBox(WStr(GetExceptionMessage()).c_str(), LoadString(IDR_APPNAME).c_str(), MB_ICONERROR | MB_OK);
}

int CLogView::ColumnToSubItem(Column::type iColumn) const
{
	int columns = GetHeader().GetItemCount();
	for (int iSubItem = 0; iSubItem < columns; ++iSubItem)
	{
		LVCOLUMN column;
		column.mask = LVCF_SUBITEM;
		GetColumn(iSubItem, &column);
		if (column.iSubItem == iColumn)
			return iSubItem;
	}
	return 0;
}

Column::type CLogView::SubItemToColumn(int iSubItem) const
{
	LVCOLUMN column;
	column.mask = LVCF_SUBITEM;
	GetColumn(iSubItem, &column);
	return static_cast<Column::type>(column.iSubItem);
}

void CLogView::UpdateColumnInfo()
{
	int count = GetHeader().GetItemCount();
	for (int i = 0; i < count; ++i)
	{
		auto& column = m_columns[SubItemToColumn(i)].column;
		auto column2 = column;
		column2.mask = LVCF_WIDTH | LVCF_ORDER;
		GetColumn(i, &column2);
		column.cx = column2.cx;
		column.iOrder = column2.iOrder;
	}
}

void CLogView::UpdateColumns()
{
	int columns = GetHeader().GetItemCount();
	for (int i = 0; i < columns; ++i)
		DeleteColumn(0);

	int col = 0;
	for (auto it = m_columns.begin(); it != m_columns.end(); ++it)
	{
		if (it->enable)
			InsertColumn(col++, &it->column);
	}
}

ColumnInfo MakeColumn(Column::type column, const wchar_t* name, int format, int width)
{
	ColumnInfo info;
	info.enable = true;
	info.column.iSubItem = column;
	info.column.iOrder = column;
	info.column.pszText = const_cast<wchar_t*>(name);
	info.column.fmt = format;
	info.column.cx = width;
	info.column.mask = LVCF_SUBITEM | LVCF_ORDER | LVCF_TEXT | LVCF_FMT | LVCF_WIDTH;
	return info;
}

LRESULT CLogView::OnCreate(const CREATESTRUCT* /*pCreate*/)
{
	DefWindowProc();

//	SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP);
	m_hdr.SubclassWindow(GetHeader());

	m_columns.push_back(MakeColumn(Column::Bookmark, L"", LVCFMT_RIGHT, 20));
	m_columns.push_back(MakeColumn(Column::Line, L"Line", LVCFMT_RIGHT, 60));
	m_columns.push_back(MakeColumn(Column::Time, L"Time", LVCFMT_RIGHT, 90));
	m_columns.push_back(MakeColumn(Column::Pid, L"PID", LVCFMT_RIGHT, 60));
	m_columns.push_back(MakeColumn(Column::Process, L"Process", LVCFMT_LEFT, 140));
	m_columns.push_back(MakeColumn(Column::Message, L"Message", LVCFMT_LEFT, 1500));
	UpdateColumns();

	ApplyFilters();

	return 0;
}

bool Contains(const RECT& rect, const POINT& pt)
{
	return pt.x >= rect.left && pt.x < rect.right && pt.y >= rect.top && pt.y < rect.bottom;
}

BOOL CLogView::OnSetCursor(CWindow /*wnd*/, UINT /*nHitTest*/, UINT /*message*/)
{
	POINT pt = GetMessagePos();
	ScreenToClient(&pt);

	RECT client;
	GetClientRect(&client);
	if (!Contains(client, pt))
	{
		SetMsgHandled(false);
		return FALSE;
	}

	LVHITTESTINFO info;
	info.flags = 0;
	info.pt = pt;
	SubItemHitTest(&info);
	if ((info.flags & LVHT_ONITEM) != 0 && info.iSubItem == ColumnToSubItem(Column::Message))
	{
		::SetCursor(m_hBeamCursor);
		return TRUE;
	}

	SetMsgHandled(false);
	return FALSE;
}

int CLogView::TextHighlightHitTest(int iItem, const POINT& pt)
{
	int pos = GetTextIndex(iItem, pt.x);
	auto highlights = GetItemData(iItem).highlights;
	auto it = highlights.begin();
	while (it != highlights.end() && it->end <= pos)
		++it;
	if (it != highlights.end() && it->begin <= pos)
		return it->id;
	return 0;
}

void CLogView::OnContextMenu(HWND /*hWnd*/, CPoint pt)
{
	if (pt == CPoint(-1, -1))
	{
		RECT rect = GetItemRect(GetNextItem(-1, LVNI_ALL | LVNI_FOCUSED), LVIR_LABEL);
		pt = CPoint(rect.left, rect.bottom - 1);
	}
	else
	{
		ScreenToClient(&pt);
	}

	HDHITTESTINFO hdrInfo;
	hdrInfo.flags = 0;
	hdrInfo.pt = pt;
	GetHeader().HitTest(&hdrInfo);

	LVHITTESTINFO info;
	info.flags = 0;
	info.pt = pt;
	SubItemHitTest(&info);

	int menuId = 0;
	if ((hdrInfo.flags & HHT_ONHEADER) != 0)
		menuId = IDR_HEADER_CONTEXTMENU;
	else if ((info.flags & LVHT_ONITEM) != 0)
	{
		switch (SubItemToColumn(info.iSubItem))
		{
		case Column::Process:
			menuId = IDR_PROCESS_CONTEXTMENU;
			break;
		case Column::Message:
			menuId = TextHighlightHitTest(info.iItem, pt) == 1 ? IDR_HIGHLIGHT_CONTEXTMENU : IDR_VIEW_CONTEXTMENU;
			break;
		default:	
			menuId = IDR_VIEW_CONTEXTMENU;
			break;
		}
	}
	else
		return;

	CMenu menuContext;
	menuContext.LoadMenu(menuId);
	CMenuHandle menuPopup(menuContext.GetSubMenu(0));
	ClientToScreen(&pt);
	menuPopup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, m_mainFrame);
}

int GetTextOffset(HDC hdc, const std::string& s, int xPos)
{
	int nFit;
	SIZE size;
	if (!GetTextExtentExPointA(hdc, s.c_str(), s.size(), xPos, &nFit, nullptr, &size) || nFit < 0 || nFit >= static_cast<int>(s.size()))
		return -1;
	return nFit;
}

int GetTextOffset(HDC hdc, const std::wstring& s, int xPos)
{
	int nFit;
	SIZE size;
	if (!GetTextExtentExPointW(hdc, s.c_str(), s.size(), xPos, &nFit, nullptr, &size) || nFit < 0 || nFit >= static_cast<int>(s.size()))
		return -1;
	return nFit;
}

bool iswordchar(int c)
{
	return isalnum(c) || c == '_';
}

LRESULT CLogView::OnClick(NMHDR* pnmh)
{
	auto& nmhdr = *reinterpret_cast<NMITEMACTIVATE*>(pnmh);

	LVHITTESTINFO info;
	info.flags = 0;
	info.pt = nmhdr.ptAction;
	SubItemHitTest(&info);
	if ((info.flags & LVHT_ONITEM) != 0 && info.iSubItem == 0)
		ToggleBookmark(info.iItem);

	return 0;
}

void CLogView::OnMouseMove(UINT flags, CPoint point)
{
	SetMsgHandled(false);

	if ((flags & MK_LBUTTON) == 0)
		return;

	m_dragEnd = point;
	Invalidate();
}

void CLogView::OnLButtonUp(UINT /*flags*/, CPoint point)
{
	SetMsgHandled(false);

	if (abs(point.x - m_dragStart.x) <= GetSystemMetrics(SM_CXDRAG) &&
		abs(point.y - m_dragStart.y) <= GetSystemMetrics(SM_CYDRAG))
		return;

	LVHITTESTINFO info;
	info.flags = 0;
	info.pt = m_dragStart;
	SubItemHitTest(&info);
	int x1 = std::min(m_dragStart.x, point.x);
	int x2 = std::max(m_dragStart.x, point.x);
	m_dragStart = CPoint();
	m_dragEnd = CPoint();
	ReleaseCapture();
	Invalidate();
	if ((info.flags & LVHT_ONITEM) == 0 || SubItemToColumn(info.iSubItem) != Column::Message)
		return;

	int begin = GetTextIndex(info.iItem, x1);
	int end = GetTextIndex(info.iItem, x2);
	SetHighlightText(GetItemWText(info.iItem, ColumnToSubItem(Column::Message)).substr(begin, end - begin));
}

void CLogView::MeasureItem(MEASUREITEMSTRUCT* pMeasureItemStruct)
{
	CClientDC dc(*this);

	GdiObjectSelection font(dc, GetFont());
	TEXTMETRIC metric;
	dc.GetTextMetrics(&metric);
	pMeasureItemStruct->itemHeight = metric.tmHeight;
}

void CLogView::DrawItem(DRAWITEMSTRUCT* pDrawItemStruct)
{
	DrawItem(pDrawItemStruct->hDC, pDrawItemStruct->itemID, pDrawItemStruct->itemState);
}

void CLogView::DeleteItem(DELETEITEMSTRUCT* lParam)
{
	COwnerDraw<CLogView>::DeleteItem(lParam);
}

int CLogView::GetTextIndex(int iItem, int xPos)
{
	CClientDC dc(*this);
	GdiObjectSelection font(dc, GetFont());
	return GetTextIndex(dc.m_hDC, iItem, xPos);
}

int CLogView::GetTextIndex(CDCHandle dc, int iItem, int xPos) const
{
	auto rect = GetSubItemRect(0, ColumnToSubItem(Column::Message), LVIR_BOUNDS);
	int x0 = rect.left + GetHeader().GetBitmapMargin();

	auto text = GetItemWText(iItem, ColumnToSubItem(Column::Message));
	int index = GetTextOffset(dc, text, xPos - x0);
	if (index < 0)
		return xPos > x0 ? text.size() : 0;
	return index;
}

LRESULT CLogView::OnDblClick(NMHDR* pnmh)
{
	auto& nmhdr = *reinterpret_cast<NMITEMACTIVATE*>(pnmh);

	if (SubItemToColumn(nmhdr.iSubItem) != Column::Message || nmhdr.iItem < 0 || static_cast<size_t>(nmhdr.iItem) >= m_logLines.size())
		return 0;

	int nFit = GetTextIndex(nmhdr.iItem, nmhdr.ptAction.x);
	auto text = GetItemWText(nmhdr.iItem, ColumnToSubItem(Column::Message));

	int begin = nFit;
	while (begin > 0)
	{
		if (!iswordchar(text[begin - 1]))
			break;
		--begin;
	}
	int end = nFit;
	while (end < static_cast<int>(text.size()))
	{
		if (!iswordchar(text[end]))
			break;
		++end;
	}
	SetHighlightText(std::wstring(text.begin() + begin, text.begin() + end));
	return 0;
}

LRESULT CLogView::OnItemChanged(NMHDR* pnmh)
{
	auto& nmhdr = *reinterpret_cast<NMLISTVIEW*>(pnmh);

	if ((nmhdr.uNewState & LVIS_FOCUSED) == 0 ||
		nmhdr.iItem < 0  ||
		static_cast<size_t>(nmhdr.iItem) >= m_logLines.size())
		return 0;

	m_autoScrollDown = nmhdr.iItem == GetItemCount() - 1;
	return 0;
}

RECT CLogView::GetItemRect(int iItem, unsigned code) const
{
	RECT rect;
	CListViewCtrl::GetItemRect(iItem, &rect, code);
	return rect;
}

RECT CLogView::GetSubItemRect(int iItem, int iSubItem, unsigned code) const
{
	RECT rect;
	CListViewCtrl::GetSubItemRect(iItem, iSubItem, code, &rect);
	if (iSubItem == 0)
		rect.right = rect.left + GetColumnWidth(0);
	return rect;
}

unsigned GetTextAlign(const HDITEM& item)
{
	switch (item.fmt & HDF_JUSTIFYMASK)
	{
	case HDF_LEFT: return DT_LEFT;
	case HDF_CENTER: return DT_CENTER;
	case HDF_RIGHT: return DT_RIGHT;
	}
	return HDF_LEFT;
}

class ScopedBkColor
{
public:
	ScopedBkColor(HDC hdc, COLORREF col) :
		m_hdc(hdc),
		m_col(SetBkColor(hdc, col))
	{
	}

	~ScopedBkColor()
	{
		SetBkColor(m_hdc, m_col);
	}

private:
	HDC m_hdc;
	COLORREF m_col;
};

class ScopedTextColor
{
public:
	ScopedTextColor(HDC hdc, COLORREF col) :
		m_hdc(hdc),
		m_col(SetTextColor(hdc, col))
	{
	}

	~ScopedTextColor()
	{
		SetTextColor(m_hdc, m_col);
	}

private:
	HDC m_hdc;
	COLORREF m_col;
};

SIZE GetTextSize(CDCHandle dc, const std::wstring& text, int length)
{
	SIZE size;
	dc.GetTextExtent(text.c_str(), length, &size);
	return size;
}

void ExtTextOut(HDC hdc, const POINT& pt, const RECT& rect, const std::wstring& text)
{
	::ExtTextOutW(hdc, pt.x, pt.y, ETO_CLIPPED | ETO_OPAQUE, &rect, text.c_str(), text.size(), nullptr);
}

void AddEllipsis(HDC hdc, std::wstring& text, int width)
{
	static const std::wstring ellipsis(L"...");
	int pos = GetTextOffset(hdc, text, width - GetTextSize(hdc, ellipsis, ellipsis.size()).cx);
	if (pos >= 0 && pos < static_cast<int>(text.size()))
		text = text.substr(0, pos) + ellipsis;
}

void InsertHighlight(std::vector<Highlight>& highlights, const Highlight& highlight)
{
	if (highlight.begin == highlight.end)
		return;

	std::vector<Highlight> newHighlights;
	newHighlights.reserve(highlights.size() + 2);

	auto it = highlights.begin();
	while (it != highlights.end() && it->begin < highlight.begin)
	{
		newHighlights.push_back(*it);
		++it;
	}

	while (it != highlights.end() && it->end <= highlight.end)
		++it;

	newHighlights.push_back(highlight);

	while (it != highlights.end())
	{
		newHighlights.push_back(*it);
		++it;
	}

	highlights.swap(newHighlights);
}

std::vector<Highlight> CLogView::GetHighlights(const std::string& text) const
{
	std::vector<Highlight> highlights;

	int highlightId = 1;
	for (auto it = m_filter.messageFilters.begin(); it != m_filter.messageFilters.end(); ++it)
	{
		if (!it->enable || it->filterType != FilterType::Token)
			continue;

		std::sregex_iterator begin(text.begin(), text.end(), it->re), end;
		int id = ++highlightId;
		for (auto tok = begin; tok != end; ++tok)
			InsertHighlight(highlights, Highlight(id, tok->position(), tok->position() + tok->length(), TextColor(it->bgColor, it->fgColor)));
	}

	auto line = boost::make_iterator_range(text);
	for (;;)
	{
		auto match = boost::algorithm::ifind_first(line, m_highlightText);
		if (match.empty())
			break;

		InsertHighlight(highlights, Highlight(1, match.begin() - text.begin(), match.end() - text.begin(), TextColor(RGB(255, 255, 55), RGB(0, 0, 0))));
		line = boost::make_iterator_range(match.end(), line.end());
	}

	return highlights;
}

void DrawHighlightedText(HDC hdc, const RECT& rect, std::wstring text, std::vector<Highlight> highlights, const Highlight& selection)
{
	InsertHighlight(highlights, selection);

	AddEllipsis(hdc, text, rect.right - rect.left);

	int height = GetTextSize(hdc, text, text.size()).cy;
	POINT pos = { rect.left, rect.top + (rect.bottom - rect.top - height)/2 };
	RECT rcHighlight = rect;
	for (auto it = highlights.begin(); it != highlights.end(); ++it)
	{
		rcHighlight.right = rect.left + GetTextSize(hdc, text, it->begin).cx;
		ExtTextOut(hdc, pos, rcHighlight, text);

		rcHighlight.left = rcHighlight.right;
		rcHighlight.right = rect.left + GetTextSize(hdc, text, it->end).cx;
		{
			ScopedTextColor txtcol(hdc, it->color.fore);
			ScopedBkColor bkcol(hdc, it->color.back);
			ExtTextOut(hdc, pos, rcHighlight, text);
		}
		rcHighlight.left = rcHighlight.right;
	}
	rcHighlight.right = rect.right;
	ExtTextOut(hdc, pos, rcHighlight, text);
}

void CLogView::DrawBookmark(CDCHandle dc, int iItem) const
{
	if (!m_logLines[iItem].bookmark)
		return;
	RECT rect = GetSubItemRect(iItem, 0, LVIR_BOUNDS);
	dc.DrawIconEx(rect.left /* + GetHeader().GetBitmapMargin() */, rect.top + (rect.bottom - rect.top - 16)/2, m_hBookmarkIcon.get(), 0, 0, 0, nullptr, DI_NORMAL | DI_COMPAT);
}

std::string TabsToSpaces(const std::string& s, int tabsize = 4)
{
	std::string result;
	result.reserve(s.size() + 3*tabsize);
	for (auto it = s.begin(); it != s.end(); ++it)
	{
		if (*it == '\t')
		{
			do
			{
				result.push_back(' ');
			}
			while (result.size() % tabsize != 0);
		}
		else
		{
			result.push_back(*it);
		}
	}
	return result;
}

ItemData CLogView::GetItemData(int iItem) const
{
	ItemData data;
	data.text[Column::Line] = GetItemWText(iItem, ColumnToSubItem(Column::Line));
	data.text[Column::Time] = GetItemWText(iItem, ColumnToSubItem(Column::Time));
	data.text[Column::Pid] = GetItemWText(iItem, ColumnToSubItem(Column::Pid));
	data.text[Column::Process] = GetItemWText(iItem, ColumnToSubItem(Column::Process));
	auto text = TabsToSpaces(m_logFile[m_logLines[iItem].line].text);
	data.highlights = GetHighlights(text);
	data.text[Column::Message] = WStr(text).str();
	data.color = GetTextColor(m_logFile[m_logLines[iItem].line]);
	return data;
}

Highlight CLogView::GetSelectionHighlight(CDCHandle dc, int iItem) const
{
	auto rect = GetSubItemRect(iItem, ColumnToSubItem(Column::Message), LVIR_BOUNDS);
	if (!Contains(rect, m_dragStart))
		return Highlight(0, 0, 0, TextColor(0, 0));

	int x1 = std::min(m_dragStart.x, m_dragEnd.x);
	int x2 = std::max(m_dragStart.x, m_dragEnd.x);

	int begin = GetTextIndex(dc, iItem, x1);
	int end = GetTextIndex(dc, iItem, x2);
	return Highlight(0, begin, end, TextColor(RGB(128, 255, 255), RGB(0, 0, 0)));	
}

void CLogView::DrawSubItem(CDCHandle dc, int iItem, int iSubItem, const ItemData& data) const
{
	auto column = SubItemToColumn(iSubItem);
	const auto& text = data.text[column];
	RECT rect = GetSubItemRect(iItem, iSubItem, LVIR_BOUNDS);
	int margin = GetHeader().GetBitmapMargin();
	rect.left += margin;
	rect.right -= margin;
	if (column == Column::Message)
		return DrawHighlightedText(dc, rect, text, data.highlights, GetSelectionHighlight(dc, iItem));

	HDITEM item;
	item.mask = HDI_FORMAT;
	unsigned align = (GetHeader().GetItem(iSubItem, &item)) ? GetTextAlign(item) : DT_LEFT;
	dc.DrawText(text.c_str(), text.size(), &rect, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void CLogView::DrawItem(CDCHandle dc, int iItem, unsigned /*iItemState*/) const
{
	auto rect = GetItemRect(iItem, LVIR_BOUNDS);
	auto data = GetItemData(iItem);

	bool selected = GetItemState(iItem, LVIS_SELECTED) == LVIS_SELECTED;
	bool focused = GetItemState(iItem, LVIS_FOCUSED) == LVIS_FOCUSED;
	auto bkColor = selected ? GetSysColor(COLOR_HIGHLIGHT) : data.color.back;
	auto txColor = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : data.color.fore;

	rect.left += GetColumnWidth(0);
	dc.FillSolidRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, bkColor);

	ScopedBkColor bcol(dc, bkColor);
	ScopedTextColor tcol(dc, txColor);

	int subitemCount = GetHeader().GetItemCount();
	DrawBookmark(dc, iItem);
	for (int i = 1; i < subitemCount; ++i)
		DrawSubItem(dc, iItem, i, data);
	if (focused)
		dc.DrawFocusRect(&rect);
}

template <typename CharT>
void CopyItemText(const CharT* s, wchar_t* buf, size_t maxLen)
{
	assert(maxLen > 0);

	for (int len = 0; len + 1U < maxLen && *s; ++s)
	{
		if (*s == '\t')
		{
			do
			{
				*buf++ = ' ';
				++len;
			}
			while (len + 1U < maxLen && len % 4 != 0);
		}
		else
		{
			*buf++ = *s;
			++len;
		}
	}

	*buf = '\0';
}

void CopyItemText(const std::string& s, wchar_t* buf, size_t maxLen)
{
	CopyItemText(s.c_str(), buf, maxLen);
}

void CopyItemText(const std::wstring& s, wchar_t* buf, size_t maxLen)
{
	CopyItemText(s.c_str(), buf, maxLen);
}

std::string GetTimeText(double time)
{
	return stringbuilder() << std::fixed << std::setprecision(6) << time;
}

std::string GetTimeText(const SYSTEMTIME& st)
{
	char buf[32];
	sprintf_s(buf, "%d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	return buf;
}

std::string GetTimeText(const FILETIME& ft)
{
	return GetTimeText(FileTimeToSystemTime(FileTimeToLocalFileTime(ft)));
}

std::string CLogView::GetColumnText(int iItem, Column::type column) const
{
	int line = m_logLines[iItem].line;
	const Message& msg = m_logFile[line];

	switch (column)
	{
	case Column::Line: return std::to_string(iItem + 1ULL);
	case Column::Time: return m_clockTime ? GetTimeText(msg.systemTime) : GetTimeText(msg.time);
	case Column::Pid: return std::to_string(msg.processId + 0ULL);
	case Column::Process: return msg.processName;
	case Column::Message: return msg.text;
	}
	return std::string();
}

LRESULT CLogView::OnGetDispInfo(NMHDR* pnmh)
{
	auto pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pnmh);
	LVITEM& item = pDispInfo->item;
	if ((item.mask & LVIF_TEXT) == 0 || item.iItem >= static_cast<int>(m_logLines.size()))
		return 0;

	CopyItemText(GetColumnText(item.iItem, SubItemToColumn(item.iSubItem)), item.pszText, item.cchTextMax);
	return 0;
}

SelectionInfo CLogView::GetSelectedRange() const
{
	int first = GetNextItem(-1, LVNI_SELECTED);
	if (first < 0)
		return SelectionInfo();

	int item = first;
	int last = first;
	do
	{
		last = item;
		item = GetNextItem(item, LVNI_SELECTED);
	}
	while (item > 0);

	return SelectionInfo(m_logLines[first].line, m_logLines[last].line, last - first + 1);
}

SelectionInfo CLogView::GetViewRange() const
{
	if (m_logLines.empty())
		return SelectionInfo();

	return SelectionInfo(m_logLines.front().line, m_logLines.back().line, m_logLines.size());
}

LRESULT CLogView::OnOdStateChanged(NMHDR* pnmh)
{
	auto& nmhdr = *reinterpret_cast<NMLVODSTATECHANGE*>(pnmh);
	nmhdr;

	return 0;
}

bool Contains(const std::string& text, const std::string& substring)
{
	return !boost::algorithm::ifind_first(text, substring).empty();
}

LRESULT CLogView::OnIncrementalSearch(NMHDR* pnmh)
{
	ScopedCursor cursor(::LoadCursor(nullptr, IDC_WAIT));
	auto& nmhdr = *reinterpret_cast<NMLVFINDITEM*>(pnmh);

	std::string text(Str(nmhdr.lvfi.psz).str());
//	int line = nmhdr.iStart; // Does not work as specified...
	int line = std::max(GetNextItem(-1, LVNI_FOCUSED), 0);
	while (line != static_cast<int>(m_logLines.size()))
	{
		if (Contains(m_logFile[m_logLines[line].line].text, text))
		{
			SetHighlightText(nmhdr.lvfi.psz);
			nmhdr.lvfi.lParam = line;
			return 0;
		}
		++line;
	}

	nmhdr.lvfi.lParam = LVNSCH_ERROR;
	return 0;
}

LRESULT CLogView::OnOdCacheHint(NMHDR* pnmh)
{
	auto& nmhdr = *reinterpret_cast<NMLVCACHEHINT*>(pnmh);
	nmhdr;
	return 0;
}

LRESULT CLogView::OnBeginDrag(NMHDR* pnmh)
{
	auto& nmhdr = *reinterpret_cast<NMLISTVIEW*>(pnmh);

	LVHITTESTINFO info;
	info.flags = 0;
	info.pt = nmhdr.ptAction;
	SubItemHitTest(&info);
	if ((info.flags & LVHT_ONITEM) == 0 || info.iSubItem != ColumnToSubItem(Column::Message))
	{
		SetMsgHandled(false);
		return 0;
	}

	StopTracking();

	SetCapture();
	m_dragStart = nmhdr.ptAction;
	m_dragEnd = nmhdr.ptAction;

	return 0;
}

void CLogView::OnViewClear(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	m_firstLine = m_logFile.Count();
	Clear();
}

void CLogView::OnViewSelectAll(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	SelectAll();
}

void CLogView::OnViewCopy(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	Copy();
}

void CLogView::OnViewScroll(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	SetScroll(!GetScroll());
}

void CLogView::OnViewTime(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	SetClockTime(!GetClockTime());
}

void CLogView::OnViewHideHighlight(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	SetHighlightText();
	StopScrolling();
}

void CLogView::OnViewFindNext(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	if (!m_highlightText.empty())
		FindNext(m_highlightText);
}

void CLogView::OnViewFindPrevious(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	if (!m_highlightText.empty())
		FindPrevious(m_highlightText);
}

bool CLogView::FindProcess(int direction)
{
	int begin = GetNextItem(-1, LVNI_FOCUSED);
	if (begin < 0)
		return false;

	auto processName = m_logFile[m_logLines[begin].line].processName;

	// Internal Compiler Error on VC2010:
//	int line = FindLine([processName, this](const LogLine& line) { return boost::iequals(m_logFile[line.line].processName, processName); }, direction);
	int line = FindLine([processName, this](const LogLine& line) { return m_logFile[line.line].processName == processName; }, direction);
	if (line < 0 || line == begin)
		return false;

	StopTracking();
	ScrollToIndex(line, true);
	return true;
}

void CLogView::OnViewNextProcess(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	FindProcess(+1);
}

void CLogView::OnViewPreviousProcess(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	FindProcess(-1);
}

COLORREF HsvToRgb(double h, double s, double v)
{
	int hi = floor_to<int>(h*6);
	double f = h*6 - hi;
	double p = v * (1 - s);
	double q = v * (1 - f*s);
	double t = v * (1 - (1 - f) * s);
	switch (hi)
	{
	case 0: return RGB(floor_to<int>(v*256), floor_to<int>(t*256), floor_to<int>(p*256));
	case 1: return RGB(floor_to<int>(q*256), floor_to<int>(v*256), floor_to<int>(p*256));
	case 2: return RGB(floor_to<int>(p*256), floor_to<int>(v*256), floor_to<int>(t*256));
	case 3: return RGB(floor_to<int>(p*256), floor_to<int>(q*256), floor_to<int>(v*256));
	case 4: return RGB(floor_to<int>(t*256), floor_to<int>(p*256), floor_to<int>(v*256));
	case 5: return RGB(floor_to<int>(v*256), floor_to<int>(p*256), floor_to<int>(q*256));
	}
	return 0;
}

COLORREF GetRandomColor(double s, double v)
{
	// use golden ratio
	static const double ratio = (1 + std::sqrt(5.))/2 - 1;
	static double h = static_cast<double>(std::rand()) / RAND_MAX;

	h += ratio;
	if (h > 1)
		h = h - 1;
	return HsvToRgb(h, s, v);
}

COLORREF GetRandomBackColor()
{
	return GetRandomColor(0.5, 0.95);
}

COLORREF GetRandomTextColor()
{
	return GetRandomColor(0.9, 0.7);
}

void CLogView::AddProcessFilter(FilterType::type filterType, COLORREF bgColor, COLORREF fgColor)
{
	int item = GetNextItem(-1, LVIS_FOCUSED);
	if (item < 0)
		return;

	const auto& name = m_logFile[m_logLines[item].line].processName;
	m_filter.processFilters.push_back(ProcessFilter(Str(name), 0, MatchType::Simple, filterType, bgColor, fgColor));
	ApplyFilters();
}

void CLogView::OnViewProcessHighlight(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	AddProcessFilter(FilterType::Highlight, GetRandomBackColor());
}

void CLogView::OnViewProcessExclude(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	AddProcessFilter(FilterType::Exclude);
}

void CLogView::OnViewProcessToken(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	AddProcessFilter(FilterType::Token, RGB(255, 255, 255), GetRandomTextColor());
}

void CLogView::OnViewProcessTrack(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	AddProcessFilter(FilterType::Track, GetRandomBackColor());
}

void CLogView::OnViewProcessOnce(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	AddProcessFilter(FilterType::Once, GetRandomBackColor());
}

void CLogView::AddMessageFilter(FilterType::type filterType, COLORREF bgColor, COLORREF fgColor)
{
	if (m_highlightText.empty())
		return;

	m_filter.messageFilters.push_back(MessageFilter(Str(m_highlightText), MatchType::Simple, filterType, bgColor, fgColor));
	ApplyFilters();
}

void CLogView::OnViewFilterHighlight(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	AddMessageFilter(FilterType::Highlight, GetRandomBackColor());
}

void CLogView::OnViewFilterExclude(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	AddMessageFilter(FilterType::Exclude);
}

void CLogView::OnViewFilterToken(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	AddMessageFilter(FilterType::Token, RGB(255, 255, 255), GetRandomTextColor());
}

void CLogView::OnViewFilterTrack(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	AddMessageFilter(FilterType::Track, GetRandomBackColor());
}

void CLogView::OnViewFilterOnce(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	AddMessageFilter(FilterType::Once, GetRandomBackColor());
}

bool CLogView::GetBookmark() const
{
	int item = GetNextItem(-1, LVIS_FOCUSED);
	return item >= 0 && m_logLines[item].bookmark;
}

void CLogView::ToggleBookmark(int iItem)
{
	m_logLines[iItem].bookmark = !m_logLines[iItem].bookmark;
	auto rect = GetSubItemRect(iItem, 0, LVIR_BOUNDS);
	InvalidateRect(&rect);
}

void CLogView::OnViewBookmark(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	int item = GetNextItem(-1, LVIS_FOCUSED);
	if (item < 0)
		return;

	ToggleBookmark(item);
}

void CLogView::FindBookmark(int direction)
{
	int line = FindLine([](const LogLine& line) { return line.bookmark; }, direction);
	if (line >= 0)
		ScrollToIndex(line, false);
}

void CLogView::OnViewNextBookmark(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	FindBookmark(+1);
}

void CLogView::OnViewPreviousBookmark(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	FindBookmark(-1);
}

void CLogView::OnViewClearBookmarks(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	for (auto it = m_logLines.begin(); it != m_logLines.end(); ++it)
		it->bookmark = false;
	Invalidate();
}

void CLogView::DoPaint(CDCHandle dc, const RECT& rcClip)
{
	dc.FillSolidRect(&rcClip, GetSysColor(COLOR_WINDOW));
 
	DefWindowProc(WM_PAINT, reinterpret_cast<WPARAM>(dc.m_hDC), 0);
}

std::wstring CLogView::GetName() const
{
	return m_name;
}

void CLogView::SetName(const std::wstring& name)
{
	m_name = name;
}

void CLogView::SetFont(HFONT hFont)
{
	CListViewCtrl::SetFont(hFont);
	GetHeader().Invalidate();

	// Trigger WM_MEASUREPOS
	// See: http://www.codeproject.com/Articles/1401/Changing-Row-Height-in-an-owner-drawn-Control
	CRect rect;
	GetWindowRect(&rect);
	WINDOWPOS wp;
	wp.hwnd = *this;
	wp.cx = rect.Width();
	wp.cy = rect.Height();
	wp.flags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER;
	SendMessage(WM_WINDOWPOSCHANGED, 0, reinterpret_cast<LPARAM>(&wp));
}

bool CLogView::GetScroll() const
{
	return m_autoScrollDown;
}

void CLogView::SetScroll(bool enable)
{
	m_autoScrollDown = enable;
	if (enable)
		ScrollDown();
}

void CLogView::Clear()
{
	SetItemCount(0);
	m_dirty = false;
	m_logLines.clear();
	m_highlightText.clear();
	m_autoScrollDown = true;
	ResetFilters();
}

int CLogView::GetFocusLine() const
{
	int item = GetNextItem(-1, LVNI_FOCUSED);
	if (item < 0)
		return -1;

	return m_logLines[item].line;
}

void CLogView::SetFocusLine(int line)
{
	auto it = std::upper_bound(m_logLines.begin(), m_logLines.end(), line, [](int line, const LogLine& logLine) { return line < logLine.line; });
	ScrollToIndex(it - m_logLines.begin() - 1, false);
}

void CLogView::Add(int line, const Message& msg)
{
	if (!IsIncluded(msg))
		return;

	m_dirty = true;
	++m_addedLines;
	int viewline = m_logLines.size();

	m_logLines.push_back(LogLine(line));
	if (m_autoScrollDown && IsStop(msg))
	{
		m_stop = [this, viewline] ()
		{
			StopScrolling();
			ScrollToIndex(viewline, true);
		};
		return;
	}

	if (IsTrack(msg))
	{
		printf("found: trackitem at line: %d, %s\n", viewline+1, msg.text);
		m_autoScrollDown = false;
		m_track = [this, viewline] () 
		{ 
			return ScrollToIndex(viewline, true);
		};
	}
}

void CLogView::BeginUpdate()
{
	m_addedLines = 0;
}

int CLogView::EndUpdate()
{
	if (m_dirty)
	{
		SetItemCountEx(m_logLines.size(), LVSICF_NOSCROLL);
		if (m_autoScrollDown)
			ScrollDown();

		m_dirty = false;
	}

	if (m_stop) 
	{
		m_stop();
		m_stop = 0;
	}
	if (m_track) 
	{
		if (m_track())
		{
			// nolonger track item after it has correctly centered
			StopTracking();
		}
	}
	return m_addedLines;
}

void CLogView::StopTracking()
{
	m_track = 0;
}

void CLogView::StopScrolling()
{
	m_autoScrollDown = false;
	for (auto it = m_filter.messageFilters.begin(); it != m_filter.messageFilters.end(); ++it)
	{
		if (it->filterType == FilterType::Track)
		{
			it->enable = false;
		}
	}
	for (auto it = m_filter.processFilters.begin(); it != m_filter.processFilters.end(); ++it)
	{
		if (it->filterType == FilterType::Track)
		{
			it->enable = false;
		}
	}
	StopTracking();
}

void CLogView::ClearSelection()
{
	int item = -1;
	for (;;)
	{
		item = GetNextItem(item, LVNI_SELECTED);
		if (item < 0)
			break;
		SetItemState(item, 0, LVIS_SELECTED);
	}
}

// returns false if centering was requested but not executed
//         because there where not enough lines below the requested index
//		   and it can be usefull to call ScrollToIndex again when more lines are available
bool CLogView::ScrollToIndex(int index, bool center)
{
	if (index < 0 || index >= static_cast<int>(m_logLines.size()))
		return true;

	ClearSelection();
	SetItemState(index, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);

	int paddingLines = GetCountPerPage() / 2;
	int minTopIndex = std::max(0, index - paddingLines);

	// this ensures the line is visible and not at the top of the view 
	// if it does not need to be, also when coming from a higher index
	EnsureVisible(minTopIndex, false);
	EnsureVisible(index, false);
	
	if (index > paddingLines)
	{
		// if there are more items above the index then half a page, then centering may be possible.
		if (center)
		{
			int maxBottomIndex = std::min<int>(m_logLines.size() - 1, index + paddingLines);
			EnsureVisible(maxBottomIndex, false);
			return (maxBottomIndex == (index + paddingLines));
		}
	}
	return true;
}

void CLogView::ScrollDown()
{
	ScrollToIndex(m_logLines.size() - 1, false);
}

bool CLogView::GetClockTime() const
{
	return m_clockTime;
}

void CLogView::SetClockTime(bool clockTime)
{
	m_clockTime = clockTime;
	Invalidate(false);
}

void CLogView::SelectAll()
{
	int lines = GetItemCount();
	for (int i = 0; i < lines; ++i)
		SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
}

std::wstring CLogView::GetItemWText(int item, int subItem) const
{
	CComBSTR bstr;
	GetItemText(item, subItem, bstr.m_str);
	return std::wstring(bstr.m_str, bstr.m_str + bstr.Length());
}

std::string CLogView::GetItemText(int item) const
{
	return stringbuilder() <<
		GetColumnText(item, Column::Line) << "\t" <<
		GetColumnText(item, Column::Time) << "\t" <<
		GetColumnText(item, Column::Pid) << "\t" <<
		GetColumnText(item, Column::Process) << "\t" <<
		GetColumnText(item, Column::Message);
}

void CLogView::Copy()
{
	std::ostringstream ss;

	if (!m_highlightText.empty())
	{
		ss << Str(m_highlightText);
	}
	else
	{
		int item = -1;
		while ((item = GetNextItem(item, LVNI_ALL | LVNI_SELECTED)) >= 0)
			ss << GetItemText(item) << "\n";
	}
	const std::string& str = ss.str();

	HGlobal hdst(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, str.size() + 1));
	GlobalLock<char> lock(hdst);
	std::copy(str.begin(), str.end(), stdext::checked_array_iterator<char*>(lock.Ptr(), str.size()));
	lock.Ptr()[str.size()] = '\0';
	if (OpenClipboard())
	{
		EmptyClipboard();
		SetClipboardData(CF_TEXT, hdst.release());
		CloseClipboard();
	}
}

std::wstring CLogView::GetHighlightText() const
{
	return m_highlightText;
}

void CLogView::SetHighlightText(const std::wstring& text)
{
	m_highlightText = text;
	Invalidate(false);
}

template <typename Predicate>
int CLogView::FindLine(Predicate pred, int direction) const
{
	ScopedCursor cursor(::LoadCursor(nullptr, IDC_WAIT));

	int begin = std::max(GetNextItem(-1, LVNI_FOCUSED), 0);
	int line = begin;

	if (m_logLines.empty())
		return -1;

	do
	{
		line += direction;
		if (line < 0)
			line += m_logLines.size();
		if (line >= static_cast<int>(m_logLines.size()))
			line -= m_logLines.size();

		if (pred(m_logLines[line]))
			return line;
	}
	while (line != begin);

	return -1;
}

bool CLogView::Find(const std::string& text, int direction)
{
	StopTracking();

	int line = FindLine([text, this](const LogLine& line) { return Contains(m_logFile[line.line].text, text); }, direction);
	if (line < 0)
		return false;

	bool sameLine = GetItemState(line, LVIS_FOCUSED) != 0;
	if (!sameLine)
		ScrollToIndex(line, true);

	auto wtext = WStr(text).str();
	if (sameLine && wtext == m_highlightText)
		return false;

	SetHighlightText(wtext);
	return true;
}

bool CLogView::FindNext(const std::wstring& text)
{
	return Find(Str(text).str(), +1);
}

bool CLogView::FindPrevious(const std::wstring& text)
{
	return Find(Str(text).str(), -1);
}

void CLogView::LoadSettings(CRegKey& reg)
{
	SetClockTime(RegGetDWORDValue(reg, L"ClockTime", 1) != 0);

	for (int i = 0; i < Column::Count; ++i)
	{
		CRegKey regColumn;
		if (regColumn.Open(reg, WStr(wstringbuilder() << L"Columns\\Column" << i)) != ERROR_SUCCESS)
			break;

		auto& column = m_columns[i];
		column.enable = RegGetDWORDValue(regColumn, L"Enable", column.enable) != 0;
		column.column.cx = RegGetDWORDValue(regColumn, L"Width", column.column.cx);
		column.column.iOrder = RegGetDWORDValue(regColumn, L"Order", column.column.iOrder);
	}

	for (int i = 0; ; ++i)
	{
		CRegKey regFilter;
		if (regFilter.Open(reg, WStr(wstringbuilder() << L"Filters\\Filter" << i)) != ERROR_SUCCESS)
			break;

		m_filter.messageFilters.push_back(MessageFilter(
			Str(RegGetStringValue(regFilter)),
			IntToMatchType(RegGetDWORDValue(regFilter, L"MatchType", MatchType::Regex)),
			IntToFilterType(RegGetDWORDValue(regFilter, L"Type")),
			RegGetDWORDValue(regFilter, L"BgColor", RGB(255, 255, 255)),
			RegGetDWORDValue(regFilter, L"FgColor", RGB(0, 0, 0)),
			RegGetDWORDValue(regFilter, L"Enable", 1) != 0));
	}

	for (size_t i = 0; ; ++i)
	{
		CRegKey regFilter;
		if (regFilter.Open(reg, WStr(wstringbuilder() << L"ProcessFilters\\Filter" << i)) != ERROR_SUCCESS)
			break;

		m_filter.processFilters.push_back(ProcessFilter(
			Str(RegGetStringValue(regFilter)),
			0,
			IntToMatchType(RegGetDWORDValue(regFilter, L"MatchType", MatchType::Regex)),
			IntToFilterType(RegGetDWORDValue(regFilter, L"Type")),
			RegGetDWORDValue(regFilter, L"BgColor", RGB(255, 255, 255)),
			RegGetDWORDValue(regFilter, L"FgColor", RGB(0, 0, 0)),
			RegGetDWORDValue(regFilter, L"Enable", 1) != 0));
	}

	ApplyFilters();
	UpdateColumns();
}

void CLogView::SaveSettings(CRegKey& reg)
{
	UpdateColumnInfo();

	reg.SetDWORDValue(L"ClockTime", GetClockTime());

	int i = 0;
	for (auto it = m_columns.begin(); it != m_columns.end(); ++it, ++i)
	{
		CRegKey regFilter;
		regFilter.Create(reg, WStr(wstringbuilder() << L"Columns\\Column" << i));
		regFilter.SetDWORDValue(L"Enable", it->enable);
		regFilter.SetDWORDValue(L"Width", it->column.cx);
		regFilter.SetDWORDValue(L"Order", it->column.iOrder);
	}

	i = 0;
	for (auto it = m_filter.messageFilters.begin(); it != m_filter.messageFilters.end(); ++it, ++i)
	{
		CRegKey regFilter;
		regFilter.Create(reg, WStr(wstringbuilder() << L"Filters\\Filter" << i));
		regFilter.SetStringValue(L"", WStr(it->text.c_str()));
		regFilter.SetDWORDValue(L"MatchType", MatchTypeToInt(it->matchType));
		regFilter.SetDWORDValue(L"FilterType", FilterTypeToInt(it->filterType));
		regFilter.SetDWORDValue(L"Type", FilterTypeToInt(it->filterType));
		regFilter.SetDWORDValue(L"BgColor", it->bgColor);
		regFilter.SetDWORDValue(L"FgColor", it->fgColor);
		regFilter.SetDWORDValue(L"Enable", it->enable);
	}

	i = 0;
	for (auto it = m_filter.processFilters.begin(); it != m_filter.processFilters.end(); ++it, ++i)
	{
		CRegKey regFilter;
		regFilter.Create(reg, WStr(wstringbuilder() << L"ProcessFilters\\Filter" << i));
		regFilter.SetStringValue(L"", WStr(it->text.c_str()));
		regFilter.SetDWORDValue(L"MatchType", MatchTypeToInt(it->matchType));
		regFilter.SetDWORDValue(L"FilterType", FilterTypeToInt(it->filterType));
		regFilter.SetDWORDValue(L"Type", FilterTypeToInt(it->filterType));
		regFilter.SetDWORDValue(L"BgColor", it->bgColor);
		regFilter.SetDWORDValue(L"FgColor", it->fgColor);
		regFilter.SetDWORDValue(L"Enable", it->enable);
	}
}

void CLogView::Save(const std::wstring& fileName) const
{
	std::ofstream file(fileName);

	int lines = GetItemCount();
	for (int i = 0; i < lines; ++i)
		file << GetItemText(i) << "\n";

	file.close();
	if (!file)
		ThrowLastError(fileName);
}

LogFilter CLogView::GetFilters() const
{
	return m_filter;
}

void CLogView::SetFilters(const LogFilter& filter)
{
	StopTracking();
	m_filter = filter;
	ApplyFilters();
}

std::vector<int> CLogView::GetBookmarks() const
{
	std::vector<int> bookmarks;
	for (auto it = m_logLines.begin(); it != m_logLines.end(); ++it)
		if (it->bookmark)
			bookmarks.push_back(it->line);
	return bookmarks;
}

void CLogView::ResetFilters()
{
	for (auto it = m_filter.messageFilters.begin(); it != m_filter.messageFilters.end(); ++it)
	{
		if (it->filterType == FilterType::Once)
		{
			it->matchCount = 0;
		}
	}
	for (auto it = m_filter.processFilters.begin(); it != m_filter.processFilters.end(); ++it)
	{
		if (it->filterType == FilterType::Once)
		{
			it->matchCount = 0;
		}
	}	
}

void CLogView::ApplyFilters()
{
	ResetFilters();
	ClearSelection();

	int focusItem = GetNextItem(-1, LVIS_FOCUSED);
	SetItemState(focusItem, 0, LVIS_FOCUSED);
	int focusLine = focusItem < 0 ? -1 : m_logLines[focusItem].line;

	auto bookmarks = GetBookmarks();
	auto itBookmark = bookmarks.begin();

	std::vector<LogLine> logLines;
	logLines.reserve(m_logLines.size());
	int count = m_logFile.Count();
	int line = m_firstLine;
	int item = 0;
	focusItem = -1;
	while (line < count)
	{
		if (IsIncluded(m_logFile[line]))
		{
			logLines.push_back(LogLine(line));
			if (itBookmark != bookmarks.end() && *itBookmark == line)
			{
				logLines.back().bookmark = true;
				++itBookmark;
			}

			if (line <= focusLine)
				focusItem = item;

			++item;
		}
		++line;
	}

	m_logLines.swap(logLines);
	SetItemCountEx(m_logLines.size(), LVSICF_NOSCROLL);
	ScrollToIndex(focusItem, false);
	SetItemState(focusItem, LVIS_FOCUSED, LVIS_FOCUSED);
	EndUpdate();
}

bool FilterSupportsColor(FilterType::type value)
{
	switch (value)
	{
	case FilterType::Include:
	case FilterType::Highlight:
	case FilterType::Track:
	case FilterType::Stop:
	case FilterType::Once:
		return true;
	}
	return false;
}

TextColor CLogView::GetTextColor(const Message& msg) const
{
	for (auto it = m_filter.messageFilters.begin(); it != m_filter.messageFilters.end(); ++it)
	{
		if (it->enable && FilterSupportsColor(it->filterType) && std::regex_search(msg.text, it->re))
			return TextColor(it->bgColor, it->fgColor);
	}

	for (auto it = m_filter.processFilters.begin(); it != m_filter.processFilters.end(); ++it)
	{
		if (it->enable && FilterSupportsColor(it->filterType) && std::regex_search(msg.processName, it->re))
			return TextColor(it->bgColor, it->fgColor);
	}

	return TextColor(GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT));
}

bool CLogView::IsMessageIncluded(const std::string& message)
{
	bool included = false;
	bool includeFilterPresent = false;
	for (auto it = m_filter.messageFilters.begin(); it != m_filter.messageFilters.end(); ++it)
	{
		if (!it->enable)
			continue;
		if (it->filterType == FilterType::Include)
		{
			includeFilterPresent = true;
			included |= std::regex_search(message, it->re);
		}
	}

	if (!includeFilterPresent) 
		included = true;

	for (auto it = m_filter.messageFilters.begin(); it != m_filter.messageFilters.end(); ++it)
	{
		if (!it->enable)
			continue;
		if (it->filterType == FilterType::Exclude && std::regex_search(message, it->re))
			return false;
	}

	for (auto it = m_filter.messageFilters.begin(); it != m_filter.messageFilters.end(); ++it)
	{
		if (!it->enable)
			continue;
		if (it->filterType == FilterType::Once && std::regex_search(message, it->re))
			return ++it->matchCount == 1;
	}
	return included;
}

bool CLogView::IsProcessIncluded(const std::string& process)
{
	bool included = false;
	bool includeFilterPresent = false;
	for (auto it = m_filter.processFilters.begin(); it != m_filter.processFilters.end(); ++it)
	{
		if (!it->enable)
			continue;
		if (it->filterType == FilterType::Include)
		{
			includeFilterPresent = true;
			included |= std::regex_search(process, it->re);
		}
	}

	if (!includeFilterPresent) 
		included = true;

	for (auto it = m_filter.processFilters.begin(); it != m_filter.processFilters.end(); ++it)
	{
		if (!it->enable)
			continue;
		if (it->filterType == FilterType::Exclude && std::regex_search(process, it->re))
			return false;
	}

	for (auto it = m_filter.processFilters.begin(); it != m_filter.processFilters.end(); ++it)
	{
		if (!it->enable)
			continue;
		if (it->filterType == FilterType::Once && std::regex_search(process, it->re))
			return ++it->matchCount == 1;
	}
	return included;
}

bool CLogView::IsIncluded(const Message& msg)
{
	return IsMessageIncluded(msg.text) && IsProcessIncluded(msg.processName);
}

bool CLogView::IsStop(const Message& msg) const
{
	for (auto it = m_filter.messageFilters.begin(); it != m_filter.messageFilters.end(); ++it)
	{
		if (!it->enable)
			continue;

		if (it->filterType == FilterType::Stop && std::regex_search(msg.text, it->re))
			return true;
	}

	for (auto it = m_filter.processFilters.begin(); it != m_filter.processFilters.end(); ++it)
	{
		if (!it->enable)
			continue;

		if (it->filterType == FilterType::Stop && std::regex_search(msg.processName, it->re))
			return true;
	}

	return false;
}

bool CLogView::IsTrack(const Message& msg) const
{
	for (auto it = m_filter.messageFilters.begin(); it != m_filter.messageFilters.end(); ++it)
	{
		if (!it->enable)
			continue;

		if (it->filterType == FilterType::Track && std::regex_search(msg.text, it->re))
			return true;
	}

	for (auto it = m_filter.processFilters.begin(); it != m_filter.processFilters.end(); ++it)
	{
		if (!it->enable)
			continue;

		if (it->filterType == FilterType::Track && std::regex_search(msg.processName, it->re))
			return true;
	}
	return false;
}

} // namespace debugviewpp 
} // namespace fusion
