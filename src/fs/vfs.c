#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <sys/errno.h>
#include <sys/dirent.h>
#include <sys/stat.h>

#include <fs/vfs.h>
#include <dev/devices.h>
#include <conf.h>

#define __DEBUG
#include <log.h>


#define FS_SEP  '/'

typedef uint16_t uid_t, gid_t;
typedef size_t inode_t;

typedef struct superblock   mountnode;
typedef struct fs_ops       fs_ops;
typedef struct inode        inode;
typedef struct fsdriver     fsdriver;


err_t vfs_mount(dev_t source, const char *target, const mount_opts_t *opts);



struct superblock {
    dev_t       sb_dev;
    fsdriver   *sb_fs;

    size_t      sb_blksz;
    struct {
        bool dirty :1 ;
        bool ro :1 ;
    } sb_flags;

    void *sb_data;                /* superblock-specific info */

    const char *sb_mntpath;       /* path relative to the parent mountpoint */
    uint32_t sb_mntpath_hash;     /* hash of this relmntpath */
    mountnode *sb_brother;        /* the next superblock in the list of childs of parent */
    mountnode *sb_parent;
    mountnode *sb_children;       /* list of this superblock child blocks */
};

struct superblock *theRootMnt = NULL;

#define N_DIRECT_BLOCKS  12
#define MAX_SHORT_SYMLINK_SIZE   60

struct inode {
    index_t i_no;               /* inode index */
    mode_t  i_mode;             /* inode type + unix permissions */
    count_t i_nlinks;
    void   *i_data;             /* fs- and type-specific info */

    union {
        struct {
            size_t block_coout; // how many blocks its data takes
            size_t directblock[ N_DIRECT_BLOCKS ];  // numbers of first DIRECT_BLOCKS blocks
            size_t indir1st_block;
            size_t indir2nd_block;
            size_t indir3rd_block;
        } reg;
        struct { } dir;
        struct {
            majdev_t maj;
            mindev_t min;
        } dev;
        struct {
            char short_symlink[ MAX_SHORT_SYMLINK_SIZE ];
            const char *long_symlink;
        } symlink;
        struct { } socket;
        struct { } namedpipe;
    } as;
};


struct fsdriver {
    const char *name;
    uint fs_id;
    fs_ops *ops;

    struct {
        fsdriver *prev, *next;
    } lst;
};



struct fs_ops {
    int (*read_superblock)(mountnode *sb);
    int (*make_directory)(mountnode *sb, inode_t *ino, const char *path, mode_t mode);
    int (*get_direntry)(mountnode *sb, inode_t ino, void **iter, struct dirent *dir);
    int (*make_inode)(mountnode *sb, inode_t *ino, mode_t mode, void *info);
    int (*lookup_inode)(mountnode *sb, inode_t *ino, const char *path, size_t pathlen);
    int (*link_inode)(mountnode *sb, inode_t ino, const char *path);
    int (*unlink_inode)(mountnode *sb, inode_t ino);
};

fsdriver * theFileSystems = NULL;

void vfs_register_filesystem(fsdriver *fs) {
    if (!theFileSystems) {
        theFileSystems = fs;
        fs->lst.next = fs->lst.prev = fs;
        return;
    }

    /* insert just before theFileSystems in circular list */
    fsdriver *lastfs = theFileSystems->lst.prev;
    fs->lst.next = theFileSystems;
    fs->lst.prev = lastfs;
    lastfs->lst.next = fs;
    theFileSystems->lst.prev = fs;
};

fsdriver *vfs_fs_by_id(uint fs_id) {
    fsdriver *fs = theFileSystems;
    if (!fs) return NULL;

    do {
        if (fs->fs_id == fs_id)
            return fs;
    } while ((fs = fs->lst.next) != theFileSystems);
    return NULL;
}


/*
 *  Sysfs
 */
#if 0
#define SYSFS_ID  0x00535953

static int sysfs_read_superblock(struct superblock *sb);
static int sysfs_make_directory(struct superblock *sb, const char *path, mode_t mode);
static int sysfs_get_direntry(struct superblock *sb, void **iter, struct dirent *dir);
static int sysfs_make_inode(struct superblock *sb, mode_t mode, void *info);

