#include "dispatcher.h"
#include "fabric.h"
#include "../../include/common.h"
#include <stddef.h>

static const fabric_property_t* fabric_device_property(const fabric_device_t* dev, const char* key)
{
    if (!dev || !key) {
        return NULL;
    }
    for (uint32_t i = 0; i < dev->property_count && i < FABRIC_PROPERTY_MAX; i++) {
        if (dev->properties[i].key && strcmp(dev->properties[i].key, key) == 0) {
            return &dev->properties[i];
        }
    }
    return NULL;
}

static int fabric_property_weight(const char* key)
{
    if (!key) {
        return 0;
    }
    if (strcmp(key, "vendor-id") == 0 || strcmp(key, "device-id") == 0) {
        return 250;
    }
    if (strcmp(key, "class-code") == 0 || strcmp(key, "subclass") == 0) {
        return 120;
    }
    if (strcmp(key, "prog-if") == 0 || strcmp(key, "bus") == 0) {
        return 80;
    }
    if (strcmp(key, "name") == 0) {
        return 40;
    }
    return 25;
}

static int fabric_property_match_score(fabric_driver_t* driver, fabric_device_t* dev)
{
    int score = 0;

    if (!driver || !dev || !driver->match_properties || driver->match_property_count == 0) {
        return FABRIC_MATCH_NONE;
    }

    for (uint32_t i = 0; i < driver->match_property_count; i++) {
        const fabric_property_t* want = &driver->match_properties[i];
        const fabric_property_t* have = fabric_device_property(dev, want->key);
        if (!have || have->type != want->type) {
            return FABRIC_MATCH_NONE;
        }
        if (want->type == FABRIC_PROP_U32) {
            if (have->value.u32 != want->value.u32) {
                return FABRIC_MATCH_NONE;
            }
        } else if (want->type == FABRIC_PROP_STR) {
            if (!have->value.str || !want->value.str ||
                strcmp(have->value.str, want->value.str) != 0) {
                return FABRIC_MATCH_NONE;
            }
        } else {
            return FABRIC_MATCH_NONE;
        }
        score += fabric_property_weight(want->key);
    }

    score += FABRIC_MATCH_WEAK + driver->match_priority;
    return score;
}

static int fabric_driver_score(fabric_driver_t* driver, fabric_device_t* dev)
{
    int property_score = FABRIC_MATCH_NONE;

    if (!driver || !dev) {
        return FABRIC_MATCH_NONE;
    }
    property_score = fabric_property_match_score(driver, dev);
    if (property_score != FABRIC_MATCH_NONE) {
        return property_score;
    }
    if (driver->match_score) {
        return driver->match_score(dev);
    }
    if (driver->probe && driver->probe(dev)) {
        return FABRIC_MATCH_GENERIC;
    }
    return FABRIC_MATCH_NONE;
}

uint32_t fabric_dispatcher_poll(fabric_device_t* const* devices,
                                uint32_t device_count,
                                fabric_driver_t* const* drivers,
                                uint32_t driver_count,
                                fabric_device_t** matched_devices,
                                fabric_driver_t** matched_drivers,
                                uint32_t max_matches)
{
    uint32_t matched = 0;

    if (!devices || !drivers || !matched_devices || !matched_drivers) {
        return 0;
    }

    for (uint32_t i = 0; i < device_count; i++) {
        fabric_device_t* dev = devices[i];
        if (!dev || dev->driver_state) {
            continue;
        }
        if (dev->dispatch_state == FABRIC_DEV_DISPATCH_ATTACHED) {
            continue;
        }

        dev->dispatch_state = FABRIC_DEV_DISPATCH_PENDING;
        fabric_driver_t* best_driver = NULL;
        int best_score = FABRIC_MATCH_NONE;

        for (uint32_t j = 0; j < driver_count; j++) {
            fabric_driver_t* driver = drivers[j];
            if (!driver) {
                continue;
            }

            int score = fabric_driver_score(driver, dev);
            if (score <= best_score) {
                continue;
            }

            fabric_log("dispatch: match %s -> %s score=%d\n",
                       driver->name ? driver->name : "(driver)",
                       dev->name ? dev->name : "(device)",
                       score);
            best_driver = driver;
            best_score = score;
        }

        if (best_driver) {
            fabric_log("dispatch: attach %s -> %s score=%d\n",
                       best_driver->name ? best_driver->name : "(driver)",
                       dev->name ? dev->name : "(device)",
                       best_score);

            dev->dispatch_state = FABRIC_DEV_DISPATCH_MATCHED;
            dev->attach_attempts++;

            if (!best_driver->attach || best_driver->attach(dev) != 0) {
                dev->dispatch_state = FABRIC_DEV_DISPATCH_FAILED;
                fabric_log("dispatch: attach failed %s -> %s\n",
                           best_driver->name ? best_driver->name : "(driver)",
                           dev->name ? dev->name : "(device)");
            } else {
                /* Leave state at MATCHED so fabric_finalize_driver_attach
                 * can transition to ATTACHED and invoke driver->publish(). */
                if (matched < max_matches) {
                    matched_devices[matched] = dev;
                    matched_drivers[matched] = best_driver;
                }
                matched++;
            }
        }

        if (!dev->driver_state && dev->dispatch_state != FABRIC_DEV_DISPATCH_ATTACHED) {
            dev->dispatch_state = FABRIC_DEV_DISPATCH_PENDING;
        }
    }

    return matched;
}

uint32_t fabric_dispatcher_poll_all(fabric_device_t* const* devices,
                                    uint32_t device_count,
                                    fabric_driver_t* const* drivers,
                                    uint32_t driver_count)
{
    fabric_device_t* matched_devices[256];
    fabric_driver_t* matched_drivers[256];

    return fabric_dispatcher_poll(devices,
                                  device_count,
                                  drivers,
                                  driver_count,
                                  matched_devices,
                                  matched_drivers,
                                  256);
}
