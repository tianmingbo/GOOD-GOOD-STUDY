/**
 * listpack结构:
 *  字节数(4 Byte)|元素数量(2 Byte)|entry|entry|结束标识（255）
 * entry结构：
 *   encoding|data|backlen
 *   encoding:节点编码格式
 *   data: 节点元素,即节点存储的位置
 *   backlen: 记录当前节点长度
 * */
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "listpack.h"
#include "listpack_malloc.h"

#define LP_HDR_SIZE 6       /* 32 bit total len + 16 bit number of elements. */
#define LP_HDR_NUMELE_UNKNOWN UINT16_MAX
#define LP_MAX_INT_ENCODING_LEN 9
#define LP_MAX_BACKLEN_SIZE 5
#define LP_MAX_ENTRY_BACKLEN 34359738367ULL
#define LP_ENCODING_INT 0
#define LP_ENCODING_STRING 1

//16,24,32,64bit.他们的编码分别占1字节


/*
 * 元素本身是7bit的无符号整数,编码类型为0,占1bit.数据+编码占1Byte
 0xxxxxxx  7位x用来存储数据
 |
 编码类型
 */
#define LP_ENCODING_7BIT_UINT 0
#define LP_ENCODING_7BIT_UINT_MASK 0x80
/*用于判断一个字节是否是使用 7 位无符号整数进行编码的
((byte) & LP_ENCODING_7BIT_UINT_MASK) 首先将给定的字节 byte 与 LP_ENCODING_7BIT_UINT_MASK 进行按位与操作。
只有最高位（最左边的位）被设置为1，其他位全部为0，只保留了最高位的值。
 */
#define LP_ENCODING_IS_7BIT_UINT(byte) (((byte)&LP_ENCODING_7BIT_UINT_MASK)==LP_ENCODING_7BIT_UINT)

#define LP_ENCODING_6BIT_STR 0x80 //前2bit表示编码,后6bit保存字符串长度
#define LP_ENCODING_6BIT_STR_MASK 0xC0
#define LP_ENCODING_IS_6BIT_STR(byte) (((byte)&LP_ENCODING_6BIT_STR_MASK)==LP_ENCODING_6BIT_STR)

// 110 00000 00000000 LP_ENCODING_13BIT_INT后13位表示整数,编码为前3bit
#define LP_ENCODING_13BIT_INT 0xC0
#define LP_ENCODING_13BIT_INT_MASK 0xE0
#define LP_ENCODING_IS_13BIT_INT(byte) (((byte)&LP_ENCODING_13BIT_INT_MASK)==LP_ENCODING_13BIT_INT)

#define LP_ENCODING_12BIT_STR 0xE0
#define LP_ENCODING_12BIT_STR_MASK 0xF0
#define LP_ENCODING_IS_12BIT_STR(byte) (((byte)&LP_ENCODING_12BIT_STR_MASK)==LP_ENCODING_12BIT_STR)

#define LP_ENCODING_16BIT_INT 0xF1
#define LP_ENCODING_16BIT_INT_MASK 0xFF
#define LP_ENCODING_IS_16BIT_INT(byte) (((byte)&LP_ENCODING_16BIT_INT_MASK)==LP_ENCODING_16BIT_INT)

#define LP_ENCODING_24BIT_INT 0xF2
#define LP_ENCODING_24BIT_INT_MASK 0xFF
#define LP_ENCODING_IS_24BIT_INT(byte) (((byte)&LP_ENCODING_24BIT_INT_MASK)==LP_ENCODING_24BIT_INT)

#define LP_ENCODING_32BIT_INT 0xF3
#define LP_ENCODING_32BIT_INT_MASK 0xFF
#define LP_ENCODING_IS_32BIT_INT(byte) (((byte)&LP_ENCODING_32BIT_INT_MASK)==LP_ENCODING_32BIT_INT)

#define LP_ENCODING_64BIT_INT 0xF4
#define LP_ENCODING_64BIT_INT_MASK 0xFF
#define LP_ENCODING_IS_64BIT_INT(byte) (((byte)&LP_ENCODING_64BIT_INT_MASK)==LP_ENCODING_64BIT_INT)

