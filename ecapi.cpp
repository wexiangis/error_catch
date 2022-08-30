
#include <stdio.h>
#include "ecapi.h"

#define ECAPI_LOG_BUFF_MAX 8192

static ECAPI_CALLBACK g_ecapi_callback = NULL;
static void* g_ecapi_priv = NULL;

#if defined(WIN) || defined(WIN32) || defined(WINCE) || defined(_MSC_VER)
/* ---------- Windows begin ---------- */
#include <Windows.h>
#include <iostream>
#include "Dbghelp.h"
#pragma comment(lib, "Dbghelp.lib" )
using namespace std;

/* xxx */
static void dump_callstack(CONTEXT *context, DWORD exceptionCode)
{
	DWORD machineType = IMAGE_FILE_MACHINE_I386;
	HANDLE hProcess = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();
	STACKFRAME sf;

	memset(&sf, 0, sizeof(STACKFRAME));
	sf.AddrPC.Offset = context->Eip;
	sf.AddrPC.Mode = AddrModeFlat;
	sf.AddrStack.Offset = context->Esp;
	sf.AddrStack.Mode = AddrModeFlat;
	sf.AddrFrame.Offset = context->Ebp;
	sf.AddrFrame.Mode = AddrModeFlat;

    int index = 0;
    int logBuffSize = 0;
    char logBuff[ECAPI_LOG_BUFF_MAX] = {0};

	/* 定位到下一个函数的堆栈位置 */
	while (StackWalk(machineType, hProcess, hThread, &sf, context, 0, SymFunctionTableAccess, SymGetModuleBase, 0)
		&& sf.AddrFrame.Offset != 0)
	{
		/* 获取函数名 */
		BYTE symbolBuffer[sizeof(SYMBOL_INFO)+1024];
		PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)symbolBuffer;
		pSymbol->SizeOfStruct = sizeof(symbolBuffer);
		pSymbol->MaxNameLen = 1024;
		BOOL ret1 = SymFromAddr(hProcess, sf.AddrPC.Offset, 0, pSymbol);
		// printlog("\nFunction : %08X - %s \n", sf.AddrPC.Offset, ret ? pSymbol->Name : "None");

		/* 获取文件名和行和号 */
		IMAGEHLP_LINE lineInfo = { sizeof(IMAGEHLP_LINE) };
		DWORD dwLineDisplacement;
		BOOL ret2 = SymGetLineFromAddr(hProcess, sf.AddrPC.Offset, &dwLineDisplacement, &lineInfo);
		// printlog("    File : %s \n", ret ? lineInfo.FileName : "None");
		// printlog("    Line : %d \n", ret ? lineInfo.LineNumber : -1);

        /* 组织回调内容 */
        logBuffSize += _snprintf(&logBuff[logBuffSize], ECAPI_LOG_BUFF_MAX - logBuffSize, "%dth: %08X - %s - %s(%d)\n",
            index++,
            sf.AddrPC.Offset,
            ret1 ? pSymbol->Name : "None",
            ret2 ? lineInfo.FileName : "None",
            lineInfo.LineNumber);
        if (logBuffSize > ECAPI_LOG_BUFF_MAX - 8)
            break;
	}

    /* callback */
    if (logBuffSize > 0)
    {
        if (g_ecapi_callback)
            g_ecapi_callback(exceptionCode, logBuff, g_ecapi_priv);
    }
}

