#include "stdafx.h"
#include "PageCache.h"
#if TEST_MODEL
#include <assert.h>
#endif
#include "Book.h"
#include "EpubBook.h"

PageCache::PageCache()
    : m_Text(NULL)
    , m_TextLength(0)
    , m_OnePageLineCount(0)
    , m_CurPageSize(0)
    , m_CurrentLine(0)
    , m_CurrentPos(NULL)
    , m_lineGap(NULL)
    , m_InternalBorder(NULL)
    , m_LeftLineCount(NULL)
    , m_WordWrap(NULL)
    , m_LineIndent(NULL)
#if ENABLE_TAG
    , m_tags(NULL)
#endif
{
    memset(&m_Rect, 0, sizeof(m_Rect));
    memset(&m_PageInfo, 0, sizeof(m_PageInfo));
}


PageCache::~PageCache()
{
    RemoveAllLine(TRUE);
}

#if ENABLE_TAG
void PageCache::Setting(HWND hWnd, INT *pos, INT* cg, INT *lg, INT *lc, INT *word_wrap, INT *indent, RECT *ib, tagitem_t *tags)
#else
void PageCache::Setting(HWND hWnd, INT *pos, INT* cg, INT *lg, INT *lc, INT *word_wrap, INT *indent, RECT *ib)
#endif
{
    m_CurrentPos = pos;
    m_charGap = cg;
    m_lineGap = lg;
    m_LeftLineCount = lc;
    m_InternalBorder = ib;
    m_WordWrap = word_wrap;
    m_LineIndent = indent;
    // fixed bug for txt modified
    if (*m_CurrentPos < 0 || *m_CurrentPos >= m_TextLength)
        *m_CurrentPos = 0;
#if ENABLE_TAG
    m_tags = tags;
#endif
    RemoveAllLine(TRUE);
    InvalidateRect(hWnd, &m_Rect, FALSE);
}

void PageCache::SetRect(RECT *rect)
{
    BOOL bNeedClear = FALSE;
    if (0 == memcmp(rect, &m_Rect, sizeof(RECT)))
        return;

    if (m_Rect.right - m_Rect.left != rect->right - rect->left)
        bNeedClear = TRUE;

    memcpy(&m_Rect, rect, sizeof(RECT));
    if (bNeedClear)
        RemoveAllLine();
}

void PageCache::Reset(HWND hWnd, BOOL redraw)
{
    RemoveAllLine();
    if (redraw)
        InvalidateRect(hWnd, &m_Rect, FALSE);
}

void PageCache::ReDraw(HWND hWnd)
{
    InvalidateRect(hWnd, &m_Rect, FALSE);
}

void PageCache::PageUp(HWND hWnd)
{
    return LineUp(hWnd, m_OnePageLineCount - (*m_LeftLineCount));
}

void PageCache::PageDown(HWND hWnd)
{
    return LineDown(hWnd, m_OnePageLineCount - (*m_LeftLineCount));
}

void PageCache::LineUp(HWND hWnd, INT n)
{
    if (!OnLineUpDownEvent(hWnd, TRUE, n))
        return;
    if (!IsValid())
        return;
    if (n == 0)
        return;
    if (m_PageInfo.line_size <= 0)
        return;
    if ((*m_CurrentPos) == 0) // already at the first line of file
        return;

    m_CurrentLine -= n;
    if (GetCover())
    {
        if ((*m_CurrentPos) == 1 && m_PageInfo.line_info[0].start == 0)
            m_CurrentLine = 0;
        else if (m_CurrentLine < 1 && m_PageInfo.line_info[0].start == 0)
            m_CurrentLine = 1;
    }
    else
    {
        if (m_CurrentLine < 0 && m_PageInfo.line_info[0].start == 0) // n is out of range
            m_CurrentLine = 0;
    }
    
    InvalidateRect(hWnd, &m_Rect, FALSE);
}

void PageCache::LineDown(HWND hWnd, INT n)
{
    if (!OnLineUpDownEvent(hWnd, FALSE, n))
        return;
    if (!IsValid())
        return;
    if (n == 0)
        return;
    if ((*m_CurrentPos) + m_CurPageSize == m_TextLength) // already show the last line of file
        return;
    
    m_CurrentLine += n;
    InvalidateRect(hWnd, &m_Rect, FALSE);
}

