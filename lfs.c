#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>

#define DATA_BLOCKS_NUM 100  // 100 个 Data Blocks
#define SIZE (4096 * 1024 * 1024)
#define BLOCK_SIZE 4096
#define BLOCK_NR (1024 * 1024)

int ttt = 0;

typedef struct INode {
    struct INode *next;
    int32_t block_num;
    int32_t Data_block[DATA_BLOCKS_NUM];
}INode;

typedef struct filenode {
    char filename[255];
    int32_t block_num;
    int32_t offset;
    INode *inode;
    struct stat *st;
    struct filenode *next;
    struct filenode *last;
}filenode;

typedef struct Data {
    char content[BLOCK_SIZE];
}Data;

typedef struct SuperNode {
    int32_t filenum;
    int32_t usedblock;
}SuperNode;
SuperNode *SNode;    // 超级节点，存放在 mem[0] 地址

static void *mem[BLOCK_NR];

static filenode *root = NULL;

void *lfs_malloc(int block_num) {
    if(mem[block_num]) {
        printf("malloc error: not empty block!\n");
        exit(-1);
    }
    size_t blocksize = (size_t)BLOCK_SIZE;
    mem[block_num] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(mem[block_num], 0, BLOCK_SIZE);
    SNode->usedblock++;
    return mem[block_num];
}

int lfs_free_inode(INode *inode, int flag) {
    int stat = flag;
    if(inode->next)
        lfs_free_inode(inode->next, 0);
    for(; flag < DATA_BLOCKS_NUM; flag++) {
        if(inode->Data_block[flag] == -1) {
            break;
        }
        munmap(mem[inode->Data_block[flag]], BLOCK_SIZE);
        inode->Data_block[flag] = -1;
        SNode->usedblock--;
    }
    if(stat == 0) {
        if(mem[inode->block_num] == NULL)
        munmap(mem[inode->block_num], BLOCK_SIZE);
    }
    return 0;
}

int lfs_find_free_block() {
    int i = 1;
    while(mem[i])
        i++;
    if(i <= BLOCK_NR)
        return i;
    return -1;
}

static struct filenode *get_filenode(const char *name) {
    struct filenode *node = root;
    while(node) {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else 
            return node;
    }
    return NULL;
}

static void create_filenode(const char *filename, const struct stat *st) {
    filenode *new;
    int block_num = lfs_find_free_block();
    new = (filenode *)lfs_malloc(block_num);
    new->block_num = block_num;
    memcpy(new->filename, filename, strlen(filename) + 1);
    new->offset = sizeof(struct filenode);
    new->st = (struct stat *)(mem[block_num] + new->offset);
    memcpy(new->st, st, sizeof(struct stat));
    new->offset += sizeof(struct stat);
    int inode_block_num = lfs_find_free_block();
    new->inode = (INode *)lfs_malloc(inode_block_num);
    new->inode->block_num = inode_block_num;
    new->inode->next = NULL;
    for(int j = 0; j < DATA_BLOCKS_NUM; j++)
        new->inode->Data_block[j] = -1;
    new->next = root->next;
    if(root->next)
        root->next->last = new;
    root->next = new;
    new->last = root;
    SNode->filenum++;
}

static void *lfs_init(struct fuse_conn_info *conn) {
    size_t blocksize = (size_t)BLOCK_SIZE;
    for(int i = 0; i < 2; i++) {
        mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memset(mem[i], 0, blocksize);
    }
    SNode = (SuperNode *)mem[0];
    SNode->filenum = 0;
    SNode->usedblock = 2;
    root = (filenode *)mem[1];
    root->last = NULL;
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

static int lfs_getattr(const char *path, struct stat *stbuf) {
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
    filenode *node = root->next;
    //printf("using lfs_readdir %d\n", ttt++);
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node) {
        //puts(node->filename);
        filler(buf, node->filename, node->st, 0);
        node = node->next;
    }
    return 0;
}

static int lfs_mknod(const char *path, mode_t mode, dev_t dev) {
    struct stat st;
    st.st_mode = S_IFREG | 0644;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    st.st_blksize = BLOCK_SIZE;
    st.st_blocks = 0;
    create_filenode(path + 1, &st);
    return 0;
}

static int lfs_open(const char *path, struct fuse_file_info *fi) {
    return 0;
}