fs_ops sysfs_fsops = {
    .read_superblock = sysfs_read_superblock,
    .make_directory = sysfs_make_directory,
    .get_direntry = sysfs_get_direntry,
    .make_inode = sysfs_make_inode,
    .lookup_inode = sysfs_lookup_inode,
    .link_inode = sysfs_link_inode,
    .unlink_inode = sysfs_unlink_inode,
};

struct sysfs_data {
    struct dirent *rootdir,
};

fsdriver_t sysfs_driver = {
    .name = "sysfs",
    .fs_id = SYSFS_ID, /* "SYS" */
    .ops = &sysfs_fsops,
};

struct dirent sysfs_inode_table[] = {
};

static int sysfs_read_superblock(mountnode *sb) {
    sb->dev = gnu_dev_makedev(CHR_VIRT, CHR0_SYSFS);
    sb->data = { .rootdir =
    return 0;
}

static int sysfs_make_directory(mountnode *sb, inode_t *ino, const char *path, mode_t mode) {
    return -ETODO;
}

static int sysfs_get_direntry(mountnode *sb, inode_t ino, void **iter, struct dirent *dir) {
}
#endif


/*
 *  ramfs
 */

/* ASCII "RAM" */
#define RAMFS_ID  0x004d4152

static int ramfs_read_superblock(mountnode *sb);
static int ramfs_lookup_inode(mountnode *sb, inode_t *ino, const char *path, size_t pathlen);
static int ramfs_make_directory(mountnode *sb, inode_t *ino, const char *path, mode_t mode);
static int ramfs_get_direntry(mountnode *sb, inode_t dirnode, void **iter, struct dirent *dirent);

struct fs_ops ramfs_fsops = {
    .read_superblock = ramfs_read_superblock,
    .lookup_inode = ramfs_lookup_inode,
    .make_directory = ramfs_make_directory,
    .get_direntry = ramfs_get_direntry,
};

struct fsdriver ramfs_driver = {
    .name = "ramfs",
    .fs_id = RAMFS_ID,
    .ops = &ramfs_fsops,
    .lst = { 0 },
};

struct inode invalid_inode;

static void ramfs_inode_free(struct inode *idata);

/* this structure is a "hierarchical" lookup table from any large index to some pointer */
struct btree_node {
    int     bt_level;       /* if 0, bt_children are leaves */
    size_t  bt_fanout;      /* how many children for a node */
    size_t  bt_used;        /* how many non-NULL children are there */
    void *  bt_children[0]; /* child BTree nodes or leaves */
};


static struct btree_node * btree_new(size_t fanout) {
    size_t bchildren_len = sizeof(void *) * fanout;
    struct btree_node *bnode = kmalloc(sizeof(struct btree_node) + bchildren_len);
    if (!bnode) return NULL;

    bnode->bt_level = 0;
    bnode->bt_used = 0;
    bnode->bt_fanout = fanout;
    memset(&(bnode->bt_children), 0, bchildren_len);

    logmsgdf("btree_new(%d) -> *%x\n", fanout, (uint)bnode);
    return bnode;
}

static void btree_free(struct btree_node *bnode) {
    size_t i;
    for (i = 0; i < bnode->bt_fanout; ++i) {
        if (bnode->bt_level == 0) {
            struct inode *idata = bnode->bt_children[i];
            if (idata && (idata != &invalid_inode))
                ramfs_inode_free(idata);
        } else {
            struct btree_node *bchild = bnode->bt_children[i];
            if (bchild)
                btree_free(bchild);
        }
    }
}

/* get leaf or NULL for index */
static void * btree_get_index(struct btree_node *bnode, size_t index) {
    int i;
    int btree_lvl = bnode->bt_level;
    size_t fanout = bnode->bt_fanout;
    size_t btsize = fanout;

    for (i = 0; i < btree_lvl; ++i)
        btsize *= fanout;

    if (btsize <= index)
        return NULL;

    /* get btree item */
    while (btree_lvl-- > 0) {
        btsize /= fanout;
        size_t this_node_index = index / btsize;
        index %= btsize;

        struct btree_node *subnode = bnode->bt_children[ this_node_index ];
        if (!subnode) return NULL;
        bnode = subnode;
    }
    return bnode->bt_children[ index ];
}

/*
 *      Searches B-tree for a free leaf,
 *      returns 0 if no free leaves found.
 *      0 index must be taken by an "invalid" entry.
 */
static inode_t btree_set_leaf(struct btree_node *bnode, struct inode *idata) {
    const char *funcname = "btree_set_leaf";

    size_t i;
    size_t fanout = bnode->bt_fanout;
    if (bnode->bt_level == 0) {
        if (bnode->bt_used < fanout) {
            for (i = 0; i < fanout; ++i)
                if (bnode->bt_children[i] == NULL) {
                    ++bnode->bt_used;
                    bnode->bt_children[i] = idata;

                    logmsgdf("%s(%d) to *%x, bnode=*%x, bt_used=%d\n",
                             funcname, i, (uint)idata, (uint)bnode, bnode->bt_used);
                    return i;
                }
        } else {
            /* no free leaves here */
            logmsgdf("no free leaves here\n");
            return 0;
        }
    } else {
        /* search in subnodes */
        for (i = 0; i < fanout; ++i) {
            struct btree_node *bchild = bnode->bt_children[i];
            if (!bchild) continue;

            int res = btree_set_leaf(bchild, idata);
            if (res > 0) {
                int j = bnode->bt_level;
                size_t index_offset = 1;
                while (j-- > 0)
                    index_offset *= fanout;
                res += i * index_offset;
                return res;
            }
        }
    }
    return 0;
}

/*
 *      Searches for a place for a new leaf with btree_set_leaf()
 *      if no free leaves:
 *          grows up a node level,
 *          old root becomes new_root[0],
 *          grows down to level 0 from new_root[1] and inserts idata there.
 *          *btree_root redirected to new_root.
 *
 *      Do not use with an empty btree, since index 0 must be invalid!
 */
static inode_t btree_new_leaf(struct btree_node **btree_root, struct inode *idata) {
    logmsgdf("btree_new_leaf(idata=*%x)\n", (uint)idata);

    int res = btree_set_leaf(*btree_root, idata);
    if (res > 0) return res;

    logmsgdf("btree_new_leaf: adding a new level\n");
    struct btree_node *old_root = *btree_root;
    int lvl = old_root->bt_level + 1;
    size_t fanout = old_root->bt_fanout;

    struct btree_node *new_root = NULL;
    struct btree_node *new_node = NULL;

    new_root = btree_new(fanout);
    if (!new_root) return 0;
    new_root->bt_level = old_root->bt_level + 1;

    new_root->bt_children[0] = old_root;
    ++new_root->bt_used;

    old_root = NULL;
    res = 1;
    while (lvl-- > 0) {
        new_node = btree_new(fanout);
        if (!new_root) {
            /* unwind new_root[1] and free all its subnodes */
            if (new_root->bt_children[1])
                btree_free(new_root->bt_children[1]);
            return 0;
        }

        if (old_root) {
            old_root->bt_children[0] = new_node;
            ++old_root->bt_used;
        } else {
            new_root->bt_children[1] = new_node;
            ++new_root->bt_used;
        }
        old_root = new_node;
        res *= fanout;
    }

    *btree_root = new_root;
    return res;
}


/* this is a directory hashtable array entry */
/* it is an intrusive list */
struct ramfs_direntry {
    uint32_t  de_hash;
    char     *de_name;          /* its length should be a power of 2 */
    inode_t   de_ino;           /* the actual value, inode_t */

    struct ramfs_direntry *htnext;  /* next in hashtable collision list */
};

/* this is a container for directory hashtable */
struct ramfs_directory {
    size_t size;                /* number of values in the hashtable */
    size_t htcap;               /* hashtable capacity */
    struct ramfs_direntry **ht; /* hashtable array */
};


static int ramfs_directory_new(struct ramfs_directory **dir) {
    const char *funcname = "ramfs_directory_new";

    struct ramfs_directory *d = kmalloc(sizeof(struct ramfs_directory));
    if (!d) goto enomem_exit;

    d->size = 0;
    d->htcap = 8;
    size_t htlen = d->htcap * sizeof(void *);
    d->ht = kmalloc(htlen);
    if (!d->ht) goto enomem_exit;
    memset(d->ht, 0, htlen);

    logmsgdf("%s: new directory *%x\n", funcname, (uint)d);
    *dir = d;
    return 0;

enomem_exit:
    logmsgdf("%s: ENOMEM\n");
    if (d) kfree(d);
    return ENOMEM;
}

static int ramfs_directory_insert(struct ramfs_directory *dir,
                                  struct ramfs_direntry *de)
{
    size_t ht_index = de->de_hash % dir->htcap;
    struct ramfs_direntry *bucket = dir->ht[ ht_index ];
    /* TODO: possibly rebalance */
    if (bucket) {
        while (true) {
            if (bucket->de_hash == de->de_hash) {
                if (!strcmp(bucket->de_name, de->de_name))
                    return EEXIST;
                else
                    logmsgef("hash collision detected: '%s'/'%s' both hash to 0x%x\n",
                              bucket->de_name, de->de_name, de->de_hash);
            }

            if (!bucket->htnext)
                break;
            bucket = bucket->htnext;
        }
        bucket->htnext = de;
    } else {
        dir->ht[ ht_index ] = de;
    }
    de->htnext = NULL;

    ++dir->size;
    return 0;
}

static int ramfs_directory_new_entry(
        mountnode *sb, struct ramfs_directory *dir,
        const char *name, struct inode *idata)
{
    logmsgf("ramfs_directory_new_entry(%s)\n", name);
    int ret = 0;
    struct ramfs_direntry *de = kmalloc(sizeof(struct ramfs_direntry));
    if (!de) return ENOMEM;

    de->de_name = strdup(name);
    if (!de->de_name) { kfree(de); return ENOMEM; }

    de->de_hash = strhash(name, strlen(name));
    de->de_ino = idata->i_no;

    ret = ramfs_directory_insert(dir, de);
    if (ret) return ret;

    ++ idata->i_nlinks;
    return 0;
}

static void ramfs_directory_free(struct ramfs_directory *dir) {
    const char *funcname = "ramfs_directory_free";
    logmsgdf("%s(*%x)\n", funcname, (uint)dir);
    size_t i;

    for (i = 0; i < dir->htcap; ++i) {
        struct ramfs_direntry *bucket = dir->ht[ i ];
        if (!bucket) continue;

        struct ramfs_direntry *nextbucket = NULL;
        while (bucket) {
            nextbucket = bucket->htnext;
            kfree(bucket->de_name);
            kfree(bucket);
            bucket = nextbucket;
        }
    }

    kfree(dir->ht);
    kfree(dir);
}


/* used by struct superblock as `data` pointer to store FS-specific state */
struct ramfs_data {
    struct btree_node *inodes_btree;  /* map from inode_t to struct inode */
    inode_t root_ino;                 /* root directory inode */
};

static int ramfs_data_new(mountnode *sb) {
    struct ramfs_data *data = kmalloc(sizeof(struct ramfs_data));
    if (!data) return ENOMEM;

    /* a B-tree that maps inode indexes to actual inodes */
    struct btree_node *bnode = btree_new(64);
    if (!bnode) {
        kfree(data);
        return ENOMEM;
    }
    /* fill inode 0 */
    invalid_inode.i_no = 0;
    btree_set_leaf(bnode, &invalid_inode);

    data->inodes_btree = bnode;

    sb->sb_data = data;
    return 0;
}

static void ramfs_data_free(mountnode *sb) {
    struct ramfs_data *data = sb->sb_data;

    btree_free(data->inodes_btree);

    kfree(sb->sb_data);
    sb->sb_data = NULL;
}



static int ramfs_inode_new(mountnode *sb, struct inode **iref, mode_t mode) {
    int ret = 0;
    struct ramfs_data *data = sb->sb_data;
    struct inode *idata = kmalloc(sizeof(struct inode));
    if (!idata) {
        ret = ENOMEM;
        goto error_exit;
    }
    memset(idata, 0, sizeof(struct inode));

    idata->i_no = btree_new_leaf(&data->inodes_btree, idata);
    idata->i_mode = mode;

    if (iref) *iref = idata;
    return 0;

error_exit:
    if (iref) *iref = NULL;
    return ret;
}

static void ramfs_inode_free(struct inode *idata) {
    const char *funcname = "ramfs_inode_free";
    logmsgdf("%s(idata=*%x, ino=%d)\n", funcname, idata, idata->i_data);

    /* some type-specific actions here */
    if (S_ISDIR(idata->i_mode)) {
        if (idata->i_data)
            ramfs_directory_free(idata->i_data);
    }
    kfree(idata);
}

static inode * ramfs_idata_by_inode(mountnode *sb, inode_t ino) {
    struct ramfs_data *data = sb->sb_data;

    return btree_get_index(data->inodes_btree, ino);
    int i;

    struct btree_node *btnode = data->inodes_btree;
    int btree_lvl = btnode->bt_level;
    size_t btree_blksz = btnode->bt_fanout;

    size_t last_ind = btree_blksz;
    for (i = 0; i < btree_lvl; ++i) {
        last_ind *= btree_blksz;
    }

    if (last_ind - 1 < ino)
        return NULL; // ENOENT

    /* get btree item */
    while (btree_lvl > 0) {
        size_t this_node_index = ino / last_ind;
        ino = ino % last_ind;
        struct btree_node *subnode = btnode->bt_children[ this_node_index ];
        if (!subnode) return NULL;
        btnode = subnode;
    }
    return btnode->bt_children[ ino ];
}


static int ramfs_make_directory(mountnode *sb, inode_t *ino, const char *path, mode_t mode) {
    const char *funcname = "ramfs_make_directory";

    int ret;
    const char *basename = strrchr(path, FS_SEP);
    struct inode *idata = NULL;
    struct ramfs_directory *dir = NULL;
    struct ramfs_directory *parent_dir = NULL;
    logmsgdf("%s: path = *%x, basename = *%x\n", funcname, (uint)path, (uint)basename);
    return_err_if(!path, EINVAL, "ramfs_make_directory(NULL)");

    ret = ramfs_inode_new(sb, &idata, S_IFDIR | mode);
    logmsgdf("%s: ramfs_inode_new(), ino=%d, ret=%d\n", funcname, idata->i_no, ret);
    if (ret) return ret;

    ret = ramfs_directory_new(&dir);
    if (ret) goto error_exit;

    if (path[0] == '\0') {
        if (sb->sb_parent == NULL) {
            /* it's a root mountpoint, '..' points to '.' */
            ret = ramfs_directory_new_entry(sb, dir, "..", idata);
            if (ret) goto error_exit;
        } else {
            /* look up the parent directory inode */
            logmsge("%s: TODO: lookup parent inode", funcname);
        }
    } else {
        /* it's a subdirectory of a directory on the same device */
        if (!basename) /* it's a subdirectory of top directory */
            basename = path;

        while (*basename == FS_SEP) ++basename;

        inode_t parino = 0;
        struct inode *par_idata = NULL;

        ret = ramfs_lookup_inode(sb, &parino, path, basename - path);
        if (ret) {
            logmsgef("%s: ramfs_lookup_inode(%s) failed", funcname, path);
            goto error_exit;
        }
        logmsgdf("%s: parino=%d\n", funcname, parino);

        par_idata = ramfs_idata_by_inode(sb, parino);
        if (!par_idata) {
            logmsgef("%s: ramfs_idata_by_inode(%d) failed", funcname, parino);
            goto error_exit;
        }

        if (! S_ISDIR(par_idata->i_mode)) {
            ret = ENOTDIR;
            goto error_exit;
        }

        parent_dir = par_idata->i_data;
        ret = ramfs_directory_new_entry(sb, parent_dir, basename, idata);
    }

    ret = ramfs_directory_new_entry(sb, dir, ".", idata);
    if (ret) goto error_exit;

    idata->i_data = dir;

    if (ino) *ino = idata->i_no;
    return 0;

error_exit:
    if (dir)    ramfs_directory_free(dir);
    if (idata)  ramfs_inode_free(idata);

    if (ino) *ino = 0;
    return ret;
}

static int ramfs_read_superblock(mountnode *sb) {
    const char *funcname = "ramfs_read_superblock";
    logmsgdf("%s()\n", funcname);
    int ret;

    sb->sb_blksz = PAGE_SIZE;
    sb->sb_fs = &ramfs_driver;

    ret = ramfs_data_new(sb);
    if (ret) return ret;

    inode_t root_ino;
    struct ramfs_data *data = sb->sb_data;

    ret = ramfs_make_directory(sb, &root_ino, "", S_IFDIR | 0755);
    if (ret) {
        logmsgef("%s: ramfs_make_directory() failed", funcname);
        goto enomem_exit;
    }
    data->root_ino = root_ino;
    logmsgdf("%s: data->root_ino = %d\n", funcname, root_ino);

    return 0;

enomem_exit:
    ramfs_data_free(sb);
    return ret;
}


/*
 *    Searches given `dir` for part between `basename` and `basename_end`
 */
static int ramfs_get_inode_by_basename(struct ramfs_directory *dir, inode_t *ino, const char *basename, size_t basename_len)
{
    const char *funcname = "ramfs_get_inode_by_basename";
    logmsgdf("%s(dir=*%x, basename='%s'[:%d]\n",
             funcname, (uint)dir, basename, basename_len);

    uint32_t hash = strhash(basename, basename_len);

    /* directory hashtable: get direntry */
    struct ramfs_direntry *de = dir->ht[ hash % dir->htcap ];

    while (de) {
        if ((de->de_hash == hash)
            && !strncmp(basename, de->de_name, basename_len))
        {
            if (ino) *ino = de->de_ino;
            return 0;
        }
        de = de->htnext;
    }

    if (ino) *ino = 0;
    return ENOENT;
}


static int ramfs_lookup_inode(mountnode *sb, inode_t *result, const char *path, size_t pathlen)
{
    const char *funcname = "ramfs_lookup_inode";
    logmsgdf("%s(path='%s', pathlen=%d)\n", funcname, path, pathlen);
    struct ramfs_data *data = sb->sb_data;

    //     "some/longdirectoryname/to/examplefilename"
    //           ^                ^
    //       basename        basename_end
    const char *basename = path;
    const char *basename_end = path;
    inode_t ino;
    int ret = 0;

    if ((basename[0] == 0) || (pathlen == 0)) {
        if (result) *result = data->root_ino;
        return 0;
    }

    struct inode *root_idata = ramfs_idata_by_inode(sb, data->root_ino);
    return_err_if(!root_idata, EKERN,
            "%s: no idata for root_ino=%d\n", funcname, data->root_ino);
    return_err_if(! S_ISDIR(root_idata->i_mode), EKERN,
            "%s: root_ino is not a directory", funcname);

    struct ramfs_directory *dir = root_idata->i_data;

    for (;;) {
        while (true) {
            if ((pathlen <= (basename_end - path + 1)) || (basename_end[0] == '\0')) {
                /* basename is "examplefilename" now */
                logmsgdf("%s: basename found\n");
                ret = ramfs_get_inode_by_basename(dir, &ino, basename, basename_end - basename);
                if (result) *result = (ret ? 0 : ino);
                return ret;
            }
            if (basename_end[0] == FS_SEP)
                break;

            ++basename_end;
        }

        ret = ramfs_get_inode_by_basename(dir, &ino, basename, basename_end - basename);
        if (ret) {
            if (result) *result = 0;
            return ret;
        }
        if (basename_end[1] == '\0') {
            logmsgdf("%s: trailing /, assuming a directory");
            if (result) *result = ino;
            return 0;
        }

        inode *idata = ramfs_idata_by_inode(sb, ino);
        return_err_if(!idata, EKERN,
                "%s: No inode for inode index %d on mountpoint *%p", funcname, ino, (uint)sb);
        return_dbg_if(! S_ISDIR(idata->i_mode), ENOTDIR, "%s: ENOTDIR\n", funcname);

        dir = idata->i_data;

        while (*++basename_end == FS_SEP);
        basename = basename_end;
    }
}

static int ramfs_get_direntry(mountnode *sb, inode_t dirnode, void **iter, struct dirent *dirent) {
    const char *funcname = "ramfs_get_direntry";
    logmsgdf("%s: inode=%d, iter=0x%x\n", funcname, dirnode, (uint)*iter);

    int ret = 0;
    struct inode *dir_idata;
    dir_idata = ramfs_idata_by_inode(sb, dirnode);
    return_err_if(!dir_idata, EKERN,
                  "%s: ramfs_idata_by_inode(%d) failed", funcname, dirnode);
    return_log_if(!S_ISDIR(dir_idata->i_mode), ENOTDIR,
                  "%s: node %d is not a directory\n", funcname, dirnode);

    struct ramfs_directory *dir = dir_idata->i_data;

    uint32_t hash = (uint32_t)*iter;
    size_t htindex = hash % dir->htcap;
    struct ramfs_direntry *de = dir->ht[ htindex ];

    if (hash) { /* search through bucket for the current hash */
        while (de->de_hash != hash) {
            return_err_if(!de->htnext, EKERN, "%s: no hash 0x%x in its bucket", funcname, hash);
            de = de->htnext;
        }
    } else { /* start enumerating dirhashtable entries */
        htindex = 0;
        /* the directory must have at least . and .. entries,
         * so the hashtable may not be empty: */
        while (!(de = dir->ht[ htindex ]))
            ++htindex;
    }

    /* fill in the dirent */
    dirent->d_namlen = strlen(de->de_name);
    strncpy(dirent->d_name, de->de_name, UCHAR_MAX);

    dirent->d_ino = de->de_ino;
    dirent->d_reclen = sizeof(struct dirent) - UCHAR_MAX + dirent->d_namlen + 1;

    struct inode *idata = ramfs_idata_by_inode(sb, dirent->d_ino);
    if (idata) {
        switch (idata->i_mode & S_IFMT) {
            case S_IFREG: dirent->d_type = DT_REG; break;
            case S_IFDIR: dirent->d_type = DT_DIR; break;
            case S_IFLNK: dirent->d_type = DT_LNK; break;
            case S_IFCHR: dirent->d_type = DT_CHR; break;
            case S_IFBLK: dirent->d_type = DT_BLK; break;
            case S_IFSOCK: dirent->d_type = DT_SOCK; break;
            case S_IFIFO: dirent->d_type = DT_FIFO; break;
            default:
                logmsgef("%s: unknown inode mode 0x%x for inode %d\n",
                              funcname, idata->i_mode, dirent->d_ino);
                dirent->d_type = DT_UNKNOWN;
        }
    } else logmsgef("%s: no idata for inode %d", funcname, dirent->d_ino);

    /* search next */
    de = de->htnext;
    if (de) {
        *iter = (void *)de->de_hash;
        return 0;
    }

    /* search for the next non-empty bucket */
    while (++htindex < dir->htcap) {
        de = dir->ht[ htindex ];
        if (de) {
            *iter = (void *)de->de_hash;
            return 0;
        }
    }

    *iter = NULL; /* signal the end of the directory list */
    return 0;
}


/*
 *  VFS operations
 */

static const char * vfs_match_mountpath(mountnode *parent_mnt, mountnode **match_mnt, const char *path) {
    mountnode *child_mnt = parent_mnt->sb_children;
    while (child_mnt) {
        const char *mountpath = child_mnt->sb_mntpath;
        size_t mountpath_len = strlen(mountpath);
        if (!strncmp(path, mountpath, mountpath_len)) {
            const char *nextpath = path + mountpath_len;
            if (nextpath[0] == '0')
                break;

            while (nextpath[0] == FS_SEP)
                ++nextpath;

            if (match_mnt) *match_mnt = child_mnt;
            return nextpath;
        }
        child_mnt = child_mnt->sb_brother;
    }

    if (match_mnt) *match_mnt = parent_mnt;
    return NULL;
}

/*
 *   Determines the mountnode `path` resides on.
 *   Returns:
 *      its pointer through `*mntnode` (if not NULL);
 *      filesystem-specific part of `path` through `*relpath` (if not NULL);
 *
 */
int vfs_mountnode_by_path(const char *path, mountnode **mntnode, const char **relpath) {
    return_log_if(!theRootMnt, EBADF, "vfs_mountnode_by_path(): theRootMnt absent\n");

    return_log_if(!(path && *path), EINVAL, "vfs_mountnode_by_path(NULL or '')\n");
    return_log_if(path[0] != FS_SEP, EINVAL, "vfs_mountnode_by_path('%s'): requires the absolute path\n", path);
    ++path;

    mountnode *mnt = theRootMnt;
    mountnode *chld = NULL;
    while (true) {
        const char * nextpath = vfs_match_mountpath(mnt, &chld, path);
        if (!nextpath)
            break;

        mnt = chld;
        path = nextpath;
    }

    if (mntnode) *mntnode = mnt;
    if (relpath) *relpath = path;
    return 0;
}

err_t vfs_mount(dev_t source, const char *target, const mount_opts_t *opts) {
    if (theRootMnt == NULL) {
        if ((target[0] != '/') || (target[1] != '\0')) {
             logmsgef("vfs_mount: mount('%s') with no root", target);
             return -1;
        }

        fsdriver *fs = vfs_fs_by_id(opts->fs_id);
        if (!fs) return -2;

        struct superblock *sb = kmalloc(sizeof(struct superblock));
        return_err_if(!sb, -3, "vfs_mount: kmalloc(superblock) failed");

        sb->sb_parent = NULL;
        sb->sb_brother = NULL;
        sb->sb_children = NULL;
        sb->sb_mntpath = "";
        sb->sb_mntpath_hash = 0;
        sb->sb_fs = fs;

        int ret = fs->ops->read_superblock(sb);
        return_err_if(ret, -4, "vfs_mount: read_superblock failed (%d)", ret);

        theRootMnt = sb;
        return 0;
    }
    logmsge("vfs_mount: TODO: non-root");
    return -110;
}


int vfs_mkdir(const char *path, mode_t mode) {
    int ret;
    mountnode *sb;
    const char *localpath;

    mode &= !S_IFMT;
    mode |= S_IFDIR;

    ret = vfs_mountnode_by_path(path, &sb, &localpath);
    return_err_if(ret, ENOENT, "mkdir: no filesystem for path '%s'\n", path);
    logmsgdf("vfs_mkdir: localpath = '%s'\n", localpath);

    return_log_if(!sb->sb_fs->ops->make_directory, EBADF,
                  "mkdir: %s filesystem cannot mkdir\n", sb->sb_fs->name);

    ret = sb->sb_fs->ops->make_directory(sb, NULL, localpath, mode);
    return_err_if(ret, ret, "mkdir: failed (%d)\n", ret);

    return 0;
}



void print_ls(const char *path) {
    int ret;
    mountnode *sb;
    const char *localpath;
    void *iter;
    struct dirent de;
    inode_t ino = 0;

    ret = vfs_mountnode_by_path(path, &sb, &localpath);
    returnv_err_if(ret, "ls: path '%s' not found\n", path, ret);
    logmsgdf("print_ls: localpath = '%s' \n", localpath);

    ret = sb->sb_fs->ops->lookup_inode(sb, &ino, localpath, SIZE_MAX);
    returnv_err_if(ret, "no inode at '%s' (%d)\n", localpath, ret);
    logmsgdf("print_ls: ino = %d\n", ino);

    iter = NULL; /* must be NULL at the start of enumeration */
    do {
        ret = sb->sb_fs->ops->get_direntry(sb, ino, &iter, &de);

        if (ret) {
             logmsgef("ls error: %d", ret);
             return;
        }

        k_printf("%d\t%s\n", de.d_ino, de.d_name);
        logmsgdf("%d\t%s\n", de.d_ino, de.d_name);
    } while (iter);

    /* the end of enumeration */
    k_printf("\n");
}

void print_mount(void) {
    struct superblock *sb = theRootMnt;
    if (!sb) return;

    k_printf("%s on /\n", sb->sb_fs->name);
    if (sb->sb_children) {
        logmsge("TODO: print child mounts");
    }
}

void vfs_setup(void) {
    int ret;

    /* register filesystems here */
    vfs_register_filesystem(&ramfs_driver);

    /* mount actual filesystems */
    dev_t fsdev = gnu_dev_makedev(CHR_VIRT, CHR0_UNSPECIFIED);
    mount_opts_t mntopts = { .fs_id = RAMFS_ID };
    ret = vfs_mount(fsdev, "/", &mntopts);
    returnv_err_if(ret, "root mount on sysfs failed (%d)", ret);

    k_printf("%s on / mounted successfully\n", theRootMnt->sb_fs->name);
    // */
}