Bitmap * PageCache::GetCover(void)
{
    Book *book = NULL;
    EpubBook *epub = NULL;
    book = dynamic_cast<Book *>(this);
    if (!book)
        return NULL;
    if (book_epub != book->GetBookType())
        return NULL;
    epub = dynamic_cast<EpubBook *>(book);
    if (!epub)
        return NULL;

    return epub->GetCoverImage();
}

BOOL PageCache::DrawCover(HDC hdc)
{
    Gdiplus::Bitmap *cover = NULL;
    Gdiplus::Graphics *g = NULL;
    int w,h,bw,bh;
    double d,bd;
    Rect src;
    Rect dst;

    if (m_PageInfo.line_size <= 0 || m_PageInfo.line_info[m_CurrentLine].start != 0)
        return FALSE;

    cover = GetCover();
    if (!cover)
        return FALSE;
    (*m_CurrentPos) = 0;
    m_CurPageSize = 1; // 1 wchar_t for cover
    m_CurrentLine = 0;
    m_OnePageLineCount = 1;

    // calc image rect
    w = m_Rect.right - m_Rect.left;
    h = m_Rect.bottom - m_Rect.top;
    bw = cover->GetWidth();
    bh = cover->GetHeight();
    d = ((double)w)/h;
    bd = ((double)bw)/bh;
    if (bd > d)
    {
        // image is too wide
        bw = w;
        bh = (int)(bw / bd);
    }
    else if (bd < d)
    {
        // image is too high
        bh = h;
        bw = (int)(bd * bh);
    }
    src.X = 0;
    src.Y = 0;
    src.Width = cover->GetWidth();
    src.Height = cover->GetHeight();
    dst.X = (w - bw)/2;
    dst.Y = (h - bh)/2;
    dst.Width = bw;
    dst.Height = bh;
    g = new Gdiplus::Graphics(hdc);
    g->SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g->DrawImage(cover, dst, src.X, src.Y, src.Width, src.Height, UnitPixel);
    delete g;
    return TRUE;
}