static int lfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    long temp = ((long)BLOCK_NR - (long)SNode->usedblock) * (long)BLOCK_SIZE;
    if((long)size > temp) {
        printf("No space to write\n");
        return -ENOSPC;
    }

    filenode *node = get_filenode(path);
    if(offset + size > node->st->st_size) {
        node->st->st_size = offset + size;
        node->st->st_blocks = node->st->st_size / BLOCK_SIZE;
    }
    int used_block = (int)offset / BLOCK_SIZE;
    int off = (int)offset % BLOCK_SIZE;
    int used_inode = used_block / DATA_BLOCKS_NUM;
    used_block = used_block % DATA_BLOCKS_NUM;
    int i = 0;
    int rest_size = (int)size;
    INode *inode = node->inode;
    for(i = 0; i < used_inode; i++) {
        if(inode->next == NULL) {
            int new_inode = lfs_find_free_block();
            inode->next = (INode *)lfs_malloc(new_inode);
            for(int k = 0; k < DATA_BLOCKS_NUM; k++)
                inode->next->Data_block[k] = -1;
            inode->next->next = NULL;
            inode->next->block_num = new_inode;
        }
        inode = inode->next;
    }

    Data *data_node;
    if(inode->Data_block[used_block] > 0) {
        if(mem[inode->Data_block[used_block]] == NULL)
        data_node = (Data *)mem[inode->Data_block[used_block]];
    }
    else {
        int fr_block = lfs_find_free_block();
        data_node = (Data *)lfs_malloc(fr_block);
        inode->Data_block[used_block] = fr_block;
    }
    if((int)size > BLOCK_SIZE - off) {
        memcpy(data_node->content + off, buf, BLOCK_SIZE - off);
        rest_size -= (BLOCK_SIZE - off);
    }
    else {
        memcpy(data_node->content + off, buf, (int)size);
        return size;
    }

    int need_block = ((int)size - (BLOCK_SIZE - off)) / BLOCK_SIZE + 1;
    used_block = (used_block + 1) % DATA_BLOCKS_NUM;
    int free_block = 0;
    while(rest_size > 0) {      // need fix
        if(used_block == 0) {
            free_block = lfs_find_free_block();
            inode->next = (INode *)lfs_malloc(free_block);
            inode = inode->next;
            inode->block_num = free_block;
            for(int j = 0; j < DATA_BLOCKS_NUM; j++)
                inode->Data_block[j] = -1;
            inode->next = NULL;
        }
        free_block = lfs_find_free_block();
        data_node = (Data *)lfs_malloc(free_block);
        inode->Data_block[used_block] = free_block;
        if(rest_size < BLOCK_SIZE) {
            memcpy(data_node->content, buf, rest_size);
        }
        else {
            memcpy(data_node->content, buf, BLOCK_SIZE);
        }
        used_block = (used_block + 1) % DATA_BLOCKS_NUM;
        rest_size -= BLOCK_SIZE;
    }
    return size;
}

static int lfs_truncate(const char *path, off_t size) {
    filenode *node = get_filenode(path);
    node->st->st_size = size;
    node->st->st_blocks = node->st->st_size / BLOCK_SIZE;
    int curr_size = (int)size;
    int curr_block = 0;
    INode *inode = node->inode;
    Data *data = (Data *)mem[inode->Data_block[curr_block]];
    while(curr_size > 0) {
        if(curr_size < BLOCK_SIZE) {
            memset(data->content + curr_size, 0, BLOCK_SIZE - curr_size);
            if(curr_block == DATA_BLOCKS_NUM - 1 && inode->next) {
                lfs_free_inode(inode->next, 0);
                inode->next = NULL;
            }
            else {
                lfs_free_inode(inode, curr_block + 1);
                inode->next = NULL;         
            }
            return 0;
        }
        else if(curr_size == BLOCK_SIZE) {
            if(curr_block == DATA_BLOCKS_NUM - 1 && inode->next) {
                lfs_free_inode(inode->next, 0);
                inode->next = NULL;
            }
            else {
                lfs_free_inode(inode, curr_block + 1);
                inode->next = NULL;
            }
            return 0;
        }
        else {
            curr_size -= BLOCK_SIZE;
            curr_block = (curr_block + 1) % DATA_BLOCKS_NUM;
            if(curr_block == 0) {
                inode = inode->next;
            }
        }
    }
    return 0;
}

static int lfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    filenode *node = get_filenode(path);
    int ret = size;
    if(offset + size > node->st->st_size)
        ret = node->st->st_size - offset;
    int block_num = 0;
    INode *inode = node->inode;
    Data *data = (Data *)mem[inode->Data_block[0]];
    int rest_size = (int)size;
    int off = (int)offset;
    while(off >= BLOCK_SIZE) {
        block_num = (block_num + 1) % DATA_BLOCKS_NUM;
        off -=BLOCK_SIZE;
        if(block_num == 0)
            inode = inode->next;
    }

    if(inode->Data_block[block_num] == -1)
        return ret;
    data = (Data *)mem[inode->Data_block[block_num]];
    if(size > BLOCK_SIZE - off) {
        memcpy(buf, data->content + off, BLOCK_SIZE - off);
    }
    else {
        memcpy(buf, data->content + off, size);
        return ret;
    }
    int used_size = (BLOCK_SIZE - off);
    rest_size -= (BLOCK_SIZE - off);
    block_num = (block_num + 1) % DATA_BLOCKS_NUM;
    while(rest_size > 0) {
        if(block_num == 0) {
            inode = inode->next;
        }
        
        data = (Data *)mem[inode->Data_block[block_num]];

        if(rest_size < BLOCK_SIZE) {
            memcpy(buf + used_size, data->content, rest_size);
        }
        else {
            memcpy(buf + used_size, data->content, BLOCK_SIZE);
            used_size += BLOCK_SIZE;
            block_num = (block_num + 1) % DATA_BLOCKS_NUM;
        }
        rest_size -= BLOCK_SIZE;
    }
    return ret;
}

static int lfs_unlink(const char *path) {
    filenode *node = get_filenode(path);
    if(lfs_free_inode(node->inode, 0)) {
        exit(-1);
    }
    node->last->next = node->next;
    if(node->next)
        node->next->last = node->last;
    munmap(mem[node->block_num], BLOCK_SIZE);
    SNode->usedblock--;
    return 0;
}

static const struct fuse_operations op = {
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

int main(int argc, char *argv[]){
    return fuse_main(argc, argv, &op, NULL);
}