#include <linux/cleanup.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/namei.h>
#include <linux/init.h>
#include <linux/mnt_idmapping.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "http.h"

#define VTFS_ROOT_INO 1000
#define VTFS_MAGIC 0x56544653
#define VTFS_MAX_SIZE (16 * 1024 * 1024)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VTFS lab filesystem");

static const struct inode_operations vtfs_inode_ops;

#define VTFS_CHUNK 512

enum vtfs_action {
	VTFS_CREATE = 1,
	VTFS_LINK,
	VTFS_UNLINK,
	VTFS_RMDIR,
	VTFS_WRITE,
	VTFS_TRUNCATE,
};

/* Fixed-width, little-endian records shared with server.py. */
struct vtfs_record {
	__le32 action;
	__le32 mode;
	__le64 ino;
	__le64 parent;
	__le64 offset;
	__le32 len;
	u8 payload[VTFS_CHUNK];
} __packed;

struct vtfs_remote {
	const char *token;
	atomic_long_t next_ino;
};

static int vtfs_record(struct super_block *sb, enum vtfs_action action,
		       unsigned long ino, unsigned long parent, umode_t mode,
		       loff_t offset, const void *payload, size_t len)
{
	struct vtfs_remote *remote = sb->s_fs_info;
	struct vtfs_record *record __free(kfree) = NULL;
	char *encoded __free(kfree) = NULL;

	if (!remote)
		return 0;
	record = kzalloc(sizeof(*record), GFP_KERNEL);
	if (!record)
		return -ENOMEM;
	encoded = kmalloc(2 * sizeof(*record) + 1, GFP_KERNEL);
	if (!encoded)
		return -ENOMEM;

	record->action = cpu_to_le32(action);
	record->mode = cpu_to_le32(mode);
	record->ino = cpu_to_le64(ino);
	record->parent = cpu_to_le64(parent);
	record->offset = cpu_to_le64(offset);
	record->len = cpu_to_le32(len);
	if (len)
		memcpy(record->payload, payload, len);
	*bin2hex(encoded, record, sizeof(*record)) = '\0';
	return vtfs_http_call(remote->token, "append", encoded, NULL, 0);
}

static int vtfs_record_name(struct inode *dir, struct dentry *dentry,
			    struct inode *inode, enum vtfs_action action)
{
	return vtfs_record(dir->i_sb, action, inode->i_ino, dir->i_ino,
			   inode->i_mode, 0, dentry->d_name.name,
			   dentry->d_name.len);
}


static int vtfs_resize(struct inode *inode, size_t size)
{
	size_t old_size = i_size_read(inode);
	void *data;

	if (size == old_size)
		return 0;
	if (size > old_size) {
		data = kvzalloc(size, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		if (old_size)
			memcpy(data, inode->i_private, old_size);
		kvfree(inode->i_private);
		inode->i_private = data;
	} else if (!size) {
		kvfree(inode->i_private);
		inode->i_private = NULL;
	}
	i_size_write(inode, size);
	return 0;
}

static int vtfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	int error;

	error = setattr_prepare(idmap, dentry, attr);
	if (error)
		return error;
	if (attr->ia_valid & ATTR_SIZE) {
		size_t old_size = i_size_read(inode);

		error = inode_newsize_ok(inode, attr->ia_size);
		if (error)
			return error;
		if (attr->ia_size > old_size) {
			error = vtfs_resize(inode, attr->ia_size);
			if (error)
				return error;
		}
		if (inode->i_nlink)
			error = vtfs_record(inode->i_sb, VTFS_TRUNCATE,
					    inode->i_ino, 0, 0,
					    attr->ia_size, NULL, 0);
		if (error) {
			vtfs_resize(inode, old_size);
			return error;
		}
		vtfs_resize(inode, attr->ia_size);
	}
	setattr_copy(idmap, inode, attr);
	return 0;
}

static ssize_t vtfs_read(struct file *file, char __user *buf, size_t len,
			 loff_t *pos)
{
	struct inode *inode = file_inode(file);
	ssize_t result;

	inode_lock_shared(inode);
	result = simple_read_from_buffer(buf, len, pos, inode->i_private,
					 i_size_read(inode));
	inode_unlock_shared(inode);
	file_accessed(file);
	return result;
}

