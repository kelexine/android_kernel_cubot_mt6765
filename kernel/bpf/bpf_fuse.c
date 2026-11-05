// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021 Google LLC

#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <uapi/linux/android_fuse.h>

static const struct bpf_func_proto *
fuse_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_trace_printk:
		return bpf_get_trace_printk_proto();

	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;

	case BPF_FUNC_get_current_pid_tgid:
		return &bpf_get_current_pid_tgid_proto;

	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;

	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;

	default:
		pr_debug("Invalid fuse bpf func %d\n", func_id);
		return NULL;
	}
}

static bool fuse_prog_is_valid_access(int off, int size,
				enum bpf_access_type type,
				const struct bpf_prog *prog,
				struct bpf_insn_access_aux *info)
{
	int i;

	if (off < 0 || off > offsetofend(struct fuse_bpf_args, out_args))
		return false;

	for (i = 0; i < FUSE_MAX_IN_ARGS; i++) {
		if (off == offsetof(struct fuse_bpf_args, in_args[i].value)) {
			info->reg_type = PTR_TO_RDONLY_BUF;
			info->ctx_field_size = 256;
			if (type != BPF_READ)
				return false;
			return true;
		}
	}
	for (i = 0; i < FUSE_MAX_OUT_ARGS; i++) {
		if (off == offsetof(struct fuse_bpf_args, out_args[i].value)) {
			info->reg_type = PTR_TO_RDWR_BUF;
			info->ctx_field_size = 256;
			return true;
		}
	}
	if (type != BPF_READ)
		return false;

	return true;
}

const struct bpf_verifier_ops fuse_verifier_ops = {
	.get_func_proto  = fuse_prog_func_proto,
	.is_valid_access = fuse_prog_is_valid_access,
};

const struct bpf_prog_ops fuse_prog_ops = {
};

struct bpf_prog *fuse_get_bpf_prog(struct file *file)
{
	struct bpf_prog *bpf_prog = ERR_PTR(-EINVAL);

	if (!file || IS_ERR(file))
		return bpf_prog;

	if (file->f_op != &bpf_prog_fops)
		goto out;

	bpf_prog = file->private_data;
	if (bpf_prog->type == BPF_PROG_TYPE_FUSE)
		bpf_prog_inc(bpf_prog);
	else
		bpf_prog = ERR_PTR(-EINVAL);

out:
	if (file)
		fput(file);
	return bpf_prog;
}
EXPORT_SYMBOL(fuse_get_bpf_prog);
