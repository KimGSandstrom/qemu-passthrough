#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/log.h"
#include "qapi/error.h" /* provides error_fatal() handler */
#include "hw/sysbus.h"	/* provides all sysbus registering func */
#include "hw/misc/nvidia_gpio_guest.h"

#define TYPE_NVIDIA_GPIO_GUEST "nvidia_gpio_guest"
typedef struct NvidiaGpioGuestState NvidiaGpioGuestState;
DECLARE_INSTANCE_CHECKER(NvidiaGpioGuestState, NVIDIA_GPIO_GUEST, TYPE_NVIDIA_GPIO_GUEST)

#define MEM_SIZE 0x18       // mem size in bytes is 3 64 bit words
#define RETURN_OFF 0x10	// offset (in bytes) for return value is two 64 bit words
// #define RETURN_OFF 0
#define HOST_DEVICE_PATH "/dev/gpio-host"

struct NvidiaGpioGuestState
{
	SysBusDevice parent_obj;
	MemoryRegion iomem;
	int host_device_fd;
	uint8_t mem[MEM_SIZE];
    unsigned char length;
    int written;
    uint64_t return_value;
};

pthread_mutex_t io_mutex = PTHREAD_MUTEX_INITIALIZER;

// Device memory: 0x090c1000 +  /* Base address */

/*
static uint64_t nvidia_gpio_guest_read(void *opaque, hwaddr addr, unsigned int size)
{
	NvidiaGpioGuestState *s = opaque;

	if (addr >= MEM_SIZE)
		return 0xABEDFACE;

    qemu_printf("%s: return: 0x%016lX, addr: 0x%lX, size: %d\n", __func__, *(uint64_t*)&s->mem[addr], addr, size);
	// Cast buffer location as uint64_t
	return *(uint64_t*)&s->mem[addr];
}
*/

static inline ssize_t safe_read(int fd, void *buf, size_t count) {
    ssize_t ret;
    pthread_mutex_lock(&io_mutex); // Use io_mutex for read as well
    ret = read(fd, buf, count);
    pthread_mutex_unlock(&io_mutex);
    return ret;
}