#define LP_ENCODING_32BIT_STR 0xF0
#define LP_ENCODING_32BIT_STR_MASK 0xFF
#define LP_ENCODING_IS_32BIT_STR(byte) (((byte)&LP_ENCODING_32BIT_STR_MASK)==LP_ENCODING_32BIT_STR)

#define LP_EOF 0xFF

#define LP_ENCODING_6BIT_STR_LEN(p) ((p)[0] & 0x3F)
#define LP_ENCODING_12BIT_STR_LEN(p) ((((p)[0] & 0xF) << 8) | (p)[1])
#define LP_ENCODING_32BIT_STR_LEN(p) (((uint32_t)(p)[1]<<0) | \
                                      ((uint32_t)(p)[2]<<8) | \
                                      ((uint32_t)(p)[3]<<16) | \
                                      ((uint32_t)(p)[4]<<24))

/*
 * (uint32_t)(p)[0] << 0 字节数组 p 的第一个字节 (p)[0] 转换为 uint32_t 类型，并将其左移 0 位（保持不变）。
 * 然后 | 将结果按位进行组合，得到一个32位的整数。
 * */
#define lpGetTotalBytes(p)           (((uint32_t)(p)[0]<<0) | \
                                      ((uint32_t)(p)[1]<<8) | \
                                      ((uint32_t)(p)[2]<<16) | \
                                      ((uint32_t)(p)[3]<<24))

#define lpGetNumElements(p)          (((uint32_t)(p)[4]<<0) | \
                                      ((uint32_t)(p)[5]<<8))

/*
 * (v)&0xff 表示取整数 v 的最低位字节，将其与 0xff 进行按位与操作，得到一个无符号8位整数。
((v)>>8)&0xff 表示将整数 v 右移8位，即丢弃最低位字节，然后再取最低位字节，得到第二个字节。
((v)>>16)&0xff 表示将整数 v 右移16位，即丢弃最低的两个字节，然后再取最低位字节，得到第三个字节。
((v)>>24)&0xff 表示将整数 v 右移24位，即丢弃最低的三个字节，然后再取最低位字节，得到第四个字节。
 * */
#define lpSetTotalBytes(p, v) do { \
    (p)[0] = (v)&0xff; \
    (p)[1] = ((v)>>8)&0xff; \
    (p)[2] = ((v)>>16)&0xff; \
    (p)[3] = ((v)>>24)&0xff; \
} while(0)

#define lpSetNumElements(p, v) do { \
    (p)[4] = (v)&0xff; \
    (p)[5] = ((v)>>8)&0xff; \
} while(0)

/* Convert a string into a signed 64 bit integer.
 * The function returns 1 if the string could be parsed into a (non-overflowing)
 * signed 64 bit int, 0 otherwise. The 'value' will be set to the parsed value
 * when the function returns success.
 *
 * Note that this function demands that the string strictly represents
 * a int64 value: no spaces or other characters before or after the string
 * representing the number are accepted, nor zeroes at the start if not
 * for the string "0" representing the zero number.
 *
 * Because of its strictness, it is safe to use this function to check if
 * you can convert a string into a long long, and obtain back the string
 * from the number without any loss in the string representation. *
 *
 * -----------------------------------------------------------------------------
 *
 * Credits: this function was adapted from the Redis source code, file
 * "utils.c", function string2ll(), and is copyright:
 *
 * Copyright(C) 2011, Pieter Noordhuis
 * Copyright(C) 2011, Salvatore Sanfilippo
 *
 * The function is released under the BSD 3-clause license.
 */
int lpStringToInt64(const char *s, unsigned long slen, int64_t *value) {
    const char *p = s;
    unsigned long plen = 0;
    int negative = 0;
    uint64_t v;

    if (plen == slen)
        return 0;

    /* Special case: first and only digit is 0. */
    if (slen == 1 && p[0] == '0') {
        if (value != NULL) *value = 0;
        return 1;
    }

    if (p[0] == '-') {
        negative = 1;
        p++;
        plen++;

        /* Abort on only a negative sign. */
        if (plen == slen)
            return 0;
    }

    /* First digit should be 1-9, otherwise the string should just be 0. */
    if (p[0] >= '1' && p[0] <= '9') {
        v = p[0] - '0';
        p++;
        plen++;
    } else if (p[0] == '0' && slen == 1) {
        *value = 0;
        return 1;
    } else {
        return 0;
    }

    while (plen < slen && p[0] >= '0' && p[0] <= '9') {
        if (v > (UINT64_MAX / 10)) /* Overflow. */
            return 0;
        v *= 10;

        if (v > (UINT64_MAX - (p[0] - '0'))) /* Overflow. */
            return 0;
        v += p[0] - '0';

        p++;
        plen++;
    }

    /* Return if not all bytes were used. */
    if (plen < slen)
        return 0;

    if (negative) {
        if (v > ((uint64_t) (-(INT64_MIN + 1)) + 1)) /* Overflow. */
            return 0;
        if (value != NULL) *value = -v;
    } else {
        if (v > INT64_MAX) /* Overflow. */
            return 0;
        if (value != NULL) *value = v;
    }
    return 1;
}

