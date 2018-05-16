#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>

#define DATA_BLOCKS_NUM 100         // 100 个数据块索引
#define SIZE (4096 * 1024 * 1024)   // 文件系统大小
#define BLOCK_SIZE 4096             // 块大小
#define BLOCK_NR (1024 * 1024)      // 块数

typedef struct INode {      // inode 节点
    struct INode *next;
    int32_t block_num;
    int32_t Data_block[DATA_BLOCKS_NUM];
}INode;

typedef struct filenode {    // 文件节点
    char filename[255];
    int32_t block_num;
    int32_t offset;
    INode *inode;
    struct stat *st;
    struct filenode *next;
    struct filenode *last;
}filenode;

typedef struct Data {       // 数据节点
    char content[BLOCK_SIZE];
}Data;

typedef struct SuperNode {
    int32_t filenum;
    int32_t usedblock;
    int32_t pos;                        // 记录每次查找空余块位置
}SuperNode;
SuperNode *SNode;    // 超级节点，存放在 mem[0] 地址

static void *mem[BLOCK_NR];     // mem 块，共有 1024 * 1024 个
int bitmap[BLOCK_NR];           // 记录 mem 使用情况

static filenode *root = NULL;   // 根节点

void *lfs_malloc(int block_num) {       // 给节点分配空间，block_num 为需要分配的 mem块
    if(bitmap[block_num]) {        // 判断该 mem 块是否为空，本实验中因就在内存中实现，故未通过实现 bitmap 来进行判断该块是否有数据
        printf("malloc error: not empty block!\n");
        return (void *)-1;
    }
    size_t blocksize = (size_t)BLOCK_SIZE;
    mem[block_num] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    // mmap 分配空间
    memset(mem[block_num], 0, BLOCK_SIZE);
    bitmap[block_num] = 1;
    SNode->usedblock++;
    return mem[block_num];  // 返回地址
}

int lfs_free_inode(INode *inode, int flag) {    // 释放空间
    int stat = flag;        // 记录当前 inode 节点是否需要释放
    if(inode->next)         // 如果有下个 inode，进行递归操作
        lfs_free_inode(inode->next, 0);
    for(; flag < DATA_BLOCKS_NUM; flag++) {
        if(inode->Data_block[flag] == -1) {
            break;
        }
        munmap(mem[inode->Data_block[flag]], BLOCK_SIZE);   // 释放数据块
        bitmap[inode->Data_block[flag]] = 0;
        inode->Data_block[flag] = -1;   // 释放掉的数据块索引为 -1
        SNode->usedblock--;     // 记录已用块数减一
    }
    if(stat == 0) {         // 如果从第零块开始就释放，则该 inode 节点也需要进行释放
        bitmap[inode->block_num] = 0;
        munmap(mem[inode->block_num], BLOCK_SIZE);      // 释放 inode 节点空间
    }
    return 0;
}

int lfs_find_free_block() {     // 查找未被分配空间块 mem
    int i = SNode->pos;
    int count = 0;
    while(bitmap[i] && count < BLOCK_NR) {      // 同上，因在内存中实现，故未用 bitmap，仅根据 mem 有无进行判断
        i = (i + 1) % BLOCK_NR;
        count++;
    }
    if(!bitmap[i]) {      // 如果找到且 i < BLOCK_NR 就返回位置
        SNode->pos = i;
        return i;
    }
    return -1;         // 否则返回 -1
}

static struct filenode *get_filenode(const char *name) {    // 找到文件节点，与示例程序相同
    struct filenode *node = root;
    while(node) {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else 
            return node;
    }
    return NULL;
}

