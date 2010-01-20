#define _LARGEFILE64_SOURCE 1

#include <ctype.h>     /* isspace() */
#include <errno.h>     /* errno, EINPROGRESS */
#include <fcntl.h>     /* open(), its flags, etc. */
#include <inttypes.h>  /* sized integer types *and formatting* */
#include <stdio.h>     /* fprintf(), stdout */
#include <stdlib.h>    /* EXIT_SUCCESS, EXIT_FAILURE */
#include <string.h>    /* strerror() */
#include <sys/stat.h>  /* struct stat */
#include <sys/ioctl.h> /* ioctl(), etc. */
#include <unistd.h>    /* getopt(), etc. */

/* Kernel-defined types and constants. */

#define XFER_ISOC 0
#define XFER_INTR 1
#define XFER_CTRL 2
#define XFER_BULK 3

#define PKT_INPUT(PKT) ((PKT)->epnum & 0x80)

/** Descriptor for a single USB transfer. */
struct mon_packet {
        /** USB Request Block (URB) identifier. */
        uint64_t id;
        /** Event type.  'S' for submission, 'C' for callback, 'E' for
         * submission error.
         */
        unsigned char type;
        /** Transfer type.  0 for isochronous, 1 for interrupt, 2 for
         * control, 3 for bulk.
         */
        unsigned char xfer_type;
        /** Endpoint number.  Bitwise-or'ed with 0x80 for input,
         * otherwise output.
         */
        unsigned char epnum;
        /** Device number on the bus. */
        unsigned char devnum;
        /** Bus number. */
        uint16_t busnum;
        /** Setup packet indicator.  '\0' if a setup packet, '-' if
         * the setup packet could not be captured, otherwise not a
         * setup packet.
         */
        char flag_setup;
        /** Data validity indicator, if #mon_packet::length > 0.  '\0'
         * if data has been captured, otherwise a flag like '<' or
         * '>'.
         */
        char flag_data;
        /** Seconds portion of capture timestamp. */
        int64_t ts_sec;
        /** Microseconds portion of capture timestamp. */
        int32_t ts_usec;
        /** Transfer status (usually not valid for submission
         * events).
         */
        int status;
        /** Number of data bytes associated with the event. */
        unsigned int length;
        /** Number of data bytes actually captured. */
        unsigned int len_cap;
        union {
                /** Setup Data field descriptors, including
                 * bmRequestType (byte, 0x20), bRequest (byte, 0x00 or
                 * 0xE0), wValue (short, 0x00) and wIndex (short,
                 * 0x00), wLength (short, 0x00).
                 */
                unsigned char setup[8];
                /** Isochronous request information. */
                struct iso_rec {
                        int error_count;
                        int numdesc;
                } iso;
        } s;
        /* Note: Only GETX populates the following fields. */
        /** Bus interval number? */
        int interval;
        /** Start frame? */
        int start_frame;
        /** URB transfer flags (e.g. URB_SHORT_NOT_OK). */
        unsigned int xfer_flags;
        /** Number of mon_isodesc structures in the capture area
         * before the actual data.
         */
        unsigned int ndesc;
};

/** Description of an isochronous something or another. */
struct mon_isodesc {
        int iso_stat;
        unsigned int iso_off;
        unsigned int iso_len;
        int iso_pad;
};

/** Structure for #MON_IOCG_STATS ioctl. */
struct mon_bin_stats {
        uint32_t queued;
        uint32_t dropped;
};

/** Descriptor for #MON_IOCX_GET and #MON_IOCX_GETX ioctls. */
struct mon_get_arg {
        /** Pointer to USB monitor packet descriptor. */
        struct mon_packet *hdr;
        /** Buffer to receive data. */
        void *data;
        /** Number of bytes in receive buffer. */
        size_t alloc;
};

/** Descriptor for #MON_IOCX_MFETCH ioctl. */
struct mon_mfetch_arg {
        /** Receives offsets inside buffer of fetched descriptors. */
        uint32_t *offvec;
        /** Maximum number of events to fetch. */
        uint32_t nfetch;
        /** Number of events to flush before fetch. */
        uint32_t nflush;
};

/** Magic number to distinguish usbmon ioctls. */
#define MON_IOC_MAGIC      0x92
/** Return length of data in the next event (possibly zero). */
#define MON_IOCQ_URB_LEN   _IO  (MON_IOC_MAGIC, 1)
/** Query number of events queued in the buffer and dropped since last query. */
#define MON_IOCG_STATS     _IOR (MON_IOC_MAGIC, 3, struct mon_bin_stats)
/** Set buffer size in bytes (may be rounded down). */
#define MON_IOCT_RING_SIZE _IO  (MON_IOC_MAGIC, 4)
/** Get buffer size in bytes. */
#define MON_IOCQ_RING_SIZE _IO  (MON_IOC_MAGIC, 5)
/** Wait for an event, and return the first one. */
#define MON_IOCX_GET       _IOW (MON_IOC_MAGIC, 6, struct mon_get_arg)
#define MON_IOCX_GETX      _IOW (MON_IOC_MAGIC, 10, struct mon_get_arg)
/** Used to check where events are in the mmap'ed buffer. */
#define MON_IOCX_MFETCH    _IOWR(MON_IOC_MAGIC, 7, struct mon_mfetch_arg)
/** Remove events from the kernel's buffer. */
#define MON_IOCH_MFLUSH    _IO  (MON_IOC_MAGIC, 8)

/* Bluetooth-defined values. */

const char* lmp_features[] = {
        /* Byte 0: */
        "3 slot packets",
        "5 slot packets",
        "Encryption",
        "Slot offset",
        "Timing accuracy",
        "Role switch",
        "Hold mode",
        "Sniff mode",
        /* Byte 1: */
        "Park state",
        "Power control requests",
        "Channel quality driven data rate (CQDDR)",
        "SCO link",
        "HV2 packets",
        "HV3 packets",
        "Mu-law log synchronous data",
        "A-law log synchronous data",
        /* Byte 2: */
        "CVSD synchronous data",
        "Paging parameter negotiation",
        "Power control",
        "Transparent synchronous data",
        "Flow control lag (LSB)",
        "Flow control lag (middle bit)",
        "Flow control lag (MSB)",
        "Broadcast encryption",
        /* Byte 3: */
        "Reserved (bit 24)",
        "Enhanced Data Rate ACL 2 Mbps mode",
        "Enhanced Data Rate ACL 3 Mbps mode",
        "Enhanced inquiry scan",
        "Interlaced inquiry scan",
        "Interlaced page scan",
        "RSSI with inquiry results",
        "Extended SCO link (EV3 packets)",
        /* Byte 4: */
        "EV4 packets",
        "EV5 packets",
        "Reserved (bit 34)",
        "AFH capable slave",
        "AFH classification slave",
        "BR/EDR Not Supported",
        "LE Supported (Controller)",
        "3-slot Enhanced Data Rate ACL packets",
        /* Byte 5: */
        "5-slot Enhanced Data Rate ACL packets",
        "Sniff subrating",
        "Pause encryption",
        "AFH capable master",
        "AFH classification master",
        "Enhanced Dtaa Rate eSCO 2 Mbps mode",
        "Enhanced Dtaa Rate eSCO 3 Mbps mode",
        "3-slot Enhanced Data Rate eSCO packets",
        /* Byte 6: */
        "Extended Inquiry Response",
        "Simultaneous LE and BR/EDR to Same Device Capable (Controller)",
        "Reserved (bit 50)",
        "Secure Simple Pairing",
        "Encapsulated PDU",
        "Erroneous Data Reporting",
        "Non-flushable Packet Boundary Flag",
        "Reserved (bit 55)",
        /* Byte 7: */
        "Link SUpervision Timeout Changed Event",
        "Inquiry TX Power Level",
        "Enhanced Power Control",
        "Reserved (bit 59)",
        "Reserved (bit 60)",
        "Reserved (bit 61)",
        "Reserved (bit 62)",
        "Extended features",
        NULL
};

