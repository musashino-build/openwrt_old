// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/leds.h>

#include <linux/mfd/realtek-led-ctrl-common.h>

#define DRIVER_VERSION		"0.01"

struct realtek_pled_data {
	struct led_classdev cdev;
	struct realtek_port_leds *pleds;
	int port;
	int index;
	char sysfs_regname[10];
	bool registered;
};

struct realtek_port_leds {
	struct device *dev;
	struct realtek_led_ctrl_intf *intf;
	struct realtek_pled_data leds[0];
	struct mutex lock;
};

static struct realtek_pled_data *cdev_to_data(struct led_classdev *cdev)
{
	return container_of(cdev, struct realtek_pled_data, cdev);
}

static int port_led_index_get(struct realtek_led_ctrl_intf *intf,
			      int port, int index)
{
	int target;
	int max = intf->port_max, leds = intf->pled_num;

	target = port + max * index;
	if (target >= max * leds)
		return -EINVAL;

	return target;
}

static int pled_user_set(struct realtek_port_leds *pleds, int port,
			 int index, int mode)
{
	struct realtek_pled_ops *ops = pleds->intf->pled_ops;

	return ops->port_led_user_set(pleds->intf, port, index, mode);
}

static void pled_user_brightness_set(struct led_classdev *cdev,
				    enum led_brightness brnss)
{
	struct realtek_pled_data *data = cdev_to_data(cdev);

	pled_user_set(data->pleds, data->port, data->index,
		      brnss > LED_OFF ? LED_SW_P_ON : LED_SW_P_OFF);
}

/* get mode from delay */
static int pled_delay_to_mode(unsigned long *delay)
{
	unsigned long _delay;

	if (*delay < 32)
		return LED_SW_P_32MS;
	if (*delay > 1024)
		return LED_SW_P_1024MS;

	_delay = *delay >> 4;

	return find_last_bit(&_delay, 7);
}

static int pled_user_blink_set(struct led_classdev *cdev,
			       unsigned long *delay_on,
			       unsigned long *delay_off)
{
	struct realtek_pled_data *data = cdev_to_data(cdev);
	int mode = LED_SW_P_512MS;

//	dev_info(cdev->dev, "%s: delay_on -> %lu, delay_off -> %lu\n",
//		 __func__, *delay_on, *delay_off);
	if (*delay_on)
		mode = pled_delay_to_mode(delay_on);
	else if (*delay_off)
		mode = pled_delay_to_mode(delay_off);
	
//	dev_info(cdev->dev, "  mode: %d\n", mode);
	return pled_user_set(data->pleds, data->port, data->index, mode);
}

/*
 * Port LED unragistration from kernel
 */
static int port_led_user_unregister(struct realtek_port_leds *pleds,
				    int port, int index)
{
	struct realtek_led_ctrl_intf *intf = pleds->intf;
	struct realtek_pled_data *data;
	int aindex = port_led_index_get(intf, port, index);

	if (aindex < 0) {
		dev_err(pleds->dev,
			"failed to lookup index of the LED from array: %d.%d\n",
			port, index);
		return -EINVAL;
	}

	data = &pleds->leds[aindex];

	if (!data->registered) {
		dev_warn(pleds->dev,
			 "already unregistered: %d.%d\n", port, index);
		return 0;
	}

	devm_led_classdev_unregister(pleds->dev,
				     &data->cdev);

	intf->pled_ops->port_led_user_register(intf,
					       data->port, data->index,
					       false);

	data->registered = false;

	dev_info(pleds->dev,
		"User LED: unregistered: port->%2d, index->%d, aindex->%2d\n",
		port, index, aindex);

	return 0;
}

