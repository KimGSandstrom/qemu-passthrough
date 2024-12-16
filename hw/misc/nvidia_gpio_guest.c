#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/log.h"
#include "qapi/error.h" /* provides error_fatal() handler */
#include "hw/sysbus.h"	/* provides all sysbus registering func */
#include "hw/misc/nvidia_gpio_guest.h"
#include "monitor/qdev.h"
#include "hw/irq.h"
#include "qom/object.h"
#include "hw/qdev-core.h"
#include "linux/vfio.h"
#include "hw/vfio/vfio-common.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"

#define TYPE_NVIDIA_MEM "nvidia_mem_guest_class"
typedef DeviceClass NvidiaMemClass;
typedef struct NvidiaMemDevice NvidiaMemDevice;

#define NVIDIA_MEM_GET_CLASS(obj) \
   OBJECT_GET_CLASS(NvidiaMemClass, obj, TYPE_NVIDIA_MEM)
#define NVIDIA_MEM_CLASS(klass) \
   OBJECT_CLASS_CHECK(NvidiaMemClass, klass, TYPE_NVIDIA_MEM)
#define NVIDIA_MEM_DEVICE(obj) \
   OBJECT_CHECK(NvidiaMemDevice, obj, TYPE_NVIDIA_MEM)

#define TYPE_NVIDIA_IRQ "nvidia_irq_guest_class"
typedef DeviceClass NvidiaIrqClass;
typedef struct NvidiaIrqDevice NvidiaIrqDevice;

#define NVIDIA_IRQ_GET_CLASS(obj) \
   OBJECT_GET_CLASS(NvidiaIrqClass, obj, TYPE_NVIDIA_IRQ)
#define NVIDIA_IRQ_CLASS(klass) \
   OBJECT_CLASS_CHECK(NvidiaIrqClass, klass, TYPE_NVIDIA_IRQ)
#define NVIDIA_IRQ_DEVICE(obj) \
   OBJECT_CHECK(NvidiaIrqDevice, obj, TYPE_NVIDIA_IRQ)

/* help at: 
 * https://www.qemu.org/docs/master/devel/qom.html#creating-a-qom-class
 */

// reg = <0x00 0x2430000 0x00 0x19100 0x00 0xc300000 0x00 0x4000>;
#define PINMUX_NAME "gpiochip0"
#define PINMUX_REG_BASE 0x2430000  // [tegra234-gpio] (164 lines)
#define PINMUX_REG_SIZE 0x1D1000   // 0x19100 + 0x4000
 
 /*
  * gpiochip0 - 164 lines
  * gpiochip1 - 32 lines 
  */

// reg = <0x00 0x2200000 0x00 0x10000 0x00 0x2210000 0x00 0x10000>;
#define GPIOCHIP0_NAME "gpiochip0"
#define GPIOCHIP0_REG_BASE 0x2200000  // [tegra234-gpio] (164 lines)
#define GPIOCHIP0_REG_SIZE  0x20000    // 0x10000+0x10000
#define GPIOCHIP0_NUM_LINES 164
#define GPIOCHIP0_OFFSET 0x120
#define GPIOCHIP0_ACTIVE_LEVEL 1

// reg = <0x00 0xc2f0000 0x00 0x1000 0x00 0xc2f1000 0x00 0x1000>;
#define GPIOCHIP1_NAME "gpiochip1"
#define GPIOCHIP1_REG_BASE 0xc2F0000   // [tegra234-gpio-aon] (32 lines)
#define GPIOCHIP1_REG_SIZE 0x2000      // 0x1000+0x1000
#define GPIOCHIP1_NUM_LINES 32
#define GPIOCHIP1_OFFSET 0x38
#define GPIOCHIP1_ACTIVE_LEVEL 0
// #define GPIOCHIP1_OFFSET 0x6A

#define QEMU_DEBUG_MSG

/************************
 * Memory passthrough
 ************************/