static ssize_t vtfs_write_locked(struct file *file, const char __user *buf,
				 size_t len, loff_t *pos)
{
	struct inode *inode = file_inode(file);
	void *data;
	loff_t offset;
	ssize_t result;
	size_t written = 0, old_size;

	offset = (file->f_flags & O_APPEND) ? i_size_read(inode) : *pos;
	if (offset < 0 || offset >= VTFS_MAX_SIZE)
		return offset < 0 ? -EINVAL : -EFBIG;
	len = min_t(size_t, len, VTFS_MAX_SIZE - offset);
	data = vmemdup_user(buf, len);
	if (IS_ERR(data))
		return PTR_ERR(data);
	result = file_remove_privs(file);
	if (!result)
		result = file_update_time(file);
	old_size = i_size_read(inode);
	if (!result && offset + len > old_size)
		result = vtfs_resize(inode, offset + len);
	if (result) {
		kvfree(data);
		return result;
	}

	if (inode->i_sb->s_fs_info && inode->i_nlink) {
		while (written < len) {
			size_t chunk = min_t(size_t, len - written, VTFS_CHUNK);

			result = vtfs_record(inode->i_sb, VTFS_WRITE, inode->i_ino,
					     0, 0, offset + written,
					     (char *)data + written, chunk);
			if (result)
				break;
			written += chunk;
		}
	} else {
		written = len;
	}

	if (written) {
		memcpy((char *)inode->i_private + offset, data, written);
		*pos = offset + written;
		result = written;
	}
	vtfs_resize(inode, max_t(size_t, old_size, written ? offset + written : 0));
	kvfree(data);
	return result;
}

static ssize_t vtfs_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *pos)
{
	struct inode *inode = file_inode(file);
	ssize_t result;

	if (!len)
		return 0;

	inode_lock(inode);
	result = vtfs_write_locked(file, buf, len, pos);
	inode_unlock(inode);
	return result;
}

static const struct file_operations vtfs_file_ops = {
	.read = vtfs_read,
	.write = vtfs_write,
	.llseek = generic_file_llseek,
	.fsync = noop_fsync,
};

static const struct inode_operations vtfs_file_inode_ops = {
	.setattr = vtfs_setattr,
};

static struct inode *vtfs_get_inode(struct super_block *sb,
				   const struct inode *dir, umode_t mode,
				   unsigned long ino)
{
	struct inode *inode = new_inode(sb);

	if (!inode)
		return NULL;

	inode->i_ino = ino;

	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
	simple_inode_init_ts(inode);
	if (S_ISDIR(mode)) {
		inode->i_op = &vtfs_inode_ops;
		inode->i_fop = &simple_dir_operations;
		inc_nlink(inode);
	} else {
		inode->i_op = &vtfs_file_inode_ops;
		inode->i_fop = &vtfs_file_ops;
	}
	return inode;
}

static int vtfs_add(struct inode *dir, struct dentry *dentry, umode_t mode,
		    unsigned long ino, bool replay)
{
	struct vtfs_remote *remote = dir->i_sb->s_fs_info;
	struct inode *inode;
	int err;

	if (dentry->d_name.len > NAME_MAX)
		return -ENAMETOOLONG;

	if (!ino && remote)
		ino = atomic_long_inc_return(&remote->next_ino);
	while (!ino || ino == VTFS_ROOT_INO)
		ino = get_next_ino();
	inode = vtfs_get_inode(dir->i_sb, dir, mode, ino);

	if (!inode)
		return -ENOMEM;

	if (!replay) {
		err = vtfs_record_name(dir, dentry, inode, VTFS_CREATE);
		if (err) {
			iput(inode);
			return err;
		}
	}
	if (remote)
		insert_inode_hash(inode);
	d_instantiate(dentry, inode);
	/* Positive dentries are the RAM directory entries. */
	dget(dentry);
	if (S_ISDIR(mode))
		inc_nlink(dir);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	return 0;
}

static int vtfs_create(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode, bool excl)
{
	return vtfs_add(dir, dentry, S_IFREG | mode, 0, false);
}

static int vtfs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		      struct dentry *dentry, umode_t mode)
{
	return vtfs_add(dir, dentry, S_IFDIR | mode, 0, false);
}

static int vtfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int err;

	err = vtfs_record_name(dir, dentry, d_inode(dentry), VTFS_UNLINK);
	if (!err)
		err = simple_unlink(dir, dentry);
	return err;
}