void PageCache::DrawPage(HWND hWnd, HDC hdc)
{
    int i;
    int h;
    line_info_t *line;
    RECT rect;
    int j;
    SIZE sz = { 0 };
#if ENABLE_TAG
	HFONT tagfonts[MAX_TAG_COUNT] = {0};
#endif	

    if (!IsValid())
        return;

    if (!OnDrawPageEvent(hWnd))
        return;

#if ENABLE_TAG
    for (i=0; i<MAX_TAG_COUNT; i++)
    {
        if (m_tags[i].enable && m_tags[i].keyword[0])
        {
            tagfonts[i] = CreateFontIndirect(&m_tags[i].font);
        }
    }
    h = GetLineHeight(hdc, tagfonts);
#else
    h = GetLineHeight(hdc);
#endif
    m_OnePageLineCount = (m_Rect.bottom - m_Rect.top + (*m_lineGap) - (m_InternalBorder->top + m_InternalBorder->bottom)) / h;

    if (m_PageInfo.line_size == 0 || m_CurrentLine < 0 
        || (m_PageInfo.line_info[m_PageInfo.line_size - 1].start + m_PageInfo.line_info[m_PageInfo.line_size - 1].length != m_TextLength && m_CurrentLine + m_OnePageLineCount >= m_PageInfo.line_size))
    {
#if ENABLE_TAG
        LoadPageInfo(hdc, m_Rect.right - m_Rect.left - (m_InternalBorder->left + m_InternalBorder->right), m_OnePageLineCount, tagfonts);
#else
        LoadPageInfo(hdc, m_Rect.right - m_Rect.left - (m_InternalBorder->left + m_InternalBorder->right), m_OnePageLineCount);
#endif
    }
    if (m_PageInfo.line_size == 0) // fixed bug
        return;

    UnitTest1();
    UnitTest2();

    if (DrawCover(hdc))
#if ENABLE_TAG
        goto end;
#else
        return;
#endif

    memcpy(&rect, &m_Rect, sizeof(RECT));
    m_CurPageSize = 0;
    rect.left = m_InternalBorder->left;
    rect.top = m_InternalBorder->top;
    for (i = 0; i < m_OnePageLineCount && m_CurrentLine + i < m_PageInfo.line_size; i++)
    {
        line = &m_PageInfo.line_info[m_CurrentLine + i];
        rect.bottom = rect.top + h;
        if (line->indent)
            rect.left = m_InternalBorder->left + GetIndentWidth(hdc);
        else
            rect.left = m_InternalBorder->left;
#if ENABLE_TAG
        for (j=0; j<line->length; j++)
        {
            int tagid = IsTag(j+line->start);
            rect.left += (*m_charGap) / 2;
            if (tagid >= 0 && tagid < MAX_TAG_COUNT)
            {
                COLORREF oldcolor = SetBkColor(hdc, m_tags[tagid].bg_color);
                COLORREF oldfontcolor = SetTextColor(hdc, m_tags[tagid].font_color);
                HFONT oldfont = (HFONT)SelectObject(hdc, tagfonts[tagid]);
                int oldmode = SetBkMode(hdc, OPAQUE);

                TextOut(hdc, rect.left, rect.top, m_Text + j + line->start, 1);
                GetTextExtentPoint32(hdc, m_Text+j+line->start, 1, &sz);
                SetBkColor(hdc, oldcolor);
                SetTextColor(hdc, oldfontcolor);
                SelectObject(hdc, oldfont);
                SetBkMode(hdc, oldmode);
            }
            else
            {
                TextOut(hdc, rect.left, rect.top, m_Text + j + line->start, 1);
                GetTextExtentPoint32(hdc, m_Text+j+line->start, 1, &sz);
            }
            rect.left += sz.cx;
            rect.left += (*m_charGap) - ((*m_charGap) / 2);
        }
#else
        for (j = 0; j < line->length; j++)
        {
            GetTextExtentPoint32(hdc, m_Text + line->start + j, 1, &sz);
            rect.left += (*m_charGap) / 2;
            TextOut(hdc, rect.left, rect.top, m_Text + line->start + j, 1);
            rect.left += sz.cx;
            rect.left += (*m_charGap) - ((*m_charGap) / 2);
        }
#endif
        rect.top += h;
        m_CurPageSize += line->length;
    }
    (*m_CurrentPos) = m_PageInfo.line_info[m_CurrentLine].start;

#if ENABLE_TAG
end:
    for (i=0; i<MAX_TAG_COUNT; i++)
    {
        if (tagfonts[i])
            DeleteObject(tagfonts[i]);
    }
#endif
}

INT PageCache::GetCurPageSize(void)
{
    return m_CurPageSize;
}

INT PageCache::GetTextLength(void)
{
    return m_TextLength;
}

BOOL PageCache::IsFirstPage(void)
{
    if (!m_CurrentPos)
        return FALSE;
    return (*m_CurrentPos) == 0;
}

BOOL PageCache::IsLastPage(void)
{
    if (!m_CurrentPos)
        return FALSE;
    return (*m_CurrentPos) + m_CurPageSize == m_TextLength;
}

BOOL PageCache::IsCoverPage(void)
{
    if (!m_CurrentPos)
        return FALSE;
    if (!GetCover())
        return FALSE;
    return (*m_CurrentPos) == 0;
}

double PageCache::GetProgress(void)
{
    if (!m_CurrentPos)
        return 0.0f;
    if (m_TextLength == 0)
        return 0.0f;
    return (double)((*m_CurrentPos) + m_CurPageSize)*100.0/m_TextLength;
}

BOOL PageCache::GetCurPageText(TCHAR **text)
{
    int newlinecount = 0;
    TCHAR *c = NULL;
    int i,j;

    if (m_CurPageSize > 0)
    {
        for (i=0; i<m_CurPageSize; i++)
        {
            c = m_Text + ((*m_CurrentPos) + i);
            if (*c == 0x0A)
            {
                newlinecount++;
            }
        }

        *text = (TCHAR *)malloc(sizeof(TCHAR) * (m_CurPageSize + newlinecount + 1));

        for (i=0,j=0; i<m_CurPageSize; i++)
        {
            c = m_Text + ((*m_CurrentPos) + i);
            if (*c == 0x0A)
            {
                (*text)[j++] = 0x0D;
            }
            (*text)[j++] = *(m_Text + ((*m_CurrentPos) + i));
        }
        (*text)[j] = 0;
        return TRUE;
    }
    return FALSE;
}

