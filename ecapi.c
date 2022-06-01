#include <stdio.h>
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

// #include <stdarg.h>
// #include <signal.h>
#include "ecapi.h"

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

static ECAPI_CALLBACK g_ecapi_callback = NULL;
static void* g_ecapi_priv = NULL;

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
    Elf32_Shdr* shdr32 = NULL;

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

            /* 没有 section header name table 则无法匹配 .strtab 和 .symtab */
            if (ehdr64.e_shstrndx == SHN_XINDEX || ehdr64.e_shstrndx >= ehdr64.e_shnum)
                break;
            /* 理论应该一致 */
            if (ehdr64.e_shentsize != sizeof(Elf64_Shdr))
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

                // printf("shdr64: %dth, "
                //     "sh_name/%08llX "
                //     "sh_type/%08llX "
                //     "sh_flags/%08llX "
                //     "sh_addr/%08llX "
                //     "sh_offset/%08llX "
                //     "sh_size/%08llX "
                //     "%s "
                //     "\r\n",
                //     i,
                //     shdr64[i].sh_name,
                //     shdr64[i].sh_type,
                //     shdr64[i].sh_flags,
                //     shdr64[i].sh_addr,
                //     shdr64[i].sh_offset,
                //     shdr64[i].sh_size,
                //     sectionName);
                
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

                // if (!strcmp((const char*)".symtab", sectionName))
                // {
                //     uint32_t j, k;
                //     uint32_t* up32 = (uint32_t*)(*tarMem);
                //     for (j = 0, k = 0; j < *tarSize; j += sizeof(Elf64_Sym), k++)
                //     {
                //         printf("sym64: %dth, "
                //             "st_name/%08X "
                //             "st_value/%08X "
                //             "st_size/%08X "
                //             "\r\n",
                //             k,
                //             up32[0],
                //             up32[2],
                //             up32[4]);
                //         up32 += sizeof(Elf64_Sym) / 4;
                //     }
                // }
                // else if (!strcmp((const char*)".dynstr", sectionName))
                // {
                //     uint32_t j;
                //     char* str = NULL;
                //     for (j = 0; j < *tarSize; j += strlen(str) + 1)
                //     {
                //         str = (char*)(*tarMem) + j;
                //         if (*str)
                //             printf("strtab: offset/%08X %s \r\n", j, str);
                //     }
                // }
            }
        }

        ret = 0;
    } while (0);

    if (shstrtab)
        free(shstrtab);

    if (shdr32)
        free(shdr32);
    if (shdr64)
        free(shdr64);

    fclose(fp);
    return ret;
}

static int ecapi_loadSymbolFromElfByAddr(ECElf_Struct* elf, uint64_t addr, char* symbol, int symbolLen)
{
    uint32_t i;
    uint32_t* up32;

    uint32_t addrErr;
    uint32_t addrErrMin = 0xFFFFFFFF;
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
                if (up32[0] < elf->dynstrSize && up32[2])
                {
                    /* 直接地址范围匹配 */
                    if (addr >= up32[2] && addr < up32[2] + up32[4])
                    {
                        tarSymbol = ((const char*)elf->dynstr) + up32[0];
                        break;
                    }
                    /* 找地址相差最小那个 */
                    else
                    {
                        addrErr = addr - up32[2];
                        if (addrErr < addrErrMin)
                        {
                            addrErrMin = addrErr;
                            tarSymbol = ((const char*)elf->dynstr) + up32[0];
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
                if (up32[0] < elf->strtabSize && up32[2])
                {
                    /* 找地址相差最小那个 */
                    if (addr >= up32[2] && addr < up32[2] + up32[4])
                    {
                        tarSymbol = ((const char*)elf->strtab) + up32[0];
                        break;
                    }
                    /* 找地址相差最小那个 */
                    else
                    {
                        addrErr = addr - up32[2];
                        if (addrErr < addrErrMin)
                        {
                            addrErrMin = addrErr;
                            tarSymbol = ((const char*)elf->strtab) + up32[0];
                        }
                    }
                }
                up32 += sizeof(Elf64_Sym) / 4;
            }
        }
    }

    if (tarSymbol)
        return snprintf(symbol, symbolLen, "%08llX %s", (long long unsigned int)addr, tarSymbol);
    else
        return snprintf(symbol, symbolLen, "%08llX", (long long unsigned int)addr);
}