static int vtfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int err;

	if (!simple_empty(dentry))
		return -ENOTEMPTY;
	err = vtfs_record_name(dir, dentry, d_inode(dentry), VTFS_RMDIR);
	return err ? err : simple_rmdir(dir, dentry);
}

static int vtfs_link(struct dentry *old, struct inode *dir, struct dentry *new)
{
	int err;

	if (new->d_name.len > NAME_MAX)
		return -ENAMETOOLONG;
	err = vtfs_record_name(dir, new, d_inode(old), VTFS_LINK);
	return err ? err : simple_link(old, dir, new);
}

static const struct inode_operations vtfs_inode_ops = {
	.lookup = simple_lookup,
	.create = vtfs_create,
	.unlink = vtfs_unlink,
	.mkdir = vtfs_mkdir,
	.rmdir = vtfs_rmdir,
	.link = vtfs_link,
};

static void vtfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	kvfree(inode->i_private);
}

static const struct super_operations vtfs_super_ops = {
	.statfs = simple_statfs,
	.drop_inode = generic_delete_inode,
	.evict_inode = vtfs_evict_inode,
};

static int vtfs_replay_entry(struct inode *dir, struct dentry *base,
			     const char *name, unsigned int len, umode_t mode,
			     unsigned long ino, unsigned int action)
{
	struct super_block *sb = dir->i_sb;
	struct inode *target;
	struct dentry *entry, *alias;
	struct qstr qname;
	int err = -EPROTO;

	qname.name = name;
	qname.len = len;
	entry = d_hash_and_lookup(base, &qname);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	switch (action) {
	case VTFS_CREATE:
	case VTFS_LINK:
		if (entry && d_really_is_positive(entry))
			break;
		if (!entry) {
			entry = d_alloc_name(base, name);
			if (!entry) {
				err = -ENOMEM;
				break;
			}
			d_add(entry, NULL);
		}
		if (action == VTFS_CREATE) {
			if (S_ISDIR(mode) || S_ISREG(mode)) {
				target = ilookup(sb, ino);
				if (target)
					iput(target);
				else
					err = vtfs_add(dir, entry, mode, ino, true);
			}
		} else {
			target = ilookup(sb, ino);
			if (!target)
				break;
			alias = d_find_alias(target);
			if (alias && S_ISREG(target->i_mode))
				err = simple_link(alias, dir, entry);
			dput(alias);
			iput(target);
		}
		break;
	case VTFS_UNLINK:
	case VTFS_RMDIR:
		if (!entry || d_really_is_negative(entry) ||
		    d_inode(entry)->i_ino != ino)
			break;
		if (action == VTFS_UNLINK && S_ISREG(d_inode(entry)->i_mode))
			err = simple_unlink(dir, entry);
		else if (action == VTFS_RMDIR && S_ISDIR(d_inode(entry)->i_mode))
			err = simple_rmdir(dir, entry);
		if (!err)
			d_delete(entry);
		break;
	}
	dput(entry);
	return err;
}

static int vtfs_replay_name(struct super_block *sb,
			    const struct vtfs_record *record,
			    unsigned long ino, unsigned int action)
{
	unsigned long parent = le64_to_cpu(record->parent);
	unsigned int len = le32_to_cpu(record->len);
	umode_t mode = le32_to_cpu(record->mode);
	char name[NAME_MAX + 1];
	struct inode *dir;
	struct dentry *base;
	int err = -EPROTO;

	if (!len || len > NAME_MAX || memchr(record->payload, '/', len) ||
	    memchr(record->payload, '\0', len))
		return -EPROTO;
	memcpy(name, record->payload, len);
	name[len] = '\0';
	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return -EPROTO;

	dir = ilookup(sb, parent);
	if (!dir)
		return -EPROTO;
	base = d_find_alias(dir);
	if (base && S_ISDIR(dir->i_mode))
		err = vtfs_replay_entry(dir, base, name, len, mode, ino, action);
	dput(base);
	iput(dir);
	return err;
}

