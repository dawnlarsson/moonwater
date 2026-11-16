/*
        test/checks/standard.c: the standard's own library: the C library's names and what each does.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_standard
#include "../../src/lib.util.c"

// Reduces compiler noise for tests
#if defined(__clang__)
#pragma clang diagnostic ignored "-Woverflow"
#endif

#if defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic ignored "-Woverflow"
#endif

#define SHARED_named_cases
#include "../checks.c"
#undef SHARED_named_cases

#define fail_equals(a, b) if((a) == (b)) return false

#define fail_not_equals(a, b) \
    if((a) != (b)) { \
        string_format(log_direct, "\n [FAIL] expected %p,  got %p\n", \
                (positive)(b), (positive)(a)); \
        return false; \
    }

p32 address_to p32_nulled = ((address_any)0);

p8 test_write_buffer[1000];
positive test_write_pos = 0;

fn test_writer(address_any data, positive length) {
        if(length == 0) length = string_length(data);
        memory_copy(test_write_buffer + test_write_pos, data, length);
        test_write_pos += length;
}

#define test_type_basics(type_name, max, min) \
        test(type_name##_sizeof) { fail_not_equals(sizeof(type_name), type_name##_bytes); return true; } \
        test(type_name##_bytes_constant) { fail_not_equals(type_name##_bytes, sizeof(type_name)); return true; } \
        test(type_name##_max) { fail_not_equals(type_name##_max, max); return true; } \
        test(type_name##_min) { fail_not_equals(type_name##_min, min); return true; } \
        test(type_name##_overflow) { fail_not_equals((type_name)(type_name##_max + 1), type_name##_min); return true; } \
        test(type_name##_underflow) { fail_not_equals((type_name)(type_name##_min - 1), type_name##_max); return true; }

// Integers wrap; decimals saturate toward infinity, so f-types get the size
// and bounds checks without the wrap-around ones.
#define test_type_decimal(type_name, max, min) \
        test(type_name##_sizeof) { fail_not_equals(sizeof(type_name), type_name##_bytes); return true; } \
        test(type_name##_bytes_constant) { fail_not_equals(type_name##_bytes, sizeof(type_name)); return true; } \
        test(type_name##_max) { fail_not_equals(type_name##_max, max); return true; } \
        test(type_name##_min) { fail_not_equals(type_name##_min, min); return true; }

#define case_type_basics(type_name) \
        case(type_name##_sizeof), \
        case(type_name##_bytes_constant), \
        case(type_name##_max), \
        case(type_name##_min), \
        case(type_name##_overflow), \
        case(type_name##_underflow)

#define case_type_decimal(type_name) \
        case(type_name##_sizeof), \
        case(type_name##_bytes_constant), \
        case(type_name##_max), \
        case(type_name##_min)

test_type_basics(p8, 255, 0);
test_type_basics(b8, 127, -128);

test_type_basics(p16, 65535, 0);
test_type_basics(b16, 32767, -32768);

test_type_basics(p32, 4294967295U, 0);
test_type_basics(b32, 2147483647, -2147483648);

test_type_basics(p64, 18446744073709551615U, 0);
test_type_basics(b64, 9223372036854775807, -9223372036854775808);

// C has no 128 bit literal syntax, so the expected bounds are built the same
// way the library builds them rather than written out as constants.
#ifdef HAS_128
test_type_basics(p128, (p128) ~ (p128)0, (p128)0);
test_type_basics(b128, (b128)(((p128)1 << 127) - 1), (b128)((p128)1 << 127));
#endif

// Decimals have no wrapping overflow, so they get the bounds tests only.
test_type_decimal(f32, 3.402823466e+38f, 1.175494351e-38f);
test_type_decimal(f64, 1.7976931348623157e+308, 2.2250738585072014e-308);
test_type_decimal(f128, __LDBL_MAX__, __LDBL_MIN__);

test(bit_flip_zero_to_one) {
    p32 value = 0;
    bit_flip(0, &value);
    fail_not_equals(value, 1);
    return true;
}

test(bit_flip_one_to_zero) {
    p32 value = 1;
    bit_flip(0, &value);
    fail_not_equals(value, 0);
    return true;
}

test(bit_set_basic) {
    p32 value = 0;
    bit_set(0, &value);
    fail_not_equals(value, 1);
    return true;
}

test(bit_clear_basic) {
    p32 value = 1;
    bit_clear(0, &value);
    fail_not_equals(value, 0);
    return true;
}

test(bit_test_set_bit) {
    p32 value = 1;
    fail(bit_test(0, &value));
    return true;
}

test(bit_test_clear_bit) {
    p32 value = 0;
    fail(!bit_test(0, &value));
    return true;
}

test(bit_set_high_bit) {
    p32 value = 0;
    bit_set(31, &value);
    fail_not_equals(value, 0x80000000);
    return true;
}

test(addresses) {
        positive some_positive = 123456;
        positive address_to ptr = &some_positive;

        fail_not_equals(address_to ptr, some_positive);
        fail_not_equals(ptr, &some_positive);

        positive address_to null_ptr = (positive address_to)0;
        fail(is_null(null_ptr));

        positive arr[2] = {1,2};
        positive address_to arr_ptr = &arr[0];
        fail_not_equals(*(arr_ptr+1), 2);

        return true;
}

test(is_null) {
        fail(is_null(p32_nulled));
        fail(is_null(null));
        fail(is_null(0));
        fail(!is_null(&p32_nulled));

        positive value = 0;
        fail(!is_null(&value));

        int stack_var = 1;
        fail(!is_null(&stack_var));

        static int static_var = 2;
        fail(!is_null(&static_var));

        return true;
}

// Atomic operations test (basic)
test(atomic_operations) {
        positive value = 0;

        atomic_add(&value, 5);
        fail_not_equals(value, 5);

        atomic_sub(&value, 2);
        fail_not_equals(value, 3);

        atomic_inc(&value);
        fail_not_equals(value, 4);

        atomic_dec(&value);
        fail_not_equals(value, 3);

        positive old = atomic_exchange(&value, 10);
        fail_not_equals(old, 3);
        fail_not_equals(value, 10);

        p8 success = atomic_compare_exchange(&value, 10, 20);
        fail(success);
        fail_not_equals(value, 20);

        success = atomic_compare_exchange(&value, 10, 30);
        fail(!success);
        fail_not_equals(value, 20);

        return true;
}

test(memory_fill) {
        p8 buffer[100];

        memory_fill(buffer, 0x42, 100);

        for(positive i = 0; i < 100; i++) {
                fail_not_equals(buffer[i], 0x42);
        }

        memory_fill(buffer, 0, 50);

        for(positive i = 0; i < 50; i++) {
                fail_not_equals(buffer[i], 0);
        }

        for(positive i = 50; i < 100; i++) {
                fail_not_equals(buffer[i], 0x42);
        }

        memory_fill(buffer, 0x99, 0);

        for(positive i = 0; i < 100; i++) {
                fail(buffer[i] == 0 || buffer[i] == 0x42);
        }

        memory_fill(buffer, 0xFF, 100);

        for(positive i = 0; i < 100; i++) {
                fail_not_equals(buffer[i], 0xFF);
        }

        return true;
}

test(memory_copy) {

        p8 buffer[] = "1234567890";
        p8 source[] = "Hello, World!";
        p8 destination[50] = {0};

        memory_copy(buffer + 2, buffer, 5);
        fail_not_equals(buffer[2], '1');
        fail_not_equals(buffer[6], '5');

        /*
        // non-overlapping regions
        memory_copy(destination, source, 16);
        fail_not_equals(string_compare(destination, source), 0);
        */

        return true;
}

test(string_length) {

        fail_not_equals(string_length(""), 0);
        fail_not_equals(string_length("a"), 1);
        fail_not_equals(string_length("Hello"), 5);
        fail_not_equals(string_length("Visible\0Hidden"), 7);

        p8 buffer[100];
        memory_fill(buffer, 'A', 99);

        buffer[99] = end;
        fail_not_equals(string_length(buffer), 99);

        p8 only_null[1] = {end};
        fail_not_equals(string_length(only_null), 0);

        return true;
}

test(string_compare) {
        fail_not_equals(string_compare("", ""), 0);
        fail_not_equals(string_compare("abc", "abc"), 0);
        fail(string_compare("abc", "abd") < 0);
        fail(string_compare("abd", "abc") > 0);
        fail(string_compare("abc", "abcd") < 0);
        fail(string_compare("abcd", "abc") > 0);
        fail(string_compare("", "a") < 0);
        fail(string_compare("a", "") > 0);

        return true;
}

test(string_copy) {
        p8 dest[100];

        string_copy(dest, "Hello");
        fail_not_equals(string_compare(dest, "Hello"), 0);

        string_copy(dest, "");
        fail_not_equals(string_length(dest), 0);

        string_copy(dest, "A very long string that tests the copy function");
        fail_not_equals(string_length(dest), 47);

        string_copy(dest, dest);
        fail_not_equals(string_compare(dest, "A very long string that tests the copy function"), 0);

        return true;
}

test(string_copy_max) {

        p8 dest[100];

        memory_fill(dest, 'X', 100);
        string_copy_max(dest, "Hello, World!", 5);

        fail_not_equals(dest[5], 'X');
        fail_not_equals(dest[0], 'H');
        fail_not_equals(dest[4], 'o');

        // Source shorter than the limit: terminate inside the bound.
        string_copy_max(dest, "Hi", 10);
        fail_not_equals(dest[2], end);

        // A zero limit must write nothing at all, not even a terminator.
        memory_fill(dest, 'H', 4);
        string_copy_max(dest, "Zebra", 0);
        fail_not_equals(dest[0], 'H');

        return true;
}

test(string_first_of) {
        string_address result;

        result = string_first_of("Hello, World!", 'o');
        fail(result != null);
        fail_not_equals(*result, 'o');

        result = string_first_of("Hello, World!", 'z');
        fail_not_equals(result, null);

        result = string_first_of("Hello, World!", end);
        fail(result != null);
        fail_not_equals(*result, end);

        result = string_first_of("abc", 'a');
        fail(result != null);
        fail_not_equals(*result, 'a');

        result = string_first_of("abc", 'c');
        fail(result != null);
        fail_not_equals(*result, 'c');

        return true;
}

test(string_last_of) {
        string_address result;

        result = string_last_of("Hello, World!", 'o');
        fail(result != null);
        fail_not_equals(*result, 'o');

        result = string_last_of("Hello, World!", 'H');
        fail(result != null);

        result = string_last_of("Hello, World!", 'z');
        fail_not_equals(result, null);

        result = string_last_of("abc", 'c');
        fail(result != null);
        fail_not_equals(*result, 'c');

        result = string_last_of("abc", 'a');
        fail(result != null);
        fail_not_equals(*result, 'a');

        return true;
}

test(string_cut) {
        p8 buffer[] = "Hello, World!";

        string_address result = string_cut(buffer, ' ');
        fail(result != null);
        fail_not_equals(string_compare(buffer, "Hello,"), 0);
        fail_not_equals(string_compare(result, "World!"), 0);

        p8 buffer2[] = "NoSpaces";
        result = string_cut(buffer2, ' ');
        fail_not_equals(result, null);

        p8 buffer3[] = "End ";
        result = string_cut(buffer3, ' ');
        fail_not_equals(result, null);

        /*
        p8 buffer4[] = " Xcut";
        result = string_cut(buffer4, ' ');
        fail(result != null);
        fail_not_equals(string_compare(buffer4, ""), 0);
        fail_not_equals(string_compare(result, "Xcut"), 0);
        */

        return true;
}

test(string_replace_all) {
        p8 buffer2[] = "aaaa";
        string_replace_all(buffer2, 'a', 'b');
        fail_not_equals(string_compare(buffer2, "bbbb"), 0);

        p8 buffer3[] = "cccc";
        string_replace_all(buffer3, 'c', 'c');
        fail_not_equals(string_compare(buffer3, "cccc"), 0);

        p8 buffer4[] = "dddd";
        string_replace_all(buffer4, 'z', 'y');
        fail_not_equals(string_compare(buffer4, "dddd"), 0);

        return true;
}

test(string_format_basic) {
        test_write_pos = 0;
        memory_fill(test_write_buffer, 0, 1000);

        string_format(test_writer, "Hello %s!", "World");
        test_write_buffer[test_write_pos] = end;
        fail_not_equals(string_compare(test_write_buffer, "Hello World!"), 0);

        test_write_pos = 0;
        string_format(test_writer, "%% test");
        test_write_buffer[test_write_pos] = end;
        fail_not_equals(string_compare(test_write_buffer, "% test"), 0);

        return true;
}

test(string_format_numbers) {
        test_write_pos = 0;
        memory_fill(test_write_buffer, 0, 1000);

        string_format(test_writer, "Positive: %p", 12345);
        test_write_buffer[test_write_pos] = end;
        fail_not_equals(string_compare(test_write_buffer, "Positive: 12345"), 0);

        test_write_pos = 0;
        string_format(test_writer, "Bipolar: %b", -42);
        test_write_buffer[test_write_pos] = end;
        fail_not_equals(string_compare(test_write_buffer, "Bipolar: -42"), 0);

        test_write_pos = 0;
        string_format(test_writer, "Zero: %p", 0);
        test_write_buffer[test_write_pos] = end;
        fail_not_equals(string_compare(test_write_buffer, "Zero: 0"), 0);

        return true;
}

test(string_format_mixed) {
        test_write_pos = 0;
        memory_fill(test_write_buffer, 0, 1000);

        string_format(test_writer, "%s has %p items worth %b each", "Ada", 5, -10);

        test_write_buffer[test_write_pos] = end;

        fail_not_equals(string_compare(test_write_buffer, "Ada has 5 items worth -10 each"), 0);

        return true;
}

// Path operations
test(path_basename) {
        test_write_pos = 0;
        memory_fill(test_write_buffer, 0, 1000);

        path_basename(test_writer, "/usr/bin/test");
        test_write_buffer[test_write_pos] = end;
        fail_not_equals(string_compare(test_write_buffer, "test"), 0);

        test_write_pos = 0;
        path_basename(test_writer, "/usr/bin/");
        test_write_buffer[test_write_pos] = end;
        fail_not_equals(string_compare(test_write_buffer, "bin"), 0);

        test_write_pos = 0;
        path_basename(test_writer, "/");
        test_write_buffer[test_write_pos] = end;
        fail_not_equals(string_compare(test_write_buffer, "/"), 0);

        test_write_pos = 0;
        path_basename(test_writer, "test.txt");
        test_write_buffer[test_write_pos] = end;
        fail_not_equals(string_compare(test_write_buffer, "test.txt"), 0);

        return true;
}

test(str_macro) {
        string_address data;
        positive length;

        data = str("Hello");
        fail_not_equals(data[0], 'H');
        fail_not_equals(data[1], 'e');

        return true;
}

test(string_end) {
        fail_not_equals(end, '\0');
        fail_not_equals(end, 0);

        p8 buffer[] = "Test";
        fail_not_equals(buffer[4], end);

        return true;
}

test(writer_pattern) {
        test_write_pos = 0;
        memory_fill(test_write_buffer, 0, 1000);

        test_writer("Hello", 5);
        fail_not_equals(test_write_pos, 5);
        fail_not_equals(string_compare(test_write_buffer, "Hello"), 0);

        test_writer(", ", 2);
        test_writer("World!", 0);
        fail_not_equals(string_compare(test_write_buffer, "Hello, World!"), 0);

        return true;
}


/*
        A literal is as long as its letters.

        str() once expanded to the string and sizeof(string), which counts the
        terminator, so every literal written through it carried a stray NUL
        after it. A terminal draws nothing for that and nothing here measured
        the bytes, so it shipped for years.
*/
positive measured_length;
string_address measured_data;

fn measure(address_any data, positive length)
{
        measured_data = data;
        measured_length = length;
}

test(str_length) {
        measure(str(""));
        fail_not_equals(measured_length, 0);

        measure(str("a"));
        fail_not_equals(measured_length, 1);

        measure(str("seventy"));
        fail_not_equals(measured_length, 7);

        measure(str("eightyyy"));
        fail_not_equals(measured_length, 8);

        measure(str("Hello, World!"));
        fail_not_equals(measured_length, 13);
        fail_not_equals(measured_data[12], '!');

        return true;
}

test(str_writes_no_terminator) {
        memory_fill(test_write_buffer, 0xAA, 1000);
        test_write_pos = 0;

        test_writer(str("Hello"));

        fail_not_equals(test_write_pos, 5);
        fail_not_equals(test_write_buffer[4], 'o');
        fail_not_equals(test_write_buffer[5], 0xAA);

        return true;
}

/*
        Strings that are not literals.

        Every needle in this file used to be a string literal, and literals sit
        end to end in .rodata: a routine that reads eight bytes at a time picks
        up the next literal past the terminator and still compares equal. A
        name of exactly seven characters was broken for a fortnight because of
        it. These build their strings in a buffer whose bytes after the
        terminator are 0xff, which is a byte no answer can contain.
*/
p8 padded_a[64];
p8 padded_b[64];

// Writes count letters and a terminator into buffer, with 0xff everywhere
// else, at an offset so the start is not word aligned either.
static string_address padded(p8 address_to buffer, positive offset, positive count)
{
        p8 address_to at = buffer + offset;

        memory_fill(buffer, 0xff, 64);

        for (positive i = 0; i < count; i++)
                at[i] = 'a' + (p8)(i % 16);

        at[count] = end;

        return at;
}

test(string_length_padded) {
        for (positive offset = 0; offset < 8; offset++)
                for (positive count = 0; count < 40; count++)
                        fail_not_equals(string_length(padded(padded_a, offset, count)),
                                        count);

        return true;
}

test(string_compare_padded) {
        for (positive count = 0; count < 40; count++)
        {
                string_address left = padded(padded_a, 1, count);
                string_address right = padded(padded_b, 3, count);

                fail_not_equals(string_compare(left, right), 0);

                if (!count)
                        continue;

                // One byte apart at the very end, which is where a routine
                // that stops a word early stops noticing.
                padded_b[3 + count - 1] = 'Z';
                fail_equals(string_compare(left, right), 0);
        }

        return true;
}

test(string_first_of_padded) {
        for (positive count = 1; count < 24; count++)
        {
                string_address source = padded(padded_a, 5, count);

                fail_not_equals(string_first_of(source, source[count - 1]),
                                source + ((count - 1) % 16 < count - 1
                                              ? (count - 1) % 16
                                              : count - 1));

                fail_not_equals(string_first_of(source, 0xfe), (string_address)0);
        }

        return true;
}

/*
        A table of names looked up by name.

        Each entry is built in its own buffer rather than written as a
        literal, and the name of length seven is a prefix of the one of length
        eight, which is the pair a word at a time gets wrong.
*/
p8 table_storage[16][32];
string_address table_names[17];

