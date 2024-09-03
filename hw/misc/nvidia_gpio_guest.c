#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/log.h"
#include "qapi/error.h" /* provides error_fatal() handler */
#include "hw/sysbus.h"	/* provides all sysbus registering func */
#include "hw/misc/nvidia_gpio_guest.h"

#define TYPE_NVIDIA_GPIO_GUEST "nvidia_gpio_guest"
typedef struct NvidiaGpioGuestState NvidiaGpioGuestState;
DECLARE_INSTANCE_CHECKER(NvidiaGpioGuestState, NVIDIA_GPIO_GUEST, TYPE_NVIDIA_GPIO_GUEST)

#define MEM_SIZE 0x18	   // mem size in bytes is 3 64 bit words
#define RETURN_OFF 0x10	// offset (in bytes) for return value is two 64 bit words
#define RETURN_SIZE 8
// #define RETURN_OFF 0
#define HOST_DEVICE_PATH "/dev/gpio-host"

#define GPIO_PT_DEBUG
// #define GPIO_PT_DEBUG_VERBOSE

_Static_assert(sizeof(uint64_t) == RETURN_SIZE, "size assertion for RETURN_SIZE failed");
_Static_assert(sizeof(uint64_t)*3 == MEM_SIZE, "size assertion for MEM_SIZE failed");
_Static_assert(sizeof(uint64_t)*2 == RETURN_OFF, "size assertion for RETURN_OFF failed");


struct NvidiaGpioGuestState
{
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

static uint64_t nvidia_gpio_guest_read(void *opaque, hwaddr addr, unsigned int size)
{
	struct NvidiaGpioGuestState *s = opaque;
	uint64_t mask = ( size >= 8) ? (uint64_t)0xFFFFFFFFFFFFFFFF : ( (uint64_t)0x0000000000000001 << (size << 3) ) - 1;
	uint64_t retval = ( s->return_value >> (addr<<3) ) & mask;

	#ifdef GPIO_PT_DEBUG_VERBOSE
	qemu_printf("qemu: (   + read	 ) written: %d: addr: %ld, size: %d, return_value: 0x%016lX, retval: 0x%lX\n", s->written, addr, size, s->return_value, retval);
	#endif

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

	#ifdef GPIO_PT_DEBUG_VERBOSE
	qemu_printf("qemu: (	read	 ) retval(processed): 0x%lX\n", retval);
	#endif
	return retval;
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

static inline ssize_t safe_write(int fd, const void *buf, size_t count) {
	ssize_t ret;
	pthread_mutex_lock(&io_mutex);
	ret = write(fd, buf, count);
	pthread_mutex_unlock(&io_mutex);
	return ret;
}

static void nvidia_gpio_guest_write(void *opaque, hwaddr addr, uint64_t data, unsigned int size)
{
	NvidiaGpioGuestState *s = opaque;
	int ret = 0;
	uint64_t mask;
	#ifdef GPIO_PT_DEBUG_VERBOSE
    int i;
    #endif

	if(addr == 0) {
		s->length = (*(unsigned char *)&data & 0xFE) >> 1;		   // s->length is 7 top MSB bits in first byte
		*(unsigned char *)&data = *(unsigned char *)&data & 0x01;	// remove lenght data from message
		memset(s->mem, 0, s->length);
		s->towrite = 0;
	    #ifdef GPIO_PT_DEBUG_VERBOSE
		// print debug
		qemu_printf("qemu: ( +++ write +++ ) length (coded in msg): %d\n", s->length);
        #endif
	}

	#ifdef GPIO_PT_DEBUG_VERBOSE
	qemu_printf("qemu: (	 write	 ) addr: %ld, size: %d, data: 0x%016lX\n", addr, size, data);
	#endif

	if (addr > s->length - size){
		qemu_printf("%s: **Error** addr (%ld) > s->length (%d)- size (%d)\n", __func__, addr, s->length, size);
		return;
	}

	// accumulate message
	memcpy(s->mem + addr, &data, size);
	s->towrite += size;

	// writeing last block
	if(addr == s->length - size) {
		// print debug
	    #ifdef GPIO_PT_DEBUG_VERBOSE
		qemu_printf("qemu: (	 write	 ) signal \'%c\', hexdump:\n", s->mem[1]);
		for(i = 0; i < (s->towrite + 7)/8; i++)
			qemu_printf("\t\t\t\t(%d) 0x%016lX\n", i, *((uint64_t *)(s->mem+i)));
        #endif

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
	        #ifdef GPIO_PT_DEBUG_VERBOSE
			qemu_printf("qemu: (	 write	 ) +++locked+++ return mutex\n");
			qemu_printf("qemu: (	 write	 ) Ready to write, (%d)\n", s->towrite);
            #endif
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
	                #ifdef GPIO_PT_DEBUG_VERBOSE
					qemu_printf("qemu: (	 write	 ) Expected return size: %d\n", s->written);
                    #endif
					// note: shift left does not work when we shift 64 bits (8 bytes) because the '1' is lost at 65 bits
					mask = ( s->written >= 8) ? (uint64_t)0xFFFFFFFFFFFFFFFF : ( (uint64_t)0x0000000000000001 << (s->written << 3) ) - 1;
					s->return_value = *(uint64_t *)(s->mem + RETURN_OFF) & mask;
	                #ifdef GPIO_PT_DEBUG_VERBOSE
					qemu_printf("qemu: (	 write	 ) Return value 0x%016lX, is copied from raw 0x%016lX, with mask = 0x%016lX\n", s->return_value, *(uint64_t *)(s->mem + RETURN_OFF), mask);
                    #endif
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
		qemu_printf("qemu: (	 write --- ) return_value: 0x%016lX\n", s->return_value);
        #endif
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
	memset(s->mem, 0, MEM_SIZE);

	/* allocate memory map region */
	memory_region_init_io(&s->iomem, obj, &nvidia_gpio_guest_ops, s, TYPE_NVIDIA_GPIO_GUEST, MEM_SIZE);
	sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

	s->host_device_fd = open(HOST_DEVICE_PATH, O_RDWR); // Open the device with read/write access

	if (s->host_device_fd < 0)
	{
		qemu_printf("%s: **Error** Failed to open the host device..\n", __func__);
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