struct NvidiaMemDevice
{
	SysBusDevice parent_obj;
    qemu_irq *irq;
    const char *device_name;
    void *host_memory;
    int regbase_address;
    int regbase_size;
    MemoryRegion mmio;
};

static int nvidia_gpio_memory_access(void *opaque, hwaddr addr, unsigned int size, bool is_write) {
    NvidiaMemDevice *dev = opaque;

    #ifdef QEMU_DEBUG_MSG
    qemu_printf("MEM: %s, dev=%p, addr=0x%lx, size=%d, is_write=%d\n", __func__, dev, addr, size, is_write);
    #endif

    // Forward the memory access to the host's memory
    if (is_write) {
        // Write to the host's memory
        *(uint64_t *)(dev->host_memory + addr) = size;
    } else {
        // Read from the host's memory
        uint64_t value = *(uint64_t *)(dev->host_memory + addr);
        #ifdef QEMU_DEBUG_MSG
        qemu_printf("Read value: 0x%lx\n", value);
        #endif
    }

    return 0;
}

static uint64_t nvidia_gpio_memory_read(void *opaque, hwaddr addr, unsigned int size) {
    NvidiaMemDevice *dev = opaque;
    #ifdef QEMU_DEBUG_MSG
    qemu_printf("MEM: %s, dev=%p, addr=0x%lx, size=%d\n", __func__, dev, addr, size);
    #endif

    // Read from the host's memory
    uint64_t value = *(uint64_t *)(dev->host_memory + addr);
    #ifdef QEMU_DEBUG_MSG
    qemu_printf("Read value: 0x%lx\n", value);
    #endif
    return value;
}

static void nvidia_gpio_memory_write(void *opaque, hwaddr addr, uint64_t data, unsigned int size) {
    NvidiaMemDevice *dev = opaque;
    #ifdef QEMU_DEBUG_MSG
    qemu_printf("MEM: %s, addr=0x%lx, size=%d\n", __func__, addr, size);
    #endif

    // Write to the host's memory
    *(uint64_t *)(dev->host_memory + addr) = data;
}

static const MemoryRegionOps nvidia_gpio_mem_ops = {
    .read = &nvidia_gpio_memory_read,
    .write = &nvidia_gpio_memory_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,

};

static void nvidia_gpio_mem_reset(DeviceState *dev) {
    #ifdef QEMU_DEBUG_MSG
	qemu_printf("MEM_RESET: %s, dev=%p\n", __func__, dev);
    #endif
    // Implementation of device reset
}