test(string_table_find_lengths) {
        p8 needle[32];

        for (positive n = 0; n < 16; n++)
        {
                memory_fill(table_storage[n], 0xff, 32);

                for (positive i = 0; i <= n; i++)
                        table_storage[n][i] = 'a' + (p8)i;

                table_storage[n][n + 1] = end;
                table_names[n] = table_storage[n];
        }

        table_names[16] = (string_address)0;

        for (positive n = 0; n < 16; n++)
        {
                memory_fill(needle, 0xff, 32);
                memory_copy(needle, table_storage[n], n + 2);

                fail_not_equals(string_table_find(needle, table_names,
                                                  sizeof(string_address), 16),
                                n);
        }

        // One letter longer than the longest, and one that is a prefix of
        // nothing: both have to come back as not here.
        memory_fill(needle, 0xff, 32);
        for (positive i = 0; i < 20; i++)
                needle[i] = 'a' + (p8)i;
        needle[20] = end;
        fail_not_equals(string_table_find(needle, table_names,
                                          sizeof(string_address), 16), 16);

        memory_fill(needle, 0xff, 32);
        needle[0] = 'z';
        needle[1] = end;
        fail_not_equals(string_table_find(needle, table_names,
                                          sizeof(string_address), 16), 16);

        return true;
}

/*
        A stack frame the compiler's own stores can reach.

        The kernel enters _start without pushing a return address, so a plain
        C entry point leaves the stack eight bytes out of phase and the
        aligned SSE stores emitted for a fill like this one fault. This was a
        heredoc in the CI workflow; it is a test.
*/
test(stack_is_aligned) {
        p8 buffer[4096];

        memory_fill(buffer, 0x5a, sizeof(buffer));

        for (positive i = 0; i < sizeof(buffer); i++)
                fail_not_equals(buffer[i], 0x5a);

        return true;
}

#if LINUX

/*
        The fourth argument of a system call.

        Linux takes it in r10 and the syscall instruction overwrites rcx, so a
        register shuffle that put it in rcx handed the kernel whatever the
        return address happened to be -- and openat created its files with a
        random mode. Two checks, because the first needs no struct layout:
        pread's fourth argument is an offset, and reading from three bytes in
        either gives the fourth byte or it does not.
*/
test(syscall_argument_four) {
        string_address path = "/tmp/dawning_argument_four";
        p8 buffer[8];
        bipolar file = system_call_4(syscall(openat), AT_FDCWD, (positive)path,
                                     FILE_CREATE | FILE_WRITE | O_TRUNC, 0644);

        fail(file >= 0);

        system_call_3(syscall(write), file, (positive)"abcdefgh", 8);
        system_call_1(syscall(close), file);

        file = system_call_4(syscall(openat), AT_FDCWD, (positive)path, FILE_READ, 0);
        fail(file >= 0);

        memory_fill(buffer, 0, sizeof(buffer));
        fail_not_equals(system_call_4(syscall(pread64), file, (positive)buffer, 4, 3), 4);
        system_call_1(syscall(close), file);

        fail_not_equals(buffer[0], 'd');
        fail_not_equals(buffer[3], 'g');

        return true;
}

/*
        And the mode that argument carried.

        newfstatat fills a struct whose layout is not the same on the three
        machines -- st_mode is sixteen bytes in on the generic layout and
        twenty four in on x86_64 -- so the offset is chosen here rather than
        named, and the field is read out of a byte buffer.
*/
#if X64
#define STAT_MODE_OFFSET 24
#else
#define STAT_MODE_OFFSET 16
#endif

/*
        A name of this process's own, gone before and after: a mode given to
        openat reaches only a file it makes, so a file left at the one shared
        name -- by an older run, or by another run at the same moment -- kept
        whatever mode it had and this failed at "expected 420, got 493" for
        a leftover at 0755, which is how the standard lane read on the box
        for days.
*/
test(created_file_mode) {
        p8 path[64] = "/tmp/dawning_created_mode.";
        p8 status[256];
        p32 mode;
        bipolar file;

        path[26 + positive_into(path + 26, (positive)system_call(syscall(getpid)))] = end;
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)path, 0);
        file = system_call_4(syscall(openat), AT_FDCWD, (positive)path,
                             FILE_CREATE | FILE_WRITE | O_TRUNC, 0644);

        fail(file >= 0);
        system_call_1(syscall(close), file);

        memory_fill(status, 0, sizeof(status));
        fail_not_equals(system_call_4(syscall(newfstatat), AT_FDCWD, (positive)path,
                                      (positive)status, 0), 0);

        memory_copy(address_of mode, status + STAT_MODE_OFFSET, sizeof(mode));
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)path, 0);

        fail_not_equals(mode & 0777, 0644);

        return true;
}

/*
        The folded write loop's three observable exits.

        An invalid descriptor is the ordinary negative-syscall path. The
        zero-length call pairs it with an invalid address so the assembly has
        to take its no-syscall fast path before the pointer can matter.
*/

/*
        The byte classes, against the definitions rather than against a table.

        Every value a C program may pass is checked, which is -1 for EOF and
        then all 256 bytes. The expected answer is written out here as the
        plain condition, so this compares the assembly against what the
        standard says rather than against another copy of the same trick.
*/

/*
        The classes that describe how a byte prints, against the same
        definitions rather than against the arithmetic that answers them.

        Punctuation is written here the way the standard defines it -- graphic
        and not alphanumeric -- and not as the four ranges it happens to be,
        so this is a check of the assembly and not a second copy of it.
*/
test(byte_print_classes) {
        for (b32 value = -1; value < 256; value++) {
                b32 printable = value >= 0x20 && value <= 0x7e;
                b32 graphic = value >= 0x21 && value <= 0x7e;
                b32 control = (value >= 0 && value <= 0x1f) || value == 0x7f;
                b32 digit = value >= '0' && value <= '9';
                b32 alpha = (value >= 'A' && value <= 'Z') ||
                            (value >= 'a' && value <= 'z');
                b32 punctuation = graphic && !(digit || alpha);
                b32 blank = value == ' ' || value == '\t';
                b32 ascii = value >= 0 && value <= 0x7f;

                fail(!byte_is_printable(value) == !printable);
                fail(!byte_is_graphic(value) == !graphic);
                fail(!byte_is_control(value) == !control);
                fail(!byte_is_punctuation(value) == !punctuation);
                fail(!byte_is_blank(value) == !blank);
                fail(!byte_is_ascii(value) == !ascii);
                fail(byte_to_ascii(value) == (value & 0x7f));
        }

        return true;
}

/*
        And the relations the standard states between them, which no single
        range test can be checked against on its own.

        Every predicate must answer exactly one or exactly zero as well:
        punctuation is a subtraction of three range tests, and an answer of
        two or of minus one would still be true and false in the right places
        while being a number a caller may not store in a flag.
*/
test(byte_print_class_relations) {
        for (b32 value = -1; value < 256; value++) {
                fail(byte_is_printable(value) == !!byte_is_printable(value));
                fail(byte_is_graphic(value) == !!byte_is_graphic(value));
                fail(byte_is_control(value) == !!byte_is_control(value));
                fail(byte_is_punctuation(value) == !!byte_is_punctuation(value));
                fail(byte_is_blank(value) == !!byte_is_blank(value));
                fail(byte_is_ascii(value) == !!byte_is_ascii(value));

                //      A byte is graphic exactly when it is alphanumeric or
                //      punctuation, and printable is graphic plus the space.
                fail(!byte_is_graphic(value) ==
                     !(byte_is_alnum(value) || byte_is_punctuation(value)));
                fail(!byte_is_printable(value) ==
                     !(byte_is_graphic(value) || value == ' '));

                //      Inside ASCII, control and printable divide the whole
                //      of it and share none of it.
                fail(!byte_is_ascii(value) ==
                     !(byte_is_control(value) || byte_is_printable(value)));
                fail(!(byte_is_control(value) && byte_is_printable(value)));

                //      Punctuation excludes the alphanumerics by definition,
                //      and blank is two of the six that isspace takes.
                fail(!(byte_is_punctuation(value) && byte_is_alnum(value)));
                fail(!byte_is_blank(value) || byte_is_space(value));
        }

        return true;
}

/*
        Past the byte, where a C program is not supposed to go.

        isascii is defined for every int rather than for a byte and EOF, so it
        is checked over a wide sweep. The others are checked there for a
        different reason: punctuation subtracts the digit and letter answers
        out of the graphic one, which is only a flag as long as neither of
        them can be true where graphic is false. A single value anywhere that
        broke that would show up here as an answer of minus one.
*/
test(byte_print_classes_beyond_a_byte) {
        for (bipolar wide = -70000; wide < 70000; wide++) {
                b32 value = (b32)wide;
                b32 graphic = value >= 0x21 && value <= 0x7e;

                fail(byte_is_punctuation(value) ==
                     !!byte_is_punctuation(value));
                fail(graphic || !byte_is_punctuation(value));
                fail(!byte_is_graphic(value) == !graphic);
                fail(!byte_is_ascii(value) == !(value >= 0 && value <= 0x7f));
                fail(byte_to_ascii(value) == (value & 0x7f));
        }

        //      And the ends of the int, including both signs of the top bit,
        //      which the sweep above never reaches. isascii is the one here
        //      that reads the whole width rather than a byte, and a riscv
        //      answer of one unsigned compare is only right because a
        //      negative arrives sign extended and is therefore enormous.
        b32 ends[] = {b32_min, b32_min + 1, -70001, -256, -1, 0, 1,
                      0x1f, 0x20, 0x21, 0x7e, 0x7f, 0x80, 0xff, 0x100,
                      0x40000000, 0x7ffffffe, b32_max};

        for (positive at = 0; at < sizeof(ends) / sizeof(*ends); at++) {
                b32 value = ends[at];

                fail(!byte_is_ascii(value) == !(value >= 0 && value <= 0x7f));
                fail(!byte_is_printable(value) ==
                     !(value >= 0x20 && value <= 0x7e));
                fail(!byte_is_graphic(value) ==
                     !(value >= 0x21 && value <= 0x7e));
                fail(!byte_is_control(value) ==
                     !((value >= 0 && value <= 0x1f) || value == 0x7f));
                fail(!byte_is_blank(value) ==
                     !(value == ' ' || value == '\t'));
                fail(byte_to_ascii(value) == (value & 0x7f));
                fail(byte_is_punctuation(value) ==
                     !!byte_is_punctuation(value));
                fail(!byte_is_punctuation(value) ==
                     !(value >= 0x21 && value <= 0x7e &&
                       !byte_is_alnum(value)));
        }

        return true;
}

/*
        The three set scans, checked against the definition and not against a
        second copy of the trick.

        These references are the nested loop the routines exist to replace. A
        bitmap written twice agrees with itself about a byte it sign extended,
        so the slow shape is the only honest oracle here.
*/
static positive reference_span_of_set(string_address source, string_address accept)
{
        positive length = 0;

        while (source[length])
        {
                string_address member = accept;

                while (*member && *member != source[length]) member++;

                if (!*member) break;

                length++;
        }

        return length;
}

static positive reference_span_without_set(string_address source, string_address reject)
{
        positive length = 0;

        while (source[length])
        {
                string_address member = reject;

                while (*member && *member != source[length]) member++;

                if (*member) break;

                length++;
        }

        return length;
}

static string_address reference_first_of_set(string_address source, string_address accept)
{
        for (positive at = 0; source[at]; at++)
                for (string_address member = accept; *member; member++)
                        if (*member == source[at]) return source + at;

        return (string_address)0;
}

// All three at once, because they are one loop with one bit different and a
// disagreement in any of them is the same mistake.
static p8 set_scans_agree(string_address source, string_address set)
{
        if (string_span_of_set(source, set) != reference_span_of_set(source, set))
                return false;

        if (string_span_without_set(source, set) != reference_span_without_set(source, set))
                return false;

        if (string_first_of_set(source, set) != reference_first_of_set(source, set))
                return false;

        return true;
}

p8 set_scan_source[512];
p8 set_scan_members[512];

/*
        Every byte against every byte: 255 times 255 pairs, which is the whole
        input space one byte at a time.

        This is the test that would catch a sign extended byte load, a word
        index taken from the wrong bits, and a shift count that was masked to
        the wrong width, because a byte over 127 goes into the top two words
        and a byte of 64, 128 or 192 is where a missed mask lands on bit zero.
*/
test(set_scans_every_byte_pair) {
        for (positive value = 1; value < 256; value++)
        {
                set_scan_source[0] = (p8)value;
                set_scan_source[1] = end;

                for (positive member = 1; member < 256; member++)
                {
                        set_scan_members[0] = (p8)member;
                        set_scan_members[1] = end;

                        fail(set_scans_agree((string_address)set_scan_source,
                                             (string_address)set_scan_members));
                }
        }

        return true;
}

// The values on either side of every sixty four bit word boundary, in a source
// long enough that the answer is not simply zero or one.
test(set_scans_word_boundaries) {
        static const p8 edges[] = {1, 31, 32, 33, 63, 64, 65, 126, 127,
                                   128, 129, 191, 192, 193, 254, 255};

        for (positive i = 0; i < sizeof(edges); i++)
        {
                for (positive j = 0; j < sizeof(edges); j++)
                {
                        set_scan_source[0] = edges[i];
                        set_scan_source[1] = 'a';
                        set_scan_source[2] = 'b';
                        set_scan_source[3] = edges[j];
                        set_scan_source[4] = end;

                        set_scan_members[0] = edges[i];
                        set_scan_members[1] = edges[j];
                        set_scan_members[2] = 'b';
                        set_scan_members[3] = end;

                        fail(set_scans_agree((string_address)set_scan_source,
                                             (string_address)set_scan_members));
                }
        }

        return true;
}

// An empty source, an empty set, and both. An empty set holds nothing, so the
// run of members is nothing and the run of non-members is the whole string.
test(set_scans_empty_arguments) {
        fail(set_scans_agree("", ""));
        fail(set_scans_agree("abc", ""));
        fail(set_scans_agree("", "abc"));

        fail_not_equals(string_span_of_set("abc", ""), 0);
        fail_not_equals(string_span_without_set("abc", ""), 3);
        fail_not_equals((positive)(address_any)string_first_of_set("abc", ""), 0);

        fail_not_equals(string_span_of_set("", "abc"), 0);
        fail_not_equals(string_span_without_set("", "abc"), 0);
        fail_not_equals((positive)(address_any)string_first_of_set("", "abc"), 0);

        return true;
}

/*
        The set-scan builds a table of one byte a member on the stack and walks
        the source two bytes a turn, so each width has its own exit: every width
        from 0 to 300 at every alignment in sixteen, with a source that is a run
        of members and then a stop, for a set of three members and a set of one.
*/
test(set_scans_widths_alignments) {
        static p8 buffer[16 + 300 + 8];
        static const p8 sets[][4] = {" \t\n", "a"};
        positive state = 0x9E3779B97F4A7C15ul;

        for (positive which = 0; which < sizeof(sets) / sizeof(sets[0]); which++)
        {
                for (positive align = 0; align < 16; align++)
                {
                        for (positive size = 0; size <= 300; size++)
                        {
                                p8 *at = buffer + align;
                                XORSHIFT64(state);
                                positive run = state % (size + 1);
                                positive length = string_length((string_address)sets[which]);

                                for (positive i = 0; i < size; i++)
                                        at[i] = i < run ? sets[which][i % length] : (p8)'!';

                                at[size] = 0;
                                fail(set_scans_agree((string_address)at, (string_address)sets[which]));
                        }
                }
        }

        return true;
}

/*
        Every length to forty at every alignment to eight, with the shapes that
        break a scan: all the same byte, a match only at the front, a match
        only at the end, no match at all, and bytes with the high bit set.

        The bytes after the terminator are 0xff so a read past the end is a
        wrong answer rather than a lucky one, and the source starts at an
        offset so nothing depends on it being word aligned.
*/
test(set_scans_lengths_and_alignments) {
        for (positive alignment = 0; alignment < 8; alignment++)
        {
                for (positive length = 0; length <= 40; length++)
                {
                        for (positive shape = 0; shape < 6; shape++)
                        {
                                p8 address_to at = set_scan_source + alignment;

                                memory_fill(set_scan_source, 0xff, 512);

                                for (positive i = 0; i < length; i++)
                                {
                                        if (shape == 0) at[i] = 'a';
                                        else if (shape == 1) at[i] = (p8)('a' + i % 3);
                                        else if (shape == 2) at[i] = (i + 1 == length) ? 'z' : 'a';
                                        else if (shape == 3) at[i] = (i == 0) ? 'z' : 'a';
                                        else if (shape == 4) at[i] = (p8)(0x80 + i % 8);
                                        else at[i] = (p8)(i % 255 + 1);
                                }

                                at[length] = end;

                                fail(set_scans_agree((string_address)at, "a"));
                                fail(set_scans_agree((string_address)at, "az"));
                                fail(set_scans_agree((string_address)at, "z"));
                                fail(set_scans_agree((string_address)at, "aaabbbaaa"));
                                fail(set_scans_agree((string_address)at, "\x80\x81\xff"));
                                fail(set_scans_agree((string_address)at, " \t\n"));
                        }
                }
        }

        return true;
}

// The set is a string too, so it gets the same treatment: every alignment,
// every length to twenty, with repeats in it and the high bit set.
test(set_scans_member_alignments) {
        for (positive alignment = 0; alignment < 8; alignment++)
        {
                for (positive length = 0; length <= 20; length++)
                {
                        p8 address_to at = set_scan_members + alignment;

                        memory_fill(set_scan_members, 0xff, 512);

                        for (positive i = 0; i < length; i++)
                                at[i] = (p8)(1 + (i * 37) % 255);

                        at[length] = end;

                        fail(set_scans_agree("hello, world\x80\xff", (string_address)at));
                        fail(set_scans_agree("\x01\x02\x03", (string_address)at));
                        fail(set_scans_agree("", (string_address)at));
                }
        }

        return true;
}

/*
        A miss is null, and the end of the source is always a miss.

        The terminator cannot be a member of the set: the set is a string, so
        the build loop stops at its own terminator and bit zero is never set.
        string_first_of takes a byte rather than a set, so a NUL is a legal
        thing to look for there and string_first_of(source, 0) succeeds at the
        terminator; there is no spelling of that question here.
*/
test(string_first_of_set_answers_null_at_the_end) {
        string_address source = "abc";

        fail_not_equals((positive)(address_any)string_first_of_set(source, "xyz"), 0);
        fail_not_equals((positive)(address_any)string_first_of_set(source, ""), 0);
        fail_not_equals((positive)(address_any)string_first_of_set("", ""), 0);
        fail_not_equals(string_first_of_set(source, "c"), source + 2);
        fail_not_equals(string_first_of_set(source, "a"), source);
        fail_not_equals(string_first_of_set(source, "cba"), source);

        return true;
}

/* ---- register these in test_cases[] ---- */

/*
        Case-insensitive comparison, checked against the definition.

        The reference below is the contract written out longhand -- fold each
        byte down, subtract, stop at the terminator -- and shares no trick with
        the assembly, so a mistake in the range test cannot hide in both. The
        pair sweep is exhaustive over every ordered pair of bytes, which is
        what pins the direction of the fold: '_' against 'a' is negative when
        both go down and positive when both go up, and only one of those is
        strcasecmp.
*/
static b32 folded_reference(string_address one, string_address two)
{
        for (;;) {
                b32 a = *one++;
                b32 b = *two++;

                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;

                if (a != b) return a - b;
                if (a == 0) return 0;
        }
}

static b32 folded_reference_max(string_address one, string_address two,
                                positive bound)
{
        while (bound--) {
                b32 a = *one++;
                b32 b = *two++;

                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;

                if (a != b) return a - b;
                if (a == 0) return 0;
        }

        return 0;
}

// Letters in both spellings, the six bytes between 'Z' and 'a' that the fold
// direction is visible on, a digit, a blank, and four bytes with the high bit
// set that must not fold at all.
static const p8 folded_bytes[16] = {
        'a', 'B', 'c', 'Z', '_', '0', ' ', '[',
        '`', '{', 0x80, 0xC1, 0xE1, '~', 'A', 'z'
};

