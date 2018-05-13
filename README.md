# lab3——内存文件系统

### 张立夫-PB15020718

## 完成部分

1. 设计文件系统块大小 blocksize `BLOCK_SIZE = 4096`

2. 设计一段连续的文件系统地址空间，文件系统地址空间大小为 size `SIZE = (4096 * 1024 * 1024)` ，则共有 blocknr=size/blocksize 个 block `BLOCK_NR = (1024 * 1024)`

3. 自行设计存储算法，将文件系统的所有内容（包括元数据和链表指针等）均映射到文件系统地址空间。

   - 创建一超级节点 `SNode`，其中存放一共使用的块数和文件数

   - 设计一个文件节点 `filenode` 指向一个 `inode` 节点，每一个 `inode` 节点有大小为 100 的数组，存储 `Data` 节点在 mem 数组中的序号。如果该 100 个 `Data` 节点大小不够，则会开辟新的 `inode` 节点，由上一个 `inode` 节点的 `next` 指针指向新节点，以此达到扩展的目的。
   - 上述 `filenode`, `inode`, `Data` 均对应 mem 数组中的单独一个，即每一个 `filenode` 对应单独一个文件，每一个 `inode` 对应 100 个数据块 `Data` ，每一个数据块 `Data` 大小为 `BLOCK_SIZE = 4096` 

4. 将文件系统地址空间切分为blocknr个大小为blocksize的块，并在需要时将对应块通过mmap映射到进程地址空间，在不再需要时通过munmap释放对应内存

   - 在创建新文件或增加写入时会进行新的节点的申请，会先找到未被分配的 mem 元素，即仍有剩余空间，申请到 `BLOCK_SIZE` 大小的空间，再将由 mem 存储的空间地址进行强制类型转换赋值给所需节点，以此实现动态申请空间
   - 在有文件或内容被删除或截断时，会对对应的文件节点的指针进行处理，之后会对不再占用的 mem 空间进行释放，以达到动态释放
   - 在调用写入函数时，如果写入大小 `size` 大于剩余空间 `(BLOCK_NR - SNode->usedblock) * BLOCK_SIZE` 时，会返回剩余空间不足，无法进行写入。

5. 实现了 4K 对齐，即数据块完全由 4K 大小的数据构成，其由 `inode` 其中的数组序号进行索引，而未在数据块中增加指针进行数据的链接操作，以实现对齐。

## 数据结构

1. 文件节点

   ```c
   typedef struct filenode {
       char filename[255];			// 文件名
       int32_t block_num;			// 该文件节点所在 mem 中位置
       int32_t offset;				// 在 mem 块中偏移量
       INode *inode;				// inode 节点指针
       struct stat *st;			// stat 指针
       struct filenode *next;		// 下一文件节点
       struct filenode *last;		// 上一文件节点
   }filenode;
   ```

2. inode 节点

   ```c
   typedef struct INode {
       struct INode *next;			// 下一 inode 节点
       int32_t block_num;			// 该节点所在 mem 中位置
       int32_t Data_block[DATA_BLOCKS_NUM];	// 100 个数据节点所在 mem 中位置
   }INode;
   ```

3. Data 节点

   ```c
   typedef struct Data {
       char content[BLOCK_SIZE];		// 数据内容
   }Data;
   ```

4. SuperNode 节点

   ```c
   typedef struct SuperNode {
       int32_t filenum;				// 目前已存放文件数量
       int32_t usedblock;				// 目前已使用块数
   }SuperNode;
   SuperNode *SNode; 				// 超级节点存放在 mem[0] 中
   ```

## 函数实现

具体函数实现见源代码注释，不在其中赘述。