static void nvidia_gpio_mem_realize(DeviceState *sdev, Error **errp) {
    NvidiaMemDevice *dev = NVIDIA_MEM_DEVICE(sdev);
    #ifdef QEMU_DEBUG_MSG
    qemu_printf("MEM_REALIZE: %s, device=%s, dev=%p\n", __func__, dev->device_name, dev);
    #endif

    // Create a unique name for the shared memory object
    char shm_name[64];
    snprintf(shm_name, sizeof(shm_name), "/%s_shared_memory", dev->device_name);

    // Open the shared memory object
    int shm_fd = shm_open(shm_name, O_RDWR | O_CREAT, 0600);
    if (shm_fd == -1) {
        qemu_printf("%s **Error** Failed to create shared memory object\n", __func__);
        return;
    }

    // Set the size of the shared memory object
    if (ftruncate(shm_fd, dev->regbase_size) == -1) {
        qemu_printf("%s **Error** Failed to set shared memory size\n", __func__);
        close(shm_fd);
        return;
    }

    // Map the shared memory into the address space
    dev->host_memory = mmap(NULL, dev->regbase_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (dev->host_memory == MAP_FAILED) {
        qemu_printf("%s **Error** Failed to map shared memory\n", __func__);
        close(shm_fd);
        return;
    }
    // Close the shared memory file descriptor (mapping remains valid)
    close(shm_fd);

    // Communicate the shared memory details to the host driver (not shown here)

    // Initialize the MMIO memory region with custom handlers
    memory_region_init_io(&dev->mmio, OBJECT(dev), &nvidia_gpio_mem_ops, dev, "nvidia-gpio-mmio", dev->regbase_size);
    // sysbus_init_mmio(SYS_BUS_DEVICE(dev), &dev->mmio);
    MemoryRegion *sys_mem = get_system_memory();
    memory_region_add_subregion(sys_mem, dev->regbase_address, &dev->mmio);
}

static void nvidia_gpio_mem_unrealize(DeviceState *sdev) {
    #ifdef QEMU_DEBUG_MSG
    NvidiaMemDevice *dev = NVIDIA_MEM_DEVICE(sdev);
	qemu_printf("FREE: %s, dev=%p\n", __func__, dev);
    #endif
}

// Define the initialisation function to create the virtual IRQ device
static void nvidia_mem_guest_instance_init(Object *obj) {
    NvidiaMemDevice *dev = NVIDIA_MEM_DEVICE(obj);
    // struct vfio_device *vfio_dev = vfio_get_device(dev->device_name);
    #ifdef QEMU_DEBUG_MSG
	qemu_printf("MEM_GUEST_INIT: %s, dev=%p\n", __func__, dev);
    #endif

}

// Define a function to create the IRQ module class 
static void nvidia_mem_guest_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    #ifdef QEMU_DEBUG_MSG
	qemu_printf("MEM_CLASS_INIT: %s, klass=%p\n", __func__, klass);
    #endif
    dc->bus_type = TYPE_SYSTEM_BUS; // Set the bus type
    dc->reset = nvidia_gpio_mem_reset;
    dc->realize = nvidia_gpio_mem_realize;
    dc->unrealize = nvidia_gpio_mem_unrealize;
    dc->desc = "NVIDIA GPIO MEM Device";
}

/************************
 * IRQ passthrough
 ************************/

struct NvidiaIrqDevice
{
	SysBusDevice parent_obj;
    qemu_irq *irq;
    const char *device_name;
    int offset;
    int num_lines;
    int irq_active_level;
    MemoryRegion mmio;
};

// Define a function to handle interrupts
static void nvidia_irq_handler(void *opaque, int n, int level)
{
    // Handle the interrupt
    // 0x120 to 0x1C4 is gpiochip0 
    // 0x38  to 0x58  is gpiochip1
    NvidiaIrqDevice *dev = opaque;
    int index = n - dev->offset;
    #ifdef QEMU_DEBUG_MSG
	qemu_printf("IRQ: %s, dev=%p, index=%d\n", __func__, dev, index);
    #endif
    if(index < 0 || index >= dev->num_lines) {
        qemu_printf("**Error** Illegal IRQ index (%d)\n", n);
        return;
    }
    qemu_irq_pulse(dev->irq[index]);
}

static void nvidia_gpio_irq_reset(DeviceState *sdev) {
    NvidiaIrqDevice *dev = NVIDIA_IRQ_DEVICE(sdev);
    #ifdef QEMU_DEBUG_MSG
	qemu_printf("IRQ_RESET: %s, dev=%p\n", __func__, dev);
    #endif
    // Implementation of device reset
    for (int i = 0; i < dev->num_lines; i++) {
        if(! dev->irq[i]) {
            qemu_printf("%s **Error** Null pointer in dev->irq[%d]\n", __func__, i);
            return;
        }
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &dev->irq[i]);    // GPIO does not need System Bus ?
        // Set up the IRQ routing
        qemu_set_irq(dev->irq[i], dev->irq_active_level);      // Set the IRQ trigger level
    }
}