/* Create a new, empty listpack.
 * On success the new listpack is returned, otherwise an error is returned. */
unsigned char *lpNew(void) {
    //分配LP_HDR_SIZE + 1， 4个字节记录listpack总字节数，2个记录listpack的元素数量，1个是listpack结尾标识
    unsigned char *lp = lp_malloc(LP_HDR_SIZE + 1);
    if (lp == NULL) return NULL;
    //设置listpack的大小
    lpSetTotalBytes(lp, LP_HDR_SIZE + 1); //初始化总字节数,为7
    lpSetNumElements(lp, 0); //设置元素个数为0
    lp[LP_HDR_SIZE] = LP_EOF; //结束标识 255
    return lp;
}

/* Free the specified listpack. */
void lpFree(unsigned char *lp) {
    lp_free(lp);
}

/* 对元素内容进行编码,返回编码类型,并且将编码(encode属性)存储在intenc中,元素长度存储在enclen中 */
int lpEncodeGetType(unsigned char *ele, uint32_t size, unsigned char *intenc, uint64_t *enclen) {
    int64_t v;
    if (lpStringToInt64((const char *) ele, size, &v)) {
        if (v >= 0 && v <= 127) {
            /* 0XXXXXXX */
            intenc[0] = v;
            *enclen = 1;
        } else if (v >= -4096 && v <= 4095) {
            /* 110XXXXXXXXXXXXX */
            if (v < 0) v = ((int64_t) 1 << 13) + v;
            intenc[0] = (v >> 8) | LP_ENCODING_13BIT_INT;
            intenc[1] = v & 0xff;
            *enclen = 2;
        } else if (v >= -32768 && v <= 32767) {
            /* 11110001XXXXXXXXXXXXXXXX */
            if (v < 0) v = ((int64_t) 1 << 16) + v;
            intenc[0] = LP_ENCODING_16BIT_INT;
            intenc[1] = v & 0xff;
            intenc[2] = v >> 8;
            *enclen = 3;
        } else if (v >= -8388608 && v <= 8388607) {
            /* 11110010XXXXXXXXXXXXXXXXXXXXXXXX */
            if (v < 0) v = ((int64_t) 1 << 24) + v;
            intenc[0] = LP_ENCODING_24BIT_INT;
            intenc[1] = v & 0xff;
            intenc[2] = (v >> 8) & 0xff;
            intenc[3] = v >> 16;
            *enclen = 4;
        } else if (v >= -2147483648 && v <= 2147483647) {
            /* 11110011XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
            if (v < 0) v = ((int64_t) 1 << 32) + v;
            intenc[0] = LP_ENCODING_32BIT_INT;
            intenc[1] = v & 0xff;
            intenc[2] = (v >> 8) & 0xff;
            intenc[3] = (v >> 16) & 0xff;
            intenc[4] = v >> 24;
            *enclen = 5;
        } else {
            /* 64 bit integer. */
            uint64_t uv = v;
            intenc[0] = LP_ENCODING_64BIT_INT;
            intenc[1] = uv & 0xff;
            intenc[2] = (uv >> 8) & 0xff;
            intenc[3] = (uv >> 16) & 0xff;
            intenc[4] = (uv >> 24) & 0xff;
            intenc[5] = (uv >> 32) & 0xff;
            intenc[6] = (uv >> 40) & 0xff;
            intenc[7] = (uv >> 48) & 0xff;
            intenc[8] = uv >> 56;
            *enclen = 9;
        }
        return LP_ENCODING_INT;
    } else {
        if (size < 64)
            *enclen = 1 + size;
        else if (size < 4096)
            *enclen = 2 + size;
        else
            *enclen = 5 + (uint64_t) size;
        return LP_ENCODING_STRING;
    }
}

