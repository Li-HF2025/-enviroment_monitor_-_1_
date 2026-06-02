#ifndef STM_AN3155_H
#define STM_AN3155_H

#define AN3155_CMD_GET              0x00  // 获取 bootloader 版本及支持的命令
#define AN3155_CMD_GET_VERSION      0x01  // 获取 bootloader 版本
#define AN3155_CMD_GET_ID           0x02  // 获取芯片 PID
#define AN3155_CMD_READ_MEMORY      0x11  // 读取内存
#define AN3155_CMD_GO               0x21  // 跳转到指定地址执行
#define AN3155_CMD_WRITE_MEMORY     0x31  // 写内存（Flash/RAM）
#define AN3155_CMD_ERASE            0x43  // 全局擦除（仅支持部分中低容量芯片）
#define AN3155_CMD_EXTENDED_ERASE   0x44  // 扩展擦除（支持页擦除和全局擦除）
#define AN3155_CMD_WRITE_PROTECT    0x63  // 写保护
#define AN3155_CMD_WRITE_UNPROTECT  0x73  // 解除写保护
#define AN3155_CMD_READOUT_PROTECT  0x82  // 读保护
#define AN3155_CMD_READOUT_UNPROTECT 0x92 // 解除读保护

#endif