/* IRQ passthrough */
static void nvidia_gpio_irq_realize(DeviceState *sdev, Error **errp) {
    NvidiaIrqDevice *dev = NVIDIA_IRQ_DEVICE(sdev);
    #ifdef QEMU_DEBUG_MSG
    qemu_printf("IRQ_REALIZE: %s, device=%s, lines=%d, dev=%p\n", __func__, dev->device_name, dev->num_lines, dev);
    #endif

    dev->irq = malloc(sizeof(qemu_irq) * dev->num_lines);
    // check for errors
    if(! dev->irq) {
        qemu_printf("%s **Error** , Null pointer (malloc fail)\n", __func__);
        return;
    }

    for (int i = 0; i < dev->num_lines; i++) {
        // Allocate IRQ lines based on base and offset addresses
        // qemu_printf("IRQ: %s, chip=%s, irq=%d\n", __func__, dev->device_name, i + dev->offset);
        dev->irq[i] = qemu_allocate_irq(nvidia_irq_handler, dev, i + dev->offset);
        if(! dev->irq[i]) {
            qemu_printf("%s **Error** Null pointer in dev->irq[%d]\n", __func__, i);
            return;
        }
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &dev->irq[i]);    // GPIO does not need System Bus ?
        // Set up the IRQ routing
        qemu_set_irq(dev->irq[i], dev->irq_active_level);      // Set the IRQ trigger level
    }
    #ifdef QEMU_DEBUG_MSG
    qemu_printf("IRQ_REALIZE: %s, allocated %d irq lines for %s\n", __func__, dev->num_lines, dev->device_name);
    #endif
}

static void nvidia_gpio_irq_unrealize(DeviceState *sdev) {
    NvidiaIrqDevice *dev = NVIDIA_IRQ_DEVICE(sdev);
    #ifdef QEMU_DEBUG_MSG
	qemu_printf("FREE: %s, dev=%p\n", __func__, dev);
    #endif

    for (int i = 0; i < dev->num_lines; i++) {
        qemu_free_irq(dev->irq[i]);
    }
    free(dev->irq);
}

// Define the initialisation function to create the virtual IRQ device
static void nvidia_irq_guest_instance_init(Object *obj) {
    NvidiaIrqDevice *dev = NVIDIA_IRQ_DEVICE(obj);
    // struct vfio_device *vfio_dev = vfio_get_device(dev->device_name);
    #ifdef QEMU_DEBUG_MSG
	qemu_printf("INSTANCE_INIT: %s, dev=%p\n", __func__, dev);
    #endif
}

// Define a function to create the IRQ module class 
static void nvidia_irq_guest_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    #ifdef QEMU_DEBUG_MSG
	qemu_printf("CLASS_INIT: %s, klass=%p\n", __func__, klass);
    #endif
    dc->bus_type = TYPE_SYSTEM_BUS; // Set the bus type
    dc->reset = nvidia_gpio_irq_reset;
    dc->realize = nvidia_gpio_irq_realize;
    dc->unrealize = nvidia_gpio_irq_unrealize;
    dc->desc = "NVIDIA GPIO IRQ Device";
}

/* create and init */

// create a new type to define a irq device
static const TypeInfo nvidia_gpio_irq_class_info = {
	.name = TYPE_NVIDIA_IRQ,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(NvidiaIrqDevice),
    .instance_init = nvidia_irq_guest_instance_init,
	.class_init = nvidia_irq_guest_class_init,
};

// create a new type to define a mem device
static const TypeInfo nvidia_gpio_mem_class_info = {
	.name = TYPE_NVIDIA_MEM,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(NvidiaMemDevice),
    .instance_init = nvidia_mem_guest_instance_init,
	.class_init = nvidia_mem_guest_class_init,
};

// we do not need module_obj() if we build the module statically, type_init() should be enough
module_obj(TYPE_NVIDIA_MEM);
module_obj(TYPE_NVIDIA_IRQ);

