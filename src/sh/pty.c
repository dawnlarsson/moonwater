/*
        One pseudoterminal creation floor for the recorder and terminal UI.

        Opening /dev/ptmx is not enough: the grant must be unlocked, named by
        the kernel, and the slave opened before the master can be handed to an
        event loop.  Keeping the whole transaction here gives every caller
        identical close-on-error and close-on-exec behavior.
*/

// Linux TCGETS/TCSETS use the kernel's compact layout, not libc's termios.
#define PTY_TCGETS 0x5401u
#define PTY_TCSETS 0x5402u
typedef struct
{
        unsigned int arriving, leaving, hardware, behaviour;
        p8 discipline;
        p8 controls[19];
} terminal_modes;

#define PTY_TIOCSPTLCK 0x40045431u
#define PTY_TIOCGPTN 0x80045430u
#define PTY_TIOCSCTTY 0x540eu

static bipolar process_pty_open(b32 address_to master_out,
                                b32 address_to slave_out,
                                bool nonblocking)
{
        positive flags = FILE_READ_WRITE | O_NOCTTY | O_CLOEXEC;
        if (nonblocking)
                flags |= O_NONBLOCK;

        bipolar master = system_open_at(AT_FDCWD, "/dev/ptmx", flags);
        if (master < 0)
                return master;

        b32 unlock = 0;
        p32 number = 0;
        bipolar answer = system_control((b32)master, PTY_TIOCSPTLCK,
                                        address_of unlock);
        if (answer >= 0)
                answer = system_control((b32)master, PTY_TIOCGPTN,
                                        address_of number);
        if (answer < 0)
        {
                system_close((positive)master);
                return answer;
        }

        p8 path[32] = "/dev/pts/";
        positive used = 9;
        used += positive_into(path + used, number);
        path[used] = end;

        bipolar slave = system_open_at(AT_FDCWD, path,
                                       FILE_READ_WRITE | O_NOCTTY |
                                           O_CLOEXEC | O_NOFOLLOW);
        if (slave < 0)
        {
                system_close((positive)master);
                return slave;
        }

        address_to master_out = (b32)master;
        address_to slave_out = (b32)slave;
        return 0;
}

/* Become the slave's session and make it the three standard descriptors.
   Callers used to repeat this sequence and ignore every failure.  In
   particular, dup3(fd, fd) is EINVAL and a close-on-exec slave already in a
   standard slot would then disappear at exec. */
static bipolar process_pty_child_setup(b32 master, b32 slave,
                                       bipolar close_one,
                                       bipolar close_two)
{
        /* signalfd/pidfd can occupy a standard slot when the caller was
           launched with descriptors closed.  Close them before dup3 starts
           installing the tty, never after a slot may have been replaced. */
        if (close_one >= 0 && close_one != master && close_one != slave)
                system_close((positive)close_one);
        if (close_two >= 0 && close_two != close_one &&
            close_two != master && close_two != slave)
                system_close((positive)close_two);

        bipolar answer = system_call(syscall(setsid));

        if (answer >= 0)
                answer = system_control(slave, PTY_TIOCSCTTY, 0);
        if (answer >= 0)
        {
                system_close((positive)master);
                master = -1;

                for (b32 target = 0; target < 3; target++)
                {
                        answer = system_descriptor_install(slave, target);
                        if (answer < 0)
                                break;
                }
        }

        if (slave > 2)
                system_close((positive)slave);
        if (master >= 0)
                system_close((positive)master);
        return answer < 0 ? answer : 0;
}