BOOL PageCache::SetCurPageText(HWND hWnd, TCHAR *dst_text)
{
    Book *book = NULL;
    TCHAR *src_text = NULL;
    TCHAR *text = NULL;
    int dst_len;
    int src_len;
    int len;

    book = dynamic_cast<Book *>(this);
    if (!book)
        return FALSE;

    if (GetCurPageText(&src_text))
    {
        if (0 == _tcscmp(dst_text, src_text))
        {
            return TRUE;
        }

        // format dest text
        src_len = m_CurPageSize;
        dst_len = _tcslen(dst_text);
        book->FormatText(dst_text, &dst_len, false);

        // change text
        len = m_TextLength - src_len + dst_len;
        text = (TCHAR *)malloc(sizeof(TCHAR) * (len+1));
        text[len] = 0;
        if ((*m_CurrentPos) > 0)
            memcpy(text, m_Text, sizeof(TCHAR) * (*m_CurrentPos));
        memcpy(text+(*m_CurrentPos), dst_text, sizeof(TCHAR) * dst_len);
        memcpy(text+(*m_CurrentPos)+dst_len, m_Text+(*m_CurrentPos)+m_CurPageSize, sizeof(TCHAR) * (m_TextLength-(*m_CurrentPos)-m_CurPageSize));
        free(m_Text);
        m_Text = text;
        m_TextLength = len;

        free(src_text);

        // save file
        if (!book->SaveBook(NULL))
            return FALSE;

        // update chapter
        if (!book->UpdateChapters(dst_len - src_len))
            return FALSE;

#if ENABLE_MD5
        // update md5
        book->UpdateMd5();
#endif

        // reset pageinfo
        Reset(hWnd, TRUE);

        return TRUE;
    }
    return FALSE;
}

#if ENABLE_TAG
LONG PageCache::GetLineHeight(HDC hdc, HFONT *tagfonts)
#else
LONG PageCache::GetLineHeight(HDC hdc)
#endif
{
    SIZE sz = { 0 };
#if ENABLE_TAG
    int i;
    LONG maxcy = 0;
    
	for (i=0; i<MAX_TAG_COUNT; i++)
    {
        if (tagfonts[i])
        {
            HFONT oldfont = (HFONT)SelectObject(hdc, tagfonts[i]);
            GetTextExtentPoint32(hdc, _T("AaBbYyZz"), 8, &sz);
            SelectObject(hdc, oldfont);

            if (sz.cy > maxcy)
            {
                maxcy = sz.cy;
            }
        }
    }

    GetTextExtentPoint32(hdc, _T("AaBbYyZz"), 8, &sz);
    if (sz.cy > maxcy)
    {
        maxcy = sz.cy;
    }
	if (!m_lineGap)
        return maxcy;
    return maxcy + (*m_lineGap);
#else
    GetTextExtentPoint32(hdc, _T("AaBbYyZz"), 8, &sz);
    if (!m_lineGap)
        return sz.cy;
    return sz.cy + (*m_lineGap);
#endif
}

INT PageCache::GetCahceUnitSize(HDC hdc, INT hcnt)
{
    SIZE sz = { 0 };
    INT wcnt;
    GetTextExtentPoint32(hdc, _T("."), 1, &sz);
    if (sz.cx == 0)
        sz.cx = 1;
    wcnt = (m_Rect.right - m_Rect.left) / (sz.cx + (*m_charGap));
    return wcnt * hcnt;
}

LONG PageCache::GetIndentWidth(HDC hdc)
{
    SIZE sz = { 0 };
    TCHAR buf[3] = { 0x3000, 0x3000, 0 };

    GetTextExtentPoint32(hdc, buf, 2, &sz);
    return sz.cx;
}

