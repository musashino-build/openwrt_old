// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Renesas R8C MCU driver on I-O DATA HDL series
 *
 * This MCU has the following functions:
 *
 * - LED
 * - button
 * - temperature
 * - fan control
 * - poweroff
 * - reset
 * - beeper
 * - hardware information storage
 *
 * This driver is based on the R8C driver of GPL source provided by
 * I-O DATA, and rave-sp.c in Linux Kernel
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/serdev.h>

#include <linux/mfd/landisk-r8c.h>

#define R8C_BAUD		57600
#define R8C_TIMEOUT		HZ

#define CMD_MAX_LEN		10
#define CMD_MIN_LEN		2
#define ARG_MAX_LEN		60
#define MSG_MIN_LEN		2
#define MSG_MAX_LEN		64

#define MSG_TYPE_CMD		':'
#define MSG_TYPE_EVENT		'@'
#define MSG_TYPE_RESULT		';'

#define R8C_CHILD_CNT		4

/* basic commands */
#define CMD_MODEL		"model"
#define CMD_VER			"ver"

struct r8c_reply {
	bool waiting;
	size_t pos;
	char data[MSG_MAX_LEN];
};

struct r8c_mcu {
	struct serdev_device *serdev;
	struct mutex lock;
	struct delayed_work ev_work;

	u32 model_id;
	char ev_code;
	struct r8c_reply reply;

	struct blocking_notifier_head ev_list;
	struct completion rpl_recv;
};

static void r8c_unregister_event_notifier(struct device *dev,
					  void *res)
{
	struct r8c_mcu *r8c = dev_get_drvdata(dev->parent);
	struct notifier_block *nb = *(struct notifier_block **)res;
	struct blocking_notifier_head *bnh = &r8c->ev_list;

	WARN_ON(blocking_notifier_chain_unregister(bnh, nb));
}

int devm_r8c_register_event_notifier(struct device *dev,
				     struct notifier_block *nb)
{
	struct r8c_mcu *r8c = dev_get_drvdata(dev->parent);
	struct notifier_block **rcnb;
	int ret;

	rcnb = devres_alloc(r8c_unregister_event_notifier,
			    sizeof(*rcnb), GFP_KERNEL);
	if (!rcnb)
		return -ENOMEM;

