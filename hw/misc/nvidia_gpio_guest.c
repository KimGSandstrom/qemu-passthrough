#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/log.h"
#include "qapi/error.h" /* provides error_fatal() handler */
#include "hw/sysbus.h"	/* provides all sysbus registering func */
#include "hw/misc/nvidia_gpio_guest.h"

#define TYPE_NVIDIA_GPIO_GUEST "nvidia_gpio_guest"
typedef struct NvidiaGpioGuestState NvidiaGpioGuestState;
DECLARE_INSTANCE_CHECKER(NvidiaGpioGuestState, NVIDIA_GPIO_GUEST, TYPE_NVIDIA_GPIO_GUEST)

#define MEM_SIZE 0x600
#define HOST_DEVICE_PATH "/dev/gpio-host"

struct NvidiaGpioGuestState
{
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	int host_device_fd;
	uint8_t mem[MEM_SIZE];
};

// Device memory map:

// 0x090c1000 +  /* Base address */

static uint64_t nvidia_gpio_guest_read(void *opaque, hwaddr addr, unsigned int size)
{
	struct NvidiaGpioGuestState *s = opaque;
    ssize_t ret;
    uint64_t return_value = 0xDEADFACE;
    struct iovec retval;

	if (addr > 0)
		return 0xDEADBEEF;

    retval.iov_base = &return_value;
    retval.iov_len = size; 
    ret = readv(s->host_device_fd, &retval, 1);
    if(ret != size)
        qemu_printf("qemu: size error, ret: %ld, size %d", ret, size);
    else
        qemu_printf("qemu: (in nvidia_gpio_guest_read) %ld return_value: 0x%08lX\n", ret, return_value);

	// Cast buffer location as uint64_t
	// return *(uint64_t*)(&s->mem[addr]) & mask;
	// return *(uint64_t*)(&s->mem[addr]);
    return return_value;
}

/*
 *   void *opaque:       This is a pointer to opaque data associated with the memory region. 
 *                       It is used to pass additional context or information to the callback function. 
 *                       In this case, it can be used to access device-specific data structures or state 
 *                       information required for handling the write operation.
 *
 *   hwaddr addr:        This parameter represents the hardware address or the offset within the memory region 
 *                       where the write operation is taking place. It specifies the location where the data should be written.
 * 
 *   uint64_t data:      This parameter holds the data that needs to be written to the memory region. 
 *                       It represents the value that will be stored at the specified address (addr).
 *
 *   unsigned int size:  This parameter indicates the size of the data being written, in bytes. It specifies the number of bytes 
 *                       to be written starting from the given address.
 */
static void nvidia_gpio_guest_write(void *opaque, hwaddr addr, uint64_t data, unsigned int size)
{
	NvidiaGpioGuestState *s = opaque;
	int ret = 0;
    // TODO move to opaque memory to avoid memory collision
    static unsigned char *prt_msg;
    static unsigned char length;
    unsigned char *mask;
    int return_value;
    // struct iovec retval;

    // uint8_t test16[16] = { 0xFA, 0xCE, 0xBE, 0xEF, 0xBE, 0xD0, 0xFA, 0xCE, 0xDE, 0xAD, 0xFA, 0xCE, 0xBE, 0xEF, 0xDD, 0x20 };  // 16 bytes
    // uint8_t test8[8] = { 0xDE, 0xAD, 0xFA, 0xCE, 0xBE, 0xEF, 0xDD, 0x10 };  // 8 bytes
    // uint8_t test4[4] = { 0xBE, 0xEF, 0xDD, 0x08 };  // 4 bytes


    if(addr == 0) {
        // s->mem = malloc(length);
	    memset(s->mem, 0, length);
        mask = (unsigned char *)&data;
        length = (*mask & 0xFE) >> 1; // length is 7 top MSB bits in first byte
        qemu_printf("qemu: --- case 0x00, length: 0x%X\n", length);
    }

    qemu_printf("qemu: addr %ld, data: 0x%016lX, size: %d\n", addr, data, size);

	if (addr > length - size){
        qemu_printf("qemu: Error addr (%ld) > length (%d)- size (%d)\n", addr, length, size);
		qemu_log_mask(LOG_UNIMP, "qemu: Error addr (%ld) > length (%d)- size (%d)\n", addr, length, size);
		return;
	}

    memcpy(s->mem + addr, &data, size);
    qemu_printf("qemu: memcpy, 0x%08X, size: %d\n", *(uint16_t *)(s->mem + addr), size);

    if(addr == length - size) {
        *(char *)s->mem = *(char *)s->mem & 0x01;   // remove lenght data from message
        qemu_printf("qemu: length = %d, s->mem = 0x", length);
        for(prt_msg = s->mem + length - 1; prt_msg >= s->mem; prt_msg--) qemu_printf("%02X", *prt_msg);
        qemu_printf("\n");
        if(length == 4)
        ret = write(s->host_device_fd, s->mem, length);  // Send the data to the host module
        // ret = write(s->host_device_fd, test4, 4);  // Send test data
        if (ret < 0)
        {
            qemu_log_mask(LOG_UNIMP, "%s: Failed to write the host device..\n", __func__);
            return;
        }

        memcpy(&return_value, s->mem, sizeof(return_value));
        qemu_printf("qemu: (in nvidia_gpio_guest_write) return_value: 0x%08X\n", return_value);
        // free(s->mem);
    }
	return;
}

static const MemoryRegionOps nvidia_gpio_guest_ops = {
	.read = nvidia_gpio_guest_read,
	.write = nvidia_gpio_guest_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static void nvidia_gpio_guest_instance_init(Object *obj)
{
	struct NvidiaGpioGuestState *s = NVIDIA_GPIO_GUEST(obj);

	/* allocate memory map region */
	memory_region_init_io(&s->iomem, obj, &nvidia_gpio_guest_ops, s, TYPE_NVIDIA_GPIO_GUEST, MEM_SIZE);
	sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

	s->host_device_fd = open(HOST_DEVICE_PATH, O_RDWR); // Open the device with read/write access

	if (s->host_device_fd < 0)
	{
		qemu_log_mask(LOG_UNIMP, "%s: Failed to open the host device..\n", __func__);
		return;
	}
}

/* create a new type to define the info related to our device */
static const TypeInfo nvidia_gpio_guest_info = {
	.name = TYPE_NVIDIA_GPIO_GUEST,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(NvidiaGpioGuestState),
	.instance_init = nvidia_gpio_guest_instance_init,
};

static void nvidia_gpio_guest_register_types(void)
{
	type_register_static(&nvidia_gpio_guest_info);
}

type_init(nvidia_gpio_guest_register_types)

	/*
	 * Create the Nvidia GPIO guest device.
	 */
	DeviceState *nvidia_gpio_guest_create(hwaddr addr)
{
	DeviceState *dev = qdev_new(TYPE_NVIDIA_GPIO_GUEST);
	sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
	sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
	return dev;
}