#define is_space_indent(c) (c == 0x20 || c == 0x3000 || c == 0xA0 || c == 0x09 || c == 0x0A || c == 0x0B || c == 0x0C /*|| c == 0x0D*/)

VOID PageCache::SetIndent(HDC hdc, INT index, BOOL* indent, LONG* width)
{
    *indent = FALSE;
    if (m_LineIndent && *m_LineIndent)
    {
        if (index >= 0 && index < m_TextLength && m_Text)
        {
            if (m_Text[index] == 0x0A
                && (index < m_TextLength - 1 && !is_space_indent(m_Text[index+1])))
            {
                *indent = TRUE;
                *width += GetIndentWidth(hdc);
            }
        }
    }
}

#define is_space(c) (c == 0x20 || c == 0x09 /*|| c == 0x0A*/ || c == 0x0B || c == 0x0C /*|| c == 0x0D*/)
#define is_minus(c) (c == 0x2D /* - */)

#if ENABLE_TAG
void PageCache::LoadPageInfo(HDC hdc, INT maxw, INT hcnt, HFONT *tagfonts)
#else
void PageCache::LoadPageInfo(HDC hdc, INT maxw, INT hcnt)
#endif
{
    INT MAX_FIND_SIZE = GetCahceUnitSize(hdc, hcnt);
    INT pos1, pos2, pos3, pos4;
    INT i;
    INT start;
    INT length;
    LONG width;
    INT index;
    SIZE sz = { 0 };
    BOOL flag = TRUE;
    int word_start_pos;
    int word_width;
    BOOL indent = FALSE;

    // pageup/lineup:         [pos1, pos2)
    // already in cache page: [pos2, pos3)
    // pagedown/linedown:     [pos3, pos4)

    // set startpos
    if (m_PageInfo.line_size > 0)
        pos2 = m_PageInfo.line_info[0].start;
    else
        pos2 = (*m_CurrentPos);
    pos1 = pos2 <= MAX_FIND_SIZE ? 0 : pos2 - MAX_FIND_SIZE;

    if (m_PageInfo.line_size > 0)
        pos3 = m_PageInfo.line_info[m_PageInfo.line_size - 1].start + m_PageInfo.line_info[m_PageInfo.line_size - 1].length;
    else
        pos3 = (*m_CurrentPos);
    //pos4 = pos3 + MAX_FIND_SIZE >= m_TextLength ? m_TextLength : pos3 + MAX_FIND_SIZE; // no use for FAST_MODEL
    pos4 = m_TextLength;

    if (m_CurrentLine < 0)
    {
        flag = FALSE;

        // [pos1, pos2)
        start = pos1;
        length = 0;
        width = 0;
        index = 0;
        SetIndent(hdc, pos1 - 1, &indent, &width);
        if (*m_WordWrap)
        {
            word_start_pos = start;
            word_width = 0;
        }
        for (i = pos1; i < pos2; i++)
        {
            // new line
#if 0 // because do FormatText
            if (m_Text[i] == 0x0D && m_Text[i + 1] == 0x0A) // the last char in data is 0x00, So it won��t cross the memory.
            {
                i++;
                length++;
            }
#endif
            if (m_Text[i] == 0x0A)
            {
                length++;
                AddLine(start, length, indent, index++);
                start = i + 1;
                length = 0;
                width = 0;
                SetIndent(hdc, i, &indent, &width);
                if (*m_WordWrap)
                {
                    word_start_pos = start;
                    word_width = 0;
                }
                continue;
            }

            // calc char width
#if ENABLE_TAG
            int tagid = IsTag(i);
            if (tagid >= 0 && tagid < MAX_TAG_COUNT)
            {
                if (tagfonts[tagid])
                {
                    HFONT oldfont = (HFONT)SelectObject(hdc, tagfonts[tagid]);
                    GetTextExtentPoint32(hdc, &m_Text[i], 1, &sz);
                    SelectObject(hdc, oldfont);
                }
            }
            else
            {
                GetTextExtentPoint32(hdc, &m_Text[i], 1, &sz);
            }
#else
            GetTextExtentPoint32(hdc, &m_Text[i], 1, &sz);
#endif

            if (*m_WordWrap)
            {
                if (is_space(m_Text[i]))
                {
                    word_start_pos = i + 1;
                    word_width = 0;
                }
                else if (is_minus(m_Text[i]) /*&& !is_minus(m_Text[i-1]) && !is_minus(m_Text[i + 1])*/)
                {
                    word_start_pos = i + 1;
                    word_width = 0;
                }
                else
                {
                    word_width += sz.cx + (*m_charGap);
                }
            }

            width += sz.cx + (*m_charGap);
            if (width > maxw)
            {
                if (*m_WordWrap)
                {
                    if (is_space(m_Text[i])) // add left space
                    {
                        memset(&sz, 0, sizeof(sz));
                        for (int j = i; j < pos2; j++)
                        {
                            if (is_space(m_Text[j]))
                            {
                                length++;
                                i++;
                                continue;
                            }
                            GetTextExtentPoint32(hdc, &m_Text[i], 1, &sz);
                            break;
                        }

                        // add line
                        AddLine(start, length, indent, index++);
                        start = i;
                        length = i == pos2 ? 0 : 1;
                        width = i == pos2 ? 0 : sz.cx + (*m_charGap);
                        word_start_pos = start;
                        word_width = width;
                        SetIndent(hdc, i, &indent, &width);
                    }
                    else
                    {
                        if (word_start_pos == start) // too long word
                        {
                            // do nothing

                            // add line
                            AddLine(start, length, indent, index++);
                            start = i;
                            length = 1;
                            width = sz.cx + (*m_charGap);
                            word_start_pos = start;
                            word_width = width;
                            SetIndent(hdc, i, &indent, &width);
                        }
                        else
                        {
                            // move current word to next line
                            length -= i - word_start_pos;

                            // add line
                            AddLine(start, length, indent, index++);
                            start = word_start_pos;
                            length = i - word_start_pos + 1;
                            width = word_width;
                            word_start_pos = start;
                            word_width = width;
                            SetIndent(hdc, i, &indent, &width);

                            if (width > maxw) // goto -> [too long word]
                            {
                                // add line
                                length--;
                                AddLine(start, length, indent, index++);
                                start = i;
                                length = 1;
                                width = sz.cx + (*m_charGap);
                                word_start_pos = start;
                                word_width = width;
                                SetIndent(hdc, i, &indent, &width);
                            }
                        }
                    }
                }
                else
                {
                    AddLine(start, length, indent, index++);
                    start = i;
                    length = 1;
                    width = sz.cx + (*m_charGap);
                    SetIndent(hdc, i, &indent, &width);
                }
                continue;
            }
            length++;
        }
        if ((width > 0 && width <= maxw) || (length > 0 && pos2 - start == length))
        {
            AddLine(start, length, indent, index++);
            SetIndent(hdc, i, &indent, &width);
        }

        // fixed bug
        if (GetCover())
        {
            if ((*m_CurrentPos) == 1 && m_PageInfo.line_info[0].start == 0)
                m_CurrentLine = 0;
            else if (m_CurrentLine < 1 && m_PageInfo.line_info[0].start == 0)
                m_CurrentLine = 1;
        }
        else
        {
            if (m_CurrentLine < 0 && m_PageInfo.line_info[0].start == 0) // n is out of range
                m_CurrentLine = 0;
        }
    }

    if (flag)
    {
        // [pos3, pos4)
        start = pos3;
        length = 0;
        width = 0;
        index = 0;
        SetIndent(hdc, pos3 - 1, &indent, &width);
        if (*m_WordWrap)
        {
            word_start_pos = start;
            word_width = 0;
        }
        for (i = pos3; i < pos4; i++)
        {
            // new line
#if 0 // because do FormatText
            if (m_Text[i] == 0x0D && m_Text[i + 1] == 0x0A) // the last char in data is 0x00, So it won��t cross the memory.
            {
                i++;
                length++;
            }
#endif
            if (m_Text[i] == 0x0A)
            {
                length++;
                AddLine(start, length, indent);
                start = i + 1;
                length = 0;
                width = 0;
                SetIndent(hdc, i, &indent, &width);
                if (*m_WordWrap)
                {
                    word_start_pos = start;
                    word_width = 0;
                }
#if FAST_MODEL
                if (m_CurrentLine + m_OnePageLineCount <= m_PageInfo.line_size)
                    break;
#endif
                continue;
            }

            // calc char width
#if ENABLE_TAG
            int tagid = IsTag(i);
            if (tagid >= 0 && tagid < MAX_TAG_COUNT)
            {
                if (tagfonts[tagid])
                {
                    HFONT oldfont = (HFONT)SelectObject(hdc, tagfonts[tagid]);
                    GetTextExtentPoint32(hdc, &m_Text[i], 1, &sz);
                    SelectObject(hdc, oldfont);
                }
            }
            else
            {
                GetTextExtentPoint32(hdc, &m_Text[i], 1, &sz);
            }
#else
            GetTextExtentPoint32(hdc, &m_Text[i], 1, &sz);
#endif

            if (*m_WordWrap)
            {
                if (is_space(m_Text[i]))
                {
                    word_start_pos = i + 1;
                    word_width = 0;
                }
                else if (is_minus(m_Text[i]) /*&& !is_minus(m_Text[i-1]) && !is_minus(m_Text[i + 1])*/)
                {
                    word_start_pos = i + 1;
                    word_width = 0;
                }
                else
                {
                    word_width += sz.cx + (*m_charGap);
                }
            }

            width += sz.cx + (*m_charGap);
            if (width > maxw)
            {
                if (*m_WordWrap)
                {
                    if (is_space(m_Text[i])) // add left space
                    {
                        memset(&sz, 0, sizeof(sz));
                        for (int j = i; j < pos4; j++)
                        {
                            if (is_space(m_Text[j]))
                            {
                                length++;
                                i++;
                                continue;
                            }
                            GetTextExtentPoint32(hdc, &m_Text[i], 1, &sz);
                            break;
                        }

                        // add line
                        AddLine(start, length, indent);
                        start = i;
                        length = i == pos4 ? 0 : 1;
                        width = i == pos4 ? 0 : sz.cx + (*m_charGap);
                        word_start_pos = start;
                        word_width = width;
                        SetIndent(hdc, i, &indent, &width);
                    }
                    else
                    {
                        if (word_start_pos == start) // too long word
                        {
                            // do nothing

                            // add line
                            AddLine(start, length, indent);
                            start = i;
                            length = 1;
                            width = sz.cx + (*m_charGap);
                            word_start_pos = start;
                            word_width = width;
                            SetIndent(hdc, i, &indent, &width);
                        }
                        else
                        {
                            // move current word to next line
                            length -= i - word_start_pos;

                            // add line
                            AddLine(start, length, indent);
                            start = word_start_pos;
                            length = i - word_start_pos + 1;
                            width = word_width;
                            word_start_pos = start;
                            word_width = width;
                            SetIndent(hdc, i, &indent, &width);

                            if (width > maxw) // goto -> [too long word]
                            {
                                // add line
                                length--;
                                AddLine(start, length, indent);
                                start = i;
                                length = 1;
                                width = sz.cx + (*m_charGap);
                                word_start_pos = start;
                                word_width = width;
                                SetIndent(hdc, i, &indent, &width);
                            }
                        }
                    }
                }
                else
                {
                    AddLine(start, length, indent);
                    start = i;
                    length = 1;
                    width = sz.cx + (*m_charGap);
                    SetIndent(hdc, i, &indent, &width);
                }

#if FAST_MODEL
                if (m_CurrentLine + m_OnePageLineCount <= m_PageInfo.line_size)
                {
                    width = 0;
                    break;
                }
#endif
                continue;
            }
            length++;
        }
        if (pos4 == m_TextLength)
        {
            if (width > 0 && width <= maxw)
            {
                AddLine(start, length, indent);
                SetIndent(hdc, i, &indent, &width);
            }
        }
        else
        {
            // Discard dirty line
        }
    }
}