static int vtfs_replay_record(struct super_block *sb,
			      const struct vtfs_record *record)
{
	struct vtfs_remote *remote = sb->s_fs_info;
	unsigned int action = le32_to_cpu(record->action);
	unsigned int len = le32_to_cpu(record->len);
	u64 offset = le64_to_cpu(record->offset);
	u64 ino = le64_to_cpu(record->ino);
	struct inode *inode;
	int err;

	if (ino <= VTFS_ROOT_INO || ino >= LONG_MAX)
		return -EPROTO;
	if (action >= VTFS_CREATE && action <= VTFS_RMDIR) {
		err = vtfs_replay_name(sb, record, ino, action);
		if (!err && ino > atomic_long_read(&remote->next_ino))
			atomic_long_set(&remote->next_ino, ino);
		return err;
	}
	if (offset > VTFS_MAX_SIZE || len > sizeof(record->payload) ||
	    len > VTFS_MAX_SIZE - offset)
		return -EPROTO;
	inode = ilookup(sb, ino);
	if (!inode)
		return -EPROTO;
	err = -EPROTO;
	if (S_ISREG(inode->i_mode)) {
		if (action == VTFS_TRUNCATE && !len) {
			err = vtfs_resize(inode, offset);
		} else if (action == VTFS_WRITE && len) {
			err = vtfs_resize(inode, max_t(u64, i_size_read(inode),
						      offset + len));
			if (!err)
				memcpy((char *)inode->i_private + offset,
				       record->payload, len);
		}
	}
	iput(inode);
	return err;
}

static int vtfs_replay(struct super_block *sb)
{
	struct vtfs_remote *remote = sb->s_fs_info;
	struct vtfs_record *record;
	unsigned long index;
	char argument[24];
	int err;

	record = kmalloc(sizeof(*record), GFP_KERNEL);
	if (!record)
		return -ENOMEM;
	for (index = 1; ; index++) {
		snprintf(argument, sizeof(argument), "%lu", index);
		err = vtfs_http_call(remote->token, "entry", argument,
				     record, sizeof(*record));
		if (err == -ENOENT) {
			err = 0;
			break;
		}
		if (err)
			break;
		err = vtfs_replay_record(sb, record);
		if (err)
			break;
		cond_resched();
	}
	kfree(record);
	return err;
}

static int vtfs_fill_super(struct super_block *sb, void *data, int silent)
{
	const char *token = data;
	struct vtfs_remote *remote;
	struct inode *root;

	if (token) {
		remote = kzalloc(sizeof(*remote), GFP_KERNEL);
		if (!remote)
			return -ENOMEM;
		remote->token = kstrdup(token, GFP_KERNEL);
		if (!remote->token) {
			kfree(remote);
			return -ENOMEM;
		}
		atomic_long_set(&remote->next_ino, VTFS_ROOT_INO);
		sb->s_fs_info = remote;
	}

	sb->s_magic = VTFS_MAGIC;
	sb->s_op = &vtfs_super_ops;
	sb->s_maxbytes = VTFS_MAX_SIZE;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_time_gran = 1;
	root = vtfs_get_inode(sb, NULL, S_IFDIR | 0777, VTFS_ROOT_INO);
	if (!root)
		return -ENOMEM;
	if (token)
		insert_inode_hash(root);
	sb->s_root = d_make_root(root);
	if (!sb->s_root)
		return -ENOMEM;
	return token ? vtfs_replay(sb) : 0;
}

static struct dentry *vtfs_mount(struct file_system_type *type, int flags,
				 const char *source, void *data)
{
	const char *token = NULL;
	const char *ch;

	if (source && !strncmp(source, "remote:", 7)) {
		token = source + 7;
		if (!*token || strlen(token) > 64)
			return ERR_PTR(-EINVAL);
		for (ch = token; *ch; ch++) {
			if (!isalnum(*ch) && *ch != '-' && *ch != '_')
				return ERR_PTR(-EINVAL);
		}
	}
	return mount_nodev(type, flags, (void *)token, vtfs_fill_super);
}

static void vtfs_kill_sb(struct super_block *sb)
{
	struct vtfs_remote *remote = sb->s_fs_info;

	kill_litter_super(sb);
	if (remote) {
		kfree(remote->token);
		kfree(remote);
	}
}

static struct file_system_type vtfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "vtfs",
	.mount = vtfs_mount,
	.kill_sb = vtfs_kill_sb,
};
MODULE_ALIAS_FS("vtfs");

static int __init vtfs_init(void)
{
	int error = register_filesystem(&vtfs_fs_type);

	if (!error)
		pr_info("[vtfs] VTFS joined the kernel\n");
	return error;
}

static void __exit vtfs_exit(void)
{
	unregister_filesystem(&vtfs_fs_type);
	pr_info("[vtfs] VTFS left the kernel\n");
}

module_init(vtfs_init);
module_exit(vtfs_exit);