static uint64_t nvidia_gpio_guest_read(void *opaque, hwaddr addr, unsigned int size)
{
	struct NvidiaGpioGuestState *s = opaque;
    uint64_t mask = ( (uint64_t)0x0000000000000001 << (size << 3) ) - 1;
    uint64_t retval = ( s->return_value >> (addr<<3) ) & mask;
    int i;

    qemu_printf("qemu: ( +++ read     ) addr: %ld, size: %d, 3rd_word: 0x%016lX, return_value: 0x%016lX\n", addr, size, *((uint64_t *)s->mem + RETURN_OFF), s->return_value);

    if( size + addr > MEM_SIZE ) {
        qemu_printf("%s: **Error** address overflow, addr: 0x%lX, size: %d\n", __func__, addr, size);
        return 0x01234567ABEDFACE;
    }

    // Get the data from the host module
    // if(read(s->host_device_fd, s->mem + addr, size) < 0) { 
//    if((ret = safe_read(s->host_device_fd, return_buffer + addr, size)) < 0) { 
//        qemu_printf("qemu: (in nvidia_gpio_guest_read) *error* read error %d, addr: 0x%lX, size %d\n", ret, addr, size);
//    }
//    else
//    {
//        qemu_printf("qemu: (in nvidia_gpio_guest_read) return_value: 0x%08lX, addr: 0x%lX, size %d\n", *return_value, addr, size);
        // qemu_printf("qemu: (in nvidia_gpio_guest_read) %ld return_value: 0x%08X\n", ret, *(uint32_t *)s->mem);
//    }
    // qemu_printf("qemu: (in nvidia_gpio_guest_read) read chardev, addr = %ld, size = %d, *(uint32_t *)s->mem = 0x%08X\n", addr, size, *(uint32_t *)s->mem);

    // qemu_printf("qemu: ( read ) dump return_buffer:\n");
    // for(i=0; i<MEM_SIZE/8; i++)
    //    qemu_printf("    (%d) 0x%016lX\n", i, *((uint64_t *)return_buffer+i)) ;

    if( addr == 0 ) {
        qemu_printf("qemu: (    read     ) addr = %ld, addr, dump s->mem:\n", addr);
        for(i=0; i<MEM_SIZE/sizeof(uint64_t); i++)
            qemu_printf("    (%d) 0x%016lX\n", i, *((uint64_t *)s->mem+i)) ;
    }

    // our total retval memspace is 64 bits -- third word in return_value
    // retval = *((uint64_t *)(s->mem + addr)) & mask;
    // retval = ( return_value >> (addr<<3) ) & mask;
    qemu_printf("qemu: (    read --- ) mask: 0x%lX, -------- retval(processed): 0x%016lX\n", mask, retval);
	return retval;
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
    int ret = 0, i;
    uint64_t mask;

    if(addr == 0) {
        // print debug
        qemu_printf("qemu: ( +++ write     ) addr: %ld, size: %d, data: 0x%016lX\n", addr, size, data);

        s->length = (*(unsigned char *)&data & 0xFE) >> 1;              // s->length is 7 top MSB bits in first byte
        *(unsigned char *)&data = *(unsigned char *)&data & 0x01;    // remove lenght data from message
	    memset(s->mem, 0, s->length);
        s->return_value = -1;
        s->written = 0;
    }

	if (addr > s->length - size){
		qemu_printf("%s: **Error** addr (%ld) > s->length (%d)- size (%d)\n", __func__, addr, s->length, size);
		return;
	}

    // accumulate message
    memcpy(s->mem + addr, &data, size);

    // writeing last block
    if(addr == s->length - size) {
        // print debug
        qemu_printf("qemu: (     write     ) signal: \'%c\', s->length = %d, hexdump:\n", s->mem[1], s->length);
        for(i = 0; i < (s->length + 7)/8; i++)
            qemu_printf("    (%d) 0x%016lX\n", i, *((uint64_t *)(s->mem+i))) ;

        // debug exception
        // if( s->length > 0x18 || s->mem[1] >= 0x80 || s->mem[1] < 0x20 || s->mem[1] == '<' || s->mem[1] == '>' || s->mem[1] == 'B') {
        if( s->length > 0x18 || s->mem[0]&0xFE || s->mem[1] >= 0x80 || s->mem[1] < 0x20 || s->mem[1] == '<' || s->mem[1] == '>' ) { // block writel and obvious errors only
          s->return_value = 0xF1F2F3F4F5F6F7F8;
          qemu_printf("operation \'%c\' was blocked for debug (chip=%d)\n", s->mem[1], s->mem[0]);
        }
        else {

        while( ret >=0 && s->written < s->length )
        if ( (ret = safe_write(s->host_device_fd, s->mem + s->written, s->length - s->written)) < 0 )
        {
            s->return_value = 0xFACEDEAF;
            qemu_printf("%s: **Error** 0x%02X, Failed to write the host device\n", __func__, errno);
        }
        else {
            qemu_printf("%s: **Success** writing the host device\n", __func__);
            s->written += ret;
            if ( s->written != s->length*8 ) {
                qemu_printf("%s: **Warning** Only %d bytes of %d, are written to host\n", __func__, s->written, s->length);
            }
            else {
                // we get the return values here one word of return should be enough (copying less saves no or little CPU)
                // note: we dont actually know the real size of the return value
                mask = ( (uint64_t)0x0000000000000001 << (s->length << 3) ) - 1;
                s->return_value = *(uint64_t *)(s->mem + RETURN_OFF) & mask;
            }
        }

        } // close debug
        
        qemu_printf("qemu: (     write --- ) return_value: 0x%016lX\n", s->return_value);
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
		qemu_printf("%s: Failed to open the host device..\n", __func__);
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