static void init_chip_devices(Object *obj) {

    // Create device for gpiochip0 (164 lines)
    DeviceState *gpiochip0 = qdev_new(TYPE_NVIDIA_IRQ);
    NvidiaIrqDevice *chip0_dev = NVIDIA_IRQ_DEVICE(gpiochip0);
    chip0_dev->device_name = GPIOCHIP0_NAME;
    chip0_dev->num_lines = GPIOCHIP0_NUM_LINES;
    chip0_dev->irq_active_level = GPIOCHIP0_ACTIVE_LEVEL;
    chip0_dev->offset=GPIOCHIP0_OFFSET;

    // gpiochip0 registers
    DeviceState *gpioreg0 = qdev_new(TYPE_NVIDIA_MEM);
    NvidiaMemDevice *chip0_reg = NVIDIA_MEM_DEVICE(gpioreg0);
    chip0_reg->device_name = GPIOCHIP0_NAME"_reg";
    chip0_reg->regbase_address = GPIOCHIP0_REG_BASE;
    chip0_reg->regbase_size = GPIOCHIP0_REG_SIZE;

    // Create device for gpiochip1 (32 lines)
    DeviceState *gpiochip1 = qdev_new(TYPE_NVIDIA_IRQ);
    NvidiaIrqDevice *chip1_dev = NVIDIA_IRQ_DEVICE(gpiochip1);
    chip1_dev->device_name = GPIOCHIP1_NAME;
    chip1_dev->num_lines = GPIOCHIP1_NUM_LINES;
    chip1_dev->irq_active_level = GPIOCHIP1_ACTIVE_LEVEL;
    chip1_dev->offset=GPIOCHIP1_OFFSET;

    // gpiochip0 registers
    DeviceState *gpioreg1 = qdev_new(TYPE_NVIDIA_MEM);
    NvidiaMemDevice *chip1_reg = NVIDIA_MEM_DEVICE(gpioreg1);
    chip1_reg->device_name = GPIOCHIP1_NAME"_reg";
    chip1_reg->regbase_address = GPIOCHIP1_REG_BASE;
    chip1_reg->regbase_size = GPIOCHIP1_REG_SIZE;

    // pinmux chip registers
    DeviceState *pinmux = qdev_new(TYPE_NVIDIA_MEM);
    NvidiaMemDevice *pinmux_dev = NVIDIA_MEM_DEVICE(pinmux);
    pinmux_dev->device_name = PINMUX_NAME;
    pinmux_dev->regbase_address = PINMUX_REG_BASE;
    pinmux_dev->regbase_size = PINMUX_REG_SIZE;

    // Create the devices
    qdev_realize(gpiochip0, sysbus_get_default(), &error_fatal);
    qdev_realize(gpioreg0, sysbus_get_default(), &error_fatal);
    qdev_realize(gpiochip1, sysbus_get_default(), &error_fatal);
    qdev_realize(gpioreg1, sysbus_get_default(), &error_fatal);
    qdev_realize(pinmux, sysbus_get_default(), &error_fatal);
}

/*******************************************************
 * Section below implements pasthrough 
 * to a custom chardev on host, 
 * one for each gpiochip
 *******************************************************/

#define TYPE_NVIDIA_GPIO "nvidia_gpio_guest"
typedef struct NvidiaGpioState NvidiaGpioState;

#define NVIDIA_GPIO(obj) \
    OBJECT_CHECK(NvidiaGpioState, obj, TYPE_NVIDIA_GPIO)

#define MEM_SIZE 0x18	   // mem size in bytes is 3 64 bit words
#define RETURN_OFF 0x10	   // offset (in bytes) for return value is two 64 bit words
#define RETURN_SIZE 8
// #define RETURN_OFF 0
#define HOST_DEVICE_PATH "/dev/gpio-host"

// #define GPIO_PT_DEBUG
// #define GPIO_PT_DEBUG_VERBOSE

_Static_assert(sizeof(uint64_t) == RETURN_SIZE, "size assertion for RETURN_SIZE failed");
_Static_assert(sizeof(uint64_t)*3 == MEM_SIZE, "size assertion for MEM_SIZE failed");
_Static_assert(sizeof(uint64_t)*2 == RETURN_OFF, "size assertion for RETURN_OFF failed");


struct NvidiaGpioState
{
    // NvidiaIrqDevice parent; // embed the class struct
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	int host_device_fd;
	uint8_t mem[MEM_SIZE];
	unsigned char length;
	int towrite, written;
	uint64_t return_value;
};