	ret = blocking_notifier_chain_register(&r8c->ev_list, nb);
	if (!ret) {
		*rcnb = nb;
		devres_add(dev, rcnb);
	} else {
		devres_free(rcnb);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_r8c_register_event_notifier);

static void r8c_reply_clear(struct r8c_mcu *r8c)
{
	memset(&r8c->reply, 0, sizeof(r8c->reply));
}

static void r8c_notifier_call_work(struct work_struct *work)
{
	struct r8c_mcu *r8c = container_of(work, struct r8c_mcu,
					   ev_work.work);

	blocking_notifier_call_chain(&r8c->ev_list, r8c->ev_code, NULL);
}

static void r8c_receive_reply(struct r8c_mcu *r8c,
			      const unsigned char *data, size_t size)
{
	struct r8c_reply *reply = &r8c->reply;

	while (size > 0 && reply->pos < MSG_MAX_LEN) {
		/* exit on line breaking */
		if (*data == '\n') {
//			dev_info(dev, "'\\n' matched, pos-> %d\n", reply->pos);
			complete(&r8c->rpl_recv);
			break;
		}

		reply->data[reply->pos] = *data;
		dev_info(&r8c->serdev->dev, "char-> '%c', data[%d]-> '%c'\n",
			 *data, reply->pos, reply->data[reply->pos]);

		data++;
		size--;
		reply->pos++;
	}
}

static int r8c_receive_buf(struct serdev_device *serdev,
			   const unsigned char *buf, size_t size)
{
	struct device *dev = &serdev->dev;
	struct r8c_mcu *r8c = dev_get_drvdata(dev);
	size_t offset = 0;

//	pr_info("received msg-> \"%s\" (%d bytes)\n", buf, size);

	if ((buf[0] == MSG_TYPE_EVENT || buf[0] == MSG_TYPE_RESULT) &&
	    size < MSG_MIN_LEN) {
		dev_warn(dev,
			 "received message has invalid length for event/result, %d\n",
			 size);
		return 0;
	}

	switch (buf[0]) {
	case MSG_TYPE_EVENT:
		dev_info(dev, "ev code: %c\n", buf[1]);
		r8c->ev_code = buf[1];
		/* add work to system workqueue to exit receive_buf */
		mod_delayed_work(system_wq, &r8c->ev_work, 0);
		break;
	case MSG_TYPE_RESULT:
		offset++;
		/* fall through */
	default:
		if (!r8c->reply.waiting) {
			dev_warn(dev,
				 "waiting flag is false, ignore buf \"%s\" (%u bytes)\n",
				 buf, size);
			return size;
		}

		r8c_receive_reply(r8c, buf + offset, size - offset);
		break;
	}

	return size;
}

static int r8c_exec_raw(struct r8c_mcu *r8c, const char *buf,
			size_t len, char *rcv_buf, size_t rcv_len)
{
	int ret = 0, wrote;
	/* including trailing null */
	char _buf[MSG_MAX_LEN + 1];
	unsigned long jiffies_left;

	/* for command prefix ':' and '\n' at the end */
	if (len > MSG_MAX_LEN - 2) {
		dev_err(&r8c->serdev->dev,
			"message length is larger than maximum length (msg %d, max %d)\n",
			len, MSG_MAX_LEN);
		return -EINVAL;
	}

	len = scnprintf(_buf, MSG_MAX_LEN + 1, ":%s\n", buf);

	dev_info(&r8c->serdev->dev, "buf-> \"%s\" (len-> %d)\n",
		 _buf, len);

	mutex_lock(&r8c->lock);

	r8c->rpl_recv = COMPLETION_INITIALIZER_ONSTACK(r8c->rpl_recv);

	wrote = serdev_device_write(r8c->serdev, _buf, len, HZ);
	pr_info("wrote %d bytes", wrote);

	if (rcv_buf) {
		r8c->reply.waiting = true;
		jiffies_left = wait_for_completion_timeout(&r8c->rpl_recv,
							   R8C_TIMEOUT);
//		dev_info(&r8c->serdev->dev, "jiffies_left-> %lu\n",
//			 jiffies_left);
		/*
		 * Note:
		 *   some commands don't return message
		 */
		if (!jiffies_left) {
			dev_err(&r8c->serdev->dev, "command timeout\n");
			ret = -ETIMEDOUT;
			goto exit;
		}

		memcpy(rcv_buf, r8c->reply.data, rcv_len);
	}

exit:
	r8c_reply_clear(r8c);

	mutex_unlock(&r8c->lock);

	return ret;
}

int r8c_exec_cmd(struct r8c_mcu *r8c, const char *cmd, const char *arg,
		  char *rcv_buf, size_t rcv_len)
{
	const char *buf;
	size_t cmd_len = strlen(cmd);
	size_t arg_len = arg ? strlen(arg) : 0;

	if (!r8c) {
		pr_info("%s: r8c is null\n", __func__);
		return -EINVAL;
	}

	if (cmd_len > CMD_MAX_LEN) {
		dev_err(&r8c->serdev->dev,
			"specified command is too long, %u\n",
			cmd_len);
		return -EINVAL;
	}

	if (arg_len > ARG_MAX_LEN) {
		dev_err(&r8c->serdev->dev,
			"specified argument is too long, %u\n",
			arg_len);
		return -EINVAL;
	}

	buf = devm_kasprintf(&r8c->serdev->dev, GFP_KERNEL, "%s%s%s",
			     cmd, arg ? " " : "", arg ? arg : "");

	return r8c_exec_raw(r8c, buf, cmd_len + arg_len,
			    rcv_buf, rcv_len);
}
EXPORT_SYMBOL_GPL(r8c_exec_cmd);

static const struct serdev_device_ops landisk_r8c_ops = {
	.receive_buf = r8c_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

static ssize_t command_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct r8c_mcu *r8c = dev_get_drvdata(dev);
	ssize_t ret;
	size_t left_len = len;
	char cmdbuf[MSG_MAX_LEN];
	const char *cur = buf;

	if (len == 0 || len > MSG_MAX_LEN)
		return -EINVAL;

	if (cur[0] == MSG_TYPE_CMD) {
		left_len--;
		cur++;
	}

	if (left_len < CMD_MIN_LEN)
		return -EINVAL;

	ret = r8c_exec_raw(r8c, cur, left_len, cmdbuf, MSG_MAX_LEN);
	if (ret)
		return ret;

	pr_info("%s\n", cmdbuf);

	return len;
}

static DEVICE_ATTR_WO(command);

static const char *landisk_r8c_models[] = {
	[ID_HDL_A]  = "HDL-A-0",
	[ID_HDL2_A] = "HDL2-A-0",
};

static int r8c_detect(struct device *dev, struct r8c_mcu *r8c)
{
	char r8c_model[16], r8c_ver[8];
	int ret;

	ret = r8c_exec_cmd(r8c, CMD_MODEL, NULL, r8c_model, 16);
	if (ret || strlen(r8c_model) < 6) {
		dev_err(dev, "failed to get model name from R8C\n");
		return ret;
	}

	if (strcmp(r8c_model, landisk_r8c_models[r8c->model_id])) {
		dev_err(dev,
			"unsupported R8C model (expected: %s, returned: %s)\n",
			landisk_r8c_models[r8c->model_id], r8c_model);
		return -ENOTSUPP;
	}

	ret = r8c_exec_cmd(r8c, CMD_VER, NULL, r8c_ver, 8);
	if (ret || strlen(r8c_ver) < 3) {
		dev_err(dev, "failed to get version from R8C\n");
		return ret;
	}

	dev_info(dev, "R8C FW Model: %s, Version: %s\n",
		 r8c_model, r8c_ver);

	return 0;
}

static int landisk_r8c_probe(struct serdev_device *serdev)
{
	int ret;
	struct device *dev = &serdev->dev;
	struct r8c_mcu *r8c;

	pr_info("Renesas R8C driver for I-O DATA LAN DISK series\n");

	r8c = devm_kzalloc(dev, sizeof(*r8c), GFP_KERNEL);
	if (!r8c) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	r8c->serdev = serdev;
	serdev_device_set_drvdata(serdev, r8c);

	r8c->model_id = (u32)of_device_get_match_data(dev);

	/* set platform_data for child devices */
	dev->platform_data = &r8c->model_id;

	r8c_reply_clear(r8c);

	mutex_init(&r8c->lock);
	BLOCKING_INIT_NOTIFIER_HEAD(&r8c->ev_list);
	INIT_DELAYED_WORK(&r8c->ev_work, r8c_notifier_call_work);

	serdev_device_set_client_ops(serdev, &landisk_r8c_ops);
	ret = devm_serdev_device_open(dev, serdev);
	if (ret)
		goto err;

	serdev_device_set_baudrate(serdev, R8C_BAUD);
	serdev_device_set_flow_control(serdev, false);

	ret = serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);
	if (ret)
		goto err;