static p8 folded_left[256];
static p8 folded_right[256];

test(string_compare_folded_pairs)
{
        positive first, second;

        for (first = 0; first < 256; first++) {
                for (second = 0; second < 256; second++) {
                        p8 one[2], two[2];

                        one[0] = (p8)first;
                        one[1] = 0;
                        two[0] = (p8)second;
                        two[1] = 0;

                        fail(string_compare_folded(one, two)
                             == folded_reference(one, two));
                        fail(string_compare_folded_max(one, two, 0) == 0);
                        fail(string_compare_folded_max(one, two, 1)
                             == folded_reference_max(one, two, 1));
                        fail(string_compare_folded_max(one, two, 2)
                             == folded_reference_max(one, two, 2));
                }
        }

        return true;
}

test(string_compare_folded_direction)
{
        // The six bytes between 'Z' and 'a'. Folding down leaves 'a' above
        // them; folding up would put 'A' below, and flip every sign here.
        fail(string_compare_folded("_", "a") < 0);
        fail(string_compare_folded("_", "A") < 0);
        fail(string_compare_folded("[", "a") < 0);
        fail(string_compare_folded("`", "A") < 0);
        fail(string_compare_folded("a", "_") > 0);
        fail(string_compare_folded("A", "_") > 0);
        // The case the two directions disagree on: down puts 'Z' at 'z',
        // above '_'; up would put it at 'Z', below.
        fail(string_compare_folded("Z", "_") > 0);
        fail(string_compare_folded("A", "^") > 0);
        fail(string_compare_folded_max("Z", "_", 1) > 0);
        fail(string_compare_folded("z", "{") < 0);

        fail(string_compare_folded("ABC", "abc") == 0);
        fail(string_compare_folded("abc", "ABC") == 0);
        fail(string_compare_folded("AbC", "aBc") == 0);
        fail(string_compare_folded("", "") == 0);
        fail(string_compare_folded("", "a") < 0);
        fail(string_compare_folded("a", "") > 0);

        // Bytes with the high bit set are not letters and have one spelling.
        fail(string_compare_folded("\xC1", "\xE1") < 0);
        fail(string_compare_folded("\xE1", "\xC1") > 0);
        fail(string_compare_folded("\xC1", "\xC1") == 0);
        fail(string_compare_folded("\x80", "\xA0") < 0);

        return true;
}

/* The wide tiers of string_compare_folded and string_compare_folded_max against
   the byte reference: every width from 0 to 300, bounds either side of every
   window, both strings laid across the page edge at every offset
   in the last seventy bytes of a page (and one at the start of one), and each
   byte of the second string set to the values that sit on the fold's edges,
   so the first difference lands in every position of every tier. */
static p8 fold_tier_one[8192] __attribute__((aligned(4096)));
static p8 fold_tier_two[8192] __attribute__((aligned(4096)));

static p8 folded_tier_pass(void)
{
        static const p8 edges[4] = { 0, 'Z', 'a', 0x80 };
        static const p8 letters[8] = { 'a', 'B', 'c', 'Z', 'A', 'z', 'm', 'N' };

        for (positive which = 0; which < 70; which += 5)
        for (positive second = 0; second < 2; second++)
        for (positive length = 0; length <= 300; length++)
        {
                positive left_at = 4096 - 70 + which;
                positive right_at = second ? (which * 17) % 64 : 4096 - 70 + (which * 29) % 70;
                string_address one = fold_tier_one + left_at;
                string_address two = fold_tier_two + right_at;

                for (positive at = 0; at < length + 64; at++) {
                        p8 value = letters[(at * 5 + length) & 7];
                        one[at] = value;
                        two[at] = (p8)(value >= 'A' && value <= 'Z' ? value + 32 : value - 32);
                }
                one[length] = 0;
                two[length] = 0;

                fail(string_compare_folded(one, two) == folded_reference(one, two));
                fail(string_compare_folded(two, one) == folded_reference(two, one));
                for (positive bound = 0; bound < 16; bound++) {
                        static const positive bounds[16] = { 1, 15, 16, 17, 31, 32, 33,
                                63, 64, 65, 127, 128, 129, 200, 300, 301 };
                        positive wanted = bounds[bound];

                        fail(string_compare_folded_max(one, two, wanted)
                             == folded_reference_max(one, two, wanted));
                        fail(string_compare_folded_max(two, one, wanted)
                             == folded_reference_max(two, one, wanted));
                }

                for (positive at = 0; at <= length; at++) {
                        p8 keep = two[at];

                        for (positive value = 0; value < 4; value++) {
                                two[at] = edges[value];
                                fail(string_compare_folded(one, two)
                                     == folded_reference(one, two));
                                fail(string_compare_folded(two, one)
                                     == folded_reference(two, one));
                                fail(string_compare_folded_max(one, two, length + 1)
                                     == folded_reference_max(one, two, length + 1));
                                fail(string_compare_folded_max(two, one, length + 64)
                                     == folded_reference_max(two, one, length + 64));
                        }

                        two[at] = keep;
                }
        }

        return true;
}

test(string_compare_folded_tiers)
{
        p8 fine = true;
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;

        for (positive tier = 0; tier < 3; tier++)
        {
                cpu_has_avx2 = tier < 2 ? avx2 : 0;
                cpu_has_avx512 = tier < 1 ? avx512 : 0;
                fine &= folded_tier_pass();
        }
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
#else
        fine = folded_tier_pass();
#endif
        return fine;
}

test(string_compare_folded_shapes)
{
        positive left_at, right_at, length, at, value, cut;

        for (left_at = 0; left_at < 16; left_at++) {
        for (right_at = 0; right_at < 16; right_at++) {
        for (length = 0; length <= 48; length++) {
                string_address one = folded_left + left_at;
                string_address two = folded_right + right_at;

                for (at = 0; at < length; at++)
                        one[at] = two[at] = folded_bytes[(at * 5 + length) & 15];

                one[length] = 0;
                two[length] = 0;

                // Whatever follows the terminator must not reach the answer.
                for (at = length + 1; at < length + 17; at++) {
                        one[at] = (p8)(at + 1);
                        two[at] = (p8)(200 - at);
                }

                fail(string_compare_folded(one, two) == 0);

                for (at = 0; at <= length + 4; at++)
                        fail(string_compare_folded_max(one, two, at)
                             == folded_reference_max(one, two, at));

                for (at = 0; at < length; at++) {
                        p8 keep = two[at];

                        for (value = 0; value < 16; value++) {
                                two[at] = folded_bytes[value];

                                fail(string_compare_folded(one, two)
                                     == folded_reference(one, two));
                                fail(string_compare_folded(two, one)
                                     == folded_reference(two, one));
                                fail(string_compare_folded_max(one, two, length + 1)
                                     == folded_reference_max(one, two, length + 1));
                                fail(string_compare_folded_max(one, two, at + 1)
                                     == folded_reference_max(one, two, at + 1));
                                fail(string_compare_folded_max(one, two, at)
                                     == folded_reference_max(one, two, at));
                        }

                        two[at] = keep;
                }

                for (cut = 0; cut <= length; cut++) {
                        p8 keep = two[cut];

                        two[cut] = 0;

                        fail(string_compare_folded(one, two)
                             == folded_reference(one, two));
                        fail(string_compare_folded(two, one)
                             == folded_reference(two, one));
                        fail(string_compare_folded_max(one, two, length + 2)
                             == folded_reference_max(one, two, length + 2));

                        two[cut] = keep;
                }
        }
        }
        }

        return true;
}

test(string_compare_folded_every_byte)
{
        positive left_at, right_at, length, at, value;

        for (left_at = 0; left_at < 4; left_at++) {
        for (right_at = 0; right_at < 4; right_at++) {
        for (length = 1; length <= 34; length++) {
                string_address one = folded_left + left_at;
                string_address two = folded_right + right_at;

                for (at = 0; at < length; at++)
                        one[at] = two[at] = folded_bytes[(at * 3 + length) & 15];

                one[length] = 0;
                two[length] = 0;

                for (at = 0; at < length; at++) {
                        p8 keep = two[at];

                        for (value = 0; value < 256; value++) {
                                two[at] = (p8)value;

                                fail(string_compare_folded(one, two)
                                     == folded_reference(one, two));
                                fail(string_compare_folded_max(one, two, length + 1)
                                     == folded_reference_max(one, two, length + 1));
                        }

                        two[at] = keep;
                }
        }
        }
        }

        return true;
}

test(string_compare_folded_page_edge)
{
        // The unbounded form reads sixteen bytes at a time, so the page after
        // the string is the thing that proves the offset check. Two pages are
        // taken and the second given back, which leaves nothing mapped above
        // the first: a read that runs past the end faults instead of passing.
        positive back, at;
        p8 address_to base = (p8 address_to)memory(8192);

        fail((bipolar)base > 4096);

        memory_free(base + 4096, 4096);

        for (back = 1; back <= 48; back++) {
                string_address one = base + 4096 - back;
                string_address two = base + 2048;

                for (at = 0; at + 1 < back; at++)
                        one[at] = two[at] = folded_bytes[(at + back) & 15];

                one[back - 1] = 0;
                two[back - 1] = 0;

                fail(string_compare_folded(one, two) == 0);
                fail(string_compare_folded(two, one) == 0);
                fail(string_compare_folded(one, one) == 0);

                if (back > 1) {
                        p8 keep = two[back - 2];

                        two[back - 2] = (p8)(keep ^ 1);
                        fail(string_compare_folded(one, two)
                             == folded_reference(one, two));
                        fail(string_compare_folded(two, one)
                             == folded_reference(two, one));
                        two[back - 2] = keep;
                }

                for (at = 0; at < back; at++)
                        fail(string_compare_folded_max(one, two, at)
                             == folded_reference_max(one, two, at));
        }

        memory_free(base, 4096);

        return true;
}

test(string_compare_folded_max_bound)
{
        // A bound of zero looks at nothing, a bound short of the difference
        // does not reach it, and a bound past both terminators stops at them.
        fail(string_compare_folded_max("a", "b", 0) == 0);
        fail(string_compare_folded_max("QQQQQQQQQQQQQQQQQQQx", "qqqqqqqqqqqqqqqqqqqy", 19) == 0);
        fail(string_compare_folded_max("QQQQQQQQQQQQQQQQQQQx", "qqqqqqqqqqqqqqqqqqqy", 20) < 0);
        fail(string_compare_folded_max("abc", "ABC", 1000) == 0);
        fail(string_compare_folded_max("abc", "ABCD", 1000) < 0);
        fail(string_compare_folded_max("abc", "ABCD", 3) == 0);
        fail(string_compare_folded_max("_", "a", 1) < 0);
        fail(string_compare_folded_max("\xC1", "\xE1", 1) < 0);

        return true;
}

/* --- the case(...) lines, for the test_cases table --- */

/*
        The scans and copies that answer with a pointer.

        Every one of these is checked against a reference written out here as
        the definition reads, not against a second copy of the trick: a byte
        loop builds what the answer should be, the routine is called, and both
        the bytes and the pointer are compared. The buffers carry a poison
        byte on both sides of the span so that a write one past the contract
        fails rather than passing quietly, and the lengths run past the widths
        the wide bodies switch on -- sixteen on arm64, thirty two and sixty
        four on x86_64 -- at every misalignment those bodies peel.

        The expected pointer is computed before the call and compared once.
        Writing it as an if/else pair of fail() calls does not work: fail is
        an if with no braces, so the else binds to the macro's own if and the
        success path then runs the other check. That cost an afternoon.
*/
test(memory_copy_until) {
        p8 room[192];
        p8 got[192];
        p8 want[192];
        p8 bytes[5];

        bytes[0] = 0;
        bytes[1] = 1;
        bytes[2] = 'a';
        bytes[3] = 0x80;
        bytes[4] = 0xff;

        for (positive pick = 0; pick < 5; pick++) {
                p8 value = bytes[pick];

                for (positive length = 0; length <= 80; length++) {
                        // at == length is the byte being absent from the span.
                        for (positive at = 0; at <= length; at++) {
                                for (positive skew = 0; skew < 4; skew++) {
                                        string_address from = room + skew;

                                        for (positive i = 0; i < length; i++)
                                                from[i] = (p8)('A' + (i & 7));

                                        if (at < length) from[at] = value;

                                        memory_fill(got, 0xCC, 192);
                                        memory_fill(want, 0xCC, 192);

                                        positive taken = at < length ? at + 1 : length;

                                        for (positive i = 0; i < taken; i++)
                                                want[i] = from[i];

                                        // Just past the byte it stopped on, or null
                                        // when the span did not contain it at all.
                                        address_any expected = at < length
                                                ? (address_any)(got + at + 1)
                                                : (address_any)0;

                                        address_any answer =
                                                memory_copy_until(got, from, (b8)value, length);

                                        fail(answer == expected);
                                        fail(memory_compare(got, want, 192) == 0);
                                }
                        }
                }
        }

        return true;
}
test(string_copy_end) {
        p8 room[192];
        p8 got[192];

        for (positive length = 0; length <= 80; length++) {
                for (positive skew = 0; skew < 4; skew++) {
                        string_address from = room + skew;

                        for (positive i = 0; i < length; i++)
                                from[i] = (p8)('a' + (i % 26));

                        from[length] = end;

                        memory_fill(got, 0xCC, 192);

                        p8 address_to answer = string_copy_end(got, from);

                        fail(answer == got + length);
                        fail(answer[0] == end);
                        // One byte past the terminator is the caller's, still poison.
                        fail(got[length + 1] == 0xCC);

                        for (positive i = 0; i < length; i++)
                                fail(got[i] == from[i]);
                }
        }

        return true;
}
test(string_copy_max_endptr) {
        p8 room[192];
        p8 got[192];
        p8 want[192];

        for (positive length = 0; length <= 48; length++) {
                for (positive bound = 0; bound <= 72; bound++) {
                        for (positive skew = 0; skew < 4; skew++) {
                                string_address from = room + skew;

                                for (positive i = 0; i < length; i++)
                                        from[i] = (p8)('a' + (i % 26));

                                from[length] = end;

                                memory_fill(got, 0xCC, 192);
                                memory_fill(want, 0xCC, 192);

                                // stpncpy: at most bound bytes, zero padded to the
                                // bound, and no terminator when the source filled it.
                                positive taken = length < bound ? length : bound;

                                for (positive i = 0; i < taken; i++)
                                        want[i] = from[i];

                                for (positive i = taken; i < bound; i++)
                                        want[i] = end;

                                p8 address_to answer =
                                        string_copy_max_endptr(got, from, bound);

                                fail(answer == got + taken);
                                fail(memory_compare(got, want, 192) == 0);
                        }
                }
        }

        return true;
}
test(string_append_max) {
        p8 room[128];
        p8 got[192];
        p8 want[192];

        for (positive already = 0; already <= 24; already++) {
                for (positive length = 0; length <= 32; length++) {
                        for (positive bound = 0; bound <= 40; bound++) {
                                string_address from = room;

                                for (positive i = 0; i < length; i++)
                                        from[i] = (p8)('a' + (i % 26));

                                from[length] = end;

                                memory_fill(got, 0xCC, 192);
                                memory_fill(want, 0xCC, 192);

                                for (positive i = 0; i < already; i++) {
                                        got[i] = (p8)('A' + (i % 26));
                                        want[i] = got[i];
                                }

                                got[already] = end;

                                // strncat: bound counts source bytes only, and the
                                // terminator is always written after them.
                                positive taken = length < bound ? length : bound;

                                for (positive i = 0; i < taken; i++)
                                        want[already + i] = from[i];

                                want[already + taken] = end;

                                string_address answer =
                                        string_append_max(got, from, bound);

                                fail(answer == got);
                                fail(memory_compare(got, want, 192) == 0);
                        }
                }
        }

        return true;
}
test(memory_zero) {
        p8 got[192];
        p8 want[192];

        for (positive size = 0; size <= 96; size++) {
                for (positive skew = 0; skew < 8; skew++) {
                        memory_fill(got, 0xCC, 192);
                        memory_fill(want, 0xCC, 192);

                        for (positive i = 0; i < size; i++)
                                want[skew + i] = end;

                        memory_zero(got + skew, size);

                        fail(memory_compare(got, want, 192) == 0);
                }
        }

        return true;
}
test(memory_copy_source_first) {
        p8 got[192];
        p8 want[192];

        // Disjoint first, then both overlap directions, because bcopy is the
        // spelling that promises to survive them.
        for (positive size = 0; size <= 72; size++) {
                for (positive skew = 0; skew < 4; skew++) {
                        memory_fill(got, 0xCC, 192);
                        memory_fill(want, 0xCC, 192);

                        for (positive i = 0; i < size; i++) {
                                got[skew + i] = (p8)(i * 7 + 1);
                                want[skew + i] = got[skew + i];
                                want[100 + skew + i] = got[skew + i];
                        }

                        memory_copy_source_first(got + skew, got + 100 + skew, size);
                        fail(memory_compare(got, want, 192) == 0);
                }

                for (positive shift = 1; shift <= 9; shift++) {
                        memory_fill(got, 0xCC, 192);
                        memory_fill(want, 0xCC, 192);

                        for (positive i = 0; i < size + shift; i++)
                                got[16 + i] = (p8)(i * 5 + 3);

                        memory_copy(want, got, 192);

                        // Forwards: destination above the source.
                        for (positive i = 0; i < size; i++)
                                want[16 + shift + i] = got[16 + i];

                        memory_copy_source_first(got + 16, got + 16 + shift, size);
                        fail(memory_compare(got, want, 192) == 0);

                        memory_fill(got, 0xCC, 192);
                        memory_fill(want, 0xCC, 192);

                        for (positive i = 0; i < size + shift; i++)
                                got[16 + i] = (p8)(i * 5 + 3);

                        memory_copy(want, got, 192);

                        // Backwards: destination below the source.
                        for (positive i = 0; i < size; i++)
                                want[16 + i] = got[16 + shift + i];

                        memory_copy_source_first(got + 16 + shift, got + 16, size);
                        fail(memory_compare(got, want, 192) == 0);
                }
        }

        return true;
}
test(memory_last_of) {
        p8 room[192];
        p8 bytes[5];

        bytes[0] = 0;
        bytes[1] = 1;
        bytes[2] = 'a';
        bytes[3] = 0x80;
        bytes[4] = 0xff;

        // memrchr, and the one routine here whose answer a word at a time
        // cannot be read straight off the SWAR mask: the borrow that finds a
        // zero byte lights bits above it too, so only the lowest set bit is
        // trustworthy and the greatest match has to be refined by hand. Both
        // shapes are walked -- every earlier position also matching, and only
        // one position matching -- because a refine that stops at the wrong
        // end passes the first and fails the second.
        for (positive pick = 0; pick < 5; pick++) {
                p8 value = bytes[pick];

                for (positive size = 0; size <= 80; size++) {
                        for (positive skew = 0; skew < 4; skew++) {
                                string_address block = room + skew;

                                for (positive i = 0; i < size; i++)
                                        block[i] = (p8)('A' + (i & 7));

                                fail(memory_last_of(block, (b8)value, size) == (address_any)0);

                                for (positive at = 0; at < size; at++) {
                                        block[at] = value;

                                        fail(memory_last_of(block, (b8)value, size)
                                             == (address_any)(block + at));
                                }

                                for (positive i = 0; i < size; i++)
                                        block[i] = (p8)('A' + (i & 7));

                                for (positive at = 0; at < size; at++) {
                                        block[at] = value;

                                        fail(memory_last_of(block, (b8)value, size)
                                             == (address_any)(block + at));

                                        block[at] = (p8)('A' + (at & 7));
                                }
                        }
                }
        }

        return true;
}

