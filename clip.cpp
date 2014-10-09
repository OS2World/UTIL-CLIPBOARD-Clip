/*
** Module   :CLIP.CPP
** Abstract :Clipboard command line utility
**
** Public domain by Sergey I. Yevtushenko
**
** Log: Tue  15/02/2000 Created
**
*/

#define INCL_DOS
#define INCL_WIN

#include <os2.h>
#include <stdio.h>
#include <string.h>

#define MODE_NONE   0
#define MODE_IN     0x0001
#define MODE_OUT    0x0002

struct Chunk
{
    Chunk *next;
    int  sz;
    char data[2048];


    Chunk():next(0), sz(0) {}
};

BOOL (APIENTRY *_inCloseClipbrd)(HAB);
HMQ  (APIENTRY *_inCreateMsgQueue)(HAB, LONG);
BOOL (APIENTRY *_inDestroyMsgQueue)(HMQ);
BOOL (APIENTRY *_inEmptyClipbrd)(HAB);
HAB  (APIENTRY *_inInitialize)(ULONG);
BOOL (APIENTRY *_inOpenClipbrd)(HAB);
ULONG(APIENTRY *_inQueryClipbrdData)(HAB, ULONG);
BOOL (APIENTRY *_inQueryClipbrdFmtInfo)(HAB, ULONG, PULONG);
BOOL (APIENTRY *_inSetClipbrdData)(HAB, ULONG, ULONG, ULONG);
BOOL (APIENTRY *_inTerminate)(HAB);

HAB hab;
HMQ hmq;

//---------------------------------------------------------------
// STDIN Stuff
//---------------------------------------------------------------

char *ReadPipe(int iPipe)
{
    APIRET rc;
    ULONG ulTotal     = 0;
    ULONG ulBytesRead = 0;
    Chunk* pHead = new Chunk;
    Chunk* pCurr = pHead;

    do
    {
        ulBytesRead = 0;

        rc = DosRead(0, pCurr->data, sizeof(pCurr->data), &ulBytesRead);

        pCurr->sz = ulBytesRead;
        ulTotal += ulBytesRead;

        if(ulBytesRead)
        {
            pCurr->next = new Chunk;
            pCurr       = pCurr->next;
        }
    }
    while(!rc && ulBytesRead);

    char *str = 0;

    if(ulTotal)
    {
        str = new char[ulTotal+1];

        char *ptr = str;

        for(pCurr = pHead; pCurr; pCurr = pCurr->next)
        {
            if(!pCurr->sz)
                continue;

            memcpy(ptr, pCurr->data, pCurr->sz);
            ptr += pCurr->sz;
        }
        *ptr = 0;
    }

    //Free memory

    for(pCurr = pHead; pCurr; )
    {
        Chunk* pTmp  = pCurr;
        pCurr = pCurr->next;

        delete pTmp;
    }

    return str;
}

//---------------------------------------------------------------
// PM Stuff
//---------------------------------------------------------------

int init_pm(void)
{
    PPIB pib;
    PTIB tib;
    APIRET rc;
    HMODULE hMte = 0;
    char loadErr[256];

    if(hab || hmq)
        return 0;

    rc = DosGetInfoBlocks(&tib, &pib);

    rc = DosQueryModuleHandle("PMWIN", &hMte);

    if(rc)
        return 1;

    pib->pib_ultype = 3;

    rc = DosLoadModule(loadErr, sizeof(loadErr), "PMWIN", &hMte);

    if(rc)
        return 1;

    rc = DosQueryProcAddr(hMte, 707, 0, (PFN*)&_inCloseClipbrd);
    rc = DosQueryProcAddr(hMte, 716, 0, (PFN*)&_inCreateMsgQueue);
    rc = DosQueryProcAddr(hMte, 726, 0, (PFN*)&_inDestroyMsgQueue);
    rc = DosQueryProcAddr(hMte, 733, 0, (PFN*)&_inEmptyClipbrd);
    rc = DosQueryProcAddr(hMte, 763, 0, (PFN*)&_inInitialize);
    rc = DosQueryProcAddr(hMte, 793, 0, (PFN*)&_inOpenClipbrd);
    rc = DosQueryProcAddr(hMte, 806, 0, (PFN*)&_inQueryClipbrdData);
    rc = DosQueryProcAddr(hMte, 807, 0, (PFN*)&_inQueryClipbrdFmtInfo);
    rc = DosQueryProcAddr(hMte, 854, 0, (PFN*)&_inSetClipbrdData);
    rc = DosQueryProcAddr(hMte, 888, 0, (PFN*)&_inTerminate);

    hab = _inInitialize(0);
    hmq = _inCreateMsgQueue(hab, 0);

    return 0;
}

