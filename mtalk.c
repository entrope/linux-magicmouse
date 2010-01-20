#include <byteswap.h> /* bswap_16() */
#include <errno.h>  /* errno */
#include <locale.h> /* setlocale() */
#include <math.h>   /* ldexpf() */
#include <poll.h>   /* poll(), etc */
#include <stdio.h>  /* sscanf(), fprintf() */
#include <stdlib.h> /* exit() */
#include <string.h> /* memcpy(), strerror() */
#include <sys/socket.h> /* socket(), etc */
#include <unistd.h> /* close(), getopt(), etc */

#if !defined(AF_BLUETOOTH)
# define AF_BLUETOOTH 31
#endif

#if !defined(BTPROTO_L2CAP)
# define BTPROTO_L2CAP 0
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define htobs(d)  (d)
#elif __BYTE_ORDER == __BIG_ENDIAN
# define htobs(d)  bswap_16(d)
#else
# error "Unknown byte order"
#endif

typedef struct {
	unsigned char b[6];
} __attribute__((packed)) bdaddr_t;

struct sockaddr_l2 {
        sa_family_t     l2_family;
        unsigned short  l2_psm;
        bdaddr_t        l2_bdaddr;
        unsigned short  l2_cid;
};

bdaddr_t local;
bdaddr_t remote;

int ctrl_psm = 0x11;
int intr_psm = 0x13;
int ctrl;
int intr;

int scan_bdaddr(bdaddr_t *addr, const char text[])
{
        int b[6];
        int res;
        res = sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x", &b[5], &b[4], &b[3], &b[2], &b[1], &b[0]);
        if (res < 6) return 1;
        addr->b[0] = b[0];
        addr->b[1] = b[1];
        addr->b[2] = b[2];
        addr->b[3] = b[3];
        addr->b[4] = b[4];
        addr->b[5] = b[5];
        return 0;
}

void parse_args(int argc, char *argv[])
{
        int opt;

        while ((opt = getopt(argc, argv, "c:i:l:r:")) != -1) {
                switch (opt) {
                        char *sep;
                case 'c':
                        ctrl_psm = strtol(optarg, &sep, 0);
                        if (ctrl_psm < 0 || ctrl_psm > 65535 || !(ctrl_psm & 1)) goto usage;
                        break;
                case 'i':
                        intr_psm = strtol(optarg, &sep, 0);
                        if (intr_psm < 0 || intr_psm > 65535 || !(intr_psm & 1)) goto usage;
                        break;
                case 'l':
                        if (scan_bdaddr(&local, optarg)) goto usage;
                        break;
                case 'r':
                        if (scan_bdaddr(&remote, optarg)) goto usage;
                        break;
                case '?':
                        usage:
                        fprintf(stdout, "Usage:\n%s [-c ctrl_psm] [-i intr_psm] [-l local_addr] [-r remote_addr]\n",
                                argv[0]);
                        exit(EXIT_FAILURE);
                }
        }
}

int connect_socket(const char name[], int psm)
{
        struct sockaddr_l2 la;
        int res;
        int fd;

        fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
        if (fd < 0) {
                fprintf(stderr, "Unable to create %s socket: %s\n", name, strerror(errno));
                return -errno;
        }
        memset(&la, 0, sizeof(la));
        la.l2_family = AF_BLUETOOTH;
        memcpy(&la.l2_bdaddr, &local, sizeof(bdaddr_t));
        res = bind(fd, (struct sockaddr*)&la, sizeof(la));
        if (res < 0) {
                fprintf(stderr, "Unable to bind %s socket: %s\n", name, strerror(errno));
                return -errno;
        }
        memset(&la, 0, sizeof(la));
        la.l2_family = AF_BLUETOOTH;
        la.l2_psm = htobs(psm);
        memcpy(&la.l2_bdaddr, &remote, sizeof(bdaddr_t));
        res = connect(fd, (struct sockaddr*)&la, sizeof(la));
        if (res < 0) {
                fprintf(stderr, "Unable to connect %s socket: %s\n", name, strerror(errno));
                return -errno;
        }
        return fd;
}

void connect_sockets(void)
{
        ctrl = connect_socket("control", ctrl_psm);
        if (ctrl < 0) {
                exit(EXIT_FAILURE);
        }

        intr = connect_socket("interrupt", intr_psm);
        if (intr < 0) {
                exit(EXIT_FAILURE);
        }
}