/* and in test_cases[], after case(string_table_find_lengths): */

/*
        The integer parsers, against a reference that gets there another way.

        The assembly finds an overflow in the half of the multiply a 64 bit
        result throws away. The reference below finds it the way glibc does,
        by dividing the largest value by the base once and comparing against
        that cutoff before every step. Two different mistakes would have to
        agree for this to pass wrongly, which is the point of not writing the
        reference the same way twice.

        The byte dimension is exhaustive: every one of the 256 values in each
        position where the parse makes a decision -- first byte, byte after a
        sign, byte after a leading zero, byte after 0x -- against every base
        in the table, including the three bases nobody has. What that leaves
        is the value dimension, which is covered by the boundaries either side
        of every limit the routines clamp at and by a fuzz over an alphabet
        made of exactly the bytes the branches care about.
*/
const b32 parse_bases[] = {0, 1, 2, 3, 7, 8, 10, 16, 20, 35, 36, 37, -1, 100};

const string_address parse_edges[] = {
        (string_address) "", (string_address) "0", (string_address) "-0",
        (string_address) "+0", (string_address) "1", (string_address) "-1",
        (string_address) "-", (string_address) "+", (string_address) "  -",
        (string_address) "0x", (string_address) "0X", (string_address) "0xz",
        (string_address) "0x0", (string_address) "00", (string_address) "08",
        (string_address) "0b1", (string_address) "  0x", (string_address) "0xg",
        (string_address) "2147483647", (string_address) "2147483648",
        (string_address) "-2147483648", (string_address) "-2147483649",
        (string_address) "4294967295", (string_address) "4294967296",
        (string_address) "9223372036854775806",
        (string_address) "9223372036854775807",
        (string_address) "9223372036854775808",
        (string_address) "9223372036854775809",
        (string_address) "-9223372036854775807",
        (string_address) "-9223372036854775808",
        (string_address) "-9223372036854775809",
        (string_address) "18446744073709551614",
        (string_address) "18446744073709551615",
        (string_address) "18446744073709551616",
        (string_address) "18446744073709551617",
        (string_address) "-18446744073709551615",
        (string_address) "-18446744073709551616",
        (string_address) "99999999999999999999999999999999",
        (string_address) "-999999999999999999999999999999",
        (string_address) "7fffffffffffffff",
        (string_address) "8000000000000000",
        (string_address) "ffffffffffffffff",
        (string_address) "10000000000000000",
        (string_address) "0x7fffffffffffffff",
        (string_address) "0x8000000000000000",
        (string_address) "0xffffffffffffffff",
        (string_address) "0x10000000000000000",
        (string_address) "-0x8000000000000000",
        (string_address) "-0xffffffffffffffff",
        (string_address) "1777777777777777777777",
        (string_address) "2000000000000000000000",
        (string_address)
        "1111111111111111111111111111111111111111111111111111111111111111",
        (string_address)
        "11111111111111111111111111111111111111111111111111111111111111111",
        (string_address) "zzzzzzzzzzzz", (string_address) "1y2p0ij32e8e7",
        (string_address) "1y2p0ij32e8e8", (string_address) "3w5e11264sgsf",
        (string_address) "3w5e11264sgsg", (string_address) "  \t\n\v\f\r 42abc",
        (string_address) "\t\t\t-0X1F+", (string_address) " +0x10",
        (string_address) "12a34", (string_address) "0.5", (string_address) "42 ",
        null};

p8 parse_scratch[96];

p8 parse_reference_hexadecimal(p32 byte)
{
        return (p32)(byte - '0') < 10 || (p32)((byte | 32) - 'a') < 6;
}

positive parse_reference(string_address input, string_address address_to stopped,
                         b32 base, p8 unsigned_contract)
{
        string_address at = input;
        p8 negative = false, any = false, overflowed = false;
        positive value = 0, cutoff, limit;
        p32 cutoff_digit;

        if (base != 0 && (base < 2 || base > 36))
        {
                if (stopped)
                        *stopped = input;
                return 0;
        }

        while (*at == ' ' || (*at >= 9 && *at <= 13))
                at++;

        if (*at == '-')
        {
                negative = true;
                at++;
        }
        else if (*at == '+')
                at++;

        if (base == 0)
        {
                if (at[0] == '0' && (at[1] | 32) == 'x' &&
                    parse_reference_hexadecimal(at[2]))
                {
                        base = 16;
                        at += 2;
                }
                else if (at[0] == '0')
                        base = 8;
                else
                        base = 10;
        }
        else if (base == 16 && at[0] == '0' && (at[1] | 32) == 'x' &&
                 parse_reference_hexadecimal(at[2]))
                at += 2;

        cutoff = (positive)-1 / (positive)base;
        cutoff_digit = (p32)((positive)-1 % (positive)base);

        for (;;)
        {
                p32 byte = *at, digit;

                if ((p32)(byte - '0') < 10)
                        digit = byte - '0';
                else if ((p32)((byte | 32) - 'a') < 26)
                        digit = (byte | 32) - 'a' + 10;
                else
                        break;

                if (digit >= (p32)base)
                        break;

                any = true;

                if (value > cutoff || (value == cutoff && digit > cutoff_digit))
                        overflowed = true;

                value = value * (positive)base + digit;
                at++;
        }

        if (!any)
        {
                if (stopped)
                        *stopped = input;
                return 0;
        }

        if (stopped)
                *stopped = at;

        if (unsigned_contract)
        {
                if (overflowed)
                        return (positive)-1;
                return negative ? (positive)0 - value : value;
        }

        limit = negative ? (positive)1 << 63 : ((positive)1 << 63) - 1;

        if (overflowed || value > limit)
                value = limit;

        return negative ? (positive)0 - value : value;
}

p8 parse_agrees(string_address input, b32 base)
{
        string_address ours = null, reference = null;
        bipolar signed_reference, signed_ours;
        positive unsigned_reference, unsigned_ours;

        signed_reference = (bipolar)parse_reference(input, &reference, base, false);
        signed_ours = string_to_number(input, &ours, base);

        if (signed_ours != signed_reference || ours != reference)
                return false;

        unsigned_reference = parse_reference(input, &reference, base, true);
        unsigned_ours = string_to_number_unsigned(input, &ours, base);

        if (unsigned_ours != unsigned_reference || ours != reference)
                return false;

        //      A null end pointer is allowed and must not be written through.
        if (string_to_number(input, null, base) != signed_reference)
                return false;

        if (string_to_number_unsigned(input, null, base) != unsigned_reference)
                return false;

        if (base == 10)
        {
                if (string_to_whole_wide(input) != signed_reference)
                        return false;

                //      atoi is the wide answer cut down, not a narrow parse:
                //      "99999999999999999999" clamps to the largest long and
                //      then keeps its low half, which is minus one.
                if (string_to_whole(input) != (b32)signed_reference)
                        return false;
        }

        return true;
}

p8 parse_agrees_every_base(string_address input)
{
        for (positive at = 0; at < sizeof(parse_bases) / sizeof(*parse_bases); at++)
                if (!parse_agrees(input, parse_bases[at]))
                        return false;

        return true;
}

test(string_to_number_every_byte)
{
        //      Byte zero is the terminator and is covered by the shorter forms
        //      of each shape rather than as a value inside them.
        for (p32 byte = 1; byte < 256; byte++)
        {
                p8 shapes[9][5] = {
                        {(p8)byte, 0, 0, 0, 0},
                        {(p8)byte, '7', 0, 0, 0},
                        {'-', (p8)byte, 0, 0, 0},
                        {'+', (p8)byte, '9', 0, 0},
                        {'0', (p8)byte, 0, 0, 0},
                        {'0', (p8)byte, '1', 0, 0},
                        {'0', 'x', (p8)byte, 0, 0},
                        {'0', 'X', (p8)byte, '5', 0},
                        {(p8)byte, '-', '4', 0, 0},
                };

                for (positive shape = 0; shape < 9; shape++)
                        fail(parse_agrees_every_base(shapes[shape]));
        }

        return true;
}

test(string_to_number_edges)
{
        for (positive at = 0; parse_edges[at]; at++)
                fail(parse_agrees_every_base(parse_edges[at]));

        //      The named answers, so that a reference that drifted with the
        //      assembly would still be caught here.
        fail(string_to_whole((string_address) "  -0012xyz") == -12);
        fail(string_to_whole((string_address) "42abc") == 42);
        fail(string_to_whole((string_address) "4294967296") == 0);
        fail(string_to_whole((string_address) "99999999999999999999") == -1);
        fail(string_to_whole((string_address) "-99999999999999999999") == 0);
        fail(string_to_whole_wide((string_address) "0x10") == 0);
        fail(string_to_whole_wide((string_address) "-9223372036854775808") ==
             (bipolar)((positive)1 << 63));
        fail(string_to_number((string_address) "9223372036854775808", null, 10) ==
             (bipolar)(((positive)1 << 63) - 1));
        fail(string_to_number((string_address) "-9223372036854775809", null, 10) ==
             (bipolar)((positive)1 << 63));
        fail(string_to_number_unsigned((string_address) "-1", null, 10) ==
             (positive)-1);
        fail(string_to_number_unsigned((string_address) "-18446744073709551615",
                                       null, 10) == 1);
        fail(string_to_number_unsigned((string_address) "18446744073709551616",
                                       null, 10) == (positive)-1);
        fail(string_to_number_unsigned((string_address) "-99999999999999999999",
                                       null, 10) == (positive)-1);
        fail(string_to_number((string_address) "zz", null, 36) == 1295);
        fail(string_to_number((string_address) "  +0x1f", null, 0) == 31);
        fail(string_to_number((string_address) "0b101", null, 0) == 0);

        return true;
}

test(string_to_number_end_pointer)
{
        string_address stopped;

        //      "0x" with nothing usable after it is not a failed conversion:
        //      the subject sequence is the zero and the end lands on the x.
        stopped = null;
        fail(string_to_number((string_address) "0x", &stopped, 0) == 0);
        fail(stopped == (string_address) "0x" + 1);

        stopped = null;
        fail(string_to_number((string_address) "0xg", &stopped, 16) == 0);
        fail(stopped == (string_address) "0xg" + 1);

        stopped = null;
        fail(string_to_number((string_address) "0x1f", &stopped, 16) == 31);
        fail(stopped == (string_address) "0x1f" + 4);

        //      Nothing converted: the end is the input, not where the scan
        //      gave up, so the whitespace and the sign are given back.
        stopped = null;
        fail(string_to_number((string_address) "   -zz", &stopped, 10) == 0);
        fail(stopped == (string_address) "   -zz");

        //      An unrecognised base converts nothing and hands the input back,
        //      which is one thing more than C promises and one thing less than
        //      an uninitialised pointer.
        for (b32 base = -2; base < 40; base++)
        {
                if (base == 0 || (base >= 2 && base <= 36))
                        continue;

                stopped = null;
                fail(string_to_number((string_address) "42", &stopped, base) == 0);
                fail(stopped == (string_address) "42");

                stopped = null;
                fail(string_to_number_unsigned((string_address) "42", &stopped, base) == 0);
                fail(stopped == (string_address) "42");
        }

        return true;
}

test(string_to_number_long_runs)
{
        //      Leading zeros multiply nothing and must not look like overflow,
        //      and a run longer than any register is where a sticky flag that
        //      was set once and never cleared would show.
        for (positive zeros = 0; zeros < 70; zeros++)
        {
                memory_fill(parse_scratch, '0', zeros);
                memory_copy(parse_scratch + zeros, "1234567", 8);
                fail(parse_agrees_every_base(parse_scratch));

                parse_scratch[0] = '-';
                memory_fill(parse_scratch + 1, '0', zeros);
                memory_copy(parse_scratch + 1 + zeros, "9876543", 8);
                fail(parse_agrees_every_base(parse_scratch));
        }

        //      Sixty four ones is the largest unsigned value in base two and
        //      sixty five of them is the first that is not.
        for (positive ones = 1; ones < 70; ones++)
        {
                memory_fill(parse_scratch, '1', ones);
                parse_scratch[ones] = 0;
                fail(parse_agrees_every_base(parse_scratch));
        }

        //      Every whitespace byte, and the ones next to it that are not.
        {
                p8 spacing[] = {9, 10, 11, 12, 13, 32, 8, 14, 31, 33, 0xa0};

                for (positive at = 0; at < sizeof(spacing); at++)
                {
                        parse_scratch[0] = spacing[at];
                        parse_scratch[1] = spacing[at];
                        parse_scratch[2] = spacing[at];
                        memory_copy(parse_scratch + 3, "-0x1f", 6);
                        fail(parse_agrees_every_base(parse_scratch));

                        parse_scratch[1] = 0;
                        fail(parse_agrees_every_base(parse_scratch));
                }

                memory_fill(parse_scratch, ' ', 40);
                memory_copy(parse_scratch + 40, "-17", 4);
                fail(parse_agrees_every_base(parse_scratch));
        }

        return true;
}

test(string_to_number_fuzz)
{
        //      An alphabet of exactly the bytes the branches care about, so a
        //      short random string is much more likely to reach a corner than
        //      a random byte string would be. The generator is a fixed xorshift
        //      so a failure is reproducible.
        const p8 alphabet[] = "0123456789abcdefxXzZ+- \t\n/:@[`{gG";
        positive state = 88172645463325252ULL;

        for (positive trial = 0; trial < 20000; trial++)
        {
                positive length;

                XORSHIFT64(state);
                length = state % 20;

                for (positive at = 0; at < length; at++)
                {
                        XORSHIFT64(state);
                        parse_scratch[at] = alphabet[state % (sizeof(alphabet) - 1)];
                }

                parse_scratch[length] = 0;
                fail(parse_agrees_every_base(parse_scratch));
        }

        return true;
}

#if X64 || ARM64
/*
        The base is an int, and on these two the top half of the register it
        arrives in is whatever was there before. A routine that reads all
        sixty four bits refuses a perfectly good base ten, and a test written
        only in C would never see it, because a C caller happens to leave the
        top half zero. So the call is made through a pointer that says the
        argument is wide, which is exactly the rubbish the ABI permits.

        riscv64 is not in this test because lp64d says the opposite: an int
        arrives sign extended, and a caller that put rubbish above it would be
        the one breaking the contract. The negative bases in the table are
        what covers the reading of a2 there.
*/
typedef bipolar(address_to parse_wide_base)(string_address, string_address address_to,
                                            positive);

test(string_to_number_reads_a_narrow_base)
{
        parse_wide_base wide = (parse_wide_base)string_to_number;
        positive rubbish = (positive)0xdeadbeef00000000ULL;

        fail(wide((string_address) "42", null, rubbish | 10) == 42);
        fail(wide((string_address) "0x1f", null, rubbish | 16) == 31);
        fail(wide((string_address) "0x1f", null, rubbish) == 31);
        fail(wide((string_address) "42", null, rubbish | 1) == 0);
        fail(wide((string_address) "42", null, rubbish | 37) == 0);

        return true;
}
#endif

/*      And the case() lines, which go in the #if LINUX group beside
        case(absolute_value), where the rest of the standard-names block's coverage sits:

#if X64 || ARM64
#endif
*/

/*
        Bit counting, checked against the definition and not against another
        copy of the same trick.

        The references below walk the bits and answer where the first one
        is, which is the question and nothing like the way the routines
        answer it. Every single bit is tried on its own, then with random
        bits above it and random bits below it: the bits above are what tell
        a real lowest-set-bit answer from the index of whichever set bit a
        sequence happened to land on, and the bits below do the same for the
        highest. Every sixteen bit word is then tried at four positions in
        the register, which is every pattern of adjacent bits any of these
        folds can meet, and a sweep of random words follows -- dense ones
        and sparse ones, because a fold that carries between its fields is
        only wrong when the fields are full and one that drops a bit is only
        wrong when they are not.
*/
positive bit_test_seed = 0x123456789abcdefULL;

positive bit_test_random()
{
        XORSHIFT64(bit_test_seed);
        return bit_test_seed;
}

b32 bit_test_counted(positive value)
{
        b32 count = 0;

        for (b32 at = 0; at < 64; at++)
                if (value & (1ULL << at))
                        count++;

        return count;
}

b32 bit_test_trailing(positive value)
{
        for (b32 at = 0; at < 64; at++)
                if (value & (1ULL << at))
                        return at;

        return 64;
}

b32 bit_test_leading(positive value)
{
        for (b32 at = 0; at < 64; at++)
                if (value & (1ULL << (63 - at)))
                        return at;

        return 64;
}

b32 bit_test_first(positive value)
{
        for (b32 at = 0; at < 64; at++)
                if (value & (1ULL << at))
                        return at + 1;

        return 0;
}

//      Every routine against every reference, including the narrow ffs
//      against the low half of the same word.
p8 bit_test_all(positive value)
{
        fail(bits_counted(value) == bit_test_counted(value));
        fail(bits_trailing_zeros(value) == bit_test_trailing(value));
        fail(bits_leading_zeros(value) == bit_test_leading(value));
        fail(bits_first_set_wide((bipolar)value) == bit_test_first(value));
        fail(bits_first_set((b32)value) == bit_test_first((positive)(p32)value));

        return true;
}

test(bit_counting_defined_at_zero) {
        fail(bits_counted(0) == 0);
        fail(bits_trailing_zeros(0) == 64);
        fail(bits_leading_zeros(0) == 64);
        fail(bits_first_set(0) == 0);
        fail(bits_first_set_wide(0) == 0);

        return true;
}

test(bit_counting_single_bits) {
        for (positive at = 0; at < 64; at++) {
                positive one = 1ULL << at;

                fail(bits_counted(one) == 1);
                fail(bits_trailing_zeros(one) == (b32)at);
                fail(bits_leading_zeros(one) == (b32)(63 - at));
                fail(bits_first_set_wide((bipolar)one) == (b32)(at + 1));
        }

        for (positive at = 0; at < 32; at++)
                fail(bits_first_set((b32)(1u << at)) == (b32)(at + 1));

        //      The top bit of each width, which is where an answer that
        //      folds 64 onto 0 to get the zero case right stops being
        //      right about anything else.
        fail(bits_first_set_wide((bipolar)(1ULL << 63)) == 64);
        fail(bits_first_set((b32)0x80000000) == 32);
        fail(bits_first_set(-1) == 1);

        return true;
}

test(bit_counting_noise_around_the_bit) {
        for (positive at = 0; at < 64; at++)
                for (positive round = 0; round < 32; round++) {
                        positive one = 1ULL << at;
                        positive above = at == 63 ? 0 :
                                bit_test_random() & ~((one << 1) - 1);
                        positive below = bit_test_random() & (one - 1);

                        fail(bit_test_all(one | above));
                        fail(bit_test_all(one | below));
                        fail(bit_test_all(one | above | below));
                }

        return true;
}