static int port_led_user_register(struct realtek_port_leds *pleds,
				  struct fwnode_handle *_fwnode,
				  int port, int index)
{
	struct realtek_led_ctrl_intf *intf = pleds->intf;
	struct realtek_pled_data *data;
	int ret = 0, aindex;

	if (port < 0 || intf->port_max <= port ||
	    index < 0 || intf->pled_num <= index ||
	    !(BIT_ULL(port) & intf->port_mask)) {
		dev_err(pleds->dev,
			"invalid LED specified for this device: %d.%d\n",
			port, index);
		return -EINVAL;
	}

	aindex = port_led_index_get(intf, port, index);
	if (aindex < 0) {
		dev_err(pleds->dev,
			"failed to lookup index of the LED from array: %d.%d\n",
			port, index);
		return -EINVAL;
	}

	data = &pleds->leds[aindex];

	if (data->registered) {
		dev_warn(pleds->dev,
			 "already registered: %d.%d\n", port, index);
		return 0;
	}

	data->pleds = pleds;
	data->port = port;
	data->index = index;

	data->cdev.brightness = LED_OFF;
	data->cdev.brightness_set = pled_user_brightness_set;
	data->cdev.blink_set = pled_user_blink_set;

	if (_fwnode) {
		struct led_init_data init_data = { .fwnode = _fwnode };

		ret = devm_led_classdev_register_ext(pleds->dev,
						     &data->cdev,
						     &init_data);
	} else {
		scnprintf(data->sysfs_regname, 8, "p%d.%d", port, index);
		data->cdev.name = data->sysfs_regname;

		ret = devm_led_classdev_register(pleds->dev, &data->cdev);
	}

	if (ret) {
		dev_err(pleds->dev,
			"failed to register port LED: %d.%d\n",
			port, index);
		return ret;
	}

	intf->pled_ops->port_led_user_register(intf, port, index, true);

	data->registered = true;

	dev_info(pleds->dev,
		"User LED: registered: port->%2d, index->%d, aindex->%2d\n",
		port, index, aindex);

	return 0;
}

/*
 * Port LED registration as user-controlled LED
 *
 *        port  index
 *   reg = <0     0>; //port0,index0
 */
static int port_leds_user_register(struct realtek_port_leds *pleds)
{
	struct realtek_led_ctrl_intf *intf = pleds->intf;
	struct realtek_pled_data *data;
	struct fwnode_handle *child;
	int ret, i = 0;
	uint32_t prop[2];

//	dev_info(pleds->dev, "%s\n", __func__);

	device_for_each_child_node(pleds->dev, child) {
		int port, index;

		ret = fwnode_property_read_u32_array(child, "reg", prop, 2);
		if (ret) {
			dev_err(pleds->dev, "failed to read reg property\n");
			fwnode_handle_put(child);
			goto fail_unregister;
		}

		port = prop[0];
		index = prop[1];

		ret = port_led_user_register(pleds, child, port, index);
		if (ret) {
			fwnode_handle_put(child);
			goto fail_unregister;
		}

		i++;
	}

	dev_info(pleds->dev, "User LED: %dx registered\n", i);

	return 0;

fail_unregister:
	for (i = 0; i < intf->port_max * intf->pled_num; i++) {
		data = &pleds->leds[i];

		if (!data->registered)
			continue;

		intf->pled_ops->port_led_user_register(intf, data->port,
						       data->index,
						       false);

		data->registered = false;
	}

	return ret;
}

static int port_leds_pset_set(struct realtek_port_leds *pleds,
			      int pset, uint64_t ports, int type)
{
	struct realtek_pled_ops *ops = pleds->intf->pled_ops;
	int ret;

	ports &= pleds->intf->port_mask;
	ret = ops->port_led_pset_set(pleds->intf,
					    pset, ports, type);
	if (ret) {
		dev_err(pleds->dev,
			 "ASIC LED: failed: set%d pmask (%s) -> 0x%016llx (%d)\n",
			 pset,
			 type == MEDIA_TYPE_FIBRE ? "fibre" : "copper",
			 ports, ret);
		return ret;
	}

	dev_info(pleds->dev,
		 "ASIC LED: set%d pmask (%s) -> 0x%016llx\n",
		 pset,
		 type == MEDIA_TYPE_FIBRE ? "fibre" : "copper",
		 ports);

	return 0;
}