void done_pm(void)
{
    if(hmq)
        _inDestroyMsgQueue(hmq);
    if(hab)
        _inTerminate(hab);

    hab = 0;
    hmq = 0;
}

void set_clip(char *text)
{
    if(!hab || !text)
        return;

    char *pByte = 0;

    //Calculate size of buffer

    int sz = strlen(text) + 1;
    int i;

    _inOpenClipbrd(hab);
    _inEmptyClipbrd(hab);

    if (!DosAllocSharedMem((PPVOID)&pByte, 0, sz,
        PAG_WRITE | PAG_COMMIT | OBJ_GIVEABLE | OBJ_GETTABLE))
    {
        memcpy(pByte, text, sz);
        _inSetClipbrdData(hab, (ULONG) pByte, CF_TEXT, CFI_POINTER);
    }
    _inCloseClipbrd(hab);
}

char* get_clip(void)
{
    if(!hab)
        return 0;

    char *ClipData;
    ULONG ulFormat;

    _inQueryClipbrdFmtInfo(hab, CF_TEXT, &ulFormat);

    if(ulFormat != CFI_POINTER)
        return 0;

    _inOpenClipbrd(hab);

    ClipData = (char *)_inQueryClipbrdData(hab, CF_TEXT);

    if(!ClipData)
        return 0;

    int sz = strlen(ClipData) + 1;
    char *str = new char[sz];

    memcpy(str, ClipData, sz);

    _inCloseClipbrd(hab);

    return str;
}


char cUsage[] = "CLIP V0.1 Command line clipboard handler.\r\n"
                "Copyright (C) 2000  Sergey I. Yevtushenko.\r\n"
                "\r\n"
                "Usage: CLIP [-o[-]] [-i[-]] [-{h|?}] \r\n"
                ;

void usage(void)
{
    ULONG  ulWrote = 0;
    DosWrite(1, cUsage, sizeof(cUsage) - 1, &ulWrote);
}

int main(int argc, char **argv)
{
    ULONG ulType = 0;
    ULONG ulAttr = 0;
    APIRET rc;
    int i;
    int iMode = MODE_NONE;

    rc = DosQueryHType(0, &ulType, &ulAttr);    //STDIN

    if(!rc)
    {
        ulType &= 0xFF;

        if(ulType == 0 || ulType == 2)  //file or pipe
            iMode |= MODE_IN;
    }

    rc = DosQueryHType(1, &ulType, &ulAttr);    //STDOUT

    if(!rc)
    {
        ulType &= 0xFF;

        if(ulType == 0 || ulType == 2)  //file or pipe
            iMode |= MODE_OUT;
    }

    for(i = 1; i < argc; i++)
    {
        if(argv[i][0] == '-' || argv[i][0] == '/')
        {
            switch(argv[i][1])
            {
                case 'I':
                case 'i':
                    if(argv[i][2] == '-')
                        iMode &= ~MODE_IN;
                    else
                        iMode |= MODE_IN;
                    break;

                case 'o':
                case 'O':
                    if(argv[i][2] == '-')
                        iMode &= ~MODE_OUT;
                    else
                        iMode |= MODE_OUT;
                    break;

                case 'h':
                case 'H':
                case '?':
                    usage();
                    return 0;
            }
        }
    }

    if(!iMode)
    {
        usage();
        return 0;
    }

    rc = init_pm();

    if(rc)
        return -1;

    if(iMode & MODE_IN)
    {
        char *str = ReadPipe(0);

        if(str)
            set_clip(str);

        delete str;
    }

    if(iMode & MODE_OUT)
    {
        char *str = get_clip();

        if(str)
        {
            ULONG  ulWrote = 0;
            rc = DosWrite(1, str, strlen(str), &ulWrote);
        }
    }

    done_pm();

    return rc;
}