test(bit_counting_rounding_at_the_top) {
        //      The exponent bodies convert the word to a double, rounding
        //      toward zero. The words that sit just under a power of two,
        //      or just under the top of the 64 bit range, are where a round
        //      up would move the exponent, so each is checked against the
        //      reference in every routine.
        positive edges[] = {~0ULL, ~0ULL << 1, ~0ULL << 11, 0xfffffffffffff800ULL,
                            0x7ffffffffffffc00ULL, 0xfffffffffffffffeULL,
                            0x8000000000000001ULL, (1ULL << 53) - 1,
                            (1ULL << 53) + 1, (1ULL << 54) - 1};

        for (positive at = 0; at < sizeof(edges) / sizeof(*edges); at++)
                fail(bit_test_all(edges[at]));

        return true;
}

test(bit_counting_every_sixteen_bit_word) {
        for (positive at = 0; at < 65536; at++) {
                fail(bit_test_all(at));
                fail(bit_test_all(at << 16));
                fail(bit_test_all(at << 32));
                fail(bit_test_all(at << 48));
        }

        return true;
}

test(bit_counting_patterns_and_random_words) {
        positive fixed[] = {~0ULL, 0x5555555555555555ULL, 0xaaaaaaaaaaaaaaaaULL,
                            0x3333333333333333ULL, 0xccccccccccccccccULL,
                            0x0f0f0f0f0f0f0f0fULL, 0xf0f0f0f0f0f0f0f0ULL,
                            0x0101010101010101ULL, 0x8000000000000000ULL,
                            0x7fffffffffffffffULL, 0xffffffff00000000ULL,
                            0x00000000ffffffffULL, 0x0000000100000000ULL};

        for (positive at = 0; at < sizeof(fixed) / sizeof(*fixed); at++)
                fail(bit_test_all(fixed[at]));

        for (positive at = 0; at < 100000; at++) {
                positive value = bit_test_random();

                fail(bit_test_all(value));
                fail(bit_test_all(value >> (bit_test_random() & 63)));
                fail(bit_test_all(value & bit_test_random() & bit_test_random()));
        }

        return true;
}

/* and, registered with the others: */

//
//      Rounding and selection on decimals.
//
//      Everything here compares bit patterns rather than values, because
//      every corner case these routines are written for is invisible to ==:
//      a negative zero compares equal to a positive one, and a not-a-number
//      compares equal to nothing at all, so an == test cannot tell a right
//      answer from a wrong one on exactly the inputs that are hard.
//
//      The references are written from the definition and not from the
//      trick: ref_truncated masks the fraction bits out of the exponent's
//      shift, and the other four are built on top of it out of comparisons.
//      Nothing here re-implements the sequence it is checking.
//
typedef union
{
        decimal value;
        p64 bits;
} decimal_pattern_union;

static p64 bits_of_decimal(decimal value)
{
        decimal_pattern_union u;
        u.value = value;
        return u.bits;
}

static decimal decimal_from_bits(p64 bits)
{
        decimal_pattern_union u;
        u.bits = bits;
        return u.value;
}

#define only_the_sign 0x8000000000000000ULL
#define all_but_the_sign 0x7fffffffffffffffULL
#define only_the_fraction 0x000fffffffffffffULL

static p8 ref_not_a_number(decimal value)
{
        return (bits_of_decimal(value) & all_but_the_sign) > 0x7ff0000000000000ULL;
}

// A not-a-number is allowed to differ in its payload and in its quiet bit:
// x86 and arm64 quiet a signalling one on the way through the rounding
// instruction and riscv hands back the argument untouched.
static p8 decimal_same(decimal ours, decimal wanted)
{
        if (bits_of_decimal(ours) == bits_of_decimal(wanted)) return true;
        return ref_not_a_number(ours) && ref_not_a_number(wanted);
}

static decimal ref_truncated(decimal value)
{
        p64 u = bits_of_decimal(value);
        b32 exponent = (b32)((u >> 52) & 0x7ff) - 1023;
        if (exponent >= 52) return value;
        if (exponent < 0) return decimal_from_bits(u & only_the_sign);
        return decimal_from_bits(u & ~(only_the_fraction >> exponent));
}

static decimal ref_floor(decimal value)
{
        decimal whole = ref_truncated(value);
        if (ref_not_a_number(value)) return value;
        if (whole == value) return value;
        return value < 0.0 ? whole - 1.0 : whole;
}

static decimal ref_ceiling(decimal value)
{
        decimal whole = ref_truncated(value);
        if (ref_not_a_number(value)) return value;
        if (whole == value) return value;
        return value > 0.0 ? whole + 1.0 : whole;
}

static decimal ref_rounded(decimal value)
{
        decimal whole = ref_truncated(value);
        if (ref_not_a_number(value)) return value;
        if (whole == value) return value;

        decimal fraction = value < 0.0 ? whole - value : value - whole;
        decimal away = value < 0.0 ? whole - 1.0 : whole + 1.0;
        return fraction >= 0.5 ? away : whole;
}

static decimal ref_nearest(decimal value)
{
        decimal whole = ref_truncated(value);
        if (ref_not_a_number(value)) return value;
        if (whole == value) return value;

        decimal fraction = value < 0.0 ? whole - value : value - whole;
        decimal away = value < 0.0 ? whole - 1.0 : whole + 1.0;
        if (fraction > 0.5) return away;
        if (fraction < 0.5) return whole;
        return ((b64)whole & 1) ? away : whole;
}

static decimal ref_with_sign(decimal magnitude, decimal sign)
{
        return decimal_from_bits((bits_of_decimal(magnitude) & all_but_the_sign) |
                                 (bits_of_decimal(sign) & only_the_sign));
}

//
//      Two places where the architectures part company, and C leaves both of
//      them open.
//
//      A pair of zeros compares equal, so which one comes back is not settled
//      by the ordering. x86 keeps the first argument, because a false compare
//      in MINSD hands back its source operand; arm64 and riscv both answer
//      the way IEEE minNum does, with the negative zero for the smaller and
//      the positive one for the larger.
//
//      A signalling not-a-number is the other. C does not define what any
//      operation does with one. x86 and riscv hand back the other argument,
//      the same as for a quiet one; arm64's FMINNM quiets the signalling one
//      and returns that instead, which is what IEEE minNum specifies and what
//      gcc and glibc do on that architecture.
//
static p8 ref_signalling(decimal value)
{
        p64 u = bits_of_decimal(value);
        return (u & all_but_the_sign) > 0x7ff0000000000000ULL &&
               !(u & 0x0008000000000000ULL);
}

static decimal ref_smaller(decimal first, decimal second)
{
#if ARM64
        if (ref_signalling(first)) return first;
        if (ref_signalling(second)) return second;
#endif
        if (ref_not_a_number(first)) return second;
        if (ref_not_a_number(second)) return first;
        if (first < second) return first;
        if (second < first) return second;
#if X64
        return first;
#else
        return (bits_of_decimal(first) & only_the_sign) ? first : second;
#endif
}

static decimal ref_larger(decimal first, decimal second)
{
#if ARM64
        if (ref_signalling(first)) return first;
        if (ref_signalling(second)) return second;
#endif
        if (ref_not_a_number(first)) return second;
        if (ref_not_a_number(second)) return first;
        if (first > second) return first;
        if (second > first) return second;
#if X64
        return first;
#else
        return (bits_of_decimal(first) & only_the_sign) ? second : first;
#endif
}

static decimal ref_difference(decimal first, decimal second)
{
        if (ref_not_a_number(first) || ref_not_a_number(second)) return first + second;
        if (first > second) return first - second;
        return 0.0;
}

//
//      The sweep. Every interesting bit pattern, each of them also at four
//      steps either side in the last place, and then three bands of
//      pseudorandom values: raw bit patterns, which are mostly enormous and
//      often not numbers at all, and two bands with the exponent forced into
//      the range where a fraction actually exists to be rounded away.
//
static p64 decimal_interesting[] = {
        0x0000000000000000ULL, 0x8000000000000000ULL,
        0x0000000000000001ULL, 0x8000000000000001ULL,
        0x000fffffffffffffULL, 0x800fffffffffffffULL,
        0x0010000000000000ULL, 0x8010000000000000ULL,
        0x3fdfffffffffffffULL, 0xbfdfffffffffffffULL,
        0x3fe0000000000000ULL, 0xbfe0000000000000ULL,
        0x3fe8000000000000ULL, 0xbfe8000000000000ULL,
        0x3ff0000000000000ULL, 0xbff0000000000000ULL,
        0x3ff8000000000000ULL, 0xbff8000000000000ULL,
        0x4004000000000000ULL, 0xc004000000000000ULL,
        0x400c000000000000ULL, 0xc00c000000000000ULL,
        0x4059000000000000ULL, 0xc059000000000000ULL,
        0x4320000000000000ULL, 0xc320000000000000ULL,
        0x4320000000000001ULL, 0xc320000000000001ULL,
        0x4320000000000002ULL, 0xc320000000000003ULL,
        0x4327ffffffffffffULL, 0xc327ffffffffffffULL,
        0x4328000000000000ULL, 0xc328000000000000ULL,
        0x4328000000000001ULL, 0xc328000000000001ULL,
        0x432ffffffffffffeULL, 0xc32ffffffffffffeULL,
        0x432fffffffffffffULL, 0xc32fffffffffffffULL,
        0x4330000000000000ULL, 0xc330000000000000ULL,
        0x4330000000000001ULL, 0xc330000000000001ULL,
        0x4340000000000000ULL, 0xc340000000000000ULL,
        0x7fefffffffffffffULL, 0xffefffffffffffffULL,
        0x7ff0000000000000ULL, 0xfff0000000000000ULL,
        0x7ff8000000000000ULL, 0xfff8000000000000ULL,
        0x7ff0000000000001ULL, 0xfff4000000000000ULL,
};

#define decimal_interesting_count (sizeof(decimal_interesting) / sizeof(decimal_interesting[0]))
#define decimal_sweep_steps 9
#define decimal_sweep_count 400000

static p64 decimal_sweep_mix(p64 index)
{
        p64 z = index + 0x9e3779b97f4a7c15ULL;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
}

static decimal decimal_sweep_at(positive index)
{
        if (index < decimal_interesting_count * decimal_sweep_steps)
        {
                p64 base = decimal_interesting[index / decimal_sweep_steps];
                b64 step = (b64)(index % decimal_sweep_steps) - 4;
                return decimal_from_bits(base + (p64)step);
        }

        p64 random = decimal_sweep_mix(index);

        if (index % 3 == 0) return decimal_from_bits(random);

        if (index % 3 == 1)
                return decimal_from_bits((random & 0x800fffffffffffffULL) |
                                         ((p64)(1013 + (random >> 55) % 70) << 52));

        // straddling 1075, which is where the riscv guard sits and where a
        // half-integer is still representable so every other value is a tie
        return decimal_from_bits((random & 0x800fffffffffffffULL) |
                                 ((p64)(1060 + (random >> 55) % 24) << 52));
}

test(decimal_truncated_sweep)
{
        for (positive i = 0; i < decimal_sweep_count; i++)
        {
                decimal x = decimal_sweep_at(i);
                fail(decimal_same(decimal_truncated(x), ref_truncated(x)));
        }
        return true;
}

test(decimal_floor_sweep)
{
        for (positive i = 0; i < decimal_sweep_count; i++)
        {
                decimal x = decimal_sweep_at(i);
                fail(decimal_same(decimal_floor(x), ref_floor(x)));
        }
        return true;
}

test(decimal_ceiling_sweep)
{
        for (positive i = 0; i < decimal_sweep_count; i++)
        {
                decimal x = decimal_sweep_at(i);
                fail(decimal_same(decimal_ceiling(x), ref_ceiling(x)));
        }
        return true;
}

test(decimal_rounded_sweep)
{
        for (positive i = 0; i < decimal_sweep_count; i++)
        {
                decimal x = decimal_sweep_at(i);
                fail(decimal_same(decimal_rounded(x), ref_rounded(x)));
        }
        return true;
}

test(decimal_nearest_sweep)
{
        for (positive i = 0; i < decimal_sweep_count; i++)
        {
                decimal x = decimal_sweep_at(i);
                fail(decimal_same(decimal_nearest(x), ref_nearest(x)));
        }
        return true;
}

test(decimal_with_sign_sweep)
{
        for (positive i = 0; i < decimal_sweep_count; i++)
        {
                decimal x = decimal_sweep_at(i);
                decimal y = decimal_sweep_at(i * 7 + 3);
                fail(bits_of_decimal(decimal_with_sign(x, y)) == bits_of_decimal(ref_with_sign(x, y)));
        }
        return true;
}

test(decimal_smaller_sweep)
{
        for (positive i = 0; i < decimal_sweep_count; i++)
        {
                decimal x = decimal_sweep_at(i);
                decimal y = decimal_sweep_at(i * 7 + 3);
                fail(decimal_same(decimal_smaller(x, y), ref_smaller(x, y)));
        }
        return true;
}

test(decimal_larger_sweep)
{
        for (positive i = 0; i < decimal_sweep_count; i++)
        {
                decimal x = decimal_sweep_at(i);
                decimal y = decimal_sweep_at(i * 7 + 3);
                fail(decimal_same(decimal_larger(x, y), ref_larger(x, y)));
        }
        return true;
}

test(decimal_difference_sweep)
{
        for (positive i = 0; i < decimal_sweep_count; i++)
        {
                decimal x = decimal_sweep_at(i);
                decimal y = decimal_sweep_at(i * 7 + 3);
                fail(decimal_same(decimal_difference(x, y), ref_difference(x, y)));
        }
        return true;
}

// Every pair drawn from the interesting table against every other, which is
// where the zeros, the infinities and the not-a-numbers meet each other.
test(decimal_pairs_all)
{
        for (positive a = 0; a < decimal_interesting_count; a++)
                for (positive b = 0; b < decimal_interesting_count; b++)
                {
                        decimal x = decimal_from_bits(decimal_interesting[a]);
                        decimal y = decimal_from_bits(decimal_interesting[b]);

                        fail(decimal_same(decimal_smaller(x, y), ref_smaller(x, y)));
                        fail(decimal_same(decimal_larger(x, y), ref_larger(x, y)));
                        fail(decimal_same(decimal_difference(x, y), ref_difference(x, y)));
                        fail(bits_of_decimal(decimal_with_sign(x, y)) ==
                             bits_of_decimal(ref_with_sign(x, y)));
                }
        return true;
}

//
//      The named corners, spelled out so a failure says which rule broke.
//
test(decimal_rounding_keeps_negative_zero)
{
        decimal minus = decimal_from_bits(only_the_sign);

        fail(bits_of_decimal(decimal_truncated(minus)) == only_the_sign);
        fail(bits_of_decimal(decimal_floor(minus)) == only_the_sign);
        fail(bits_of_decimal(decimal_ceiling(minus)) == only_the_sign);
        fail(bits_of_decimal(decimal_rounded(minus)) == only_the_sign);
        fail(bits_of_decimal(decimal_nearest(minus)) == only_the_sign);

        // and a negative fraction rounds to a negative zero, not a positive one
        fail(bits_of_decimal(decimal_truncated(-0.5)) == only_the_sign);
        fail(bits_of_decimal(decimal_ceiling(-0.5)) == only_the_sign);
        fail(bits_of_decimal(decimal_rounded(-0.25)) == only_the_sign);
        fail(bits_of_decimal(decimal_nearest(-0.25)) == only_the_sign);
        fail(bits_of_decimal(decimal_truncated(-0.0000001)) == only_the_sign);

        // while a positive one stays positive
        fail(bits_of_decimal(decimal_floor(0.5)) == 0);
        fail(bits_of_decimal(decimal_truncated(0.5)) == 0);
        fail(bits_of_decimal(decimal_rounded(0.25)) == 0);
        return true;
}

test(decimal_floor_and_ceiling_directions)
{
        fail(decimal_floor(-1.5) == -2.0);
        fail(decimal_ceiling(-1.5) == -1.0);
        fail(decimal_floor(1.5) == 1.0);
        fail(decimal_ceiling(1.5) == 2.0);
        fail(decimal_truncated(-1.5) == -1.0);
        fail(decimal_truncated(1.5) == 1.0);
        fail(decimal_floor(-1.0) == -1.0);
        fail(decimal_ceiling(-1.0) == -1.0);
        return true;
}

test(decimal_rounded_ties_go_away_from_zero)
{
        fail(decimal_rounded(0.5) == 1.0);
        fail(decimal_rounded(-0.5) == -1.0);
        fail(decimal_rounded(1.5) == 2.0);
        fail(decimal_rounded(-1.5) == -2.0);
        fail(decimal_rounded(2.5) == 3.0);
        fail(decimal_rounded(-2.5) == -3.0);
        fail(decimal_rounded(3.5) == 4.0);
        fail(decimal_rounded(0.4) == 0.0);
        fail(decimal_rounded(-0.4) == -0.0);
        return true;
}

// The famous one: a half is the largest double below a half plus one unit in
// the last place, and adding a plain half to it carries it to exactly one.
test(decimal_rounded_just_below_a_half)
{
        decimal almost = decimal_from_bits(0x3fdfffffffffffffULL);

        fail(bits_of_decimal(decimal_rounded(almost)) == 0);
        fail(bits_of_decimal(decimal_rounded(-almost)) == only_the_sign);
        fail(almost + 0.5 == 1.0);
        return true;
}

test(decimal_nearest_ties_go_to_even)
{
        fail(decimal_nearest(0.5) == 0.0);
        fail(decimal_nearest(1.5) == 2.0);
        fail(decimal_nearest(2.5) == 2.0);
        fail(decimal_nearest(3.5) == 4.0);
        fail(decimal_nearest(-0.5) == -0.0);
        fail(decimal_nearest(-1.5) == -2.0);
        fail(decimal_nearest(-2.5) == -2.0);
        fail(bits_of_decimal(decimal_nearest(-0.5)) == only_the_sign);

        // The binade below 2^52 has an ulp of a half, so every value there
        // with an odd mantissa is a halfway case and the tie rule decides all
        // of them. This is the last place a fraction exists at all, and on
        // riscv it is the last value below the guard.
        decimal half_below = decimal_from_bits(0x4320000000000001ULL);  // 2^51 + 0.5
        decimal half_above = decimal_from_bits(0x4320000000000003ULL);  // 2^51 + 1.5

        fail(bits_of_decimal(decimal_nearest(half_below)) == 0x4320000000000000ULL);
        fail(bits_of_decimal(decimal_nearest(half_above)) == 0x4320000000000004ULL);
        fail(bits_of_decimal(decimal_nearest(-half_below)) == 0xc320000000000000ULL);
        fail(bits_of_decimal(decimal_nearest(-half_above)) == 0xc320000000000004ULL);

        // and the same values under the away-from-zero rule
        fail(bits_of_decimal(decimal_rounded(half_below)) == 0x4320000000000002ULL);
        fail(bits_of_decimal(decimal_rounded(half_above)) == 0x4320000000000004ULL);
        fail(bits_of_decimal(decimal_rounded(-half_below)) == 0xc320000000000002ULL);

        // the first value with no fraction left to round, on either side
        fail(bits_of_decimal(decimal_nearest(decimal_from_bits(0x4330000000000001ULL))) ==
             0x4330000000000001ULL);
        fail(bits_of_decimal(decimal_truncated(decimal_from_bits(0x432fffffffffffffULL))) ==
             0x432ffffffffffffeULL);
        return true;
}