/* 将enclen转换为backlen,
 * 规则: 将enclen的每7个bit位划分为一组,保存到backlen的一个字节中,并且将字节序倒转
 * 0101100 0110000 -> 00110000 10101100
 * backlen的每个字节的最高位bit为1时,代表前面一个字节也是backlen的一部分
 * */
unsigned long lpEncodeBacklen(unsigned char *buf, uint64_t l) {
    //编码类型和实际长度<=127,entry-len为1Byte
    if (l <= 127) {
        if (buf) buf[0] = l;
        return 1;
    } else if (l < 16383) {
        if (buf) {
            buf[0] = l >> 7;
            buf[1] = (l & 127) | 128;
        }
        return 2;
    } else if (l < 2097151) {
        if (buf) {
            buf[0] = l >> 14;
            buf[1] = ((l >> 7) & 127) | 128;
            buf[2] = (l & 127) | 128;
        }
        return 3;
    } else if (l < 268435455) {
        if (buf) {
            buf[0] = l >> 21;
            buf[1] = ((l >> 14) & 127) | 128;
            buf[2] = ((l >> 7) & 127) | 128;
            buf[3] = (l & 127) | 128;
        }
        return 4;
    } else {
        if (buf) {
            buf[0] = l >> 28;
            buf[1] = ((l >> 21) & 127) | 128;
            buf[2] = ((l >> 14) & 127) | 128;
            buf[3] = ((l >> 7) & 127) | 128;
            buf[4] = (l & 127) | 128;
        }
        return 5;
    }
}

/* 返回entry-len的值。
 * entry-len的存储是大端序。低位字节保存在内存高地址上。且entry-len的最高位表示自己是不是entry-len的最后一个Byte
 * 比如entry-len小端序为 xxxxxxxx1 xxxxxxxx0。那么在内存中大端序为0xxxxxxx 1xxxxxxx
 * */
uint64_t lpDecodeBacklen(unsigned char *p) {
    uint64_t val = 0;
    uint64_t shift = 0;
    do {
        val |= (uint64_t) (p[0] & 127) << shift;
        if (!(p[0] & 128))
            //如果(p[0] & 128)==0,即0xxxxxxx & 128==0,说明已经到了entry-len的结束位置
            break;
        shift += 7;
        p--;
        if (shift > 28) return UINT64_MAX;
    } while (1);
    return val;
}

/*编码字符串 */
void lpEncodeString(unsigned char *buf, unsigned char *s, uint32_t len) {
    if (len < 64) {
        // 10XXXXXX
        buf[0] = len | LP_ENCODING_6BIT_STR;
        memcpy(buf + 1, s, len);
    } else if (len < 4096) {
        // 1110XXXXXXXXXXXX
        buf[0] = (len >> 8) | LP_ENCODING_12BIT_STR;
        buf[1] = len & 0xff;
        memcpy(buf + 2, s, len);
    } else {
        // 11110000XXXXXXXXXXXXXXXXXXXXXXXX
        buf[0] = LP_ENCODING_32BIT_STR;
        buf[1] = len & 0xff;
        buf[2] = (len >> 8) & 0xff;
        buf[3] = (len >> 16) & 0xff;
        buf[4] = (len >> 24) & 0xff;
        memcpy(buf + 5, s, len);
    }
}

/* 根据当前entry第一个字节的取值，来计算entry的编码类型，
 * 并根据编码类型，计算当前编码类型和实际数据的总长度*/
uint32_t lpCurrentEncodedSize(unsigned char *p) {
    if (LP_ENCODING_IS_7BIT_UINT(p[0])) return 1;
    if (LP_ENCODING_IS_6BIT_STR(p[0])) return 1 + LP_ENCODING_6BIT_STR_LEN(p); //因为是字符串,所有还需要加上字符串长度
    if (LP_ENCODING_IS_13BIT_INT(p[0])) return 2;
    if (LP_ENCODING_IS_16BIT_INT(p[0])) return 3;
    if (LP_ENCODING_IS_24BIT_INT(p[0])) return 4; //1字节编码,3字节实际数据
    if (LP_ENCODING_IS_32BIT_INT(p[0])) return 5;
    if (LP_ENCODING_IS_64BIT_INT(p[0])) return 9;
    if (LP_ENCODING_IS_12BIT_STR(p[0])) return 2 + LP_ENCODING_12BIT_STR_LEN(p);
    if (LP_ENCODING_IS_32BIT_STR(p[0])) return 5 + LP_ENCODING_32BIT_STR_LEN(p);
    if (p[0] == LP_EOF) return 1;
    return 0;
}

