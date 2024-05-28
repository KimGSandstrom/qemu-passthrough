#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h" /* provides error_fatal() handler */
#include "hw/sysbus.h"	/* provides all sysbus registering func */
#include "hw/misc/nvidia_gpio_guest.h"

#define TYPE_NVIDIA_GPIO_GUEST "nvidia_gpio_guest"
typedef struct NvidiaGpioGuestState NvidiaGpioGuestState;
DECLARE_INSTANCE_CHECKER(NvidiaGpioGuestState, NVIDIA_GPIO_GUEST, TYPE_NVIDIA_GPIO_GUEST)

#define MEM_SIZE 0x600
#define HOST_DEVICE_PATH "/dev/gpio-host"
#define MESSAGE_SIZE 0x0200

/*
// #define END_ADDR ( MEM_SIZE - 8 )			// last 64 bits to be written -- we assume alignment to 8 bytes
#define END_ADDR ((MEM_SIZE-1) - ((MEM_SIZE-1) % 8))	// this define applies also when MEM_SIZE is not aligned

_Static_assert( (MEM_SIZE % 8) == 0,  
               "MEM_SIZE not aligned to 64 bits\n");
_Static_assert( ((END_ADDR % 8) == 0) || END_ADDR < MEM_SIZE || END_ADDR < MEM_SIZE - 8 ,  
               "Other alignment failure\n");

// qemu_log_mask(LOG_UNIMP, "%s: \n", __func__ );
*/

struct NvidiaGpioGuestState
{
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	int host_device_fd;
	uint8_t mem[MEM_SIZE];
};

// Device memory map:

// 0x090c1000 +  /* Base address */
//       0x00	adddess to io register  
//       0x08	signal -- defines desired operaion
//       0x0c	value read or to write to
//       0x10 	memory bank
//       0x14	register offset
//       0x18	... first byte after used mem ( last word is 0x0010


//  Data should be aligned to 64bit

//  Protocol is:
//  1. Write data buffers to 0x0000-0x01FF and 0x0200-0x03FF
//  2. Start operation by writing mrq opcode to address 0x0500
//  3. Read ret code from 0x0410 and response data from the buffers

static uint64_t nvidia_gpio_guest_read(void *opaque, hwaddr addr, unsigned int size)
{
	struct NvidiaGpioGuestState *s = opaque;
	// int n = sizeof(data);
	//uint64_t mask = 0xff;

	if (addr >= MEM_SIZE)
		return 0xDEADBEEF;

	// mask read size
	// while ( n-- > 1) { mask |= (mask << 8); };

	// Cast buffer location as uint64_t
	// return *(uint64_t*)(&s->mem[addr]) & mask;
	return *(uint64_t*)(&s->mem[addr]);
}

static void nvidia_gpio_guest_write(void *opaque, hwaddr addr, uint64_t data, unsigned int size)
{
	struct NvidiaGpioGuestState *s = opaque;
	int ret; 

	// int n = size;
	//uint64_t mask = 0xff;

	// mask write size
	/* redundant
	while ( n-- > 1) { mask |= (mask << 8) };
	data &= mask;
	*/

	if (addr >= MEM_SIZE){
		qemu_log_mask(LOG_UNIMP, "qemu: Error addr >= MEM_SIZE in 0x%lX data: 0x%lX\n", addr, data);
		return;
	}

	// bulk of data is handled here
	memcpy(&s->mem[addr], &data, size);

    
	// Send the data to the host module
	ret = write(s->host_device_fd, s->mem, sizeof(MEM_SIZE)); 
	if (ret < 0) {
		qemu_log_mask(LOG_UNIMP, "%s: Failed to write the host device..\n", __func__);
		return;
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