void PageCache::AddLine(INT start, INT length, BOOL indent, INT pos)
{
    const int UNIT_SIZE = 1024;

    if (!m_PageInfo.line_info)
    {
        m_PageInfo.line_size = 0;
        m_PageInfo.alloc_size = UNIT_SIZE;
        m_PageInfo.line_info = (line_info_t *)malloc(m_PageInfo.alloc_size * sizeof(line_info_t));
    }
    if (m_PageInfo.line_size >= m_PageInfo.alloc_size)
    {
        m_PageInfo.alloc_size += UNIT_SIZE;
        m_PageInfo.line_info = (line_info_t *)realloc(m_PageInfo.line_info, m_PageInfo.alloc_size * sizeof(line_info_t));
    }
    if (pos == -1) // append
    {
        m_PageInfo.line_info[m_PageInfo.line_size].start = start;
        m_PageInfo.line_info[m_PageInfo.line_size].length = length;
        m_PageInfo.line_info[m_PageInfo.line_size].indent = indent;
    }
    else // insert
    {
        memcpy(&m_PageInfo.line_info[pos + 1], &m_PageInfo.line_info[pos], sizeof(line_info_t) * (m_PageInfo.line_size - pos));
        m_PageInfo.line_info[pos].start = start;
        m_PageInfo.line_info[pos].length = length;
        m_PageInfo.line_info[pos].indent = indent;
        // set currentline
        m_CurrentLine++;
    }
    m_PageInfo.line_size++;
}