	ret = r8c_detect(dev, r8c);
	if (ret)
		goto err;

	ret = device_create_file(dev, &dev_attr_command);
	if (ret) {
		dev_err(dev, "failed to create command sysfs\n");
		goto err;
	}

	ret = devm_of_platform_populate(dev);
	if (!ret)
		return 0;

err:
	mutex_destroy(&r8c->lock);

	return ret;
};

static void landisk_r8c_remove(struct serdev_device *serdev)
{
	device_remove_file(&serdev->dev, &dev_attr_command);
}

static const struct of_device_id landisk_r8c_ids[] = {
	{
		.compatible = "iodata,hdl-a-r8c",
		.data = (const void *)ID_HDL_A,
	},
	{
		.compatible = "iodata,hdl2-a-r8c",
		.data = (const void *)ID_HDL2_A,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, landisk_r8c_ids);

static struct serdev_device_driver landisk_r8c_driver = {
	.probe = landisk_r8c_probe,
	.remove = landisk_r8c_remove,
	.driver = {
		.name = "landisk-r8c",
		.of_match_table = landisk_r8c_ids,
	},
};
module_serdev_device_driver(landisk_r8c_driver);

MODULE_DESCRIPTION("Renesas R8C MCU driver for I-O DATA LAN DISK series");
MODULE_LICENSE("GPL v2");