/* 返回下一个entry指针 */
unsigned char *lpSkip(unsigned char *p) {
    unsigned long entrylen = lpCurrentEncodedSize(p);
    //entrylen为一个entry的总长度
    entrylen += lpEncodeBacklen(NULL, entrylen);
    //向右偏移
    p += entrylen;
    return p;
}

/* If 'p' points to an element of the listpack, calling lpNext() will return
 * the pointer to the next element (the one on the right), or NULL if 'p'
 * already pointed to the last element of the listpack. */
unsigned char *lpNext(unsigned char *lp, unsigned char *p) {
    ((void) lp); /* lp is not used for now. However lpPrev() uses it. */
    p = lpSkip(p);
    if (p[0] == LP_EOF) return NULL;
    return p;
}

/* 从后向前遍历 */
unsigned char *lpPrev(unsigned char *lp, unsigned char *p) {
    if (p - lp == LP_HDR_SIZE) return NULL;
    p--; /* p指向entry-len的最后一个字节开始位置 */
    uint64_t prevlen = lpDecodeBacklen(p);
    prevlen += lpEncodeBacklen(NULL, prevlen);
    return p - prevlen + 1; /* Seek the first byte of the previous entry. */
}

/* 返回listpack的第一个entry */
unsigned char *lpFirst(unsigned char *lp) {
    lp += LP_HDR_SIZE; /* 跳过listpack的头部6Byte */
    if (lp[0] == LP_EOF) return NULL;
    return lp;
}

/* 返回指向最后一个entry的指针 */
unsigned char *lpLast(unsigned char *lp) {
    unsigned char *p = lp + lpGetTotalBytes(lp) - 1; /* 指向最后一个entry的结尾 */
    return lpPrev(lp, p); /* 指向最后一个entry的开始，如果不为NULL */
}

/*
 * 获取lp长度
 * */
uint32_t lpLength(unsigned char *lp) {
    // 获取元素数量
    uint32_t numele = lpGetNumElements(lp);
    // 如果大于65535,则lp的header中就记录不下了
    if (numele != LP_HDR_NUMELE_UNKNOWN)
        return numele;

    /* 遍历记录数量 */
    uint32_t count = 0;
    unsigned char *p = lpFirst(lp);
    while (p) {
        count++;
        p = lpNext(lp, p);
    }

    /* 可能会清楚元素,当元素数量小于65535,重新设置header */
    if (count < LP_HDR_NUMELE_UNKNOWN) lpSetNumElements(lp, count);
    return count;
}

/* Return the listpack element pointed by 'p'.
 *
 * The function changes behavior depending on the passed 'intbuf' value.
 * Specifically, if 'intbuf' is NULL:
 *
 * If the element is internally encoded as an integer, the function returns
 * NULL and populates the integer value by reference in 'count'. Otherwise if
 * the element is encoded as a string a pointer to the string (pointing inside
 * the listpack itself) is returned, and 'count' is set to the length of the
 * string.
 *
 * If instead 'intbuf' points to a buffer passed by the caller, that must be
 * at least LP_INTBUF_SIZE bytes, the function always returns the element as
 * it was a string (returning the pointer to the string and setting the
 * 'count' argument to the string length by reference). However if the element
 * is encoded as an integer, the 'intbuf' buffer is used in order to store
 * the string representation.
 *
 * The user should use one or the other form depending on what the value will
 * be used for. If there is immediate usage for an integer value returned
 * by the function, than to pass a buffer (and convert it back to a number)
 * is of course useless.
 *
 * If the function is called against a badly encoded ziplist, so that there
 * is no valid way to parse it, the function returns like if there was an
 * integer encoded with value 12345678900000000 + <unrecognized byte>, this may
 * be an hint to understand that something is wrong. To crash in this case is
 * not sensible because of the different requirements of the application using
 * this lib.
 *
 * Similarly, there is no error returned since the listpack normally can be
 * assumed to be valid, so that would be a very high API cost. However a function
 * in order to check the integrity of the listpack at load time is provided,
 * check lpIsValid(). */