test(decimal_rounding_leaves_the_large_alone)
{
        decimal big = decimal_from_bits(0x4330000000000001ULL);
        decimal huge = decimal_from_bits(0x7fefffffffffffffULL);
        decimal plus_infinity = decimal_from_bits(0x7ff0000000000000ULL);
        decimal minus_infinity = decimal_from_bits(0xfff0000000000000ULL);

        fail(bits_of_decimal(decimal_truncated(big)) == 0x4330000000000001ULL);
        fail(bits_of_decimal(decimal_floor(big)) == 0x4330000000000001ULL);
        fail(bits_of_decimal(decimal_rounded(huge)) == 0x7fefffffffffffffULL);
        fail(bits_of_decimal(decimal_nearest(huge)) == 0x7fefffffffffffffULL);
        fail(bits_of_decimal(decimal_ceiling(plus_infinity)) == 0x7ff0000000000000ULL);
        fail(bits_of_decimal(decimal_floor(minus_infinity)) == 0xfff0000000000000ULL);
        fail(bits_of_decimal(decimal_rounded(minus_infinity)) == 0xfff0000000000000ULL);
        return true;
}

test(decimal_rounding_of_a_not_a_number)
{
        decimal quiet = decimal_from_bits(0x7ff8000000000000ULL);

        fail(ref_not_a_number(decimal_truncated(quiet)));
        fail(ref_not_a_number(decimal_floor(quiet)));
        fail(ref_not_a_number(decimal_ceiling(quiet)));
        fail(ref_not_a_number(decimal_rounded(quiet)));
        fail(ref_not_a_number(decimal_nearest(quiet)));
        return true;
}

test(decimal_with_sign_corners)
{
        decimal minus_zero = decimal_from_bits(only_the_sign);

        // a negative zero is a negative sign, which is the whole point
        fail(bits_of_decimal(decimal_with_sign(1.0, minus_zero)) == 0xbff0000000000000ULL);
        fail(bits_of_decimal(decimal_with_sign(1.0, 0.0)) == 0x3ff0000000000000ULL);
        fail(bits_of_decimal(decimal_with_sign(-1.0, 0.0)) == 0x3ff0000000000000ULL);
        fail(bits_of_decimal(decimal_with_sign(-1.0, minus_zero)) == 0xbff0000000000000ULL);

        // the magnitude is taken bit for bit, including a not-a-number's
        fail(bits_of_decimal(decimal_with_sign(decimal_from_bits(0x7ff8000000000000ULL), -1.0)) ==
             0xfff8000000000000ULL);
        fail(bits_of_decimal(decimal_with_sign(decimal_from_bits(0xfff0000000000000ULL), 1.0)) ==
             0x7ff0000000000000ULL);
        fail(bits_of_decimal(decimal_with_sign(minus_zero, 1.0)) == 0);
        fail(bits_of_decimal(decimal_with_sign(0.0, minus_zero)) == only_the_sign);
        return true;
}

test(decimal_selection_of_a_not_a_number)
{
        decimal quiet = decimal_from_bits(0x7ff8000000000000ULL);

        // C says: one not-a-number and the other argument is the answer
        fail(decimal_smaller(quiet, 3.0) == 3.0);
        fail(decimal_smaller(3.0, quiet) == 3.0);
        fail(decimal_larger(quiet, 3.0) == 3.0);
        fail(decimal_larger(3.0, quiet) == 3.0);
        fail(decimal_smaller(quiet, -3.0) == -3.0);
        fail(decimal_larger(quiet, -3.0) == -3.0);

        // and both, and there is nothing else to hand back
        fail(ref_not_a_number(decimal_smaller(quiet, quiet)));
        fail(ref_not_a_number(decimal_larger(quiet, quiet)));
        return true;
}

test(decimal_selection_ordinary)
{
        decimal plus_infinity = decimal_from_bits(0x7ff0000000000000ULL);
        decimal minus_infinity = decimal_from_bits(0xfff0000000000000ULL);

        fail(decimal_smaller(1.0, 2.0) == 1.0);
        fail(decimal_smaller(2.0, 1.0) == 1.0);
        fail(decimal_larger(1.0, 2.0) == 2.0);
        fail(decimal_larger(2.0, 1.0) == 2.0);
        fail(decimal_smaller(-1.0, 1.0) == -1.0);
        fail(decimal_larger(-1.0, 1.0) == 1.0);
        fail(decimal_smaller(minus_infinity, 0.0) == minus_infinity);
        fail(decimal_larger(plus_infinity, 0.0) == plus_infinity);
        return true;
}

test(decimal_selection_of_the_two_zeros)
{
        decimal minus_zero = decimal_from_bits(only_the_sign);

        // Unspecified by C, and the two answers below are what the hardware
        // actually gives. If this test ever has to change, it means somebody
        // changed the sequence, not that the compiler moved.
#if X64
        fail(bits_of_decimal(decimal_smaller(0.0, minus_zero)) == 0);
        fail(bits_of_decimal(decimal_smaller(minus_zero, 0.0)) == only_the_sign);
        fail(bits_of_decimal(decimal_larger(0.0, minus_zero)) == 0);
        fail(bits_of_decimal(decimal_larger(minus_zero, 0.0)) == only_the_sign);
#else
        fail(bits_of_decimal(decimal_smaller(0.0, minus_zero)) == only_the_sign);
        fail(bits_of_decimal(decimal_smaller(minus_zero, 0.0)) == only_the_sign);
        fail(bits_of_decimal(decimal_larger(0.0, minus_zero)) == 0);
        fail(bits_of_decimal(decimal_larger(minus_zero, 0.0)) == 0);
#endif
        return true;
}

test(decimal_selection_of_a_signalling_not_a_number)
{
        decimal loud = decimal_from_bits(0x7ff0000000000001ULL);

        // Also unspecified by C. arm64 keeps the signalling one, quieted;
        // the other two treat it the same as a quiet one and hand back the
        // number. Both are defensible and neither is an accident.
#if ARM64
        fail(ref_not_a_number(decimal_smaller(loud, 3.0)));
        fail(ref_not_a_number(decimal_larger(3.0, loud)));
#else
        fail(decimal_smaller(loud, 3.0) == 3.0);
        fail(decimal_larger(3.0, loud) == 3.0);
#endif
        return true;
}

test(decimal_difference_corners)
{
        decimal quiet = decimal_from_bits(0x7ff8000000000000ULL);
        decimal plus_infinity = decimal_from_bits(0x7ff0000000000000ULL);

        fail(decimal_difference(5.0, 3.0) == 2.0);
        fail(decimal_difference(3.0, 5.0) == 0.0);

        // and it is a positive zero, never a negative one
        fail(bits_of_decimal(decimal_difference(3.0, 5.0)) == 0);
        fail(bits_of_decimal(decimal_difference(-5.0, -3.0)) == 0);
        fail(bits_of_decimal(decimal_difference(3.0, 3.0)) == 0);
        fail(bits_of_decimal(decimal_difference(0.0, 0.0)) == 0);
        fail(bits_of_decimal(decimal_difference(decimal_from_bits(only_the_sign), 0.0)) == 0);

        // equal infinities are not greater than one another, so a zero
        fail(bits_of_decimal(decimal_difference(plus_infinity, plus_infinity)) == 0);

        // either argument not a number and the answer is one too
        fail(ref_not_a_number(decimal_difference(quiet, 1.0)));
        fail(ref_not_a_number(decimal_difference(1.0, quiet)));
        fail(ref_not_a_number(decimal_difference(quiet, quiet)));
        return true;
}

//
//      The fused multiply-add, which is defined by rounding once. Each case
//      below has an exact answer that a multiply followed by an add cannot
//      produce, so a routine that fell back to x * y + z fails every one.
//
test(decimal_multiply_add_rounds_once)
{
        decimal one_up = decimal_from_bits(0x3ff0000000000001ULL);   // 1 + 2^-52
        decimal one_down = decimal_from_bits(0x3fefffffffffffffULL); // 1 - 2^-53
        decimal two_up = decimal_from_bits(0x3ff0000000000002ULL);   // 1 + 2^-51

        // (1 + 2^-52) squared is exactly 1 + 2^-51 + 2^-104, so taking the
        // first two terms away leaves the last one, which only survives if
        // the multiply and the add rounded together instead of one at a time
        fail(bits_of_decimal(decimal_multiply_add(one_up, one_up, -two_up)) ==
             0x3970000000000000ULL);

        // and the other direction: the plain expression loses the product's
        // tail to the multiply's rounding and lands on an exact zero, while
        // the fused one keeps it
        fail(one_up * one_down - 1.0 == 0.0);
        fail(decimal_multiply_add(one_up, one_down, -1.0) != 0.0);
        fail(decimal_multiply_add(one_up, one_down, -1.0) > 0.0);
        return true;
}

test(decimal_multiply_add_ordinary)
{
        decimal quiet = decimal_from_bits(0x7ff8000000000000ULL);
        decimal plus_infinity = decimal_from_bits(0x7ff0000000000000ULL);

        fail(decimal_multiply_add(2.0, 3.0, 4.0) == 10.0);
        fail(decimal_multiply_add(-2.0, 3.0, 4.0) == -2.0);
        fail(decimal_multiply_add(1.5, 1.5, 0.0) == 2.25);
        fail(bits_of_decimal(decimal_multiply_add(0.0, 0.0, 0.0)) == 0);
        fail(bits_of_decimal(decimal_multiply_add(1.0, 1.0, -1.0)) == 0);
        fail(decimal_multiply_add(plus_infinity, 2.0, 1.0) == plus_infinity);
        fail(ref_not_a_number(decimal_multiply_add(plus_infinity, 0.0, 1.0)));
        fail(ref_not_a_number(decimal_multiply_add(quiet, 1.0, 1.0)));
        fail(ref_not_a_number(decimal_multiply_add(1.0, 1.0, quiet)));
        return true;
}

test(decimal_multiply_add_matches_exact_products)
{
        // Where the product is exact the fused answer and the plain one agree,
        // which sweeps the ordinary range for a gross error cheaply. Both
        // factors carry 26 significant bits, so the product carries 52.
        for (positive i = 0; i < decimal_sweep_count / 4; i++)
        {
                p64 r = decimal_sweep_mix(i * 3 + 1);

                decimal x = (decimal)(b64)((b64)(r & 0x3ffffff) - 0x2000000);
                decimal y = (decimal)(b64)((b64)((r >> 26) & 0x3ffffff) - 0x2000000);
                decimal z = (decimal)(b64)((b32)(r >> 32));

                fail(decimal_multiply_add(x, y, z) == x * y + z);
        }
        return true;
}

//
//      The body a machine without the fused instruction runs.
//
//      x86-64-v2 has no FMA3 -- that is v3 -- so decimal_multiply_add picks
//      between the one instruction and a long integer-register body from the
//      separate FMA feature byte. Nothing on a machine built
//      this decade reaches the second one, which is exactly why it is worth a
//      test that walks the byte down on purpose and checks the two bodies
//      agree bit for bit. On a machine that really has no FMA both sides are
//      the same body and this passes without proving anything, which is the
//      honest outcome there.
//
//      The other two architectures have the instruction at their baseline and
//      have no second body, so there is nothing here for them to run.
//
#if X64 && !defined(KERNEL_MODE)
static p8 decimal_multiply_add_paths_agree(decimal x, decimal y, decimal z)
{
        p8 saved = cpu_has_fma;
        decimal wide = decimal_multiply_add(x, y, z);
        decimal inline_wide = math_multiply_add(x, y, z);
        cpu_has_fma = 0;
        decimal narrow = decimal_multiply_add(x, y, z);
        decimal inline_narrow = math_multiply_add(x, y, z);
        cpu_has_fma = saved;
        return decimal_same(narrow, wide) && decimal_same(inline_wide, wide) &&
               decimal_same(inline_narrow, wide);
}
#endif

test(decimal_multiply_add_bodies_agree)
{
#if X64 && !defined(KERNEL_MODE)
        for (positive a = 0; a < decimal_interesting_count; a++)
                for (positive b = 0; b < decimal_interesting_count; b++)
                        for (positive c = 0; c < decimal_interesting_count; c++)
                        {
                                decimal x = decimal_from_bits(decimal_interesting[a]);
                                decimal y = decimal_from_bits(decimal_interesting[b]);
                                decimal z = decimal_from_bits(decimal_interesting[c]);

                                if (!decimal_multiply_add_paths_agree(x, y, z)) return false;
                        }

        for (positive i = 0; i < decimal_sweep_count; i++)
        {
                decimal x = decimal_sweep_at(i);
                decimal y = decimal_sweep_at(i * 7 + 3);
                decimal z = decimal_sweep_at(i * 13 + 5);

                // and half of them with the addend aimed at the product, which
                // is the only way a random sweep ever reaches the cancellation
                if (i & 1)
                {
                        decimal product = x * y;
                        z = decimal_from_bits((bits_of_decimal(product) +
                                               (p64)(b64)((b32)(i % 5) - 2)) ^ only_the_sign);
                }

                if (!decimal_multiply_add_paths_agree(x, y, z)) return false;
        }

        // the deep cancellation the sweep cannot reach: a 106 bit product that
        // agrees with the addend in its top 53 bits, at every scale
        for (b32 scale = -540; scale <= 520; scale++)
        {
                if (1023 + scale < 1 || 1023 + scale > 2046) continue;

                decimal x = decimal_from_bits(((p64)(1023 + scale) << 52) | 1);
                decimal y = decimal_from_bits(0x3ff0000000000001ULL);

                for (b32 step = -3; step <= 3; step++)
                {
                        decimal z = decimal_from_bits(
                                ((((p64)(1023 + scale) << 52) | 2) + (p64)(b64)step) ^ only_the_sign);

                        if (!decimal_multiply_add_paths_agree(x, y, z)) return false;
                }
        }
#endif
        return true;
}

/*
        The single precision family, checked against the bits rather than
        against another copy of the same trick.

        A float is a sign, an eight bit exponent and twenty three mantissa
        bits, and once the exponent has said where the binary point falls all
        four roundings are a shift and a mask away. That is what
        narrow_reference is below, and it shares neither an instruction nor an
        idea with the assembly, which converts through an integer register on
        riscv64 and asks a rounding instruction for it on the other two.

        The reference was first checked against glibc's truncf, floorf, ceilf
        and roundf over all 4294967296 float bit patterns, and then the
        assembly was checked against the reference over the same 4294967296
        patterns on x86_64, arm64 and riscv64. What is kept here is the
        subset that leaves the suite quick: every exponent, the mantissas that
        sit on a boundary, and an exhaustive walk of the two binades that
        decide the hard cases -- the one below one, where rounding chooses
        between zero and one, and the one where a unit in the last place is
        exactly a half.

        Two answers are deliberately not asserted, because the three machines
        are entitled to differ and do. A signalling NaN comes back quieted
        from x86_64 and arm64 and unquieted from riscv64, whose branch hands
        the argument straight back. And when narrow_smaller is given a
        negative zero and a positive zero the two compare equal, which C
        leaves to the implementation: arm64 and riscv64 answer with the
        negative zero and x86_64 answers with whichever came second.
*/

typedef union
{
        p32 bits;
        f32 value;
} narrow_shape;

static p32 narrow_pattern(f32 value)
{
        narrow_shape shape;
        shape.value = value;
        return shape.bits;
}

static f32 narrow_valued(p32 bits)
{
        narrow_shape shape;
        shape.bits = bits;
        return shape.value;
}

