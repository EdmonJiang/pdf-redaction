#include <windows.h>
#include <stdio.h>
int wmain(void){
    HWND h = FindWindowW(L"PdfRedactionApp", NULL);
    HWND sb, lbl;
    SCROLLINFO si;
    wchar_t buf[64];
    int targets[] = { 0, 40000, 200000, 700000, 1500000, 9999999 };
    int i;
    if(!h){ printf("no window\n"); return 1; }
    sb = GetDlgItem(h, 1027); lbl = GetDlgItem(h, 1005);
    si.cbSize=sizeof(si); si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
    GetScrollInfo(sb, SB_CTL, &si);
    printf("range min=%d max=%d page=%u\n", si.nMin, si.nMax, si.nPage);
    for(i=0;i<6;i++){
        SetScrollPos(sb, SB_CTL, targets[i], TRUE);
        SendMessageW(h, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION,0), (LPARAM)sb);
        Sleep(80);
        GetDlgItemTextW(h, 1005, buf, 64);
        GetScrollInfo(sb, SB_CTL, &si);
        printf("set=%8d -> pos=%8d  page=%ls\n", targets[i], si.nPos, buf);
    }
    return 0;
}
