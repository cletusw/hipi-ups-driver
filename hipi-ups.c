#include <linux/module.h>       /* Essential for all modules */
#include <linux/platform_device.h> /* For platform_driver and platform_device */
#include <linux/gpio/consumer.h>   /* For gpiod_* functions */
#include <linux/interrupt.h>       /* For request_irq and irqreturn_t */
#include <linux/of.h>          /* Core Device Tree support */
#include <linux/slab.h>        /* For devm_kzalloc and memory management */
#include <linux/workqueue.h>   /* Required for delayed_work */
#include <linux/reboot.h>      /* Required for orderly_poweroff */

MODULE_DESCRIPTION("Hipi UPS Driver");
MODULE_AUTHOR("Clayton Watts <cletusw@gmail.com>");
MODULE_LICENSE("Dual MIT/GPL");

/* 60 seconds delay converted to milliseconds */
#define SHUTDOWN_DELAY_MS 60000

struct gpio_data {
    struct gpio_desc *power_desc;  /* For power fault detection (Input) */
    struct gpio_desc *status_desc; /* For sending Pi status to UPS (Output) */
    int power_irq;
    struct delayed_work shutdown_work;
    struct device *dev; /* Reference for logging */
};

/* Timer expired. Shutdown now */
static void shutdown_work_handler(struct work_struct *work)
{
    struct gpio_data *data = container_of(work, struct gpio_data, shutdown_work.work);

    dev_alert(data->dev, "Power failure persisted for %d ms. Initiating shutdown.\n", SHUTDOWN_DELAY_MS);

    orderly_poweroff(/* force= */ true);
}

/* The Interrupt Handler */
static irqreturn_t power_irq_handler(int irq, void *dev_id)
{
    struct gpio_data *data = dev_id;
    int val = gpiod_get_value(data->power_desc);

    if (val == 1) {
        /* High = Power Fault. Schedule shutdown. */
        dev_warn(data->dev, "Power Lost! Shutdown scheduled in %d ms.\n", SHUTDOWN_DELAY_MS);
        schedule_delayed_work(&data->shutdown_work, msecs_to_jiffies(SHUTDOWN_DELAY_MS));
    } else {
        /* Low = Power Restored. Cancel shutdown. */
        dev_warn(data->dev, "Power Restored. Shutdown cancelled.\n");
        cancel_delayed_work_sync(&data->shutdown_work);
    }

    return IRQ_HANDLED;
}

static int hipi_ups_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct gpio_data *data;
    int ret;

    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data) return -ENOMEM;

    data->dev = dev;

    /* --- Pi status/heartbeat output --- */
    /* Request the pin and immediately initialize it to Logical 0 (Low).
     * GPIOD_OUT_LOW assumes Active High logic.
     */
    data->status_desc = devm_gpiod_get(dev, "status", GPIOD_OUT_LOW);
    if (IS_ERR(data->status_desc)) {
        dev_err(dev, "Failed to get status-gpios\n");
        /* Optional: We might not want to fail probe if this pin is missing,
           but for now we enforce it. */
        return PTR_ERR(data->status_desc);
    }

    /* Explicitly set value to 0 to be extra clear */
    gpiod_set_value(data->status_desc, 0);

    /* --- Power fault detection --- */
    /* Initialize the delayed work structure */
    INIT_DELAYED_WORK(&data->shutdown_work, shutdown_work_handler);

    /* Get the Power GPIO (corresponds to "power-gpios" in Device Tree) */
    data->power_desc = devm_gpiod_get(dev, "power", GPIOD_IN);
    if (IS_ERR(data->power_desc)) {
        dev_err(dev, "Failed to get power-gpios\n");
        return PTR_ERR(data->power_desc);
    }

    /* Check initial state in case we booted without power */
    if (gpiod_get_value(data->power_desc)) {
        dev_warn(dev, "Booted with power failure detected.\n");
        schedule_delayed_work(&data->shutdown_work, msecs_to_jiffies(SHUTDOWN_DELAY_MS));
    }

    /* Map the GPIO to an IRQ number */
    data->power_irq = gpiod_to_irq(data->power_desc);
    if (data->power_irq < 0) return data->power_irq;

    /* Request the interrupt */
    /* IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING for both edges */
    ret = devm_request_threaded_irq(dev, data->power_irq, NULL, power_irq_handler,
                                    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                    "hipi_ups_power_irq", data);
    if (ret) {
        dev_err(dev, "Failed to request IRQ\n");
        return ret;
    }

    platform_set_drvdata(pdev, data);
    dev_info(dev, "Driver probed, monitoring IRQ %d\n", data->power_irq);
    return 0;
}

static void hipi_ups_remove(struct platform_device *pdev)
{
    struct gpio_data *data = platform_get_drvdata(pdev);

    /* Ensure any pending shutdown works are cancelled if we unload the module */
    cancel_delayed_work_sync(&data->shutdown_work);

    /* When module unloads, we could let devm_ handle releasing the status pin
     * or release it manually. Choosing the former but also explicitly setting
     * it High in the meantime to make sure the UPS receives the signal.
     */
    if (data->status_desc) {
        dev_info(&pdev->dev, "Setting status pin to HIGH (Stopping).\n");
        gpiod_set_value(data->status_desc, 1);
    }

    dev_info(&pdev->dev, "Module unloaded.\n");
}

static const struct of_device_id gpio_ids[] = {
    { .compatible = "custom,hipi-ups" },
    { }
};
MODULE_DEVICE_TABLE(of, gpio_ids);

static struct platform_driver hipi_ups_driver = {
    .probe = hipi_ups_probe,
    .remove = hipi_ups_remove,
    .driver = {
        .name = "hipi_ups",
        .of_match_table = gpio_ids,
    },
};

module_platform_driver(hipi_ups_driver);
