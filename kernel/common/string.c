/**
 * @file string.c
 * @brief String utility functions implementation
 * 
 * Provides implementations of standard string functions for the kernel.
 */

#include "../../include/common.h"
#include <stddef.h>

/* ============================================================================
 * String Length
 * ============================================================================ */

size_t strlen(const char* str)
{
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

/* ============================================================================
 * String Copy
 * ============================================================================ */

char* strcpy(char* dest, const char* src)
{
    char* d = dest;
    while (*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n)
{
    char* d = dest;
    size_t i = 0;
    
    while (i < n && *src) {
        *d++ = *src++;
        i++;
    }
    
    while (i < n) {
        *d++ = '\0';
        i++;
    }
    
    return dest;
}

/* ============================================================================
 * String Compare
 * ============================================================================ */

int strcmp(const char* s1, const char* s2)
{
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

int strncmp(const char* s1, const char* s2, size_t n)
{
    size_t i = 0;
    while (i < n && *s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
        i++;
    }
    
    if (i == n) {
        return 0;
    }
    
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

/* ============================================================================
 * String Search
 * ============================================================================ */

char* strchr(const char* str, int c)
{
    while (*str) {
        if (*str == (char)c) {
            return (char*)str;
        }
        str++;
    }
    
    if (c == '\0') {
        return (char*)str;
    }
    
    return NULL;
}

char* strstr(const char* haystack, const char* needle)
{
    if (!*needle) {
        return (char*)haystack;
    }
    
    while (*haystack) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        
        if (!*n) {
            return (char*)haystack;
        }
        
        haystack++;
    }
    
    return NULL;
}

/* ============================================================================
 * Memory Functions
 * ============================================================================ */

void* memset(void* ptr, int value, size_t size)
{
    uint8_t* p = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        p[i] = (uint8_t)value;
    }
    return ptr;
}

void* memcpy(void* dest, const void* src, size_t size)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    for (size_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
    
    return dest;
}

void* memmove(void* dest, const void* src, size_t size)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    if (d < s) {
        /* Copy forward */
        for (size_t i = 0; i < size; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        /* Copy backward */
        for (size_t i = size; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    
    return dest;
}

int memcmp(const void* ptr1, const void* ptr2, size_t size)
{
    const uint8_t* p1 = (const uint8_t*)ptr1;
    const uint8_t* p2 = (const uint8_t*)ptr2;
    
    for (size_t i = 0; i < size; i++) {
        if (p1[i] != p2[i]) {
            return (int)((int)p1[i] - (int)p2[i]);
        }
    }
    
    return 0;
}