static int port_leds_asic_set(struct realtek_port_leds *pleds,
			      int pset, int index, int mode)
{
	struct realtek_pled_ops *ops = pleds->intf->pled_ops;
	int ret;

	ret = ops->port_led_asic_set(pleds->intf, pset, index, mode);

	dev_info(pleds->dev, "ASIC LED: set%d.%d -> %d\n",
			 pset, index, mode);

	if (ret == -EINVAL)
		dev_warn(pleds->dev,
			 "unsupported ASIC LED mode on this SoC: %d\n",
			 mode);
	else if (ret == -ERANGE)
		dev_warn(pleds->dev,
			 "unsupported port-set range of LED on this SoC: %d\n",
			 pset);
	else if (ret == -ENOTSUPP)
		dev_warn(pleds->dev,
			 "unsupported platform for the driver\n");

	return ret;
}

/*
 * Port LED initialization
 *
 * - LED-set setup
 *                   set    HIGH        LOW
 *   led-set-ports = <0  0x000f0ff0 0xf0ff0000>,
 *   		     <1  0x0000f000 0x0000f000>,
 *   		     ...
 *
 * - set mode of LED-set
 *                   set  index  mode
 *   led-set-modes = <0     0     0>, // set0,index0 -> link/act
 *   		     <1     2     6>, // set1,index0 -> duplex
 *   		     ...
 */
static int parse_led_pset_ports(struct realtek_port_leds *pleds,
				const uint32_t *prop, int len,
				int maplen, int type)
{
	size_t i;
	int ret, pset;

	if (len % maplen) {
		dev_err(pleds->dev,
			"led-set-%s property has invalid length %u\n",
			type == MEDIA_TYPE_FIBRE ? "fibre" : "copper",
			len);
		return -EINVAL;
	}

	for (i = 0; i < len; i += maplen * sizeof(uint32_t),
			     prop += maplen) {
		uint64_t ports;

		pset = *prop;
		if (pset > LED_PSET_MAX)
			continue;
		ports = *(prop + 1);
		if (maplen == 3) {
			ports <<= 32;
			ports |= *(prop + 2);
		}

		ret = port_leds_pset_set(pleds, pset, ports, type);
		if (ret)
			return ret;
	}

	return 0;
}

static int port_leds_init(struct realtek_port_leds *pleds)
{
	int ret, i, proplen, pmaplen = 1, pset;
	uint32_t tmp;
	const uint32_t *prop;
	struct device_node *dn = pleds->dev->of_node;
	struct realtek_pled_ops *ops = pleds->intf->pled_ops;

//	dev_info(pleds->dev, "%s\n", __func__);

	if (ops->port_led_activate)
		ops->port_led_activate(pleds->intf);

	if (!ops->port_led_pset_set)
		goto skip_ports;

	ret = of_property_read_u32(dn, "#port-cells", &tmp);
	if (!ret && (0 < tmp && tmp < 3))
		pmaplen += tmp;
	else
		pmaplen++;

	prop = of_get_property(dn, "realtek,led-set-fibre", &proplen);
	if (prop) {
		ret = parse_led_pset_ports(pleds, prop, proplen, pmaplen,
				   MEDIA_TYPE_FIBRE);
		if (ret)
			dev_warn(pleds->dev,
				 "failed to configure port-set for fibre\n");
	}

	prop = of_get_property(dn, "realtek,led-set-copper", &proplen);
	if (prop) {
		ret = parse_led_pset_ports(pleds, prop, proplen, pmaplen,
				   MEDIA_TYPE_COPPER);
		if (ret)
			dev_warn(pleds->dev,
				 "failed to configure port-set for copper\n");
	}

skip_ports:
	prop = of_get_property(dn, "realtek,led-set-modes", &proplen);
	if (!prop)
		goto end_init;

	if (proplen % 3) {
		dev_err(pleds->dev,
			"led-set-modes property has invalid length %u\n",
			proplen);
		goto end_init;
	}

	for (i = 0; i < proplen; i += 3 * sizeof(uint32_t),
				 prop += 3) {
		int index, mode;

		pset = *prop;
		index = *(prop + 1);
		if (index > pleds->intf->pled_num)
			continue;
		mode = *(prop + 2);

		port_leds_asic_set(pleds, pset, index, mode);
	}

end_init:
	ops->port_led_user_init(pleds->intf);

	return 0;
}