static p8 narrow_pattern_is_nan(p32 bits)
{
        return (bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0;
}

//      Mode zero truncates, one floors, two ceilings, three rounds a half
//      away from zero. Anything with an exponent of twenty three or more is
//      already whole, which is also how infinity and every NaN leave here.
static f32 narrow_reference(f32 value, b32 mode)
{
        p32 bits = narrow_pattern(value);
        p32 sign = bits >> 31;
        b32 exponent = (b32)((bits >> 23) & 0xff) - 127;

        if (exponent >= 23)
                return value;

        f32 zero = narrow_valued(sign << 31);
        f32 one = narrow_valued((sign << 31) | 0x3f800000u);

        if (exponent < 0)
        {
                p32 magnitude = bits & 0x7fffffffu;

                if (mode == 0)
                        return zero;
                if (mode == 1)
                        return (sign && magnitude) ? one : zero;
                if (mode == 2)
                        return (!sign && magnitude) ? one : zero;

                return magnitude >= 0x3f000000u ? one : zero;
        }

        p32 fraction_bits = 23 - (p32)exponent;
        p32 fraction_mask = (1u << fraction_bits) - 1u;
        p32 fraction = bits & fraction_mask;
        f32 whole = narrow_valued(bits & ~fraction_mask);

        if (fraction == 0)
                return value;
        if (mode == 0)
                return whole;
        if (mode == 1)
                return sign ? whole - 1.0f : whole;
        if (mode == 2)
                return sign ? whole : whole + 1.0f;
        if (fraction >= (1u << (fraction_bits - 1)))
                return sign ? whole - 1.0f : whole + 1.0f;

        return whole;
}

static p8 narrow_agrees(p32 bits)
{
        f32 value = narrow_valued(bits);
        p32 got[4];
        p32 want[4];

        got[0] = narrow_pattern(narrow_truncated(value));
        got[1] = narrow_pattern(narrow_floor(value));
        got[2] = narrow_pattern(narrow_ceiling(value));
        got[3] = narrow_pattern(narrow_rounded(value));

        for (b32 mode = 0; mode < 4; mode++)
                want[mode] = narrow_pattern(narrow_reference(value, mode));

        for (b32 mode = 0; mode < 4; mode++)
        {
                //      A NaN argument gives a NaN answer, and which NaN is
                //      the machine's business, not this library's.
                if (narrow_pattern_is_nan(want[mode]) || narrow_pattern_is_nan(got[mode]))
                {
                        if (narrow_pattern_is_nan(want[mode]) != narrow_pattern_is_nan(got[mode]))
                                return false;
                        continue;
                }

                if (got[mode] != want[mode])
                        return false;
        }

        return true;
}

test(narrow_rounding_over_every_exponent) {
        //      The mantissas that decide something: nothing, the smallest
        //      step, just under and just on and just over a half, the largest
        //      step, and a couple that are none of those.
        p32 mantissa[] = {0x000000u, 0x000001u, 0x3fffffu, 0x400000u, 0x400001u,
                          0x7fffffu, 0x123456u, 0x2aaaaau, 0x555555u, 0x7ffffeu};

        for (p32 exponent = 0; exponent < 256; exponent++)
                for (positive at = 0; at < sizeof(mantissa) / sizeof(*mantissa); at++)
                {
                        p32 bits = (exponent << 23) | mantissa[at];

                        fail(narrow_agrees(bits));
                        fail(narrow_agrees(bits | 0x80000000u));
                }

        return true;
}

test(narrow_rounding_across_the_deciding_binades) {
        //      Exponent 126 is every magnitude in [0.5, 1), where rounding
        //      has to choose between zero and one and where truncation and
        //      the floor part company on a negative. Exponent 149 is every
        //      magnitude in [2^22, 2^23), the last binade with a fraction at
        //      all, and its step is exactly one half -- the tie that decides
        //      whether a rounding is half-away-from-zero or half-to-even.
        //
        //      Both were walked mantissa by mantissa, all 8388608 of them,
        //      while this was written. What runs here is every mantissa
        //      inside a window at each of the three places a carry can start
        //      -- the bottom, the half, and the top -- and a coprime stride
        //      across the rest, which visits every low bit pattern in turn.
        p32 exponent[] = {126u, 149u};
        p32 window[] = {0x000000u, 0x3ff000u, 0x7ff000u};

        for (positive which = 0; which < 2; which++)
        {
                for (positive edge = 0; edge < 3; edge++)
                        for (p32 step = 0; step < 0x1000u; step++)
                        {
                                p32 bits = (exponent[which] << 23) | (window[edge] + step);

                                fail(narrow_agrees(bits));
                                fail(narrow_agrees(bits | 0x80000000u));
                        }

                for (p32 mantissa = 0; mantissa < 0x800000u; mantissa += 1021u)
                {
                        p32 bits = (exponent[which] << 23) | mantissa;

                        fail(narrow_agrees(bits));
                        fail(narrow_agrees(bits | 0x80000000u));
                }
        }

        return true;
}

test(narrow_rounding_named_values) {
        //      The four answers for one value each, written out, so that a
        //      reference that drifts is caught by something other than
        //      itself.
        f32 value[] = {2.5f, -2.5f, 1.5f, -1.5f, 0.5f, -0.5f, 0.4f, -0.4f,
                       2.0f, -2.0f, 0.0f};
        f32 truncated[] = {2.0f, -2.0f, 1.0f, -1.0f, 0.0f, -0.0f, 0.0f, -0.0f,
                           2.0f, -2.0f, 0.0f};
        f32 floored[] = {2.0f, -3.0f, 1.0f, -2.0f, 0.0f, -1.0f, 0.0f, -1.0f,
                         2.0f, -2.0f, 0.0f};
        f32 ceiled[] = {3.0f, -2.0f, 2.0f, -1.0f, 1.0f, -0.0f, 1.0f, -0.0f,
                        2.0f, -2.0f, 0.0f};
        f32 rounded[] = {3.0f, -3.0f, 2.0f, -2.0f, 1.0f, -1.0f, 0.0f, -0.0f,
                         2.0f, -2.0f, 0.0f};

        for (positive at = 0; at < sizeof(value) / sizeof(*value); at++)
        {
                fail(narrow_pattern(narrow_truncated(value[at])) == narrow_pattern(truncated[at]));
                fail(narrow_pattern(narrow_floor(value[at])) == narrow_pattern(floored[at]));
                fail(narrow_pattern(narrow_ceiling(value[at])) == narrow_pattern(ceiled[at]));
                fail(narrow_pattern(narrow_rounded(value[at])) == narrow_pattern(rounded[at]));
        }

        //      A negative zero survives all four, and a value below a half
        //      rounds to one that keeps its sign rather than to a bare zero.
        fail(narrow_pattern(narrow_truncated(-0.0f)) == 0x80000000u);
        fail(narrow_pattern(narrow_floor(-0.0f)) == 0x80000000u);
        fail(narrow_pattern(narrow_ceiling(-0.0f)) == 0x80000000u);
        fail(narrow_pattern(narrow_rounded(-0.0f)) == 0x80000000u);
        fail(narrow_pattern(narrow_ceiling(-0.25f)) == 0x80000000u);
        fail(narrow_pattern(narrow_rounded(-0.25f)) == 0x80000000u);

        //      One below a half must not be dragged over it. This is the
        //      value that defeats the obvious "add a half and truncate".
        fail(narrow_rounded(narrow_valued(0x3effffffu)) == 0.0f);
        fail(narrow_rounded(narrow_valued(0xbeffffffu)) == 0.0f);

        //      Everything from two to the twenty third upward is already
        //      whole and must come back untouched, infinity included.
        p32 whole[] = {0x4b000000u, 0x4b000001u, 0x7f7fffffu, 0x7f800000u,
                       0xcb000000u, 0xff800000u};

        for (positive at = 0; at < sizeof(whole) / sizeof(*whole); at++)
        {
                fail(narrow_pattern(narrow_truncated(narrow_valued(whole[at]))) == whole[at]);
                fail(narrow_pattern(narrow_floor(narrow_valued(whole[at]))) == whole[at]);
                fail(narrow_pattern(narrow_ceiling(narrow_valued(whole[at]))) == whole[at]);
                fail(narrow_pattern(narrow_rounded(narrow_valued(whole[at]))) == whole[at]);
        }

        return true;
}

test(narrow_absolute_clears_only_the_sign) {
        //      Every exponent and a ladder of mantissas, both signs, against
        //      the definition: the same bits with bit thirty one off.
        for (p32 exponent = 0; exponent < 256; exponent++)
                for (p32 mantissa = 0; mantissa < 0x800000u; mantissa += 0x00040001u)
                {
                        p32 bits = (exponent << 23) | (mantissa & 0x7fffffu);
                        p32 want = bits & 0x7fffffffu;

                        fail(narrow_pattern(narrow_absolute(narrow_valued(bits))) == want);
                        fail(narrow_pattern(narrow_absolute(narrow_valued(bits | 0x80000000u))) == want);
                }

        fail(narrow_pattern(narrow_absolute(-0.0f)) == 0);
        fail(narrow_absolute(-1.5f) == 1.5f);
        fail(narrow_absolute(1.5f) == 1.5f);

        return true;
}

test(narrow_with_sign_moves_only_the_sign) {
        p32 magnitude[] = {0x00000000u, 0x00000001u, 0x007fffffu, 0x00800000u,
                           0x3f800000u, 0x40490fdbu, 0x7f7fffffu, 0x7f800000u,
                           0x7fc00000u};
        p32 source[] = {0x00000000u, 0x80000000u, 0x3f800000u, 0xbf800000u,
                        0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u};

        for (positive one = 0; one < sizeof(magnitude) / sizeof(*magnitude); one++)
                for (positive two = 0; two < sizeof(source) / sizeof(*source); two++)
                        for (p32 flip = 0; flip < 2; flip++)
                        {
                                p32 bits = magnitude[one] | (flip << 31);
                                p32 want = (bits & 0x7fffffffu) | (source[two] & 0x80000000u);

                                fail(narrow_pattern(narrow_with_sign(narrow_valued(bits),
                                                                     narrow_valued(source[two]))) == want);
                        }

        fail(narrow_with_sign(2.5f, -1.0f) == -2.5f);
        fail(narrow_with_sign(-2.5f, 1.0f) == 2.5f);
        fail(narrow_pattern(narrow_with_sign(0.0f, -1.0f)) == 0x80000000u);

        return true;
}

test(narrow_smaller_and_larger) {
        f32 ordered[] = {-3.5f, -1.0f, -0.0f, 0.0f, 1.0f, 2.5f, 1e30f, -1e30f};

        for (positive one = 0; one < sizeof(ordered) / sizeof(*ordered); one++)
                for (positive two = 0; two < sizeof(ordered) / sizeof(*ordered); two++)
                {
                        f32 a = ordered[one], b = ordered[two];
                        f32 small = narrow_smaller(a, b);
                        f32 large = narrow_larger(a, b);

                        //      Equal arguments include the two zeros, whose
                        //      answer C leaves open; the value is pinned here
                        //      and the sign is not.
                        fail(small == (a < b ? a : b));
                        fail(large == (a > b ? a : b));
                }

        //      A NaN argument loses to a number, whichever side it is on,
        //      and two NaNs give a NaN.
        f32 not_a_number = narrow_valued(0x7fc00000u);
        f32 negative_not_a_number = narrow_valued(0xffc00000u);

        fail(narrow_pattern(narrow_smaller(not_a_number, 2.5f)) == narrow_pattern(2.5f));
        fail(narrow_pattern(narrow_smaller(2.5f, not_a_number)) == narrow_pattern(2.5f));
        fail(narrow_pattern(narrow_larger(not_a_number, 2.5f)) == narrow_pattern(2.5f));
        fail(narrow_pattern(narrow_larger(2.5f, not_a_number)) == narrow_pattern(2.5f));
        fail(narrow_pattern(narrow_smaller(negative_not_a_number, -2.5f)) == narrow_pattern(-2.5f));
        fail(narrow_pattern(narrow_larger(-2.5f, negative_not_a_number)) == narrow_pattern(-2.5f));
        fail(narrow_pattern_is_nan(narrow_pattern(narrow_smaller(not_a_number, negative_not_a_number))));
        fail(narrow_pattern_is_nan(narrow_pattern(narrow_larger(not_a_number, negative_not_a_number))));

        //      Infinity is a number here and takes part in the ordering.
        f32 forever = narrow_valued(0x7f800000u);

        fail(narrow_smaller(forever, 1.0f) == 1.0f);
        fail(narrow_larger(forever, 1.0f) == forever);
        fail(narrow_smaller(-forever, 1.0f) == -forever);

        //      Both zeros are equal, so only the value is pinned; the sign is
        //      x86_64's second argument and arm64's and riscv64's negative.
        fail(narrow_smaller(-0.0f, 0.0f) == 0.0f);
        fail(narrow_larger(-0.0f, 0.0f) == 0.0f);

        return true;
}

test(narrow_square_root_is_exact) {
        f32 exact[] = {0.0f, 1.0f, 4.0f, 9.0f, 16.0f, 25.0f, 64.0f, 256.0f,
                       1024.0f, 65536.0f, 1048576.0f};
        f32 roots[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 8.0f, 16.0f,
                       32.0f, 256.0f, 1024.0f};

        for (positive at = 0; at < sizeof(exact) / sizeof(*exact); at++)
                fail(narrow_square_root(exact[at]) == roots[at]);

        //      A square root is correctly rounded, so the narrow one is the
        //      wide one rounded back to a float -- for every argument, since
        //      f64 carries more than twice a float's mantissa and cannot
        //      round twice into a different answer.
        for (p32 exponent = 1; exponent < 255; exponent++)
                for (p32 mantissa = 0; mantissa < 0x800000u; mantissa += 0x00010001u)
                {
                        f32 value = narrow_valued((exponent << 23) | (mantissa & 0x7fffffu));

                        fail(narrow_square_root(value) == (f32)square_root((decimal)value));
                }

        //      Negative gives a NaN, and a negative zero gives itself back.
        fail(narrow_pattern_is_nan(narrow_pattern(narrow_square_root(-1.0f))));
        fail(narrow_pattern(narrow_square_root(-0.0f)) == 0x80000000u);

        return true;
}

/*      And the seven case lines, which go next to case(square_root_is_exact):

*/

test(byte_classes) {
        for (b32 value = -1; value < 256; value++) {
                b32 digit = value >= '0' && value <= '9';
                b32 upper = value >= 'A' && value <= 'Z';
                b32 lower = value >= 'a' && value <= 'z';
                b32 space = value == ' ' || (value >= 9 && value <= 13);
                b32 hex = digit || (value >= 'a' && value <= 'f') ||
                          (value >= 'A' && value <= 'F');

                fail(!byte_is_digit(value) == !digit);
                fail(!byte_is_upper(value) == !upper);
                fail(!byte_is_lower(value) == !lower);
                fail(!byte_is_alpha(value) == !(upper || lower));
                fail(!byte_is_alnum(value) == !(upper || lower || digit));
                fail(!byte_is_space(value) == !space);
                fail(!byte_is_hexadecimal(value) == !hex);
        }

        return true;
}

//      byte_class_holds answers from a table of sixteen bit words, one per
//      byte, so this compares every class the routine names, and the ones it
//      refuses, against every byte. The definitions are written out here,
//      not read back from the table, so a wrong table entry cannot agree
//      with itself.
static p8 class_definition(b32 which, p8 value)
{
        b32 digit = value >= '0' && value <= '9';
        b32 upper = value >= 'A' && value <= 'Z';
        b32 lower = value >= 'a' && value <= 'z';
        b32 alpha = upper || lower;
        b32 alnum = alpha || digit;
        b32 graph = value >= 33 && value <= 126;
        b32 xdigit = digit || (value >= 'A' && value <= 'F') || (value >= 'a' && value <= 'f');

        switch (which) {
        case 0:  return alpha;
        case 1:  return digit;
        case 2:  return alnum;
        case 3:  return upper;
        case 4:  return lower;
        case 5:  return value == ' ' || (value >= 9 && value <= 13);
        case 6:  return value == ' ' || value == '\t';
        case 7:  return value >= 32 && value <= 126;
        case 8:  return graph;
        case 9:  return value < 32 || value == 127;
        case 10: return graph && !alnum;
        case 11: return xdigit;
        default: return false;
        }
}

test(byte_class_table) {
        for (b32 which = -2; which <= 40; which++)
                for (positive value = 0; value < 256; value++)
                        fail(byte_class_holds(which, (p8)value) == class_definition(which, (p8)value));

        return true;
}

//      The C definition byte_to_upper is measured against by the bench path
//      (differential.py --bench): the twenty six lower letters, every other
//      value as it came.
b32 byte_case_upper(b32 value)
{
        return (value >= 'a' && value <= 'z') ? value - 32 : value;
}

//      Case, which must change exactly the twenty six that have another case
//      and leave every other byte as it found it.
test(byte_case) {
        for (b32 value = -1; value < 256; value++) {
                b32 up = byte_case_upper(value);
                b32 down = (value >= 'A' && value <= 'Z') ? value + 32 : value;

                fail(byte_to_upper(value) == up);
                fail(byte_to_lower(value) == down);
        }

        return true;
}

//      Bulk case conversion is exact and bounded even when a span straddles
//      a page boundary. The sentinels on both sides catch widened tails.
static p8 ascii_case_pass(void) {
        p8 original[4128];
        p8 expected[4128];
        p8 actual[4128];
        positive offsets[] = {0, 1, 7, 15, 31, 4093, 4095};
        positive lengths[] = {0, 1, 2, 7, 8, 15, 16, 17, 31, 32, 33, 47, 48,
                              63, 64, 65, 95, 96, 97, 127, 128, 129, 255,
                              256, 257, 1000};

        fail(memory_to_lower_ascii(null, 0) == null);
        fail(memory_to_upper_ascii(null, 0) == null);

        for (positive oi = 0; oi < sizeof(offsets) / sizeof(*offsets); oi++)
                for (positive li = 0; li < sizeof(lengths) / sizeof(*lengths); li++)
                {
                        positive offset = offsets[oi];
                        positive length = lengths[li];

                        if (offset + length > sizeof(original))
                                continue;

                        for (positive at = 0; at < sizeof(original); at++)
                                original[at] = (p8)(at * 37 + offset + length);

                        memory_copy(expected, original, sizeof(original));
                        memory_copy(actual, original, sizeof(original));

                        for (positive at = 0; at < length; at++)
                        {
                                p8 value = expected[offset + at];
                                if (value >= 'A' && value <= 'Z')
                                        expected[offset + at] = value + 32;
                        }

                        fail(memory_to_lower_ascii(actual + offset, length) ==
                             actual + offset);
                        fail(!memory_compare(actual, expected, sizeof(actual)));

                        memory_copy(expected, original, sizeof(original));
                        memory_copy(actual, original, sizeof(original));

                        for (positive at = 0; at < length; at++)
                        {
                                p8 value = expected[offset + at];
                                if (value >= 'a' && value <= 'z')
                                        expected[offset + at] = value - 32;
                        }

                        fail(memory_to_upper_ascii(actual + offset, length) ==
                             actual + offset);
                        fail(!memory_compare(actual, expected, sizeof(actual)));
                }

        return true;
}

//      Once for each body the machine has -- the zmm one, the ymm one, and
//      the sixteen byte one under them -- the flags put back from a copy.
test(memory_ascii_case) {
        p8 fine = true;
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;

        for (positive tier = 0; tier < 3; tier++)
        {
                cpu_has_avx2 = tier < 2 ? avx2 : 0;
                cpu_has_avx512 = tier < 1 ? avx512 : 0;
                fine &= ascii_case_pass();
        }
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
#else
        fine = ascii_case_pass();
#endif
        return fine;
}

static p8 hash_33_pass(void) {
        p8 bytes[4128];
        positive offsets[] = {0, 1, 3, 7, 15, 4093, 4095};

        for (positive at = 0; at < sizeof(bytes); at++)
                bytes[at] = (p8)(at * 37 + 11);

        for (positive oi = 0; oi < sizeof(offsets) / sizeof(*offsets); oi++)
                for (positive length = 0; length <= 257; length++)
                {
                        positive offset = offsets[oi];
                        positive wanted = 5381;

                        if (offset + length > sizeof(bytes))
                                continue;

                        for (positive at = 0; at < length; at++)
                                wanted = wanted * 33 + bytes[offset + at];

                        fail(memory_hash_33(bytes + offset, length) == wanted);
                }

        for (positive length = 258; length <= 4128; length += length < 1100 ? 37 : 211)
                for (positive oi = 0; oi < 3; oi++)
                {
                        positive offset = offsets[oi];
                        positive wanted = 5381;

                        if (offset + length > sizeof(bytes))
                                continue;

                        for (positive at = 0; at < length; at++)
                                wanted = wanted * 33 + bytes[offset + at];

                        fail(memory_hash_33(bytes + offset, length) == wanted);
                }

        return true;
}

//      Under every body the machine has: the 32 lanes where there is AVX-512
//      with DQ, the four byte loop where there is not, which is told by
//      writing the two flags down and putting them back.
test(memory_hash_33) {
        p8 fine = hash_33_pass();
#if X64
        p8 dq = cpu_has_avx512_dq, wide = cpu_has_avx512;

        cpu_has_avx512_dq = 1;
        fine &= hash_33_pass();
        cpu_has_avx512_dq = dq;
        cpu_has_avx512 = 0;
        fine &= hash_33_pass();
        cpu_has_avx512 = wide;
#endif
        return fine;
}

test(memory_span_byte) {
        p8 bytes[4128];
        positive page_lengths[] = {127, 128, 129, 255, 256, 257, 4095};

        for (positive value = 0; value < 256; value++)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive length = 0; length <= 33; length++)
                                for (positive mismatch = 0; mismatch <= length; mismatch++)
                                {
                                        if (offset)
                                                bytes[offset - 1] = 0xa5;
                                        memory_fill(bytes + offset, (p8)value, length);
                                        bytes[offset + length] = 0xa5;

                                        if (mismatch < length)
                                                bytes[offset + mismatch] = (p8)(value + 1);

                                        positive got = memory_span_byte(
                                            bytes + offset, (p8)value, length);

                                        if (got != mismatch)
                                                return false;
                                        if (offset)
                                                fail(bytes[offset - 1] == 0xa5);
                                        fail(bytes[offset + length] == 0xa5);
                                }

        for (positive value = 0; value < 256; value++)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive li = 0;
                             li < sizeof(page_lengths) / sizeof(*page_lengths); li++)
                        {
                                positive length = page_lengths[li];
                                positive positions[] = {0, 1, 15, 16, length / 2,
                                                        length - 1, length};

                                if (offset + length >= sizeof(bytes))
                                        continue;

                                for (positive pi = 0;
                                     pi < sizeof(positions) / sizeof(*positions); pi++)
                                {
                                        positive mismatch = positions[pi];

                                        if (offset)
                                                bytes[offset - 1] = 0xa5;
                                        memory_fill(bytes + offset, (p8)value, length);
                                        bytes[offset + length] = 0xa5;
                                        if (mismatch < length)
                                                bytes[offset + mismatch] = (p8)(value + 1);

                                        fail(memory_span_byte(bytes + offset, (p8)value,
                                                              length) == mismatch);
                                        if (offset)
                                                fail(bytes[offset - 1] == 0xa5);
                                        fail(bytes[offset + length] == 0xa5);
                                }
                        }

        /* Widths to 300 at every start residue, where the doubleword body
           takes over at sixteen and the head and tail bytes meet it: three
           values, and a mismatch at the edges of each word and at the middle. */
        for (positive value = 0; value < 256; value += 90)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive length = 0; length <= 300; length++)
                        {
                                positive positions[] = {0, 1, 7, 8, 9, 15, 16, length / 2, length - 1, length};

                                for (positive pi = 0; pi < sizeof(positions) / sizeof(*positions); pi++)
                                {
                                        positive mismatch = positions[pi];

                                        if (mismatch > length)
                                                continue;
                                        if (offset)
                                                bytes[offset - 1] = 0xa5;
                                        memory_fill(bytes + offset, (p8)value, length);
                                        bytes[offset + length] = 0xa5;
                                        if (mismatch < length)
                                                bytes[offset + mismatch] = (p8)(value + 1);

                                        fail(memory_span_byte(bytes + offset, (p8)value,
                                                              length) == mismatch);
                                        if (offset)
                                                fail(bytes[offset - 1] == 0xa5);
                                        fail(bytes[offset + length] == 0xa5);
                                }
                        }

        return true;
}