void write_mystery(void)
{
        unsigned char mystery_1[] = { 0x53, 0xd7, 0x01 };
        unsigned char mystery_2[] = { 0x53, 0xf8, 0x01, 0x32 };
        ssize_t res;

        res = send(ctrl, mystery_1, sizeof(mystery_1), 0);
        if (res < 0) {
                fprintf(stderr, "Cannot send first mystery on command port: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
        }
        res = send(ctrl, mystery_2, sizeof(mystery_2), 0);
        if (res < 0) {
                fprintf(stderr, "Cannot send second mystery on command port: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
        }
}

void read_socket(int fd, const char name[])
{
        static const char hexdigits[] = "0123456789abcdef";
        unsigned char data[256];
        int res;
        int ii;

        res = recv(fd, data, sizeof(data), MSG_DONTWAIT);
        if (res < 0) {
                if (errno == EAGAIN) {
                        usleep(1000);
                } else {
                        fprintf(stderr, "Read error on HID %s: %s\n", name, strerror(errno));
                }
        } else if (data[0] == 0xa1 && (data[1] & 0xf0) == 0x60 && res == 3) {
                if (data[1] == 0x61 && data[2] == 0x01) {
                        fprintf(stdout, "light: lost, please put the mouse back down!\n");
                } else if (data[1] == 0x61 && data[2] == 0x00) {
                        fprintf(stdout, "light: laser re-established\n");
                } else {
                        /* Unknown report. */
                        fprintf(stdout, "  ???: a1%02x%02x\n", data[1], data[2]);
                }
        } else if (data[0] == 0xa1 && data[1] == 0x10 && res == 7) {
                /* Mouse motion, maybe click.  This actually seems to
                 * follow the HID, so it should be parsed using report
                 * introspection under any serious driver.
                 */
                fprintf(stdout, " move: rsvd?=%02x, x=%+3d, y=%+3d\n", data[2],
                        (short)(data[3] | (data[4] << 8)),
                        (short)(data[5] | (data[6] << 8)));
        } else if (data[0] == 0xa1 && data[1] == 0x29 && ((res - 7) % 8 == 0)) {
                int ntouches = (res - 5) / 8;
                static const char btns[] = " LRB";
                fprintf(stdout, "touch: x=%+3d y=%+3d (T=%10d%c)",
                        (char)data[2], (char)data[3],
                        data[4] | (data[5] << 8) | (data[6] << 16),
                        btns[data[4] & 3]);
                for (ii = 0; ii < ntouches; ii++) {
                        /* On my mouse, X ranges from about -1100
                         * (left) to +1358 (right).  Y ranges from
                         * -2047 (Apple logo) to +1600 (front of
                         * mouse).  Angle 0 is from the left, angle
                         * 128 is from the logo to the nose, angle 255
                         * is from the right.
                         *
                         * The major and minor axis lengths appear to
                         * have different scales, with two bits of
                         * state information with unknown meaning.
                         */
                        int x_y = (data[ii*8+7] << 8) | (data[ii*8+8] << 16) | (data[ii*8+9] << 24);
                        fprintf(stdout, " (X=%+05d Y=%+05d Size=%3d minor?=%3d ?=%d major?=%2d angle=%03d, state=%02x)",
                                (x_y << 12) >> 20,
                                (x_y <<  0) >> 20,
                                data[ii * 8 + 10],
                                data[ii * 8 + 11],
                                data[ii * 8 + 12] >> 6,
                                data[ii * 8 + 12] & 63,
                                data[ii * 8 + 13],
                                data[ii * 8 + 14]);
                }
                fprintf(stdout, "\n");
        } else {
                fprintf(stdout, "%2d bytes %s:", res, name);
                for (ii = 0; ii < res; ii++) {
                        if (ii % 4 == 0) fputc(' ', stdout);
                        fputc(hexdigits[data[ii] >> 4], stdout);
                        fputc(hexdigits[data[ii] & 15], stdout);
                }
                fprintf(stdout, "\n");
        }
}

void read_data(void)
{
        struct pollfd pfd[3];
        int res;

        pfd[0].fd = ctrl;
        pfd[0].events = POLLIN;
        pfd[1].fd = intr;
        pfd[1].events = POLLIN;

        while (1) {
                pfd[0].revents = pfd[1].revents = 0;
                res = poll(pfd, 1, -1);
                switch (res) {
                case 0:
                        read_socket(pfd[0].fd, "control");
                        break;
                case 1:
                        read_socket(pfd[1].fd, "interrupt");
                        break;
                default:
                        fprintf(stdout, "poll() failed: %s\n", strerror(errno));
                }
        }
}

int main(int argc, char *argv[])
{
        parse_args(argc, argv);
        connect_sockets();
        write_mystery();
        read_data();
        return EXIT_SUCCESS;
}