static int create_filenode(const char *filename, const struct stat *st) {  //创建文件
    filenode *new;
    int block_num = lfs_find_free_block();      // 找到空余 mem 块
    if(block_num == -1)         // 无空余块，返回 -1
        return -1;
    new = (filenode *)lfs_malloc(block_num);    // 为新文件节点分配空间
    new->block_num = block_num;
    memcpy(new->filename, filename, strlen(filename) + 1);
    new->offset = sizeof(struct filenode);      // 记录偏移量，为之后 st 分配空间
    new->st = (struct stat *)(mem[block_num] + new->offset);
    memcpy(new->st, st, sizeof(struct stat));
    new->offset += sizeof(struct stat);
    int inode_block_num = lfs_find_free_block();
    if(inode_block_num == -1)
        return -1;
    new->inode = (INode *)lfs_malloc(inode_block_num);      // 为第一个 inode 分配空间
    new->inode->block_num = inode_block_num;        // 记录所在 mem 块数
    new->inode->next = NULL;
    for(int j = 0; j < DATA_BLOCKS_NUM; j++)        // 给 inode 中数据块索引统一赋初值 -1
        new->inode->Data_block[j] = -1;
    new->next = root->next;         // 将新节点插入到 root 后一位
    if(root->next)
        root->next->last = new;
    root->next = new;
    new->last = root;
    SNode->filenum++;           // 文件数加一
    return 0;
}

static void *lfs_init(struct fuse_conn_info *conn) {    // 初始化，与实例程序基本相同
    size_t blocksize = (size_t)BLOCK_SIZE;
    for(int i = 0; i < 2; i++) {
        mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memset(mem[i], 0, blocksize);
        bitmap[i] = 1;
    }
    for(int i = 2; i < BLOCK_NR; i++)
        bitmap[i] = 0;
    SNode = (SuperNode *)mem[0];
    SNode->filenum = 0;
    SNode->usedblock = 2;
    root = (filenode *)mem[1];      // root 节点始终存放在 mem[1] 中
    root->last = NULL;          // 增加对 root 节点的状态初值
    root->next = NULL;
    root->offset = sizeof(struct filenode);
    root->st = (struct stat *)(mem[1] + root->offset);
    root->st->st_ino = 0;
    root->st->st_mode = S_IFDIR | 0755;
    root->st->st_uid = fuse_get_context()->uid;
    root->st->st_gid = fuse_get_context()->gid;
    root->st->st_size = 0;
    root->st->st_blksize = BLOCK_SIZE;
    root->st->st_blocks = 0;
    root->block_num = 1;
    root->inode = NULL;
    return NULL;
}

static int lfs_getattr(const char *path, struct stat *stbuf) {  // 与示例程序相同
    int ret = 0;
    filenode *node = get_filenode(path);
    if(strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
    }
    else if(node) {
        memcpy(stbuf, node->st, sizeof(struct stat));
    }
    else {
        ret = -ENOENT;
    }
    return ret;
}

static int lfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    // 于示例程序相同
    filenode *node = root->next;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node) {
        filler(buf, node->filename, node->st, 0);
        node = node->next;
    }
    return 0;
}

static int lfs_mknod(const char *path, mode_t mode, dev_t dev) {
    // 与示例程序相同
    struct stat st;
    st.st_mode = S_IFREG | 0644;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    st.st_blksize = BLOCK_SIZE;
    st.st_blocks = 0;
    int flag = create_filenode(path + 1, &st);
    if(flag == -1)              // 空间不够，创建文件失败
        return -ENOSPC;
    return 0;
}

static int lfs_open(const char *path, struct fuse_file_info *fi) {      // 与示例程序相同
    return 0;
}