pthread_mutex_t io_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t return_mutex = PTHREAD_MUTEX_INITIALIZER;

// Device memory: 0x090c1000 +  /* Base address */

static uint64_t nvidia_gpio_read(void *opaque, hwaddr addr, unsigned int size)
{
	struct NvidiaGpioState *s = opaque;
	uint64_t mask = ( size >= 8) ? (uint64_t)0xFFFFFFFFFFFFFFFF : ( (uint64_t)0x0000000000000001 << (size << 3) ) - 1;
	uint64_t retval = ( s->return_value >> (addr<<3) ) & mask;

	s->written -= size;

	if ( s->written < 0 ) {
		qemu_printf("%s: **Error** Size error in read (%d)\n", __func__, s->written);
		s->written = 0;	// allow next message
		retval = 0x01234567ABEDFACE;
	}

	if( size + addr > RETURN_OFF + RETURN_SIZE || size > RETURN_SIZE ) {
		qemu_printf("%s: **Error** address or size overflow, addr: 0x%lX, size: %d\n", __func__, addr, size);
		s->written = 0;	// allow next message
		retval = 0x01234567ABEDFACE;
	}

	if ( s->written == 0 ) {
		pthread_mutex_unlock(&return_mutex);	// allow next message
	}

	return retval;
}

static inline ssize_t safe_write(int fd, const void *buf, size_t count) {
	ssize_t ret;
	pthread_mutex_lock(&io_mutex);
	ret = write(fd, buf, count);
	pthread_mutex_unlock(&io_mutex);
	return ret;
}

/*
 *   void *opaque:	   This is a pointer to opaque data associated with the memory region.
 *					   It is used to pass additional context or information to the callback function.
 *					   In this case, it can be used to access device-specific data structures or state
 *					   information required for handling the write operation.
 *
 *   hwaddr addr:		This parameter represents the hardware address or the offset within the memory region
 *					   where the write operation is taking place. It specifies the location where the data should be written.
 *
 *   uint64_t data:	  This parameter holds the data that needs to be written to the memory region.
 *					   It represents the value that will be stored at the specified address (addr).
 *
 *   unsigned int size:  This parameter indicates the size of the data being written, in bytes. It specifies the number of bytes
 *					   to be written starting from the given address.
 */

