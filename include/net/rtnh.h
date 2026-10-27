/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_RTNH_H
#define __NET_RTNH_H

#include <linux/rtnetlink.h>
#include <net/netlink.h>

static inline int rtnh_ok(const struct rtnexthop *rtnh, int remaining)
{
	return remaining >= (int)sizeof(*rtnh) &&
	       rtnh->rtnh_len >= sizeof(*rtnh) &&
	       rtnh->rtnh_len <= remaining;
}

static inline struct rtnexthop *rtnh_next(const struct rtnexthop *rtnh,
                                         int *remaining)
{
	int totlen = NLA_ALIGN(rtnh->rtnh_len);

	*remaining -= totlen;
	return (struct rtnexthop *) ((char *) rtnh + totlen);
}

static inline struct nlattr *rtnh_attrs(const struct rtnexthop *rtnh)
{
	return (struct nlattr *) ((char *) rtnh + NLA_ALIGN(sizeof(*rtnh)));
}

static inline int rtnh_attrlen(const struct rtnexthop *rtnh)
{
	return rtnh->rtnh_len - NLA_ALIGN(sizeof(*rtnh));
}

struct fib_nh_common;
int fib_nexthop_info(struct sk_buff *skb, const struct fib_nh_common *nhc,
		     u8 *flags, bool skip_oif);
int fib_add_nexthop(struct sk_buff *skb, const struct fib_nh_common *nhc,
		    int nh_weight);

#endif
