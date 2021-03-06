#ifndef __DEBUG_H__
#define __DEBUG_H__

// Not using extern, define your own static dbflags in each file
// extern unsigned int dbflags;

/*
 * Bit flags for DEBUG()
 */
#define DB_IO          0x001
#define DB_TIMER       0x002
#define DB_USER_INPUT  0x004
#define DB_TRAIN_CTRL  0x008
#define DB_SENSOR      0x010
// #define DB_VM          0x020
// #define DB_EXEC        0x040
// #define DB_VFS         0x080
// #define DB_SFS         0x100
// #define DB_NET         0x200
// #define DB_NETFS       0x400
// #define DB_KMALLOC     0x800


#define DEBUG(d, fmt, args...) (((dbflags) & (d)) ? plprintf(COM2, fmt, ##args) : 0)
#define DEBUG_JMP(d, line, column, fmt, args...) (((dbflags) & (d)) ? printAsciControl(COM2, ASCI_CURSOR_TO, line, column), plprintf(COM2, fmt, ##args) : 0)
    

#endif // __DEBUG_H__
