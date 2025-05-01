#include <zephyr/net_buf.h>
#include <zephyr/net/net_if.h>

extern struct net_if _net_if_list_start[];
extern struct net_if _net_if_list_end[];

/**
 * @brief override required by net library
 *
 * @param iface
 * @return * int
 */
int net_if_get_by_iface(struct net_if *iface)
{
	if (!(iface >= _net_if_list_start && iface < _net_if_list_end)) {
		return -1;
	}

	return (iface - _net_if_list_start) + 1;
}