unsigned char *lpGet(unsigned char *p, int64_t *count, unsigned char *intbuf) {
    int64_t val;
    uint64_t uval, negstart, negmax;

    if (LP_ENCODING_IS_7BIT_UINT(p[0])) {
        negstart = UINT64_MAX; /* 7 bit ints are always positive. */
        negmax = 0;
        uval = p[0] & 0x7f;
    } else if (LP_ENCODING_IS_6BIT_STR(p[0])) {
        *count = LP_ENCODING_6BIT_STR_LEN(p);
        return p + 1;
    } else if (LP_ENCODING_IS_13BIT_INT(p[0])) {
        uval = ((p[0] & 0x1f) << 8) | p[1];
        negstart = (uint64_t) 1 << 12;
        negmax = 8191;
    } else if (LP_ENCODING_IS_16BIT_INT(p[0])) {
        uval = (uint64_t) p[1] |
               (uint64_t) p[2] << 8;
        negstart = (uint64_t) 1 << 15;
        negmax = UINT16_MAX;
    } else if (LP_ENCODING_IS_24BIT_INT(p[0])) {
        uval = (uint64_t) p[1] |
               (uint64_t) p[2] << 8 |
               (uint64_t) p[3] << 16;
        negstart = (uint64_t) 1 << 23;
        negmax = UINT32_MAX >> 8;
    } else if (LP_ENCODING_IS_32BIT_INT(p[0])) {
        uval = (uint64_t) p[1] |
               (uint64_t) p[2] << 8 |
               (uint64_t) p[3] << 16 |
               (uint64_t) p[4] << 24;
        negstart = (uint64_t) 1 << 31;
        negmax = UINT32_MAX;
    } else if (LP_ENCODING_IS_64BIT_INT(p[0])) {
        uval = (uint64_t) p[1] |
               (uint64_t) p[2] << 8 |
               (uint64_t) p[3] << 16 |
               (uint64_t) p[4] << 24 |
               (uint64_t) p[5] << 32 |
               (uint64_t) p[6] << 40 |
               (uint64_t) p[7] << 48 |
               (uint64_t) p[8] << 56;
        negstart = (uint64_t) 1 << 63;
        negmax = UINT64_MAX;
    } else if (LP_ENCODING_IS_12BIT_STR(p[0])) {
        *count = LP_ENCODING_12BIT_STR_LEN(p);
        return p + 2;
    } else if (LP_ENCODING_IS_32BIT_STR(p[0])) {
        *count = LP_ENCODING_32BIT_STR_LEN(p);
        return p + 5;
    } else {
        uval = 12345678900000000ULL + p[0];
        negstart = UINT64_MAX;
        negmax = 0;
    }

    /* We reach this code path only for integer encodings.
     * Convert the unsigned value to the signed one using two's complement
     * rule. */
    if (uval >= negstart) {
        /* This three steps conversion should avoid undefined behaviors
         * in the unsigned -> signed conversion. */
        uval = negmax - uval;
        val = uval;
        val = -val - 1;
    } else {
        val = uval;
    }

    /* Return the string representation of the integer or the value itself
     * depending on intbuf being NULL or not. */
    if (intbuf) {
        *count = snprintf((char *) intbuf, LP_INTBUF_SIZE, "%lld", (long long) val);
        return intbuf;
    } else {
        *count = val;
        return NULL;
    }
}

/* 在指定位置“p”处插入、删除或替换长度为“len”的指定元素“ele”，“p”通过 lpFirst()、lpLast()、lpIndex()、lpNext()、lpPrev() 或 lpSeek()获得元素指针。
  * “where”参数:（可以是 LP_BEFORE、LP_AFTER 或 LP_REPLACE），将元素插入到“p”指向的元素之前、之后或替换。
  * 如果'ele'设置为NULL，该函数将删除'p'指向的元素而不是插入一个。
  * 当内存不足或 listpack 总长度超过 2^32-1 时返回 NULL，否则返回指向包含新元素的 listpack 的新指针（并且传递的旧指针不再被视为有效） ）
  * 如果 'newp' 不为 NULL，则在成功调用结束时 '*newp' 将被设置为刚刚添加的元素的地址，以便可以继续与 lpNext() 和 lpPrev() 进行交互 。
  * 对于删除操作（'ele' 设置为 NULL），'newp' 设置为已删除元素右侧的下一个元素，如果删除的元素是最后一个元素，则设置为 NULL。
  * */