LONG WINAPI veh_callback(PEXCEPTION_POINTERS exceptionInfo)
{
	/* 初始化 dbghelp.dll */
	SymInitialize(GetCurrentProcess(), NULL, TRUE);
	dump_callstack(exceptionInfo->ContextRecord, exceptionInfo->ExceptionRecord->ExceptionCode);
	/* 关闭 dbghelp.dll */
	SymCleanup(GetCurrentProcess());
	return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI seh_callback(EXCEPTION_POINTERS *exceptionInfo)
{
	if (exceptionInfo->ExceptionRecord->ExceptionCode == STATUS_HEAP_CORRUPTION)
    {
		/* 统一使用veh回调来整理崩溃信息 */
		return veh_callback(exceptionInfo);
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

void ecapi_register(ECAPI_CALLBACK callback, void* priv, int signal, ...)
{
    g_ecapi_callback = callback;
    g_ecapi_priv = priv;

	AddVectoredExceptionHandler(1, veh_callback);
	SetUnhandledExceptionFilter(seh_callback);
}

void ecapi_signal_test(int sig)
{
    veh_callback((PEXCEPTION_POINTERS)sig);
}

/* ---------- Windows end ---------- */
#else
/* ---------- Linux begin ---------- */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <elf.h>
#include <execinfo.h>

/* How functions to go back ? */
#define ECAPI_BACKTRACE_DEEP 16
/* callback position info len */
#define ECAPI_POSITION_LEN 512

typedef struct {
    // name = .symtab && type = 2
    uint32_t symtabSize;
    void* symtab;

    // name = .strtab && type = 3
    uint32_t strtabSize;
    void* strtab;

    // name = .dynsym && type = 11
    uint32_t dynsymSize;
    void* dynsym;

    // name = .dynstr && type = 3
    uint32_t dynstrSize;
    void* dynstr;
} ECElf_Struct;

static void ecelf_release(ECElf_Struct* elf);
static int ecelf_load(const char* file, ECElf_Struct* elf);
static int ecelf_loadByMem(uint8_t* addr, ECElf_Struct* elf);
static int ecapi_loadSymbolFromElfByAddr(ECElf_Struct* elf, uint64_t addr, char* symbol, int symbolLen);
static int ecapi_parse_proc_self_maps(uint64_t* addrList, int addrListLen, char** positionList, int positionLen);

static int _isSameLevel(uint64_t v1, uint64_t v2)
{
    uint64_t max = v1 > v2 ? v1 : v2;
    if (((v1 & v2) << 1) > max)
        return 1;
    return false;
}

static void ecapi_signal(int sig)
{
    int i, count;
    void* pointList[ECAPI_BACKTRACE_DEEP] = {0};
    uint64_t addrList[ECAPI_BACKTRACE_DEEP] = {0};
    char positionInfo[ECAPI_BACKTRACE_DEEP * ECAPI_POSITION_LEN] = {0};
    char* positionList[ECAPI_BACKTRACE_DEEP] = {0};

    /* 回溯调用过的函数地址 */
    count = backtrace(pointList, ECAPI_BACKTRACE_DEEP);
    if (count > 0)
    {
        /* 函数地址转换为uint64_t */
        for (i = 0; i < count; i++)
        {
            addrList[i] = (uint64_t)pointList[i];
            // printf("%dth %08llX \r\n", i, (long long unsigned int)addrList[i]);
        }
        /* 指针数组分段指向大内存 */
        for (i = 0; i < count; i++)
        {
            positionList[i] = &positionInfo[i * ECAPI_POSITION_LEN];
            snprintf(positionList[i], ECAPI_POSITION_LEN,
                "%08llX", (long long unsigned int)addrList[i]);
        }

        /* 减去系统内存地址偏移量,得到函数在elf文件中的实际地址(不是物理地址) */
        if (ecapi_parse_proc_self_maps(addrList, count, positionList, ECAPI_POSITION_LEN) != 0)
        {
            fprintf(stderr, "ecapi_parse_proc_self_maps failed \r\n");
            count = 0;
        }
    }
    else
        fprintf(stderr, "backtrace failed \r\n");

#if 0
    char **strings = backtrace_symbols(pointList, count);
    if (strings) {
        for (i = 0; i < count; i++)
            printf("%dth: %s \n", i, strings[i]);
        free(strings);
    }
#endif

    if (g_ecapi_callback)
    {
        int index = 0;
        int logBuffSize = 0;
        char logBuff[ECAPI_LOG_BUFF_MAX] = {0};
        for (; index < count && logBuffSize < ECAPI_LOG_BUFF_MAX - 8; index++)
        {
            logBuffSize += snprintf(&logBuff[logBuffSize], ECAPI_LOG_BUFF_MAX - logBuffSize, "%dth: %s\r\n", index, positionList[index]);
        }

        g_ecapi_callback(sig, logBuff, g_ecapi_priv);
    }
}

void ecapi_signal_test(int sig)
{
    ecapi_signal(sig);
}

static int ecapi_parse_proc_self_maps(uint64_t* addrList, int addrListLen, char** positionList, int positionLen)
{
    int i, ret;

    FILE* fp;
    char line[1024] = {0};
    uint64_t addrBegin = 0;
    uint64_t addrEnd = 0;
    uint64_t addrOffset = 0;
    char temp[1024];
    char file[1024] = {0};
    Elf64_Ehdr ehdr64 = {0};

    int hitCount = 0;
    char hitArray[ECAPI_BACKTRACE_DEEP] = {0};
    ECElf_Struct elf = {0};

    fp = fopen((const char*)"/proc/self/maps", "r");
    if (!fp)
        return -1;

    while (fgets(line, sizeof(line), fp))
    {
        /* elf文件在系统内存中分成多块进行映射 */
        ret = sscanf(line, "%llx-%llx %s %llx %s %s %s",
            (long long unsigned int*)&addrBegin,
            (long long unsigned int*)&addrEnd,
            temp, /* premission */
            (long long unsigned int*)&addrOffset,
            temp, /* time */
            temp, /* unknown */
            temp); /* file */

        // printf("%s // %08llX - %08llX - %08llX \r\n", line, addrBegin, addrEnd, addrOffset);

        if (ret == 7 && addrEnd > addrBegin && addrOffset == 0)
        {
            if (addrEnd - addrBegin > sizeof(Elf64_Ehdr))
                memcpy(&ehdr64, (void*)addrBegin, sizeof(Elf64_Ehdr));
        }

        /* 比较地址范围,找到对应的addrBegin,减去该值再加上块大小即是目标地址 */
        for (i = 0; i < addrListLen; i++)
        {
            /* already hit */
            if (hitArray[i])
                continue;
            
            /* within addrBegin ~ addrEnd ? */
            if (addrList[i] >= addrBegin && addrList[i] < addrEnd)
            {
                /* get the addr of file */
                if (!_isSameLevel(addrBegin, ehdr64.e_entry))
                    addrList[i] = addrList[i] - addrBegin + addrOffset;

                /* flag set */
                hitArray[i] = 1;
                hitCount += 1;

                /* need to parse a new elf ? */
                if (strcmp(temp, file))
                {
                    strcpy(file, temp);
                    if (ecelf_load(file, &elf))
                    {
                        // fprintf(stderr, "ecelf_load() file %s failed [%08llX - %08llX - %08llX]\r\n", file,
                        //     (long long unsigned int)addrBegin,
                        //     (long long unsigned int)addrEnd,
                        //     (long long unsigned int)addrOffset);
                            
                        // printf("ehdr64: "
                        //     "e_phoff/%08llX "
                        //     "e_shoff/%08llX "
                        //     "e_flags/%08lX "
                        //     "e_ehsize/%04X "
                        //     "e_phentsize/%04X "
                        //     "e_phnum/%04X "
                        //     "e_shentsize/%04X "
                        //     "e_shnum/%04X "
                        //     "e_shstrndx/%04X "
                        //     "\r\n",
                        //     ehdr64.e_phoff,
                        //     ehdr64.e_shoff,
                        //     ehdr64.e_flags,
                        //     ehdr64.e_ehsize,
                        //     ehdr64.e_phentsize,
                        //     ehdr64.e_phnum,
                        //     ehdr64.e_shentsize,
                        //     ehdr64.e_shnum,
                        //     ehdr64.e_shstrndx);

                        if (ecelf_loadByMem((uint8_t*)addrBegin, &elf))
                            continue;
                    }
                }

                ret = ecapi_loadSymbolFromElfByAddr(&elf, addrList[i], positionList[i], positionLen);

                /* add file name to the tail */
                snprintf(positionList[i] + ret, positionLen - ret, " - %s", file);
            }
        }

        /* 匹配完成,提前跑路 */
        if (hitCount == addrListLen)
            break;
    }

    fclose(fp);
    ecelf_release(&elf);
    return hitCount > 0 ? 0 : (-1);
}

static int ecapi_loadSymbolFromElfByAddr(ECElf_Struct* elf, uint64_t addr, char* symbol, int symbolLen)
{
    uint32_t i;
    uint32_t* up32;

    uint32_t addrErr;
    uint32_t addrErrMin = 0xFFFFFFFF;
    uint32_t addrHit = (uint32_t)addr;
    const char* tmpSymbol = NULL;
    const char* tarSymbol = NULL;

    if (!symbol)
        return -1;

    /* 32bit or 64bit system ? */
    if (sizeof(void*) == 4)
    {
        ;
    }
    else
    {
        /* check .dynsym */
        if (elf->dynsym && elf->dynstr)
        {
            for (i = 0, up32 = (uint32_t*)elf->dynsym; i < elf->dynsymSize; i += sizeof(Elf64_Sym))
            {
                /* 名称有效 && 地址有效 */
                if (up32[0] < elf->dynstrSize && up32[2] && addr >= up32[2])
                {
                    tmpSymbol = ((const char*)elf->dynstr) + up32[0];
                    /* 直接地址范围匹配 */
                    if (up32[4] > 0 && addr < up32[2] + up32[4])
                    {
                        addrHit = up32[2];
                        tarSymbol = tmpSymbol;
                        break;
                    }
                    /* 找地址相差最小那个 */
                    else
                    {
                        addrErr = addr - up32[2];
                        if (addrErr <= addrErrMin && *tmpSymbol)
                        {
                            addrHit = up32[2];
                            addrErrMin = addrErr;
                            tarSymbol = tmpSymbol;
                        }
                    }
                }
                up32 += sizeof(Elf64_Sym) / 4;
            }
        }
        /* check .symtab */
        if (elf->symtab && elf->strtab)
        {
            for (i = 0, up32 = (uint32_t*)elf->symtab; i < elf->symtabSize; i += sizeof(Elf64_Sym))
            {
                /* 名称有效 && 地址有效 */
                if (up32[0] < elf->strtabSize  && up32[2] && addr >= up32[2])
                {
                    tmpSymbol = ((const char*)elf->strtab) + up32[0];
                    /* 直接地址范围匹配 */
                    if (up32[4] > 0 && addr < up32[2] + up32[4])
                    {
                        addrHit = up32[2];
                        tarSymbol = tmpSymbol;
                        break;
                    }
                    /* 找地址相差最小那个 */
                    else
                    {
                        addrErr = addr - up32[2];
                        if (addrErr <= addrErrMin && *tmpSymbol)
                        {
                            addrHit = up32[2];
                            addrErrMin = addrErr;
                            tarSymbol = tmpSymbol;
                        }
                    }
                }
                up32 += sizeof(Elf64_Sym) / 4;
            }
        }
    }

    if (tarSymbol)
    {
        return snprintf(symbol, symbolLen, "%08llX(%08X+%08X) - %s",
            (long long unsigned int)addr, addrHit, (uint32_t)(addr - addrHit), tarSymbol);
    }
    else
    {
        return snprintf(symbol, symbolLen, "%08llX(%08X+%08X)",
            (long long unsigned int)addr, addrHit, (uint32_t)(addr - addrHit));
    }
}

void ecapi_register(ECAPI_CALLBACK callback, void* priv, int sig, ...)
{
    int i;
    va_list ap;
    const int defaultSig[7] = {
        SIGINT, SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGTERM, SIGKILL};

    g_ecapi_callback = callback;
    g_ecapi_priv = priv;

    if (sig == 0)
    {
        for (i = 0; i < 7; i++)
            signal(defaultSig[i], ecapi_signal);
    }
    else
    {
        va_start(ap, sig);
        while (sig)
        {
            signal(sig, ecapi_signal);
            sig = va_arg(ap, int);
        }
        va_end(ap);
    }
}

static void ecelf_release(ECElf_Struct* elf)
{
    if (!elf)
        return;

    if (elf->symtab)
        free(elf->symtab);
    
    if (elf->strtab)
        free(elf->strtab);
    
    if (elf->dynsym)
        free(elf->dynsym);
    
    if (elf->dynstr)
        free(elf->dynstr);

    memset(elf, 0, sizeof(ECElf_Struct));
}

static int ecelf_load(const char* file, ECElf_Struct* elf)
{
    // Elf32_Ehdr ehdr32;
    // Elf32_Shdr* shdr32 = NULL;

    Elf64_Ehdr ehdr64;
    Elf64_Shdr* shdr64 = NULL;

    uint32_t shstrtabSize = 0;
    char* shstrtab = NULL;
    char* sectionName = NULL;

    FILE* fp;

    uint32_t* tarSize;
    void** tarMem;

    uint32_t i;
    int ret = -1;

    fp = fopen(file, "rb");
    if (!fp)
        return ret;

    do
    {
        if (sizeof(void*) == 4)
        {
            ;
        }
        else
        {
            /* 读 elf 头,取 section header 偏移量 */
            if (fread(&ehdr64, 1, sizeof(Elf64_Ehdr), fp) != sizeof(Elf64_Ehdr))
                break;

#if 0
            printf("ehdr64: "
                "e_phoff/%08llX "
                "e_shoff/%08llX "
                "e_flags/%08lX "
                "e_ehsize/%04X "
                "e_phentsize/%04X "
                "e_phnum/%04X "
                "e_shentsize/%04X "
                "e_shnum/%04X "
                "e_shstrndx/%04X "
                "\r\n",
                ehdr64.e_phoff,
                ehdr64.e_shoff,
                ehdr64.e_flags,
                ehdr64.e_ehsize,
                ehdr64.e_phentsize,
                ehdr64.e_phnum,
                ehdr64.e_shentsize,
                ehdr64.e_shnum,
                ehdr64.e_shstrndx);
#endif

            /* 简单格式检查 */
            if (ehdr64.e_shentsize != sizeof(Elf64_Shdr) || !ehdr64.e_shoff || !ehdr64.e_shnum)
                break;

            /* 没有 section header name table 则无法匹配 .strtab 和 .symtab */
            if (ehdr64.e_shstrndx == SHN_XINDEX || ehdr64.e_shstrndx >= ehdr64.e_shnum)
                break;

            /* 全量读取 section header 内存 */
            if (fseek(fp, ehdr64.e_shoff, SEEK_SET) < 0)
                break;
            shdr64 = (Elf64_Shdr*)calloc(ehdr64.e_shnum, sizeof(Elf64_Shdr));
            if (fread(shdr64, 1, ehdr64.e_shnum * sizeof(Elf64_Shdr), fp) != ehdr64.e_shnum * sizeof(Elf64_Shdr))
                break;

            /* 全量读取 shstrtab */
            if (fseek(fp, shdr64[ehdr64.e_shstrndx].sh_offset, SEEK_SET) < 0)
                break;
            shstrtabSize = shdr64[ehdr64.e_shstrndx].sh_size;
            shstrtab = (char*)calloc(shstrtabSize, 1);
            if (fread(shstrtab, 1, shstrtabSize, fp) != shstrtabSize)
                break;

            /* 遍历 section header */
            for (i = 0; i < ehdr64.e_shnum; i++)
            {
                sectionName = shstrtab + shdr64[i].sh_name;

#if 0
                printf("shdr64: %dth, "
                    "sh_name/%08llX "
                    "sh_type/%08llX "
                    "sh_flags/%08llX "
                    "sh_addr/%08llX "
                    "sh_offset/%08llX "
                    "sh_size/%08llX "
                    "%s "
                    "\r\n",
                    i,
                    shdr64[i].sh_name,
                    shdr64[i].sh_type,
                    shdr64[i].sh_flags,
                    shdr64[i].sh_addr,
                    shdr64[i].sh_offset,
                    shdr64[i].sh_size,
                    sectionName);
#endif
                
                /* 匹配目标 section */
                if (shdr64[i].sh_type == SHT_SYMTAB &&
                    shdr64[i].sh_name < shstrtabSize &&
                    !strcmp((const char*)".symtab", sectionName))
                {
                    tarSize = &elf->symtabSize;
                    tarMem = &elf->symtab;
                }
                else if (shdr64[i].sh_type == SHT_STRTAB &&
                    shdr64[i].sh_name < shstrtabSize &&
                    !strcmp((const char*)".strtab", sectionName))
                {
                    tarSize = &elf->strtabSize;
                    tarMem = &elf->strtab;
                }
                else if (shdr64[i].sh_type == SHT_DYNSYM &&
                    shdr64[i].sh_name < shstrtabSize &&
                    !strcmp((const char*)".dynsym", sectionName))
                {
                    tarSize = &elf->dynsymSize;
                    tarMem = &elf->dynsym;
                }
                else if (shdr64[i].sh_type == SHT_STRTAB &&
                    shdr64[i].sh_name < shstrtabSize &&
                    !strcmp((const char*)".dynstr", sectionName))
                {
                    tarSize = &elf->dynstrSize;
                    tarMem = &elf->dynstr;
                }
                else
                    continue;

                /* 跳转 section 地址 */
                if (fseek(fp, shdr64[i].sh_offset, SEEK_SET) < 0)
                    break;

                /* section 全量读取 */
                *tarSize = (uint32_t)shdr64[i].sh_size;
                *tarMem = calloc(*tarSize, 1);
                if (fread(*tarMem, 1, *tarSize, fp) != *tarSize)
                    break;

#if 0
                if (!strcmp((const char*)".symtab", sectionName))
                {
                    uint32_t j, k;
                    uint32_t* up32 = (uint32_t*)(*tarMem);
                    for (j = 0, k = 0; j < *tarSize; j += sizeof(Elf64_Sym), k++)
                    {
                        printf("sym64: %dth, "
                            "st_name/%08X "
                            "st_value/%08X "
                            "st_size/%08X "
                            "\r\n",
                            k,
                            up32[0],
                            up32[2],
                            up32[4]);
                        up32 += sizeof(Elf64_Sym) / 4;
                    }
                }
                else if (!strcmp((const char*)".strtab", sectionName))
                {
                    uint32_t j;
                    char* str = NULL;
                    for (j = 0; j < *tarSize; j += strlen(str) + 1)
                    {
                        str = (char*)(*tarMem) + j;
                        if (*str)
                            printf("strtab: offset/%08X %s \r\n", j, str);
                    }
                }
#endif
            }
        }

        ret = 0;
    } while (0);

    if (shstrtab)
        free(shstrtab);

    // if (shdr32)
    //     free(shdr32);
    if (shdr64)
        free(shdr64);

    fclose(fp);
    return ret;
}

static int ecelf_loadByMem(uint8_t* addr, ECElf_Struct* elf)
{
    // Elf32_Ehdr ehdr32;
    // Elf32_Shdr* shdr32 = NULL;

    Elf64_Ehdr ehdr64;
    Elf64_Shdr* shdr64 = NULL;

    uint32_t shstrtabSize = 0;
    char* shstrtab = NULL;
    char* sectionName = NULL;

    uint32_t* tarSize;
    void** tarMem;

    uint32_t i;
    int ret = -1;

    do
    {
        if (sizeof(void*) == 4)
        {
            ;
        }
        else
        {
            /* 读 elf 头,取 section header 偏移量 */
            memcpy(&ehdr64, addr, sizeof(Elf64_Ehdr));

            /* 简单格式检查 */
            if (ehdr64.e_shentsize != sizeof(Elf64_Shdr) || !ehdr64.e_shoff || !ehdr64.e_shnum)
                break;

            /* 没有 section header name table 则无法匹配 .strtab 和 .symtab */
            if (ehdr64.e_shstrndx == SHN_XINDEX || ehdr64.e_shstrndx >= ehdr64.e_shnum)
                break;

            /* 全量读取 section header 内存 */
            shdr64 = (Elf64_Shdr*)(addr + ehdr64.e_shoff);

            /* 全量读取 shstrtab */
            shstrtabSize = shdr64[ehdr64.e_shstrndx].sh_size;
            shstrtab = (char*)(addr + shdr64[ehdr64.e_shstrndx].sh_offset);

            /* 遍历 section header */
            for (i = 0; i < ehdr64.e_shnum; i++)
            {
                sectionName = shstrtab + shdr64[i].sh_name;
                
                /* 匹配目标 section */
                if (shdr64[i].sh_type == SHT_SYMTAB &&
                    shdr64[i].sh_name < shstrtabSize &&
                    !strcmp((const char*)".symtab", sectionName))
                {
                    tarSize = &elf->symtabSize;
                    tarMem = &elf->symtab;
                }
                else if (shdr64[i].sh_type == SHT_STRTAB &&
                    shdr64[i].sh_name < shstrtabSize &&
                    !strcmp((const char*)".strtab", sectionName))
                {
                    tarSize = &elf->strtabSize;
                    tarMem = &elf->strtab;
                }
                else if (shdr64[i].sh_type == SHT_DYNSYM &&
                    shdr64[i].sh_name < shstrtabSize &&
                    !strcmp((const char*)".dynsym", sectionName))
                {
                    tarSize = &elf->dynsymSize;
                    tarMem = &elf->dynsym;
                }
                else if (shdr64[i].sh_type == SHT_STRTAB &&
                    shdr64[i].sh_name < shstrtabSize &&
                    !strcmp((const char*)".dynstr", sectionName))
                {
                    tarSize = &elf->dynstrSize;
                    tarMem = &elf->dynstr;
                }
                else
                    continue;

                /* section 全量读取 */
                *tarSize = (uint32_t)shdr64[i].sh_size;
                *tarMem = calloc(*tarSize, 1);
                memcpy(*tarMem, addr + shdr64[i].sh_offset, *tarSize);
            }
        }

        ret = 0;
    } while (0);

    return ret;
}

/* ---------- Linux end ---------- */
#endif