static ssize_t led_registered_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct realtek_port_leds *pleds = dev_get_drvdata(dev);
	struct realtek_led_ctrl_intf *intf = pleds->intf;
	size_t p, i;
	ssize_t ret, totlen = 0;

//	pr_info("max-> %d, pmask-> 0x%016llx\n", intf->port_max, intf->port_mask);

	for (p = 0; p < intf->port_max; p++) {
//		pr_info("p-> %d (max-> %d)\n", p, intf->port_max);
		if (!(intf->port_mask & BIT_ULL(p)))
			continue;

		ret = scnprintf(buf + totlen, 10, "p%d\t", p);
		totlen += ret;

		for (i = 0; i < intf->pled_num; i++) {
			int aindex;
			char led_char;

			aindex = port_led_index_get(intf, p, i);
			if (aindex < 0)
				return -EINVAL;
//			pr_info("p-> %d, i-> %d, aindex-> %d\n", p, i, aindex);

			led_char = pleds->leds[aindex].registered ?
						0x30 + i : '-';

			ret = scnprintf(buf + totlen, 10, "%c ", led_char);
			totlen += ret;
		}

		ret = scnprintf(buf + totlen, 10, "\n");
		totlen += ret;
	}

//	pr_info("totlen: %d\n", totlen);

	return totlen;
}

static ssize_t led_registered_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	struct realtek_port_leds *pleds = dev_get_drvdata(dev);
	ssize_t ret, offset = 0;
	int port, index;
	char suffix;

	ret = sscanf(buf, "%c", &suffix);
	if (ret <= 0)
		return -EINVAL;
	if (suffix == '-' || suffix == '+')
		offset++;

	ret = sscanf(buf + offset, "%d.%d", &port, &index);
	if (ret <= 0)
		return -EINVAL;

	pr_info("port-> %d, index-> %d\n", port, index);

	mutex_lock(&pleds->lock);
	if (suffix == '+') {
		ret = port_led_user_register(pleds, NULL, port, index);
	} else if (suffix == '-') {
		ret = port_led_user_unregister(pleds, port, index);
	} else {
		pr_info("no '-'/'+', no action\n");
		ret = 0;
	}
	mutex_unlock(&pleds->lock);

	return ret ? ret : len;
}

static DEVICE_ATTR_RW(led_registered);


/*
 * get/set ASIC LED mode of each port-set/LED
 *
 * $ cat led_pset0_modes
 * 0 2	// led mode
 * 1 7
 * 2 4
 */
static ssize_t _led_pset_modes_show(struct realtek_port_leds *pleds,
				     int pset, char *buf)
{
	struct realtek_led_ctrl_intf *intf = pleds->intf;
	ssize_t ret, totlen = 0;
	int i;

	for (i = 0; i < intf->pled_num; i++) {
		int mode;

		mode = intf->pled_ops->port_led_asic_get(intf,
							 pset, i);

		ret = scnprintf(buf + totlen, 16, "led%d\tmode%d\n",
				i, mode);

		totlen += ret;
	}

	return totlen;
}

static ssize_t _led_pset_modes_store(struct realtek_port_leds *pleds,
				      int pset, const char *buf,
				      size_t len)
{
	int ret, index, mode;

	ret = sscanf(buf, "%d %d", &index, &mode);
	if (ret <= 0)
		ret = sscanf(buf, "led%d mode%d", &index, &mode);

	if (ret <= 0)
		return -EINVAL;

	ret = port_leds_asic_set(pleds, pset, index, mode);

	return ret ? ret : len;
}

#define LED_PSET_MODE_SHOW(s)						\
	static ssize_t led_pset##s##_modes_show(struct device *dev,	\
				struct device_attribute *attr, char *buf)\
	{ struct realtek_port_leds *pleds = dev_get_drvdata(dev);	\
	  return _led_pset_modes_show(pleds, s, buf); }

#define LED_PSET_MODE_STORE(s)						\
	static ssize_t led_pset##s##_modes_store(struct device *dev,	\
				struct device_attribute *attr,		\
				const char *buf, size_t len)		\
	{ struct realtek_port_leds *pleds = dev_get_drvdata(dev);	\
	  return _led_pset_modes_store(pleds, s, buf, len); }