static int ecapi_parse_proc_self_maps(uint64_t* addrList, char** positionList, int positionLen)
{
    int i, ret;

    FILE* fp;
    char line[1024] = {0};
    uint64_t addrBegin = 0;
    uint64_t addrEnd = 0;
    uint64_t addrSize = 0;
    char temp[1024];
    char file[1024] = {0};

    int hitCount = 0;
    char hitArray[ECAPI_BACKTRACE_DEEP] = {0};
    ECElf_Struct elf = {0};

    fp = fopen((const char*)"/proc/self/maps", "r");
    if (!fp)
        return -1;

    while (fgets(line, sizeof(line), fp))
    {
        /* elf文件在系统内存中分成多块进行映射 */
        sscanf(line, "%llx-%llx %s %llx %s %s %s",
            (long long unsigned int*)&addrBegin,
            (long long unsigned int*)&addrEnd,
            temp, /* premission */
            (long long unsigned int*)&addrSize,
            temp, /* time */
            temp, /* unknown */
            temp); /* file */

        /* 比较地址范围,找到对应的addrBegin,减去该值再加上块大小即是目标地址 */
        for (i = 0; i < ECAPI_BACKTRACE_DEEP; i++)
        {
            /* already hit */
            if (hitArray[i])
                continue;
            
            /* within addrBegin ~ addrEnd ? */
            if (addrList[i] >= addrBegin && addrList[i] < addrEnd)
            {
                /* get the addr of file */
                addrList[i] = addrList[i] - addrBegin + addrSize;

                /* flag set */
                hitArray[i] = 1;
                hitCount += 1;

                /* need to parse a new elf ? */
                if (strcmp(temp, file))
                {
                    strcpy(file, temp);
                    if (ecelf_load(file, &elf))
                        continue;
                }

                ret = ecapi_loadSymbolFromElfByAddr(&elf, addrList[i], positionList[i], positionLen);

                /* add file name to the tail */
                snprintf(positionList[i] + ret, positionLen - ret, " %s", file);
            }
        }

        /* 匹配完成,提前跑路 */
        if (hitCount == ECAPI_BACKTRACE_DEEP)
            break;
    }

    fclose(fp);
    ecelf_release(&elf);
    return hitCount > 0 ? 0 : (-1);
}

static void ecapi_signal(int sig)
{
    int i, ret;
    void* pointList[ECAPI_BACKTRACE_DEEP] = {0};
    uint64_t addrList[ECAPI_BACKTRACE_DEEP] = {0};
    char positionInfo[ECAPI_BACKTRACE_DEEP * ECAPI_POSITION_LEN] = {0};
    char* positionList[ECAPI_BACKTRACE_DEEP] = {0};

    /* 回溯调用过的函数地址 */
    ret = backtrace(pointList, ECAPI_BACKTRACE_DEEP);
    if (ret > 0)
    {
        /* 函数地址转换为uint64_t */
        for (i = 0; i < ret; i++)
            addrList[i] = (uint64_t)pointList[i];
        /* 指针定点 */
        for (i = 0; i < ECAPI_BACKTRACE_DEEP; i++)
            positionList[i] = &positionInfo[i * ECAPI_POSITION_LEN];

        /* 减去系统内存地址偏移量,得到函数在elf文件中的实际地址(不是物理地址) */
        if (ecapi_parse_proc_self_maps(addrList, positionList, ECAPI_POSITION_LEN) != 0)
        {
            perror("ecapi_parse_proc_self_maps failed \r\n");
            ret = 0;
        }
    }
    else
        perror("backtrace failed \r\n");

    // char **strings = backtrace_symbols(pointList, ret);
    // printf("backtrace_symbols/%p \r\n", strings);
    // if (strings) {
    //     for (i = 0; i < ret; i++)
    //         printf("%dth: %s \n", i, strings[i]);
    //     free(strings);
    // }

    if (g_ecapi_callback)
        g_ecapi_callback(sig, (char**)positionList, ret, g_ecapi_priv);
}

void ecapi_register(ECAPI_CALLBACK callback, void* priv, int sig, ...)
{
    va_list ap;

    g_ecapi_callback = callback;
    g_ecapi_priv = priv;

    va_start(ap, sig);
    while (sig)
    {
        signal(sig, ecapi_signal);
        sig = va_arg(ap, int);
    }
    va_end(ap);
}