#define L2CAP_SIGNALING 0x0001
#define L2CAP_DYNAMIC   0x0040

#define L2CAP_CMD_REJECT       0x01
#define L2CAP_CMD_CONN_REQ     0x02
#define L2CAP_CMD_CONN_RESP    0x03
#define L2CAP_CMD_CFG_REQ      0x04
#define L2CAP_CMD_CFG_RESP     0x05
#define L2CAP_CMD_DISCONN_REQ  0x06
#define L2CAP_CMD_DISCONN_RESP 0x07
#define L2CAP_CMD_INFO_REQ     0x0A
#define L2CAP_CMD_INFO_RESP    0x0B

uint16_t l2cap_psm[65536];

/* Utility functions. */

uint16_t get_be16(const unsigned char data[])
{
        return (data[0] << 8) | data[1];
}

uint32_t get_be32(const unsigned char data[])
{
        return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

uint16_t get_le16(const unsigned char data[])
{
        return data[0]  | (data[1] << 8);
}

char *get_bt_addr(const unsigned char data[], unsigned int n)
{
        static char buf[8][18];
        if (n > 8) {
                n = 8;
        }
        snprintf(buf[n], sizeof(buf[n]), "%02x:%02x:%02x:%02x:%02x:%02x",
                data[0], data[1], data[2], data[3], data[4], data[5]);
        return buf[n];
}

uint32_t get_le24(const unsigned char data[])
{
        return data[0]  | (data[1] << 8) | (data[2] << 16);
}

uint32_t get_le32(const unsigned char data[])
{
        return data[0]  | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

void put_le16(unsigned char out[], uint16_t data)
{
        out[0] = (data >> 0) & 255;
        out[1] = (data >> 8) & 255;
}

/* Formatting and parsing functions. */

unsigned char hextab[256];

void init_hex(void)
{
        const char hexdigits[] = "0123456789abcdef";
        int ii;

        for (ii = 0; hexdigits[ii] != '\0'; ii++) {
                hextab[tolower(hexdigits[ii])] = ii;
                hextab[toupper(hexdigits[ii])] = ii;
        }
}

unsigned char fromhex(char ch)
{
        return hextab[(unsigned char)ch];
}

unsigned int min(unsigned int a, unsigned int b)
{
        return (a < b) ? a : b;
}

void print_usbmon(const struct mon_packet *pkt, const unsigned char data[])
{
        const const char hexdigits[] = "0123456789abcdef";
        static const char xfer_types[] = "ZICB";
        int xfer_type;
        char direction;

        xfer_type = pkt->xfer_type & 3;
        direction = PKT_INPUT(pkt) ? 'i': 'o';
        fprintf(stdout, "%016"PRIx64" %"PRId64".%06u %c %c%c:%u:%03u:%u",
                pkt->id, pkt->ts_sec, pkt->ts_usec, pkt->type,
                xfer_types[xfer_type], direction,
                pkt->busnum, pkt->devnum, pkt->epnum & 127);

        if (pkt->type == 'E') {
                fprintf(stdout, " %d", pkt->status);
        } else if (pkt->flag_setup == '\0') {
                fprintf(stdout, " s %02x %02x %04x %04x %04x",
                        pkt->s.setup[0], pkt->s.setup[1],
                        get_le16(pkt->s.setup + 2),
                        get_le16(pkt->s.setup + 4),
                        get_le16(pkt->s.setup + 6));
        } else if (pkt->flag_setup == '-') {
                /* if (pkt->type == 'S' && pkt->status == -EINPROGRESS)
                   fprintf(stdout, " -");
                   else */
                fprintf(stdout, " %d", pkt->status);

                if (xfer_type == XFER_ISOC || xfer_type == XFER_INTR) {
                        fprintf(stdout, ":%d", pkt->interval);
                }
                if (xfer_type == XFER_ISOC) {
                        fprintf(stdout, ":%d", pkt->start_frame);
                        if (pkt->type == 'C') {
                                fprintf(stdout, ":%d", pkt->s.iso.error_count);
                        }
                }
        } else {
                fprintf(stdout, " %c __ __ ____ ____ ____",
                        pkt->flag_setup);
        }

        fprintf(stdout, " %d", pkt->length);
        if (pkt->length == 0) {
                /* Do nothing. */
        } else if (pkt->flag_data != '\0') {
                fputc(' ', stdout);
                fputc(pkt->flag_data, stdout);
        } else {
                unsigned int data_len = pkt->len_cap;
                unsigned int ii;

                fprintf(stdout, " =");
                for (ii = 0; ii < data_len; ++ii) {
                        if (ii % 4 == 0) {
                                fputc(' ', stdout);
                        }
                        fputc(hexdigits[data[ii] >> 4], stdout);
                        fputc(hexdigits[data[ii] & 15], stdout);
                }

                if (pkt->len_cap < pkt->length) {
                        fprintf(stdout, " ...");
                }
        }
        fprintf(stdout, "\n");
}

void print_hci_command(const unsigned char data[], unsigned int len)
{
        unsigned int opcode = get_le16(data + 0);
        unsigned int param_len = min(data[2], len - 3);

        switch (opcode) {
        /* No-op commands */
        case 0x0000:
                fprintf(stdout, "  HCI_NoOp\n");
                break;
        /* Link Control commands (OGF = 0x01) */
        case 0x0401:
                fprintf(stdout, "  HCI_Inquiry(LAP=%06x, Inquiry_Length=%d, Num_Responses=%d)\n",
                        get_le24(data + 3), data[4], data[5]);
                break;
        case 0x0402:
                fprintf(stdout, "  HCI_Inquiry_Cancel()\n");
                break;
        case 0x0405:
                fprintf(stdout, "  HCI_Create_Connection(BD_ADDR=%s, Packet_Type=%#02x, Scan=%d, Clock_Offset=%d, Allow_Role_Switch=%d)\n",
                        get_bt_addr(data + 3, 0), get_le16(data + 9), data[11], get_le16(data + 13), data[15]);
                break;
        case 0x0406:
                fprintf(stdout, "  HCI_Disconnect(Connection_Handle=%d, Reason=%d)\n",
                        get_le16(data + 3), data[5]);
                break;
	case 0x0409:
		fprintf(stdout, "  HCI_Accept_Connection_Request(BD_ADDR=%s, Role=%d)\n",
			get_bt_addr(data + 3, 0), data[9]);
		break;
        case 0x040b:
                fprintf(stdout, "  HCI_Link_Key_Request_Reply(BD_ADDR=%s, Link_Key=%08x_%08x_%08x_%08x)\n",
                        get_bt_addr(data + 3, 0), get_le32(data + 9), get_le32(data + 13),
                        get_le32(data + 17), get_le32(data + 21));
                break;
        case 0x040c:
                fprintf(stdout, "  HCI_Link_Key_Request_Negative_Reply(BD_ADDR=%s)\n",
                        get_bt_addr(data + 3, 0));
                break;
        case 0x040d:
                fprintf(stdout, "  HCI_PIN_Code_Request_Reply(BD_ADDR=%s, PIN_Code_Length=%d, PIN_Code=%08x_%08x_%08x_%08x\n",
                        get_bt_addr(data + 3, 0), data[9], get_le32(data + 10), get_le32(data + 14),
                        get_le32(data + 18), get_le32(data + 22));
                break;
        case 0x0411:
                fprintf(stdout, "  HCI_Authentication_Requested(Connection_Handle=%d)\n",
                        get_le16(data + 3));
                break;
        case 0x0413:
                fprintf(stdout, "  HCI_PIN_Code_Request_Negative_Reply(BD_ADDR=%s)\n",
                        get_bt_addr(data + 3, 0));
                break;
        case 0x0419:
                fprintf(stdout, "  HCI_Remote_Name_Request(BD_ADDR=%s, Scan=%d, Clock_Offset=%d)\n",
                        get_bt_addr(data + 3, 0), data[9], get_le16(data + 11));
                break;
        case 0x041b:
                fprintf(stdout, "  HCI_Read_Remote_Supported_Features(Connection_Handle=%d)\n",
                        get_le16(data + 3));
                break;
        case 0x041d:
                fprintf(stdout, "  HCI_Read_Remote_Version_Information(Connection_Handle=%d)\n",
                        get_le16(data + 3));
                break;
        case 0x041f:
                fprintf(stdout, "  HCI_Read_Clock_Offset(Connection_Handle=%d)\n",
                        get_le16(data + 3));
                break;
        /* Link Policy commands (OGF = 0x02) */
        case 0x0807:
                fprintf(stdout, "  HCI_QoS_Setup(Connection_Handle=%d, Flags=%#02x, Service_Type=%d, Token_Rate=%d, Peak_Bandwidth=%d, Latency=%d, Delay_Variation=%d)\n",
                        get_le16(data + 3), data[5], data[6], get_le32(data + 7), get_le32(data + 11),
                        get_le32(data + 15), get_le32(data + 19));
                break;
        case 0x0809:
                fprintf(stdout, "  HCI_Role_Discovery(Connection_Handle=%d)\n",
                        get_le16(data + 3));
                break;
        case 0x080d:
                fprintf(stdout, "  HCI_Write_Link_Policy_Settings(Connection_Handle=%d, Link_Policy_Settings=%#04x)\n",
                        get_le16(data + 3), get_le16(data + 5));
                break;
	case 0x080e:
		fprintf(stdout, "  HCI_Read_Default_Link_Policy_Settings()\n");
		break;
	case 0x080f:
		fprintf(stdout, "  HCI_Write_Default_Link_Policy_Settings(Default_Link_Policy_Settings=%#04x)\n",
			get_le16(data + 3));
		break;
        /* Controller & Baseband Commands (OGF = 0x03) */
	case 0x0c01:
		fprintf(stdout, "  HCI_Set_Event_Mask(Event_Mask=%08x_%08x)\n",
			get_le32(data + 3), get_le32(data + 7));
		break;
	case 0x0c03:
		fprintf(stdout, "  HCI_Reset()\n");
		break;
	case 0x0c05:
		/** \todo Correctly parse and display the condition. */
		fprintf(stdout, "  HCI_Set_Event_Filter(Filter_Type=%d, Filter_Condition_Type=%d, Condition=...)\n",
			data[3], data[4]);
		break;
	case 0x0c0d:
		fprintf(stdout, "  HCI_Read_Stored_Link_Key(BD_ADDR=%s, Read_All_Flag=%d)\n",
			get_bt_addr(data+3, 0), data[9]);
		break;
	case 0x0c14:
		fprintf(stdout, "  HCI_Read_Local_Name()\n");
		break;
	case 0x0c16:
		fprintf(stdout, "  HCI_Write_Connection_Accept_Timeout(Conn_Accept_Timeout=%d)\n",
			get_le16(data + 3));
		break;
        case 0x0c18:
                fprintf(stdout, "  HCI_Write_Page_Timeout(Page_Timeout=%d)\n",
                        get_le16(data + 3));
                break;
	case 0x0c19:
		fprintf(stdout, "  HCI_Read_Scan_Enable()\n");
		break;
	case 0x0c1a: /* Did somebody say the C1A is here?! */
		fprintf(stdout, "  HCI_Write_Scan_Enable(Scan_Enable=%d)\n",
			data[3]);
		break;
	case 0x0c23:
		fprintf(stdout, "  HCI_Read_Class_of_Device()\n");
		break;
	case 0x0c24:
		fprintf(stdout, "  HCI_Write_Class_of_Device(Class_of_Device=%#06x)\n",
			get_le24(data + 3));
		break;
	case 0x0c25:
		fprintf(stdout, "  HCI_Read_Voice_Setting()\n");
		break;
        case 0x0c28:
                fprintf(stdout, "  HCI_Write_Automatic_Flush_Timeout(Connection_Handle=%d, Flush_Timeout=%d)\n",
                        get_le16(data + 3), get_le16(data + 5));
                break;
        case 0x0c2d:
                fprintf(stdout, "  HCI_Read_Transmit_Power_Level(Connection_Handle=%d, Type=%d)\n",
                        get_le16(data + 3), data[5]);
                break;
        case 0x0c36:
                fprintf(stdout, "  HCI_Read_Link_Supervision_Timeout(Handle=%d)\n",
                        get_le16(data + 3));
                break;
        case 0x0c37:
                fprintf(stdout, "  HCI_Write_Link_Supervision_Timeout(Handle=%d, Link_Supervision_Timeout=%d)\n",
                        get_le16(data + 3), get_le16(data + 5));
                break;
	/* Informational Parameters (OGF = 0x04) */
	case 0x1001:
		fprintf(stdout, "  HCI_Read_Local_Version_Information()\n");
		break;
	case 0x1003:
		fprintf(stdout, "  HCI_Read_Local_Supported_Features()\n");
		break;
	case 0x1005:
		fprintf(stdout, "  HCI_Read_Buffer_Size()\n");
		break;
	case 0x1009:
		fprintf(stdout, "  HCI_Read_BD_ADDR()\n");
		break;
        /* HCI Status Parameters commands (OGF = 0x05) */
        case 0x1403:
                fprintf(stdout, "  HCI_Read_Link_Quality(Handle=%d)\n",
                        get_le16(data + 3));
                break;
        case 0x1405:
                fprintf(stdout, "  HCI_Read_RSSI(Handle=%d)\n",
                        get_le16(data + 3));
                break;
        /* Unknown or unhandled commands */
        default:
                fprintf(stdout, "  Unhandled HCI command with opcode %#04x (OGF %d OCF %d)\n", opcode, opcode >> 10, opcode & 1023);
        }
        (void)param_len;
}

void print_hci_cmd_complete(unsigned int opcode, const unsigned char data[], unsigned int len)
{
        switch (opcode) {
        /* Link Control commands (OGF = 0x01) */
        case 0x040b:
                fprintf(stdout, "  HCI_Link_Key_Request_Reply: Status=%d, BD_ADDR=%s\n",
                        get_le16(data + 0), get_bt_addr(data + 2, 0));
                break;
        case 0x040c:
                fprintf(stdout, "  HCI_Link_Key_Request_Negative_Reply: Status=%d, BD_ADDR=%s\n",
                        get_le16(data + 0), get_bt_addr(data + 2, 0));
                break;
        case 0x040d:
                fprintf(stdout, "  HCI_PIN_Code_Request_Reply: Status=%d, BD_ADDR=%s\n",
                        get_le16(data + 0), get_bt_addr(data + 2, 0));
                break;
	/* Link Policy commands (OGF = 0x02) */
        case 0x0809:
                fprintf(stdout, "  HCI_Role_Discovery: Status=%d, Connection_Handle=%d, Current_Role=%d\n",
                        data[0], get_le16(data + 1), data[3]);
                break;
        case 0x080d:
                fprintf(stdout, "  HCI_Write_Link_Policy_Settings: Status=%d, Connection_Handle=%d\n",
                        data[0], get_le16(data + 1));
                break;
	case 0x080e:
		fprintf(stdout, "  HCI_Read_Default_Link_Policy_Settings: Status=%d, Default_Link_Policy_Settings=%#04x\n",
			data[0], get_le16(data + 1));
		break;
	case 0x080f:
		fprintf(stdout, "  HCI_Write_Default_Link_Policy_Settings: Status=%d\n",
			data[0]);
		break;
	/* Controller & Baseband Commands (OGF = 0x03) */
	case 0x0c01:
		fprintf(stdout, "  HCI_Set_Event_Mask: Status=%d\n",
			data[0]);
		break;
	case 0x0c03:
		fprintf(stdout, "  HCI_Reset: Status=%d\n",
			data[0]);
		break;
	case 0x0c05:
		fprintf(stdout, "  HCI_Set_Event_Filter: Status=%d\n",
			data[0]);
		break;
	case 0x0c0d:
		fprintf(stdout, "  HCI_Read_Stored_Link_Key: Status=%d, Max_Num_Keys=%d, Num_Keys_Read=%d\n",
			data[0], get_le16(data+1), get_le16(data+3));
		break;
	case 0x0c14:
		fprintf(stdout, "  HCI_Read_Local_Name: Status=%d, Local_Name=\"%s\"\n",
			data[0], data+1);
		break;
	case 0x0c16:
		fprintf(stdout, "  HCI_Write_Connection_Accept_Timeout: Status=%d\n",
			data[0]);
		break;
        case 0x0c18:
                fprintf(stdout, "  HCI_Write_Page_Timeout: Status=%d\n",
                        data[0]);
                break;
	case 0x0c19:
		fprintf(stdout, "  HCI_Read_Scan_Enable: Status=%d, Scan_Enable=%d\n",
			data[0], data[1]);
		break;
	case 0x0c1a:
		fprintf(stdout, "  HCI_Write_Scan_Enable: Status=%d\n",
			data[0]);
		break;
	case 0x0c23:
		fprintf(stdout, "  HCI_Read_Class_of_Device: Status=%d, Class_of_Device=%#06x\n",
			data[0], get_le24(data + 1));
		break;
	case 0x0c24:
		fprintf(stdout, "  HCI_Write_Class_of_Device: Status=%d\n",
			data[0]);
		break;
	case 0x0c25:
		fprintf(stdout, "  HCI_Read_Voice_Setting: Status=%d, Voice_Setting=%d\n",
			data[0], get_le16(data + 1));
		break;
        case 0x0c28:
                fprintf(stdout, "  HCI_Write_Automatic_Flush_Timeout: Status=%d, Connection_Handle=%d\n",
                        data[0], get_le16(data + 1));
                break;
        case 0x0c2d:
                fprintf(stdout, "  HCI_Read_Transmit_Power_Level: Status=%d, Connection_Handle=%d, Transmit_Power_Level=%d\n",
                        data[0], get_le16(data + 1), (signed char)data[3]);
                break;
        case 0x0c36:
                fprintf(stdout, "  HCI_Read_Link_Supervision_Timeout: Status=%d, Connection_Handle=%d, Link_Supervision_Timeout=%d\n",
                        data[0], get_le16(data + 1), get_le16(data + 3));
                break;
        case 0x0c37:
                fprintf(stdout, "  HCI_Write_Link_Supervision_Timeout: Status=%d, Handle=%d\n",
                        data[0], get_le16(data + 1));
                break;
	/* Informational Parameters (OGF = 0x04) */
	case 0x1001:
		fprintf(stdout, "  HCI_Read_Local_Version_Information: Status=%d, HCI_Version=%d, HCI_Revision=%#x, LMP/PAL_Version=%d, Manufacturer_Name=%#04x, LMP/PAL_Subversion: %#04x\n",
			data[0], data[1], get_le16(data+2), data[4],
			get_le16(data+5), get_le16(data+7));
		break;
	case 0x1003:
		fprintf(stdout, "  HCI_Read_Local_Supported_Features: Status=%d, LMP_Features=%08x_%08x\n",
			data[0], get_le32(data + 1), get_le32(data + 5));
		break;
	case 0x1005:
		fprintf(stdout, "  HCI_Read_Buffer_Size: Status=%d, HC_ACL_Data_Packet_Length=%d, HC_Synchronous_Data_Packet_Length=%d, HC_Total_Num_ACL_Data_Packets=%d, HC_Total_Num_Synchronous_Data_Packets=%d\n",
			data[0], get_le16(data+1), data[3], get_le16(data+4),
			get_le16(data+6));
		break;
	case 0x1009:
		fprintf(stdout, "  HCI_Read_BD_ADDR: Status=%d, BD_ADDR=%s\n",
			data[0], get_bt_addr(data+1, 0));
		break;
        /* HCI Status Parameters commands (OGF = 0x05) */
        case 0x1403:
                fprintf(stdout, "  HCI_Read_Link_Quality: Status=%d, Handle=%d, Link_Quality=%d\n",
                        data[0], get_le16(data + 1), data[3]);
                break;
        case 0x1405:
                fprintf(stdout, "  HCI_Read_RSSI: Status=%d, Handle=%d, RSSI=%d\n",
                        data[0], get_le16(data + 1), (signed char)data[3]);
                break;
        default:
                fprintf(stdout, "  HCI unhandled command completion (opcode=%#04x)\n", opcode);
        }
        (void)len;
}

void print_hci_event(const unsigned char data[], unsigned int len)
{
        unsigned int code = data[0];
        unsigned int param_len = min(data[1], len - 2);
        unsigned int count;
        unsigned int ii;

        fprintf(stdout, "  HCI event: ");
        switch (code) {
	case 0x00:
		fprintf(stdout, "Invalid/empty\n");
		break;
        case 0x01:
                fprintf(stdout, "Inquiry Complete\n");
                break;
        case 0x02:
                count = data[2];
                fprintf(stdout, "Inquiry Result: %d responses:\n", count);
                for (ii = 0; ii < count; ii++) {
                        fprintf(stdout, "    Addr %s, page scan rep mode %d, class %#x, clock ofs %d\n",
                                get_bt_addr(data + 6 * ii + 3, 0), *(data + 6 * count + 3),
                                get_le24(data + 9 * count + 3), get_le16(data + 12 * count + 3));
                }
                break;
        case 0x03:
                fprintf(stdout, "Connection Complete: Status=%d, Connection_Handle=%d, BD_ADDR=%s, Link_Type=%d, Encryption_Enabled=%d\n",
                        data[2], get_le16(data+3), get_bt_addr(data+5, 0), data[11], data[12]);
                break;
	case 0x04:
		fprintf(stdout, "Connection Request: BD_ADDR=%s, Class_of_Device=%#06x, Link_Type=%d\n",
			get_bt_addr(data+2, 0), get_le24(data+8), data[10]);
		break;
        case 0x05:
                fprintf(stdout, "Disconnection Complete: Status=%d, Connection_Handle=%d\n",
                        data[2], get_le16(data+ 3));
                break;
        case 0x06:
                fprintf(stdout, "Authentication Complete: Status=%d, Connection_Handle=%d\n",
                        data[2], get_le16(data+3));
                break;
        case 0x07:
                fprintf(stdout, "Remote Name Request Complete: Status=%d, BD_ADDR=%s, Remote_Name=\"%s\"\n",
                        data[2], get_bt_addr(data+3, 0), data+9);
                break;
        case 0x08:
                fprintf(stdout, "Encryption Change Event: Status=%d, Connection_Handle=%d, Encryption_Enabled=%d\n",
                        data[2], get_le16(data+3), data[5]);
                break;
        case 0x0b:
                fprintf(stdout, "Read Remote Supported Features Complete: Status=%d, Connection_Handle=%d, LMP_Features=%#08x_%#08x\n",
                        data[2], get_le16(data+3), get_le32(data+4), get_le32(data+8));
                for (ii = 0; lmp_features[ii] != NULL; ii++) {
                        if ((data[ii/8 + 4] >> (ii%8)) & 1) {
                                fprintf(stdout, "    %s\n", lmp_features[ii]);
                        }
                }
                if (ii != 64) {
                        fprintf(stdout, "   .. why were there %d lmp_features[]?\n", ii);
                }
                break;
        case 0x0c:
                fprintf(stdout, "Read Remote Version Information Complete: Status=%d, Connection_Handle=%d, Version=%d, Manufacturer_Name=%#04x, Subversion=%#04x\n",
                        data[2], get_le16(data+3), data[5], get_le16(data+6), get_le16(data+8));
                break;
        case 0x0d:
                fprintf(stdout, "QoS Setup Complete: Status=%d, Connection_Handle=%d, Flags=%#02x, Service_Type=%d, Token_Rate=%d, Peak_Bandwidth=%d, Latency=%d, Delay_Variation=%d\n",
                        data[2], get_le16(data+3), data[5], data[6], get_le32(data+7), get_le32(data+11),
                        get_le32(data+15), get_le32(data+19));
                break;
        case 0x0e:
                fprintf(stdout, "Command Complete Event: Num_HCI_Command_Packets=%d, Command_Opcode=%#04x, Return_Parameters=%d bytes\n",
                        data[2], get_le16(data+3), data[1]-3);
                print_hci_cmd_complete(get_le16(data+3), data+5, min(len-5,data[1]-3));
                break;
        case 0x0f:
                fprintf(stdout, "Command Status: Status=%d, Num_HCI_Command_Packets=%d, Command_Opcode=%#04x\n",
                        data[2], data[3], get_le16(data+4));
                break;
	case 0x12:
		fprintf(stdout, "Role Change: Status=%d, BD_ADDR=%s, New_Role=%d\n",
			data[2], get_bt_addr(data+3, 0), data[9]);
		break;
        case 0x13:
                count = data[2];
                fprintf(stdout, "Number of Completed Packets %d:\n", count);
                for (ii = 0; ii < count; ii++) {
                        fprintf(stdout, "    Connection_Handle=%d, HC_Num_Of_Completed_Packets=%d\n",
                                get_le16(data + 2 * ii + 3), get_le16(data + 2 * count + 2 * ii + 3));
                }
                break;
        case 0x14:
                fprintf(stdout, "Mode Change: Status=%d, Connection_Handle=%d, Current_Mode=%d, Interval=%d\n",
                        data[2], get_le16(data+3), data[5], get_le16(data+6));
                break;
        case 0x16:
                fprintf(stdout, "PIN Code Request: BD_ADDR=%s\n",
                        get_bt_addr(data+2, 0));
                break;
        case 0x17:
                fprintf(stdout, "Link Key Request: BD_ADDR=%s\n",
                        get_bt_addr(data+2, 0));
                break;
        case 0x18:
                fprintf(stdout, "Link Key Notification: BD_ADDR=%s, Link_Key=%08x_%08x_%08x_%08x, Key_Type=%d\n",
                        get_bt_addr(data+2, 0), get_le32(data+8), get_le32(data+12), get_le32(data+16),
                        get_le32(data+20), data[24]);
                break;
        case 0x1b:
                fprintf(stdout, "Max Slots Change: Connection_Handle=%d, LMP_Max_Slots=%d\n",
                        get_le16(data+2), data[4]);
                break;
        case 0x1c:
                fprintf(stdout, "Read Clock Offset Complete: Status=%d, Connection_Handle=%d, Clock_Offset=%d\n",
                        data[2], get_le16(data+3), get_le16(data+5));
                break;
        default:
                fprintf(stdout, "Unhandled event %#x (%d parameter bytes)\n", code, data[1]);
        }
        (void)param_len;
}

void print_l2cap_config_options(const unsigned char data[])
{
        fprintf(stdout, "    %s ", (data[-2] & 0x80) ? "Hint" : "Reqd");
        switch (data[-2] & 127) {
        case 0x01:
                fprintf(stdout, "MTU = %d\n", get_le16(data + 0));
                break;
        case 0x02:
                fprintf(stdout, "Flush_Timeout = %d\n", get_le16(data + 0));
                break;
        case 0x03:
                fprintf(stdout, "QoS: Flags=%d, Service_Type=%d, Token_Rate=%d, Token_Bucket_Size=%d, Peak_Bandwidth=%d, Latency=%d, Delay_Variation=%d\n",
                        data[0], data[1], get_le32(data + 2), get_le32(data + 6),
                        get_le32(data + 10), get_le32(data + 14), get_le32(data + 18));
                break;
        case 0x04:
                fprintf(stdout, "Rexmit: Mode=%d, TxWindowSize=%d, MaxTx=%d, RexmitTimeout=%d, MonitorTimeout=%d, Max_PDU=%d\n",
                        data[0], data[1], data[2], get_le16(data + 3), get_le16(data + 5), get_le16(data + 7));
                break;
        default:
                fprintf(stdout, "unknown option %d (%d bytes)\n", data[-2], data[-1]);
        }
}

int print_sdp_data(const unsigned char data[], unsigned int *ppos, unsigned int len)
{
        unsigned int pos = *ppos;
        unsigned int size;
        unsigned int ii;
        int more;
        int sub_more = 0;

        if (pos < len) {
                unsigned char tag = data[pos++];

                /* Parse the size portion of the field. */
                switch (tag & 7) {
                case 0: size = 1; break;
                case 1: size = 2; break;
                case 2: size = 4; break;
                case 3: size = 8; break;
                case 4: size = 16; break;
                case 5:
                        size = data[pos];
                        pos += 1;
                        break;
                case 6:
                        size = get_be16(data);
                        pos += 2;
                        break;
                case 7:
                        size = get_be32(data);
                        pos += 4;
                        break;
                }
                if (size > len - pos - 1) {
                        more = size - (len - pos - 1);
                        size = len - pos - 1;
                } else {
                        more = 0;
                }

                /* Display according to the type portion of the field. */
                switch (tag >> 3) {
                case 0:
                        if (size == 1) size = 0;
                        fprintf(stdout, "nil");
                        break;
                case 1:
                        fprintf(stdout, "uint%d(", size);
                        switch (size) {
                        case 1:
                                fprintf(stdout, "%u", data[pos]);
                                break;
                        case 2:
                                fprintf(stdout, "%u", get_be16(data+pos));
                                break;
                        case 4:
                                fprintf(stdout, "%#x", get_be32(data+pos));
                                break;
                        case 8:
                                fprintf(stdout, "%#x_%08x", get_be32(data+pos), get_be32(data+pos+4));
                                break;
                        case 16:
                                fprintf(stdout, "%#x_%08x_%08x_%08x", get_be32(data+pos), get_be32(data+pos+4), get_be32(data+pos+8), get_be32(data+pos+12));
                                break;
                        }
                        fputc(')', stdout);
                        break;
                case 2:
                        fprintf(stdout, "int%d(", size);
                        switch (size) {
                        case 1:
                                fprintf(stdout, "%d", data[pos]);
                                break;
                        case 2:
                                fprintf(stdout, "%d", get_be16(data+pos));
                                break;
                        case 4:
                                fprintf(stdout, "%#x", get_be32(data+pos));
                                break;
                        case 8:
                                fprintf(stdout, "%#x_%08x", get_be32(data+pos), get_be32(data+pos+4));
                                break;
                        case 16:
                                fprintf(stdout, "%#x_%08x_%08x_%08x", get_be32(data+pos), get_be32(data+pos+4), get_be32(data+pos+8), get_be32(data+pos+12));
                                break;
                        }
                        fputc(')', stdout);
                        break;
                case 3:
                        fprintf(stdout, "uuid%d(", size);
                        switch (size) {
                        case 2:
                                fprintf(stdout, "%#06x", get_be16(data+pos));
                                break;
                        case 4:
                                fprintf(stdout, "%#10x", get_be16(data+pos));
                                break;
                        case 16:
                                fprintf(stdout, "%08x-%04x-%04x-%04x-%04x%08x",
                                        get_be32(data+pos+0),
                                        get_be16(data+pos+4),
                                        get_be16(data+pos+6),
                                        get_be16(data+pos+8),
                                        get_be16(data+pos+10),
                                        get_be32(data+pos+12));
                                break;
                        }
                        fputc(')', stdout);
                        break;
                case 8:
                        fputs("URL:", stdout);
                        /* Fall through to the string case. */
                case 4:
                        fputc('"', stdout);
                        for (ii = 0; ii < size; ii++) {
                                if (isprint(data[pos+ii])) {
                                        fputc(data[pos+ii], stdout);
                                } else {
                                        fprintf(stdout, "\\x%02x", data[pos+ii]);
                                }
                        }
                        fputc('"', stdout);
                        break;
                case 5:
                        fputs("bool(", stdout);
                        if (pos < size) {
                                fputs(data[pos++] ? "true" : "false", stdout);
                        }
                        fputs(")", stdout);
                        break;
                case 6:
                case 7:
                        fputs(((tag >> 3) == 6) ? "seq { " : "alt { ", stdout);
                        *ppos = pos;
                        do {
                                if (*ppos > pos) fputs(", ", stdout);
                                ii = print_sdp_data(data, ppos, len);
                        } while (ii == 0 && *ppos < pos + size);
                        sub_more += ii;
                        fputs(" }", stdout);
                        break;
                default:
                        fprintf(stdout, "reserved (Type=%d, Size=%d)\n", data[pos-1] >> 3, size + more);
                }
                pos += size;
        } else {
                more = 1;
        }
        if (more && !sub_more) {
                fprintf(stdout, " ...");
        }
        *ppos = pos;
        return more + sub_more;
}

void print_sdp(const unsigned char data[], unsigned int len)
{
        /* SDP suddenly switches to being big-endian! */
        uint8_t pdu_id = data[0];
        uint16_t txn_id = get_be16(data + 1);
        uint16_t param_len = get_be16(data + 3);
        unsigned int pos;

        switch (pdu_id) {
        case 0x06:
                fprintf(stdout, "  SDP_ServiceSearchAttributeRequest(ServiceSearchPattern=");
                pos = 5;
                print_sdp_data(data, &pos, len);
                fprintf(stdout, ", MaximumAttributeByteCount=%d, AttributeIDList=",
                        get_be16(data+pos));
                pos += 2;
                print_sdp_data(data, &pos, len);
                fprintf(stdout, ", ContinuationState=%d bytes\n",
                        data[pos]);
                break;
        case 0x07:
                fprintf(stdout, "  SDP_ServiceSearchAttributeResponse(AttributeListsByteCount=%d, AttributeLists=",
                        get_be16(data+5));
                pos = 7;
                print_sdp_data(data, &pos, len);
                if (pos + 2 <= len) {
                        fprintf(stdout, ", ContinuationState=%d bytes\n",
                                get_be16(data + pos));
                } else {
                        fprintf(stdout, ", ContinuationState=? bytes\n");
                }
                break;
        default:
                fprintf(stdout, "  Unhandled SDP PDU (PDU_ID=%d, TxnId=%d, Length=%d)\n",
                        pdu_id, txn_id, param_len);
        }
}

const char *bt_hid_report_type(unsigned char type)
{
	switch (type & 3) {
	case 0: return "Reserved";
	case 1: return "Input";
	case 2: return "Output";
	case 3: return "Feature";
	}
        return "CompilerBug";
}

void print_bt_hid(const unsigned char data[], unsigned int len)
{
	int pos;

	switch (data[0] >> 4) {
	case 0:
		fprintf(stdout, "  BT-HID Handshake: Status=%d\n",
			data[0] & 15);
		break;
	case 1:
		fprintf(stdout, "  BT-HID Control: Operation=%d\n",
			data[0] & 15);
		break;
	case 4:
		fprintf(stdout, "  BT-HID Get_Report: Type=%s",
			bt_hid_report_type(data[0]));
		pos = 1;
		if (len == 2 || len == 4) {
			fprintf(stdout, ", ReportId=%d", data[pos++]);
		}
		if (data[0] & 8) {
			fprintf(stdout, ", BufferSize=%d", get_le16(data+pos));
			pos += 2;
		}
		fprintf(stdout, "\n");
		break;
	case 5:
		fprintf(stdout, "  BT-HID Set_Report: Type=%s, Length=%d\n",
			bt_hid_report_type(data[0]), len - 1);
		break;
	case 6:
		fprintf(stdout, "  BT-HID Get_Protocol: Protocol=%s\n",
			(data[1] & 1) ? "Report" : "Boot");
		break;
	case 7:
		fprintf(stdout, "  BT-HID Set_Protocol: Protocol=%s\n",
			(data[0] & 1) ? "Report" : "Boot");
		break;
	case 8:
		fprintf(stdout, "  BT-HID Get_Idle: Rate=%d\n",
			data[1]);
		break;
	case 9:
		fprintf(stdout, "  BT-HID Set_Idle: Rate=%d\n",
			data[1]);
		break;
	case 10:
	case 11:
		fprintf(stdout, "  BT-HID DAT%c: Report=%s\n",
			((data[0] >> 4 == 10) ? 'A' : 'C'),
			bt_hid_report_type(data[0]));
		break;
	default:
		fprintf(stdout, "  BT-HID Unhandled (reserved) request: Type=%d, Parameter=%d, Length=%d\n",
			data[0] >> 4, data[0] & 15, len - 1);
	}
}

void print_l2cap(const unsigned char data[], unsigned int len)
{
        static uint16_t pending_psm[256];
        uint16_t handle = get_le16(data + 0);
        uint16_t acl_len = get_le16(data + 2);
        uint16_t l2cap_len = get_le16(data + 4);
        uint16_t l2cap_cid = get_le16(data + 6);
        unsigned int limit = min(len - 8, min(acl_len - 4, l2cap_len));
        unsigned int ii;

        if (l2cap_cid == L2CAP_SIGNALING) {
                uint8_t cmd = data[8];
                uint8_t reqid = data[9];
                uint16_t data_len = get_le16(data + 10);

                limit = min(limit - 4, data_len);
                switch (cmd) {
                case L2CAP_CMD_REJECT:
                        fprintf(stdout,  "  L2CAP Command Reject (Id=%#02x, Reason=%#04x)\n",
                                reqid, get_le16(data + 12));
                        break;
                case L2CAP_CMD_CONN_REQ:
                        fprintf(stdout, "  L2CAP Connection Request (Id=%#02x, PSM=%#04x, Source_CID=%d)\n",
                                reqid, get_le16(data + 12), get_le16(data + 14));
                        pending_psm[reqid] = get_le16(data + 12);
                        break;
                case L2CAP_CMD_CONN_RESP:
                        fprintf(stdout, "  L2CAP Connection Response (Id=%#02x, Dest_CID=%d, Source_CID=%d, Result=%d, Status=%d)\n",
                                reqid, get_le16(data + 12), get_le16(data + 14), get_le16(data + 16), get_le16(data + 18));
			switch (get_le16(data + 16)) {
			case 1: /* Connection pending. */
				break;
			case 0: /* Connection succeeded. */
                                l2cap_psm[get_le16(data + 12)] = pending_psm[reqid];
				pending_psm[reqid] = 0;
				break;
			default: /* Connection failed for some reason. */
				pending_psm[reqid] = 0;
			}
                        break;
                case L2CAP_CMD_CFG_REQ:
                        fprintf(stdout, "  L2CAP Configuration Request (Id=%#02x, Dest_CID=%d, Flags=%#x):\n",
                                reqid, get_le16(data + 12), get_le16(data + 14));
                        for (ii = 16; (ii - 12) < limit; ii += 2 + data[ii+1]) {
                                print_l2cap_config_options(data + ii + 2);
                        }
                        break;
                case L2CAP_CMD_CFG_RESP:
                        fprintf(stdout, "  L2CAP Configuration Response (Id=%#02x, Source_CID=%d, Flags=%#x, Result=%d)%s\n",
                                reqid, get_le16(data + 12), get_le16(data + 14), get_le16(data + 16),
                                (data_len > 8 ? ":" : ""));
                        for (ii = 18; (ii - 10) < limit; ii += 2 + data[ii+1]) {
                                print_l2cap_config_options(data + ii + 2);
                        }
                        break;
                case L2CAP_CMD_DISCONN_REQ:
                        fprintf(stdout, "  L2CAP Disconnection Request (Id=%#02x, Dest_CID=%d, Source_CID=%d)\n",
                                reqid, get_le16(data + 12), get_le16(data + 14));
                        break;
                case L2CAP_CMD_DISCONN_RESP:
                        fprintf(stdout, "  L2CAP Disconnection Response (Id=%#02x, Dest_CID=%d, Source_CID=%d)\n",
                                reqid, get_le16(data + 12), get_le16(data + 14));
                        break;
                case L2CAP_CMD_INFO_REQ:
                        fprintf(stdout, "  L2CAP Information Request (Id=%#02x, Length=%d, InfoType=%d)\n",
                                reqid, get_le16(data + 12), get_le16(data + 14));
                        break;
                case L2CAP_CMD_INFO_RESP:
                        fprintf(stdout, "  L2CAP Information Response (Id=%#02x, InfoType=%d, Result=%d, Data=",
                                reqid, get_le16(data + 14), get_le16(data + 16));
                        switch (get_le16(data + 12)) {
                        case 0:
                                fprintf(stdout, "<empty>");
                                break;
                        case 1:
                                fprintf(stdout, "%#04x", data[18]);
                                break;
                        case 2:
                                fprintf(stdout, "%#06x", get_le16(data + 18));
                                break;
                        case 4:
                                fprintf(stdout, "%#10x", get_le32(data + 18));
                                break;
                        default:
                                fprintf(stdout, "%d bytes", get_le16(data + 12));
                                break;
                        }
                        break;
                default:
                        fprintf(stdout, "  Unhandled L2CAP signaling command (Command=%#02x, %d bytes data)\n",
                                cmd, data_len);
                }
        } else if (l2cap_cid >= L2CAP_DYNAMIC) {
                switch (l2cap_psm[l2cap_cid]) {
                case 0:
                        fprintf(stdout, "  User data on closed CID=%d?! (Length=%d)\n",
                                l2cap_cid, l2cap_len);
                        break;
                case 0x0001: /* Service Discovery Protocol */
                        print_sdp(data + 8, limit);
                        break;
		case 0x0011: /* Human Interface Device: Control */
		case 0x0013: /* Human Interface Device: Interrupt */
			print_bt_hid(data + 8, limit);
			break;
                default:
                        fprintf(stdout, "  User data on unhandled L2CAP PSM (CID=%d, PSM=%d, Length=%d)\n",
                                l2cap_cid, l2cap_psm[l2cap_cid], l2cap_len);
                }
        } else {
                fprintf(stdout, "  Unhandled L2CAP fragment (Handle=%#x, L2CAP_Length=%d, L2CAP_CID=%d)\n",
                        handle, l2cap_len, l2cap_cid);
        }
}

void print_bluetooth(const struct mon_packet *pkt, const unsigned char data[])
{
        if (pkt->type == 'S' && pkt->xfer_type == XFER_CTRL && pkt->epnum == 0
            && pkt->flag_setup == '\0' && pkt->flag_data == '\0'
            && pkt->s.setup[0] == 0x20 && (pkt->s.setup[1] == 0x00
                                           || pkt->s.setup[1] == 0xE0)
            && pkt->s.setup[2] == 0 && pkt->s.setup[3] == 0
            && pkt->s.setup[4] == 0 && pkt->s.setup[5] == 0) {
                print_hci_command(data, pkt->len_cap);
                fprintf(stdout, "\n");
        } else if (pkt->type == 'C' && pkt->xfer_type == XFER_INTR
                   && pkt->epnum >= 0x81 && pkt->flag_data == '\0'
                   && pkt->status == 0) {
                print_hci_event(data, pkt->len_cap);
                fprintf(stdout, "\n");
        } else if (pkt->xfer_type == XFER_BULK && (pkt->epnum & 127) > 0
                   && pkt->flag_data == '\0' && pkt->length > 0
                   && (pkt->type == (PKT_INPUT(pkt) ? 'C' : 'S'))) {
                print_l2cap(data, pkt->len_cap);
                fprintf(stdout, "\n");
        }
}

int parse_usbmon(const char input[], struct mon_packet *pkt, unsigned char data[], size_t data_len)
{
        const char *start;
        char *sep;
        uint64_t tmp;

        /* Parse URB id. */
        pkt->id = strtoull(start = input, &sep, 16);
        if (sep == start || !isdigit(sep[-1])) {
                return 1;
        }

        /* Parse timestamp. */
        tmp = strtoull(start = sep, &sep, 10);
        if (!isdigit(sep[-1])) {
                return 2;
        }
        if (sep[0] == '.') {
                /* seconds.microsec format */
                pkt->ts_sec = tmp;
                pkt->ts_usec = strtoull(start = sep + 1, &sep, 10);
                if (!isdigit(sep[-1]) || pkt->ts_usec < 0 || pkt->ts_usec > 999999) {
                        return 3;
                }
        } else {
                /* plain microseconds format */
                pkt->ts_sec  = tmp / 1000000;
                pkt->ts_usec = tmp % 1000000;
        }

        /* Event type. */
        while (isspace(sep[0])) {
                sep++;
        }
        pkt->type = sep[0];
        if (!isalpha(pkt->type)) {
                return 4;
        }
        sep = sep + 2;

        /* Transfer type. */
        while (isspace(sep[0])) {
                sep++;
        }
        switch (sep[0]) {
        case 'Z': pkt->xfer_type = XFER_ISOC; break;
        case 'I': pkt->xfer_type = XFER_INTR; break;
        case 'C': pkt->xfer_type = XFER_CTRL; break;
        case 'B': pkt->xfer_type = XFER_BULK; break;
        default:
                return 5;
        }

        /* Transfer direction. */
        if (sep[1] == 'i') {
                pkt->epnum = 0x80;
        } else if (sep[1] == 'o') {
                pkt->epnum = 0;
        } else {
                return 6;
        }

        /* Bus, device ID and actual endpoint. */
        if (sep[2] != ':') {
                return 7;
        }
        pkt->busnum = strtol(start = sep + 3, &sep, 10);
        if (!isdigit(sep[-1]) || sep[0] != ':') {
                return 8;
        }
        pkt->devnum = strtol(start = sep + 1, &sep, 10);
        if (!isdigit(sep[-1]) || sep[0] != ':') {
                return 9;
        }
        pkt->epnum |= strtol(start = sep + 1, &sep, 10) & 127;
        if (!isdigit(sep[-1]) || sep[0] != ' ') {
                return 10;
        }

        /* Transfer status, etc. */
        pkt->start_frame = 0;
        pkt->interval = 0;
        while (isspace(sep[0])) {
                sep++;
        }
        if (sep[0] == 's') {
                pkt->flag_setup = '\0';
                pkt->status = 0;
                pkt->s.setup[0] = strtol(start = sep + 1, &sep, 16);
                if (!isxdigit(sep[-1]) || sep[0] != ' ') {
                        return 11;
                }
                pkt->s.setup[1] = strtol(start = sep + 1, &sep, 16);
                if (!isxdigit(sep[-1]) || sep[0] != ' ') {
                        return 12;
                }
                put_le16(pkt->s.setup + 2, strtol(start = sep + 1, &sep, 16));
                if (!isxdigit(sep[-1]) || sep[0] != ' ') {
                        return 13;
                }
                put_le16(pkt->s.setup + 4, strtol(start = sep + 1, &sep, 16));
                if (!isxdigit(sep[-1]) || sep[0] != ' ') {
                        return 14;
                }
                put_le16(pkt->s.setup + 6, strtol(start = sep + 1, &sep, 16));
                if (!isxdigit(sep[-1]) || sep[0] != ' ') {
                        return 15;
                }
        } else if (sep[0] == '-' && !isdigit(sep[1]) && pkt->type == 'S') {
                pkt->flag_setup = '-';
                pkt->status = -EINPROGRESS;
                sep = sep + 2;
        } else if (isdigit(sep[0]) || (sep[0] == '-' && isdigit(sep[1]))) {
                pkt->flag_setup = '-';
                pkt->status = strtol(start = sep + 0, &sep, 10);
                if (pkt->xfer_type == XFER_ISOC || pkt->xfer_type == XFER_INTR) {
                        if (!isdigit(sep[-1]) || sep[0] != ':') {
                                return 16;
                        }
                        pkt->interval = strtol(start = sep + 1, &sep, 10);
                        if (pkt->xfer_type == XFER_ISOC) {
                                if (!isdigit(sep[-1]) || sep[0] != ':') {
                                        return 17;
                                }
                                pkt->start_frame = strtol(start = sep + 1, &sep, 10);
                                if (pkt->type == 'C') {
                                        if (!isdigit(sep[-1]) || sep[0] != ':') {
                                                return 18;
                                        }
                                        pkt->s.iso.error_count = strtol(start = sep + 1, &sep, 10);
                                }
                        }
                }
                if (!isdigit(sep[-1]) || sep[0] != ' ') {
                        return 19;
                }
        } else {
                pkt->flag_setup = sep[0];
                pkt->status = 0;
                sep = sep + 2;
        }

        /* Parse data length. */
        pkt->length = strtol(start = sep, &sep, 10);
        if (!isdigit(sep[-1]) || (sep[0] != '\0' && !isspace(sep[0]))) {
                return 20;
        }

        /* Parse the data itself. */
        while (isspace(sep[0])) {
                sep++;
        }
        if (sep[0] == '\0') {
                return 21;
        } else if (sep[0] == '=') {
                unsigned int ii;
                pkt->flag_data = '\0';
                sep = sep + 2;
                for (ii = 0; (ii < pkt->length) && (ii < data_len); ii++) {
                        while (isspace(sep[0])) {
                                sep++;
                        }
                        if (sep[0] == '\0') {
                                break;
                        }
                        if (isxdigit(sep[0]) && isxdigit(sep[1])) {
                                data[ii] = (fromhex(sep[0]) << 4) | fromhex(sep[1]);
                                sep += 2;
                        } else {
                                return 22;
                        }
                }
                pkt->len_cap = ii;
        } else {
                pkt->flag_data = *sep++;
                pkt->len_cap = 0;
        }

        if (sep[0] != '\0') {
                return 23;
        }

        return 0;
}

void read_regular_file(FILE *in)
{
        struct mon_packet pkt;
        size_t pos;
        int res;
        unsigned char data[4096];
        char input[2048];

        memset(&pkt, 0, sizeof(pkt));
        while (fgets(input, sizeof(input), in)) {
                /* Chomp trailing whitespace. */
                pos = strlen(input);
                while (isspace(input[pos - 1])) {
                        input[--pos] = '\0';
                }

                res = parse_usbmon(input, &pkt, data, sizeof(data));
                if (0 == res) {
                        print_usbmon(&pkt, data);
                        print_bluetooth(&pkt, data);
                } else {
                        fprintf(stdout, " .. parse failure %d\n", res);
                }
        }
}

int main(int argc, char *argv[])
{
        int ii;

        init_hex();

        for (ii = optind; ii < argc; ++ii) {
                struct stat st;
                const char *fname;
                int res;
                int fd;

                fname = argv[ii];
                if (!strcmp(fname, "-")) {
                        read_regular_file(stdin);
                        continue;
                }
                fd = open(fname, O_RDONLY | O_NONBLOCK | O_LARGEFILE);
                if (fd < 0) {
                        fprintf(stderr, "Unable to open %s: %s\n", fname, strerror(errno));
                        return EXIT_FAILURE;
                }
                res = fstat(fd, &st);
                if (res < 0) {
                        fprintf(stderr, "Unable to fstat() %s: %s\n", fname, strerror(errno));
                        return EXIT_FAILURE;
                }
                if (S_ISREG(st.st_mode)) {
                        FILE *str;
                        str = fdopen(fd, "r");
                        if (!str) {
                                fprintf(stderr, "Unable to fdopen() %s: %s\n", fname, strerror(errno));
                                return EXIT_FAILURE;
                        }
                        read_regular_file(str);
                        fclose(str);
                } else {
                        fprintf(stderr, "Handling non-regular files (%s) not yet implemented\n", fname);
                        close(fd);
                        /* continue on to normal handling */
                }
        }

        return EXIT_SUCCESS;
}