unsigned char *lpInsert(unsigned char *lp, unsigned char *ele, uint32_t size,
                        unsigned char *p, int where, unsigned char **newp) {
    /* size:插入元素的长度
     * p:元素插入的位置
     * */
    unsigned char intenc[LP_MAX_INT_ENCODING_LEN];
    unsigned char backlen[LP_MAX_BACKLEN_SIZE];

    uint64_t enclen; /* The length of the encoded element. */

    /* 如果插入元素为NULL,则说明是要删除该元素 */
    if (ele == NULL) where = LP_REPLACE;

    /* 如果where为LP_AFTER,则找到p的下一个位置,然后把where变成LP_BEFORE.
     * 这样就统一了逻辑,只需要处理LP_BEFORE和LP_REPLACE*/
    if (where == LP_AFTER) {
        p = lpSkip(p);
        where = LP_BEFORE;
    }

    /* 存储元素“p”的偏移量，以便在重新分配后我们可以再次获取其地址。 */
    unsigned long poff = p - lp;

    int enctype;
    if (ele) {
        /* 对插入元素内容进行编码,返回编码类型,编码类型有LP_ENCODING_INT和LP_ENCODING_STRING,
         * lpEncodeGetType处理成功后,会将编码存储在intenc中,元素长度存储在enclen中
         * */
        enctype = lpEncodeGetType(ele, size, intenc, &enclen);
    } else {
        enctype = -1;
        enclen = 0;
    }

    /* 计算backlen,以便于遍历整个listpack */
    unsigned long backlen_size = ele ? lpEncodeBacklen(backlen, enclen) : 0;
    uint64_t old_listpack_bytes = lpGetTotalBytes(lp); // 获取原来的listpack占用的字节数量
    uint32_t replaced_len = 0;
    if (where == LP_REPLACE) {
        replaced_len = lpCurrentEncodedSize(p);
        replaced_len += lpEncodeBacklen(NULL, replaced_len);
    }
    // 计算新的listpack占用的空间大小
    uint64_t new_listpack_bytes = old_listpack_bytes + enclen + backlen_size - replaced_len;
    if (new_listpack_bytes > UINT32_MAX) return NULL;


    unsigned char *dst = lp + poff; /* May be updated after reallocation. */

    /* 如果新的空间大于原来的空间,则申请新内存 */
    if (new_listpack_bytes > old_listpack_bytes) {
        if ((lp = lp_realloc(lp, new_listpack_bytes)) == NULL) return NULL;
        dst = lp + poff;
    }

    /* 如果是插入元素,则将插入位置后面的元素后移,为插入元素腾出空间 */
    if (where == LP_BEFORE) {
        memmove(dst + enclen + backlen_size, dst, old_listpack_bytes - poff);
    } else { /* LP_REPLACE. */
        // 如果是替换元素,则调整替换元素的大小
        long lendiff = (enclen + backlen_size) - replaced_len;
        memmove(dst + replaced_len + lendiff,
                dst + replaced_len,
                old_listpack_bytes - poff - replaced_len);
    }

    /* 如果新的空间小于原来的空间(删除或替换为更小的元素),也调整大小 */
    if (new_listpack_bytes < old_listpack_bytes) {
        if ((lp = lp_realloc(lp, new_listpack_bytes)) == NULL) return NULL;
        dst = lp + poff;
    }

    if (newp) {
        // 将newp指针指向插入位置,则插入元素后newp指针将指向插入元素
        *newp = dst;
        /* In case of deletion, set 'newp' to NULL if the next element is
         * the EOF element. */
        if (!ele && dst[0] == LP_EOF) *newp = NULL;
    }
    if (ele) {
        if (enctype == LP_ENCODING_INT) {
            // 插入元素为INT型,则直接将intenc写入listpack即可
            memcpy(dst, intenc, enclen);
        } else {
            lpEncodeString(dst, ele, size);
        }
        dst += enclen;
        // 写入backlen属性
        memcpy(dst, backlen, backlen_size);
        dst += backlen_size;
    }

    /* 更新listpack的元素数量属性 */
    if (where != LP_REPLACE || ele == NULL) {
        uint32_t num_elements = lpGetNumElements(lp);
        if (num_elements != LP_HDR_NUMELE_UNKNOWN) {
            if (ele)
                lpSetNumElements(lp, num_elements + 1);
            else
                lpSetNumElements(lp, num_elements - 1);
        }
    }
    // 更新字节数
    lpSetTotalBytes(lp, new_listpack_bytes);

#if 0
    /* This code path is normally disabled: what it does is to force listpack
     * to return *always* a new pointer after performing some modification to
     * the listpack, even if the previous allocation was enough. This is useful
     * in order to spot bugs in code using listpacks: by doing so we can find
     * if the caller forgets to set the new pointer where the listpack reference
     * is stored, after an update. */
    unsigned char *oldlp = lp;
    lp = lp_malloc(new_listpack_bytes);
    memcpy(lp,oldlp,new_listpack_bytes);
    if (newp) {
        unsigned long offset = (*newp)-oldlp;
        *newp = lp + offset;
    }
    /* Make sure the old allocation contains garbage. */
    memset(oldlp,'A',new_listpack_bytes);
    lp_free(oldlp);
#endif

    return lp;
}