/*
        The same from the other end: the run of the value that ends the block,
        with the first byte that differs anywhere in it, every value, every
        start residue, every length to 33 and the page-ish lengths the vector
        body steps over, and the bytes either side left alone. Called by its
        parenthesised name, which is the assembly rather than lib.util.c's
        inline check of the last byte in front of it.
*/
test(memory_span_byte_reverse) {
        p8 bytes[4128];
        positive page_lengths[] = {15, 16, 17, 31, 32, 33, 127, 128, 129,
                                   255, 256, 257, 4095};

        for (positive value = 0; value < 256; value++)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive length = 0; length <= 33; length++)
                                for (positive mismatch = 0; mismatch <= length; mismatch++)
                                {
                                        // mismatch is how long the suffix is;
                                        // the byte before it differs.
                                        if (offset)
                                                bytes[offset - 1] = 0xa5;
                                        memory_fill(bytes + offset, (p8)value, length);
                                        bytes[offset + length] = 0xa5;

                                        if (mismatch < length)
                                                bytes[offset + length - 1 - mismatch] =
                                                    (p8)(value + 1);

                                        fail((memory_span_byte_reverse)(
                                                 bytes + offset, (p8)value, length) ==
                                             mismatch);
                                        fail(memory_span_byte_reverse(
                                                 bytes + offset, (p8)value, length) ==
                                             mismatch);
                                        if (offset)
                                                fail(bytes[offset - 1] == 0xa5);
                                        fail(bytes[offset + length] == 0xa5);
                                }

        for (positive value = 0; value < 256; value += 51)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive li = 0;
                             li < sizeof(page_lengths) / sizeof(*page_lengths); li++)
                        {
                                positive length = page_lengths[li];
                                positive suffixes[] = {0, 1, 15, 16, 17, length / 2,
                                                       length - 1, length};

                                if (offset + length >= sizeof(bytes))
                                        continue;

                                for (positive si = 0;
                                     si < sizeof(suffixes) / sizeof(*suffixes); si++)
                                {
                                        positive suffix = suffixes[si];

                                        if (suffix > length)
                                                continue;
                                        memory_fill(bytes + offset, (p8)value, length);
                                        if (suffix < length)
                                                bytes[offset + length - 1 - suffix] =
                                                    (p8)(value + 1);

                                        fail((memory_span_byte_reverse)(
                                                 bytes + offset, (p8)value, length) ==
                                             suffix);
                                }
                        }

        /* Widths to 300 at every start residue: three values, and a mismatch
           at the ends of each word and at the middle, counted from the end. */
        for (positive value = 0; value < 256; value += 90)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive length = 0; length <= 300; length++)
                        {
                                positive suffixes[] = {0, 1, 7, 8, 9, 15, 16, length / 2, length - 1, length};

                                for (positive si = 0; si < sizeof(suffixes) / sizeof(*suffixes); si++)
                                {
                                        positive suffix = suffixes[si];

                                        if (suffix > length)
                                                continue;
                                        if (offset)
                                                bytes[offset - 1] = 0xa5;
                                        memory_fill(bytes + offset, (p8)value, length);
                                        bytes[offset + length] = 0xa5;
                                        if (suffix < length)
                                                bytes[offset + length - 1 - suffix] = (p8)(value + 1);

                                        fail((memory_span_byte_reverse)(
                                                 bytes + offset, (p8)value, length) == suffix);
                                        if (offset)
                                                fail(bytes[offset - 1] == 0xa5);
                                        fail(bytes[offset + length] == 0xa5);
                                }
                        }

        return true;
}

/*
        Square root, which is the hardware's own instruction.

        Exact powers of four have exact roots in binary floating point, so
        those are compared for equality rather than for nearness -- an
        implementation that approximated would fail here and a correct one
        cannot. The general case is checked by squaring the answer back.
*/
test(absolute_whole_and_wide) {
        b32 narrow[] = {0, 1, -1, 7, -7, 2147483647, -2147483647};
        bipolar wide[] = {0, 1, -1, 7, -7, 9223372036854775807LL,
                          -9223372036854775807LL};

        for (positive at = 0; at < sizeof(narrow) / sizeof(*narrow); at++)
                fail(absolute_whole(narrow[at]) ==
                     (narrow[at] < 0 ? -narrow[at] : narrow[at]));

        for (positive at = 0; at < sizeof(wide) / sizeof(*wide); at++)
                fail(absolute_wide(wide[at]) ==
                     (wide[at] < 0 ? -wide[at] : wide[at]));

        //      The narrow one must read a narrow register: a negative int
        //      widened by the caller leaves the top half set, and a 64 bit
        //      read of it would answer about a different number entirely.
        fail(absolute_whole(-5) == 5);
        fail(absolute_wide(-5) == 5);

        //      The most negative value is its own negation, and the sign
        //      flag the conditional move reads stays set, so it answers
        //      itself: the wrap the sign-spread form gave, kept exactly.
        fail(absolute_whole(-2147483647 - 1) == -2147483647 - 1);
        fail(absolute_wide(-9223372036854775807LL - 1) == -9223372036854775807LL - 1);

        return true;
}

test(square_root_is_exact) {
        decimal exact[] = {0.0, 1.0, 4.0, 9.0, 16.0, 25.0, 64.0, 256.0,
                           1024.0, 65536.0, 1048576.0};
        decimal roots[] = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 8.0, 16.0,
                           32.0, 256.0, 1024.0};

        for (positive at = 0; at < sizeof(exact) / sizeof(*exact); at++)
                fail(square_root(exact[at]) == roots[at]);

        for (positive at = 1; at < 500; at++) {
                decimal value = (decimal)at;
                decimal root = square_root(value);
                decimal back = root * root;
                decimal off = back - value;

                if (off < 0.0)
                        off = -off;

                fail(off < 0.0001);
        }

        return true;
}

test(absolute_value) {
        decimal ordinary[] = {0.0, 1.5, -1.5, 1e10, -1e10, 1e-10, -1e-10};

        for (positive at = 0; at < sizeof(ordinary) / sizeof(*ordinary); at++) {
                decimal was = ordinary[at];
                decimal now = absolute(was);

                fail(now == (was < 0.0 ? -was : was));
                fail(!(now < 0.0));
        }

        //      Negative zero is the one that catches a subtraction pretending
        //      to be an absolute value: -0.0 is not less than zero, so a
        //      conditional negate leaves it alone and the sign bit stays.
        {
                decimal zero = 0.0;
                decimal minus = -zero;

                fail(absolute(minus) == 0.0);
        }

        return true;
}

test(absolute_clears_only_the_sign) {
        //      The sign bit and nothing else, over the bit patterns the
        //      values above never reach: a NaN keeps its payload, an infinity
        //      stays one, and the smallest subnormal and the largest finite
        //      lose only their signs. The mask body must agree with the bits
        //      of every one, not only with the arithmetic answer.
        unsigned long named[] = {0, 1, 0x8000000000000000ull, 0x8000000000000001ull,
                       0x7ff0000000000000ull, 0xfff0000000000000ull,
                       0x7ff8000000000001ull, 0xfff8000000000001ull,
                       0x0010000000000000ull, 0x8010000000000000ull,
                       0xffefffffffffffffull, 0x7fefffffffffffffull};
        unsigned long seed = 0x9e3779b97f4a7c15ull;

        for (positive at = 0; at < 4096; at++) {
                unsigned long pattern = at < sizeof(named) / sizeof(*named) ? named[at] : seed;
                decimal value, answer;
                unsigned long result;

                __builtin_memcpy(&value, &pattern, sizeof value);
                answer = absolute(value);
                __builtin_memcpy(&result, &answer, sizeof result);
                fail(result == (pattern & 0x7fffffffffffffffull));

                seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        }

        return true;
}

test(system_write_all_zero_and_fault) {
        fail_not_equals(system_write_all((positive)-1, address_bad, 0), 0);
        fail_not_equals(system_write_all((positive)-1, (address_any)"x", 1), 0);
        system_write_result result =
                system_write_all_checked((positive)-1, address_bad, 0);
        fail(result.bytes == 0 && result.error == 0);
        result = system_write_all_checked((positive)-1, (address_any)"x", 1);
        fail(result.bytes == 0 && result.error == -EBADF);

        return true;
}

/*
        A nonblocking pipe is a controllable positive-short-write followed by
        EAGAIN: offer more than its empty capacity, then leave it undrained
        while system_write_all runs. The answer must be the positive prefix,
        not the negative second result, and every reported byte must really
        be present in the pipe.
*/
test(system_write_all_partial_then_fault) {
        enum { F_GETPIPE_SZ = 1032 };
        b32 ends[2];
        p8 readback[4096];
        positive seen = 0;
        p8 correct = true;

        fail_not_equals(system_call_2(syscall(pipe2), (positive)ends, O_NONBLOCK), 0);

        bipolar capacity = system_call_2(syscall(fcntl), ends[1], F_GETPIPE_SZ);
        fail(capacity > 0);

        positive offered_length = (positive)capacity + sizeof(readback);
        bipolar mapped = system_call_6(syscall(mmap), 0, offered_length,
                                       FILE_PROTECT_READ | FILE_PROTECT_WRITE,
                                       FILE_MAP_PRIVATE | FILE_MAP_ANONYMOUS,
                                       (positive)-1, 0);
        fail(mapped >= 0);

        memory_fill((address_any)(positive)mapped, 0x5a, offered_length);

        positive wrote = system_write_all((positive)ends[1],
                                           (address_any)(positive)mapped,
                                           offered_length);

        if (!wrote || wrote >= offered_length)
                correct = false;

        while (correct && seen < wrote)
        {
                positive want = wrote - seen;

                if (want > sizeof(readback))
                        want = sizeof(readback);

                bipolar got = system_call_3(syscall(read), ends[0],
                                             (positive)readback, want);

                if (got <= 0)
                {
                        correct = false;
                        break;
                }

                for (positive i = 0; i < (positive)got; i++)
                        if (readback[i] != 0x5a)
                                correct = false;

                seen += (positive)got;
        }

        system_call_1(syscall(close), ends[0]);
        system_call_1(syscall(close), ends[1]);
        system_call_2(syscall(munmap), (positive)mapped, offered_length);

        fail(correct && seen == wrote);
        return true;
}

/*
        Linux caps one write syscall at MAX_RW_COUNT. /dev/null consumes no
        pages, so a 2 GiB anonymous mapping makes that cap force two positive
        writes without allocating or storing 2 GiB. Returning the whole span
        proves the loop advanced the pointer/count and retried after progress.
*/
test(system_write_all_retries_short_progress) {
        positive span = (positive)0x80000000u;
        bipolar mapped = system_call_6(syscall(mmap), 0, span, FILE_PROTECT_READ,
                                       FILE_MAP_PRIVATE | FILE_MAP_ANONYMOUS,
                                       (positive)-1, 0);
        fail(mapped >= 0);

        bipolar sink = system_call_4(syscall(openat), AT_FDCWD,
                                     (positive)"/dev/null", FILE_READ_WRITE, 0);

        if (sink < 0)
        {
                system_call_2(syscall(munmap), (positive)mapped, span);
                return false;
        }

        positive wrote = system_write_all((positive)sink,
                                           (address_any)(positive)mapped, span);
        system_write_result result = system_write_all_checked((positive)sink,
                                            (address_any)(positive)mapped, span);

        system_call_1(syscall(close), sink);
        system_call_2(syscall(munmap), (positive)mapped, span);

        fail_not_equals(wrote, span);
        fail(result.bytes == span && result.error == 0);
        return true;
}

#endif


test_case test_cases[] = {

        case_type_basics(p8),
        case_type_basics(b8),
        case_type_basics(p16),
        case_type_basics(b16),
        case_type_basics(p32),
        case_type_basics(b32),
        case_type_basics(p64),
        case_type_basics(b64),
#ifdef HAS_128
        case_type_basics(p128),
        case_type_basics(b128),
#endif
        case_type_decimal(f32),
        case_type_decimal(f64),
        case_type_decimal(f128),

        case(bit_flip_zero_to_one),
        case(bit_flip_one_to_zero),
        case(bit_set_basic),
        case(bit_clear_basic),
        case(bit_test_set_bit),
        case(bit_test_clear_bit),
        case(bit_set_high_bit),

        case(addresses),
        case(is_null),
        case(atomic_operations),

        case(memory_fill),
        case(memory_copy),

        case(string_length),
        case(string_compare),
        case(string_copy),
        case(string_copy_max),
        case(string_first_of),
        case(string_last_of),
        case(string_cut),
        case(string_replace_all),
        case(string_format_basic),
        case(string_format_numbers),
        case(string_format_mixed),

        case(path_basename),

        case(str_macro),

        case(string_end),

        case(writer_pattern),

        case(str_length),
        case(str_writes_no_terminator),

        case(string_length_padded),
        case(string_compare_padded),
        case(string_first_of_padded),
        case(string_table_find_lengths),

        case(stack_is_aligned),

#if LINUX
        case(syscall_argument_four),
        case(created_file_mode),
        case(byte_print_classes),
        case(byte_print_class_relations),
        case(byte_print_classes_beyond_a_byte),
        case(set_scans_every_byte_pair),
        case(set_scans_word_boundaries),
        case(set_scans_empty_arguments),
        case(set_scans_lengths_and_alignments),
        case(set_scans_member_alignments),
        case(string_first_of_set_answers_null_at_the_end),
        case(string_compare_folded_pairs),
        case(string_compare_folded_direction),
        case(string_compare_folded_shapes),
        case(string_compare_folded_every_byte),
        case(string_compare_folded_tiers),
        case(string_compare_folded_page_edge),
        case(string_compare_folded_max_bound),
        case(memory_copy_until),
        case(string_copy_end),
        case(string_copy_max_endptr),
        case(string_append_max),
        case(memory_zero),
        case(memory_copy_source_first),
        case(memory_last_of),
        case(string_to_number_every_byte),
        case(string_to_number_edges),
        case(string_to_number_end_pointer),
        case(string_to_number_long_runs),
        case(string_to_number_fuzz),
#if X64 || ARM64
        //      The narrow-base read is only a question where an int
        //      arrives without its top half defined, which lp64d on
        //      riscv64 does not do.
        case(string_to_number_reads_a_narrow_base),
#endif
        case(bit_counting_defined_at_zero),
        case(bit_counting_single_bits),
        case(bit_counting_noise_around_the_bit),
        case(bit_counting_rounding_at_the_top),
        case(bit_counting_every_sixteen_bit_word),
        case(bit_counting_patterns_and_random_words),
        case(decimal_truncated_sweep),
        case(decimal_floor_sweep),
        case(decimal_ceiling_sweep),
        case(decimal_rounded_sweep),
        case(decimal_nearest_sweep),
        case(decimal_with_sign_sweep),
        case(decimal_smaller_sweep),
        case(decimal_larger_sweep),
        case(decimal_difference_sweep),
        case(decimal_pairs_all),
        case(decimal_rounding_keeps_negative_zero),
        case(decimal_floor_and_ceiling_directions),
        case(decimal_rounded_ties_go_away_from_zero),
        case(decimal_rounded_just_below_a_half),
        case(decimal_nearest_ties_go_to_even),
        case(decimal_rounding_leaves_the_large_alone),
        case(decimal_rounding_of_a_not_a_number),
        case(decimal_with_sign_corners),
        case(decimal_selection_of_a_not_a_number),
        case(decimal_selection_ordinary),
        case(decimal_selection_of_the_two_zeros),
        case(decimal_selection_of_a_signalling_not_a_number),
        case(decimal_difference_corners),
        case(decimal_multiply_add_rounds_once),
        case(decimal_multiply_add_ordinary),
        case(decimal_multiply_add_matches_exact_products),
        case(decimal_multiply_add_bodies_agree),
        case(narrow_rounding_over_every_exponent),
        case(narrow_rounding_across_the_deciding_binades),
        case(narrow_rounding_named_values),
        case(narrow_absolute_clears_only_the_sign),
        case(narrow_with_sign_moves_only_the_sign),
        case(narrow_smaller_and_larger),
        case(narrow_square_root_is_exact),
        case(byte_classes),
        case(byte_class_table),
        case(byte_case),
        case(memory_ascii_case),
        case(memory_hash_33),
        case(memory_span_byte),
        case(memory_span_byte_reverse),
        case(absolute_whole_and_wide),
        case(square_root_is_exact),
        case(absolute_value),
        case(absolute_clears_only_the_sign),
        case(system_write_all_zero_and_fault),
        case(system_write_all_partial_then_fault),
        case(system_write_all_retries_short_progress),
#endif

        {null, null},
};

// Negative until generate_report opens it, so a stray write cannot land on
// descriptor 0.
bipolar report_file = -1;

// Relative to the working directory the runner is launched from.
const_string report_path = (const_string) "docs/index.html";

fn report_writer(address_any data, positive length)
{
        if (length == 0)
                length = string_length(data);

        system_call_3(syscall(write), report_file, (positive)data, length);
}

fn generate_report(writer write)
{
        // This used to redeclare report_file, shadowing the global that
        // report_writer actually writes through -- so even when the open
        // succeeded every write went to descriptor 0 instead of the report.
        report_file = system_call_4(syscall(openat), AT_FDCWD, (positive)report_path, FILE_CREATE | FILE_WRITE | O_TRUNC, 0666);

        if (report_file < 0) {
                // Writing the report is optional; the pass/fail summary above
                // is the actual test result, so this is a note, not a failure.
                log_direct(str("(no HTML report written; set REPORT_PATH target directory to enable)\n"));
                return;
        }

        write(str("<html><head><title>C library tests</title></head>"));
        write(str("<body><h1>C library tests</h1>"));

        // table
        write(str("<table border=\"1\"><tr><th>Test</th><th>Result</th></tr>"));

        test_case address_to test = test_cases;

        while (test->name)
        {
                write(str("<tr><td>"));
                write(test->name, 0);
                write(str("</td><td>"));

                if (test->result) {
                        write(str("Passed"));
                } else {
                        write(str("Failed"));
                }

                write(str("</td></tr>"));
                test++;
        }
        write(str("</table>"));
        write(str("</body></html>"));

        system_call_1(syscall(close), report_file);
}

b32 main()
{
        log_direct(str("C library tests\n\n"));

        test_cases_walk(test_cases);

        //      The one suite that says passed and failed where the others say
        //      checks and failures, which is why this line is not test_report.
        string_format(log, "\n%p passed, %p failed\n", checks - failures,
                      failures);

        log_flush();

        #if LINUX
        generate_report(report_writer);
        #endif

        return failures > 0 ? 1 : 0;
}
#endif /* CHECK_standard */