static int lfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    // 写入函数
    long temp = ((long)BLOCK_NR - (long)SNode->usedblock) * (long)BLOCK_SIZE;
    if((long)size > temp) {             // 判断是否还有空间进行写入
        printf("No space to write\n");
        return -ENOSPC;                 // 没有空间返回 -ENOSPC
    }

    filenode *node = get_filenode(path);
    if(offset + size > node->st->st_size) {
        node->st->st_size = offset + size;
        node->st->st_blocks = node->st->st_size / BLOCK_SIZE;
    }
    int used_block = (int)offset / BLOCK_SIZE;      // 确定偏移量跳过多少块
    int off = (int)offset % BLOCK_SIZE;             // 确定一块中的偏移量
    int used_inode = used_block / DATA_BLOCKS_NUM;      // 确定偏移量跳过多少 inode
    used_block = used_block % DATA_BLOCKS_NUM;      // 偏移量所确定的 inode 中块的位置
    int i = 0;
    int rest_size = (int)size;      // 记录剩余待写量
    INode *inode = node->inode;
    for(i = 0; i < used_inode; i++) {       // 跳转到待写 inode
        if(inode->next == NULL) {       // 恰好为一 inode 节点末尾，则需开辟新的 inode 节点
            int new_inode = lfs_find_free_block();
            inode->next = (INode *)lfs_malloc(new_inode);
            if (inode->next == (INode *)-1)
                return -ENOSPC;
            for(int k = 0; k < DATA_BLOCKS_NUM; k++)
                inode->next->Data_block[k] = -1;
            inode->next->next = NULL;
            inode->next->block_num = new_inode;
        }
        inode = inode->next;
    }

    Data *data_node;        // 数据块
    if(inode->Data_block[used_block] > 0) {     // 当前位置数据块已有空间
        data_node = (Data *)mem[inode->Data_block[used_block]];     // 数据块赋值
    }
    else {          // 当前位置数据块无空间，则需要申请新的空间
        int fr_block = lfs_find_free_block();
        data_node = (Data *)lfs_malloc(fr_block);
        if(data_node == (Data *)-1)
            return -ENOSPC;
        inode->Data_block[used_block] = fr_block;       // 记录新空间块数
    }
    if((int)size > BLOCK_SIZE - off) {      // 当前一块数据块无法装下 buf 中数据
        memcpy(data_node->content + off, buf, BLOCK_SIZE - off);
        rest_size -= (BLOCK_SIZE - off);    // 剩余量更新
    }
    else {              // 能装下 buf 中数据
        memcpy(data_node->content + off, buf, (int)size);
        return size;        // 写入后直接返回
    }

    int need_block = ((int)size - (BLOCK_SIZE - off)) / BLOCK_SIZE + 1;
    used_block = (used_block + 1) % DATA_BLOCKS_NUM;
    int free_block = 0;
    while(rest_size > 0) {      // 进入循环，每次剩余量减一块的大小
        if(used_block == 0) {       // 一个 inode 用完，申请新的 inode
            free_block = lfs_find_free_block();
            inode->next = (INode *)lfs_malloc(free_block);
            if(inode->next == (INode *)-1)
                return -ENOSPC;
            inode = inode->next;
            inode->block_num = free_block;
            for(int j = 0; j < DATA_BLOCKS_NUM; j++)
                inode->Data_block[j] = -1;
            inode->next = NULL;
        }
        free_block = lfs_find_free_block();
        data_node = (Data *)lfs_malloc(free_block);     // 申请数据块
        if(data_node == (Data *)-1)
            return -ENOSPC;
        inode->Data_block[used_block] = free_block;
        if(rest_size < BLOCK_SIZE) {                    // 剩余量小于一块的大小
            memcpy(data_node->content, buf, rest_size);     // 写入
        }
        else {
            memcpy(data_node->content, buf, BLOCK_SIZE);    // 写入
        }
        used_block = (used_block + 1) % DATA_BLOCKS_NUM;    // 数据块索引更新
        rest_size -= BLOCK_SIZE;            // 剩余量更新
    }
    return size;
}