void PageCache::RemoveAllLine(BOOL freemem)
{
    if (freemem)
    {
        if (m_PageInfo.line_info)
            free(m_PageInfo.line_info);
        memset(&m_PageInfo, 0, sizeof(m_PageInfo));
    }
    else
    {
        m_PageInfo.line_size = 0;
    }
    m_CurrentLine = 0;
}

BOOL PageCache::IsValid(void)
{
    Book *book;
    if (!m_Text || m_TextLength == 0)
        return FALSE;
    if (m_Rect.right - m_Rect.left == 0)
        return FALSE;
    if (m_Rect.bottom - m_Rect.top == 0)
        return FALSE;
    book = dynamic_cast<Book *>(this);
    if (!book)
        return FALSE;
    if (book->IsLoading())
        return FALSE;
    if (!m_lineGap || !m_CurrentPos || !m_InternalBorder)
        return FALSE;
    return TRUE;
}

bool PageCache::OnDrawPageEvent(HWND hWnd)
{
    return true;
}

bool PageCache::OnLineUpDownEvent(HWND hWnd, BOOL up, int n)
{
    return true;
}

#if ENABLE_TAG
int PageCache::IsTag(int index)
{
    int i,j;
    int len,begin,end;
    for (i=0; i<MAX_TAG_COUNT; i++)
    {
        if (m_tags[i].enable && m_tags[i].keyword[0])
        {
            len = wcslen(m_tags[i].keyword);
            begin = index - len + 1 > 0 ? index - len + 1 : 0;
            end = index + 1/*+ len - 1 > m_TextLength ? m_TextLength : index + len - 1*/;

            for (j=begin; j<end; j++)
            {
                if (wcsncmp(m_tags[i].keyword, m_Text+j, len) == 0)
                    return i;
            }
        }
    }
    return -1;
}
#endif

void PageCache::UnitTest1(void)
{
#if TEST_MODEL
    assert(m_CurrentLine >= 0 && m_CurrentLine < m_PageInfo.line_size);
    if (m_CurrentLine + m_OnePageLineCount > m_PageInfo.line_size)
    {
        assert(m_PageInfo.line_info[m_PageInfo.line_size - 1].start + m_PageInfo.line_info[m_PageInfo.line_size - 1].length == m_TextLength);
    }
#endif
}

void PageCache::UnitTest2(void)
{
#if TEST_MODEL
    int i, v1, v2, v3;
    TCHAR *buf = NULL;
    for (i = 0; i < m_PageInfo.line_size; i++)
    {
        v1 = m_PageInfo.line_info[i].start;
        v2 = m_PageInfo.line_info[i].length;
        assert(v1 >= 0 && v2 >= 0 && v1 + v2 <= m_TextLength);
        if (v1 + v2 == m_TextLength)
        {
            assert(i == m_PageInfo.line_size - 1);
        }
        if (i < m_PageInfo.line_size - 1)
        {
            v3 = m_PageInfo.line_info[i+1].start;
            assert(v3 > 0 && v1 + v2 == v3);
        }
    }
#endif
}