/* Append the specified element 'ele' of length 'len' at the end of the
 * listpack. It is implemented in terms of lpInsert(), so the return value is
 * the same as lpInsert(). */
unsigned char *lpAppend(unsigned char *lp, unsigned char *ele, uint32_t size) {
    uint64_t listpack_bytes = lpGetTotalBytes(lp);
    unsigned char *eofptr = lp + listpack_bytes - 1;
    return lpInsert(lp, ele, size, eofptr, LP_BEFORE, NULL);
}

/* Remove the element pointed by 'p', and return the resulting listpack.
 * If 'newp' is not NULL, the next element pointer (to the right of the
 * deleted one) is returned by reference. If the deleted element was the
 * last one, '*newp' is set to NULL. */
unsigned char *lpDelete(unsigned char *lp, unsigned char *p, unsigned char **newp) {
    return lpInsert(lp, NULL, 0, p, LP_REPLACE, newp);
}

/* Return the total number of bytes the listpack is composed of. */
uint32_t lpBytes(unsigned char *lp) {
    return lpGetTotalBytes(lp);
}

/* Seek the specified element and returns the pointer to the seeked element.
 * Positive indexes specify the zero-based element to seek from the head to
 * the tail, negative indexes specify elements starting from the tail, where
 * -1 means the last element, -2 the penultimate and so forth. If the index
 * is out of range, NULL is returned. */
unsigned char *lpSeek(unsigned char *lp, long index) {
    int forward = 1; /* Seek forward by default. */

    /* We want to seek from left to right or the other way around
     * depending on the listpack length and the element position.
     * However if the listpack length cannot be obtained in constant time,
     * we always seek from left to right. */
    uint32_t numele = lpGetNumElements(lp);
    if (numele != LP_HDR_NUMELE_UNKNOWN) {
        if (index < 0) index = (long) numele + index;
        if (index < 0) return NULL; /* Index still < 0 means out of range. */
        if (index >= numele) return NULL; /* Out of range the other side. */
        /* We want to scan right-to-left if the element we are looking for
         * is past the half of the listpack. */
        if (index > numele / 2) {
            forward = 0;
            /* Left to right scanning always expects a negative index. Convert
             * our index to negative form. */
            index -= numele;
        }
    } else {
        /* If the listpack length is unspecified, for negative indexes we
         * want to always scan left-to-right. */
        if (index < 0) forward = 0;
    }

    /* Forward and backward scanning is trivially based on lpNext()/lpPrev(). */
    if (forward) {
        unsigned char *ele = lpFirst(lp);
        while (index > 0 && ele) {
            ele = lpNext(lp, ele);
            index--;
        }
        return ele;
    } else {
        unsigned char *ele = lpLast(lp);
        while (index < -1 && ele) {
            ele = lpPrev(lp, ele);
            index++;
        }
        return ele;
    }
}