static int lfs_truncate(const char *path, off_t size) {     // 截断
    filenode *node = get_filenode(path);
    node->st->st_size = size;
    node->st->st_blocks = node->st->st_size / BLOCK_SIZE;
    int curr_size = (int)size;      // 记录当前剩余偏移量，用来找到截断点
    int curr_block = 0;
    INode *inode = node->inode;
    Data *data = (Data *)mem[inode->Data_block[curr_block]];
    while(curr_size > 0) {
        if(curr_size < BLOCK_SIZE) {                    // 剩余偏移量小于块大小，即在当前块偏移量之后全部截断
            memset(data->content + curr_size, 0, BLOCK_SIZE - curr_size);
            if(curr_block == DATA_BLOCKS_NUM - 1 && inode->next) {      // 正好为当前 inode 的最后一块
                lfs_free_inode(inode->next, 0);         // 下一个 inode 之后全部释放
                inode->next = NULL;
            }
            else {      // 释放掉下一块之后的空间
                lfs_free_inode(inode, curr_block + 1);
                inode->next = NULL;         
            }
            return 0;
        }
        else if(curr_size == BLOCK_SIZE) {                              // 正好为一块，从下一块起全部截断
            if(curr_block == DATA_BLOCKS_NUM - 1 && inode->next) {      // 正好为当前 inode 的最后一块
                lfs_free_inode(inode->next, 0);             // 下一个 inode 之后全部释放
                inode->next = NULL;
            }
            else {                          // 释放掉下一块之后的空间
                lfs_free_inode(inode, curr_block + 1);
                inode->next = NULL;
            }
            return 0;
        }
        else {
            curr_size -= BLOCK_SIZE;                // 更新剩余偏移量
            curr_block = (curr_block + 1) % DATA_BLOCKS_NUM;
            if(curr_block == 0) {               // 移至下一 inode
                inode = inode->next;
            }
        }
    }
    return 0;
}

static int lfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {        
    // 读取函数
    filenode *node = get_filenode(path);
    int ret = size;
    if(offset + size > node->st->st_size)
        ret = node->st->st_size - offset;
    int block_num = 0;
    INode *inode = node->inode;
    Data *data = (Data *)mem[inode->Data_block[0]];
    int rest_size = (int)size;
    int off = (int)offset;                      // 记录偏移量
    while(off >= BLOCK_SIZE) {                  // 找到偏移量所对应的那一块
        block_num = (block_num + 1) % DATA_BLOCKS_NUM;
        off -=BLOCK_SIZE;
        if(block_num == 0)
            inode = inode->next;
    }

    if(inode->Data_block[block_num] == -1)
        return ret;
    data = (Data *)mem[inode->Data_block[block_num]];
    if(size > BLOCK_SIZE - off) {                               // 对偏移量所定位的第一块读取
        memcpy(buf, data->content + off, BLOCK_SIZE - off);     // 读取数据
    }
    else {
        memcpy(buf, data->content + off, size);
        return ret;
    }
    int used_size = (BLOCK_SIZE - off);             // 记录已读取数据量
    rest_size -= (BLOCK_SIZE - off);                // 剩余待读取数据量更新
    block_num = (block_num + 1) % DATA_BLOCKS_NUM;
    while(rest_size > 0) {                      // 循环读取之后 size 大小内每一块中的数据
        if(block_num == 0) {
            inode = inode->next;
        }
        
        data = (Data *)mem[inode->Data_block[block_num]];

        if(rest_size < BLOCK_SIZE) {
            memcpy(buf + used_size, data->content, rest_size);      // 读取
        }
        else {
            memcpy(buf + used_size, data->content, BLOCK_SIZE);
            used_size += BLOCK_SIZE;                            // 更新已读取量
            block_num = (block_num + 1) % DATA_BLOCKS_NUM;
        }
        rest_size -= BLOCK_SIZE;            // 更新未读取量
    }
    return ret;
}

static int lfs_unlink(const char *path) {
    filenode *node = get_filenode(path);
    if(lfs_free_inode(node->inode, 0)) {    // 从当前文件节点的第一个 inode 开始释放
        printf("unlink error!\n");
        return -1;
    }
    node->last->next = node->next;
    if(node->next)
        node->next->last = node->last;      // 文件间指针关系更改
    bitmap[node->block_num] = 0;
    munmap(mem[node->block_num], BLOCK_SIZE);   // 释放文件节点空间
    SNode->usedblock--;             // 文件数减一
    return 0;
}

static const struct fuse_operations op = {      // 与示例程序相同
    .init = lfs_init,
    .getattr = lfs_getattr,
    .readdir = lfs_readdir,
    .mknod = lfs_mknod,
    .open = lfs_open,
    .write = lfs_write,
    .truncate = lfs_truncate,
    .read = lfs_read,
    .unlink = lfs_unlink,
};

int main(int argc, char *argv[]){           // 与示例程序相同
    return fuse_main(argc, argv, &op, NULL);
}