static void nvidia_gpio_write(void *opaque, hwaddr addr, uint64_t data, unsigned int size)
{
	NvidiaGpioState *s = opaque;
	int ret = 0;
	uint64_t mask;

	if(addr == 0) {
		s->length = (*(unsigned char *)&data & 0xFE) >> 1;		   // s->length is 7 top MSB bits in first byte
		*(unsigned char *)&data = *(unsigned char *)&data & 0x01;	// remove lenght data from message
		memset(s->mem, 0, s->length);
		s->towrite = 0;
	}

	if (addr > s->length - size){
		qemu_printf("%s: **Error** addr (%ld) > s->length (%d)- size (%d)\n", __func__, addr, s->length, size);
		return;
	}

	// accumulate message
	memcpy(s->mem + addr, &data, size);
	s->towrite += size;

	// writeing last block
	if(addr == s->length - size) {

		if( s->length > 0x18 || s->mem[0]&0xFE || s->mem[1] >= 0x80 || s->mem[1] < 0x20) { // block obvious errors only
			s->return_value = 0xDEAFFACE;
			qemu_printf("%s: **Error** signal \'%c\' was blocked (chip=%d)\n", __func__, s->mem[1], s->mem[0]);
		}
		else {

			if( s->towrite != s->length ) {	  // size check before write
				qemu_printf("%s: **Error** Size error in write %d of %d\n", __func__, s->towrite, s->length);
				return;
			}

			pthread_mutex_lock(&return_mutex);
			if ( (ret = safe_write(s->host_device_fd, s->mem, s->towrite)) < 0 )
			{
				// error in write()
				qemu_printf("%s: **Error** 0x%02X, Failed to write the host device (%d)\n", __func__, errno, s->towrite);
				s->return_value = 0x1BADFACE;
				s->written = 0;
				pthread_mutex_unlock(&return_mutex);	// allow next message
			}
			else {
                #ifdef GPIO_PT_DEBUG
				qemu_printf("qemu: **Success** writing (%d) signal \'%c\' to the host device\n", ret, s->mem[1]);
                #endif
				// in addition to written length, 'ret' contains also the count of returned bytes appended to the end of the buffer
				// note: the size of the return value is (s->written - RETURN_OFF), should be 4 or 8 bytes
				s->written = ret;		   // update 'written' to actually written bytes ('ret'|| may be altered by host's return value)
				s->written -= RETURN_OFF;   // begin to handle return, subtract return offset to get expected return size
				if ( s->written > 0 && s->written <= RETURN_SIZE ) {
					// a return value is available
					// note: shift left does not work when we shift 64 bits (8 bytes) because the '1' is lost at 65 bits
					mask = ( s->written >= 8) ? (uint64_t)0xFFFFFFFFFFFFFFFF : ( (uint64_t)0x0000000000000001 << (s->written << 3) ) - 1;
					s->return_value = *(uint64_t *)(s->mem + RETURN_OFF) & mask;
				}
				else {
					// no return value
					s->written = 0;
					if ( ret != s->length ) {	   // size check after write at least 'length' must be written (return padding may occur)
						qemu_printf("%s: **Warning** %d bytes of %d, were written to host\n", __func__, s->written, s->length);
						s->return_value = 0x2BADFACE;
					}
					pthread_mutex_unlock(&return_mutex);	// allow next message
				}
			}

		} // close error check

        #ifdef GPIO_PT_DEBUG
		qemu_printf("qemu: return_value: 0x%016lX\n", s->return_value);
        #endif
	}
	return;
}

static const MemoryRegionOps nvidia_gpio_ops = {
	.read = nvidia_gpio_read,
	.write = nvidia_gpio_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static void init_chardev_devices(Object *obj) {
	struct NvidiaGpioState *s = NVIDIA_GPIO(obj);
	memset(s->mem, 0, MEM_SIZE);

	/* allocate memory map region */
	memory_region_init_io(&s->iomem, obj, &nvidia_gpio_ops, s, TYPE_NVIDIA_GPIO, MEM_SIZE);
	sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

	s->host_device_fd = open(HOST_DEVICE_PATH, O_RDWR); // Open the device with read/write access

	if (s->host_device_fd < 0)
	{
		qemu_printf("%s: **Error** Failed to open the host device\n", __func__);
        // Error *local_err = NULL;
        // error_setg_errno(&local_err, errno, "**Error** Failed to open the host device\n");
		// error_propagate(errp, local_err);
	}
}

/*
 * Create the Nvidia passthrough module
 */

static void nvidia_gpio_instance_init(Object *obj)
{
    init_chardev_devices(obj);
	init_chip_devices(obj);
}

/* create a new type to define info related to the chardev device */
static const TypeInfo nvidia_gpio_chardev_info = {
	.name = TYPE_NVIDIA_GPIO,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(NvidiaGpioState),
	.instance_init = nvidia_gpio_instance_init,
};

static void nvidia_gpio_register_types(void)
{
	type_register_static(&nvidia_gpio_chardev_info);     // top level object. code is not properly OO
	type_register_static(&nvidia_gpio_irq_class_info);
	type_register_static(&nvidia_gpio_mem_class_info);
}

type_init(nvidia_gpio_register_types)

/*
 * Create the Qemu Nvidia GPIO guest device. (declared as: nvidia_gpio_guest_create)
 */
DeviceState *nvidia_gpio_guest_create(hwaddr addr)
{
	DeviceState *dev = qdev_new(TYPE_NVIDIA_GPIO);
	// sysbus_create_and_unref(SYS_BUS_DEVICE(dev), &error_fatal); // depreceted in 9.0.2?
	sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
	return dev;
}
