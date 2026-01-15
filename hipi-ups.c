#include <linux/module.h>       /* Essential for all modules */
#include <linux/platform_device.h> /* For platform_driver and platform_device */
#include <linux/gpio/consumer.h>   /* For gpiod_* functions */
#include <linux/interrupt.h>       /* For request_irq and irqreturn_t */
#include <linux/of.h>          /* Core Device Tree support */
#include <linux/slab.h>        /* For devm_kzalloc and memory management */

MODULE_DESCRIPTION("Hipi UPS GPIO Interrupt Listener");
MODULE_AUTHOR("Clayton Watts <cletusw@gmail.com>");
MODULE_LICENSE("Dual MIT/GPL");

struct gpio_data {
    struct gpio_desc *desc;
    int irq;
};

/* The Interrupt Handler */
static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
    struct gpio_data *data = dev_id;
    int val = gpiod_get_value(data->desc);

    pr_info("hipi-ups: Interrupt! Pin is now %s\n", val ? "HIGH" : "LOW");

    return IRQ_HANDLED;
}

static int hipi_ups_gpio_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct gpio_data *data;
    int ret;

    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data) return -ENOMEM;

    /* 1. Get the GPIO descriptor (marked as input) */
    /* "monitor" corresponds to "monitor-gpios" in Device Tree */
    data->desc = devm_gpiod_get(dev, "monitor", GPIOD_IN);
    if (IS_ERR(data->desc)) {
        dev_err(dev, "Failed to get GPIO\n");
        return PTR_ERR(data->desc);
    }

    /* 2. Map the GPIO to an IRQ number */
    data->irq = gpiod_to_irq(data->desc);
    if (data->irq < 0) return data->irq;

    /* 3. Request the interrupt */
    /* IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING for both edges */
    ret = devm_request_threaded_irq(dev, data->irq, NULL, gpio_irq_handler,
                                    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                    "hipi_ups_gpio_irq", data);
    if (ret) {
        dev_err(dev, "Failed to request IRQ\n");
        return ret;
    }

    platform_set_drvdata(pdev, data);
    pr_info("hipi-ups: Driver probed, monitoring IRQ %d\n", data->irq);
    return 0;
}

static const struct of_device_id gpio_ids[] = {
    { .compatible = "custom,hipi-ups" },
    { }
};
MODULE_DEVICE_TABLE(of, gpio_ids);

static struct platform_driver hipi_ups_driver = {
    .probe = hipi_ups_gpio_probe,
    .driver = {
        .name = "hipi_ups",
        .of_match_table = gpio_ids,
    },
};

module_platform_driver(hipi_ups_driver);