LED_PSET_MODE_SHOW(0);
LED_PSET_MODE_SHOW(1);
LED_PSET_MODE_SHOW(2);
LED_PSET_MODE_SHOW(3);
LED_PSET_MODE_STORE(0);
LED_PSET_MODE_STORE(1);
LED_PSET_MODE_STORE(2);
LED_PSET_MODE_STORE(3);

static DEVICE_ATTR_RW(led_pset0_modes);
static DEVICE_ATTR_RW(led_pset1_modes);
static DEVICE_ATTR_RW(led_pset2_modes);
static DEVICE_ATTR_RW(led_pset3_modes);

static struct device_attribute *realtek_led_pset_mode_attrs[] = {
	&dev_attr_led_pset0_modes,
	&dev_attr_led_pset1_modes,
	&dev_attr_led_pset2_modes,
	&dev_attr_led_pset3_modes,
	NULL,
};

/*
 * get/set port mask of each port-set
 */
static ssize_t _led_pset_show(struct realtek_port_leds *pleds,
				 int pset, int type, char *buf)
{
	struct realtek_pled_ops *ops = pleds->intf->pled_ops;
	uint64_t ports;
	ssize_t ret;

	ret = ops->port_led_pset_get(pleds->intf, pset, &ports,
				       type);
	if (ret)
		return ret;

	return scnprintf(buf, 32, "0x%016llx\n", ports);
}

static ssize_t _led_pset_store(struct realtek_port_leds *pleds,
				   int pset, int type,
				   const char *buf, size_t len)
{
	uint64_t ports;
	ssize_t ret;

	if (kstrtou64(buf, 0, &ports))
		return -EINVAL;

	if (!ports)
		return 0;

	ret = port_leds_pset_set(pleds, pset, ports, type);

	return ret ? ret : len;
}

#define LED_PSET_SHOW(s,n,t)						\
	static ssize_t led_pset##s##_##n##_show(struct device *dev,	\
				struct device_attribute *attr, char *buf)\
	{ struct realtek_port_leds *pleds = dev_get_drvdata(dev);	\
	  return _led_pset_show(pleds, s, t, buf); }

#define LED_PSET_STORE(s,n,t)						\
	static ssize_t led_pset##s##_##n##_store(struct device *dev,	\
			     struct device_attribute *attr,		\
			     const char *buf, size_t len)		\
	{ struct realtek_port_leds *pleds = dev_get_drvdata(dev);	\
	  return _led_pset_store(pleds, s, t, buf, len); }

LED_PSET_SHOW(0, fibre, MEDIA_TYPE_FIBRE);
LED_PSET_SHOW(1, fibre, MEDIA_TYPE_FIBRE);
LED_PSET_SHOW(2, fibre, MEDIA_TYPE_FIBRE);
LED_PSET_SHOW(3, fibre, MEDIA_TYPE_FIBRE);
LED_PSET_SHOW(0, copper, MEDIA_TYPE_COPPER);
LED_PSET_SHOW(1, copper, MEDIA_TYPE_COPPER);
LED_PSET_SHOW(2, copper, MEDIA_TYPE_COPPER);
LED_PSET_SHOW(3, copper, MEDIA_TYPE_COPPER);
LED_PSET_STORE(0, fibre, MEDIA_TYPE_FIBRE);
LED_PSET_STORE(1, fibre, MEDIA_TYPE_FIBRE);
LED_PSET_STORE(2, fibre, MEDIA_TYPE_FIBRE);
LED_PSET_STORE(3, fibre, MEDIA_TYPE_FIBRE);
LED_PSET_STORE(0, copper, MEDIA_TYPE_COPPER);
LED_PSET_STORE(1, copper, MEDIA_TYPE_COPPER);
LED_PSET_STORE(2, copper, MEDIA_TYPE_COPPER);
LED_PSET_STORE(3, copper, MEDIA_TYPE_COPPER);

static DEVICE_ATTR_RW(led_pset0_fibre);
static DEVICE_ATTR_RW(led_pset1_fibre);
static DEVICE_ATTR_RW(led_pset2_fibre);
static DEVICE_ATTR_RW(led_pset3_fibre);
static DEVICE_ATTR_RW(led_pset0_copper);
static DEVICE_ATTR_RW(led_pset1_copper);
static DEVICE_ATTR_RW(led_pset2_copper);
static DEVICE_ATTR_RW(led_pset3_copper);

static struct attribute *realtek_led_pset_attrs[] = {
	&dev_attr_led_pset0_fibre.attr,
	&dev_attr_led_pset1_fibre.attr,
	&dev_attr_led_pset2_fibre.attr,
	&dev_attr_led_pset3_fibre.attr,
	&dev_attr_led_pset0_copper.attr,
	&dev_attr_led_pset1_copper.attr,
	&dev_attr_led_pset2_copper.attr,
	&dev_attr_led_pset3_copper.attr,
	NULL,
};

static const struct attribute_group realtek_led_pset_attr_group = {
	.attrs = realtek_led_pset_attrs,
};

static int realtek_pled_sysfs_register(struct realtek_port_leds *pleds)
{
	struct realtek_pled_ops *ops = pleds->intf->pled_ops;
	size_t i;
	int ret;

	for (i = 0; i <= pleds->intf->pset_max; i++) {
		ret = device_create_file(pleds->dev,
					 realtek_led_pset_mode_attrs[i]);
		if (ret)
			return ret;
	}

	ret = device_create_file(pleds->dev, &dev_attr_led_registered);
	if (ret)
		return ret;

	if (!ops->port_led_pset_get)
		return 0;

	ret = sysfs_create_group(&pleds->dev->kobj,
				 &realtek_led_pset_attr_group);
	if (ret)
		return ret;

	return 0;
}

static int realtek_port_leds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct realtek_port_leds *pleds;
	struct realtek_led_ctrl_intf *intf;
//	uint64_t pmask;
	int ret;
//	int plnum;

	pr_info("Realtek Port LED Driver v%s\n", DRIVER_VERSION);

	intf = dev_get_drvdata(dev->parent);

	dev_info(dev, "port_max: %d, port_mask: 0x%016llx, led_num: %d\n",
		intf->port_max, intf->port_mask, intf->pled_num);

/*	ret = of_property_read_u64(dev->of_node, "realtek,port-mask",
				   &pmask);
	if (!ret && pmask)
		intf->port_mask = pmask;

	ret = of_property_read_u32(dev->of_node, "realtek,leds-per-port",
				   &plnum);
	if (!ret && plnum)
		intf->pled_num = plnum;
*/
	if (!intf->port_mask || !intf->pled_num) {
		dev_err(dev, "no available port LED on this device\n");
		return -ENODEV;
	}

	pleds = devm_kzalloc(dev,
			     struct_size(pleds, leds,
					 intf->port_max * intf->pled_num),
			     GFP_KERNEL);
	if (!pleds) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	memset(pleds->leds, 0,
	       sizeof(struct realtek_pled_data)
			* intf->port_max * intf->pled_num);

	pleds->intf = intf;
	pleds->dev = dev;

	mutex_init(&pleds->lock);

	dev_set_drvdata(dev, pleds);

	ret = port_leds_init(pleds);
	if (ret) {
		dev_err(dev, "failed to initialize the port LEDs\n");
		goto fail_exit;
	}

	ret = realtek_pled_sysfs_register(pleds);
	if (ret) {
		dev_err(dev, "failed to register sysfs interfaces\n");
		goto fail_exit;
	}

	ret = port_leds_user_register(pleds);
	if (ret) {
		dev_err(dev,
			"failed to register the port LEDs as user LED\n");
		goto fail_exit;
	}

	return 0;

fail_exit:
	mutex_destroy(&pleds->lock);

	return ret;
}

static const struct of_device_id realtek_port_leds_ids[] = {
	{ .compatible = "realtek,port-leds" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, realtek_port_leds_ids);

static struct platform_driver realtek_port_leds_driver = {
	.probe = realtek_port_leds_probe,
//	.remove =
	.driver = {
		.name = "leds-realtek-port",
		.of_match_table = realtek_port_leds_ids,
	},
};
module_platform_driver(realtek_port_leds_driver);

MODULE_DESCRIPTION("Realtek Port LED driver");
MODULE_LICENSE("GPL v2");
