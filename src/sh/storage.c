/*
        Everything this shell knows about a block device.

        Four things that were four files and are one system: what a device
        is, what is mounted, how to mount it, and how to lay a fresh one
        out. They were already compiled one after the other into the same
        unit, in this order, and each of their headers described itself by
        pointing at the others -- "the storage family", "the table loaders
        above are public to the rest of it". A family that
        has to introduce itself in four places is one file.

        None of them owns command dispatch. A shell builtin and a multicall
        name hand the same argc/argv to the runners at the bottom of each
        section, and mount reaches past their presentation entirely.
*/

/* ---- Block identity probing, shared by blkid, findfs and mount. ---- */

/*
        Block identity probing shared by blkid, findfs and mount.

        This file deliberately owns no command dispatch.  A shell builtin and
        a multicall name both hand the same argc/argv to the runners at the
        bottom; mount can bypass their presentation entirely and use
        storage_probe_device or storage_resolve_tag.

        Probing is bounded.  The common first 4 KiB, the Btrfs superblock and
        the ISO volume descriptor are the only unconditional reads.  exFAT's
        root directory costs one further sector, and only after its geometry
        has passed overflow and range checks.  A short read never leaves old
        bytes behind and no recogniser examines beyond the returned count.
*/

#define STORAGE_PATH_ROOM 4096
#define STORAGE_TYPE_ROOM 16
#define STORAGE_UUID_ROOM 40
#define STORAGE_LABEL_ROOM 257
#define STORAGE_PARTUUID_ROOM 40
#define STORAGE_PARTLABEL_ROOM 257
#define STORAGE_PROBE_ROOM 4096
#define STORAGE_DEVICE_BLOCK 4096

typedef struct
{
        string_address path;
        p8 type[STORAGE_TYPE_ROOM];
        p8 uuid[STORAGE_UUID_ROOM];
        p8 label[STORAGE_LABEL_ROOM];
        p8 partuuid[STORAGE_PARTUUID_ROOM];
        p8 partlabel[STORAGE_PARTLABEL_ROOM];
        positive type_length;
        positive uuid_length;
        positive label_length;
        positive partuuid_length;
        positive partlabel_length;
} storage_identity;

/* A recogniser may expose its exact magic location without teaching the
   presentation commands how to recognise a filesystem a second time. */
typedef struct
{
        storage_identity identity;
        p64 offset;
        p8 length;
        p8 magic[10];
        string_address usage;
} storage_signature;

typedef bool (*storage_signature_visitor)(
    storage_signature address_to signature, address_any context);

typedef struct
{
        p32 sectors;
        p16 sector_size;
} storage_iso_volume;

typedef struct
{
        string_address tag;
        positive tag_length;
        string_address value;
} storage_selector;

#define STORAGE_SHOW_LABEL     1
#define STORAGE_SHOW_UUID      2
#define STORAGE_SHOW_TYPE      4
#define STORAGE_SHOW_PARTLABEL 8
#define STORAGE_SHOW_PARTUUID  16
#define STORAGE_SHOW_DEVNAME   32

/* One tag schema drives selection and blkid's output mask.  DEVNAME is the
   sole direct pointer; the others name an inline byte array and its length. */
typedef struct
{
        string_address name;
        p8 length;
        p8 show;
        p16 value_offset;
        p16 length_offset;
} storage_tag_descriptor;

#define STORAGE_TAG(name, member, length_member, flag)                      \
        {(string_address)#name, sizeof(#name) - 1, STORAGE_SHOW_##flag,      \
         __builtin_offsetof(storage_identity, member),                      \
         __builtin_offsetof(storage_identity, length_member)}
static const storage_tag_descriptor storage_tags[] = {
    STORAGE_TAG(LABEL, label, label_length, LABEL),
    STORAGE_TAG(UUID, uuid, uuid_length, UUID),
    STORAGE_TAG(TYPE, type, type_length, TYPE),
    STORAGE_TAG(PARTLABEL, partlabel, partlabel_length, PARTLABEL),
    STORAGE_TAG(PARTUUID, partuuid, partuuid_length, PARTUUID),
    {(string_address)"DEVNAME", 7, STORAGE_SHOW_DEVNAME, 0, 0},
};
#undef STORAGE_TAG

#define storage_le16(at) memory_load_unaligned(p16, (at))
#define storage_le32(at) memory_load_unaligned(p32, (at))
#define storage_le64(at) memory_load_unaligned(p64, (at))

#define storage_be16 network_load_16
#define storage_be32 network_load_32

static p64 storage_be64(p8 address_to at)
{
        return ((p64)network_load_32(at) << 32) |
               network_load_32(at + 4);
}

static bool storage_bytes(p8 address_to bytes, positive have,
                          positive at, p8 address_to wanted,
                          positive length)
{
        return at <= have && length <= have - at &&
               !memory_compare(bytes + at, wanted, length);
}

/* pread64 is one 64-bit argument on every architecture Moonwater supports. */
static positive storage_read(bipolar handle, p8 address_to bytes,
                             positive room, p64 offset)
{
        positive used = 0;

        while (used < room)
        {
                bipolar got = system_call_4(syscall(pread64), (positive)handle,
                                            (positive)(bytes + used),
                                            room - used,
                                            (positive)(offset + used));

                if (got == -4) /* EINTR */
                        continue;
                if (got <= 0)
                        break;
                used += (positive)got;
        }

        /* Only unread bytes need padding. A complete read already overwrote
           every byte; clearing the whole probe first doubled the stores and
           faulted fresh pages before the kernel filled them. */
        if (used < room)
                memory_zero(bytes + used, room - used);
        return used;
}

static bipolar storage_write(bipolar handle, p8 address_to bytes,
                             positive length, p64 offset)
{
        return file_transfer_exact(syscall(pwrite64), handle, bytes, length, offset);
}

static fn storage_trimmed(p8 address_to into, positive room,
                          positive address_to length,
                          p8 address_to source, positive size,
                          bool trim_space)
{
        positive used = memory_span_without_byte(source, 0, size);

        while (used && (source[used - 1] == 0 ||
                        (trim_space && source[used - 1] == ' ')))
                used--;

        if (room)
        {
                positive copied = used < room - 1 ? used : room - 1;

                memory_copy(into, source, copied);
                into[copied] = end;
                address_to length = copied;
        }
        else
                address_to length = 0;
}

static fn storage_uuid_bytes(p8 address_to into,
                             p8 address_to bytes)
{
        p8 digits[32];
        memory_into_hex(digits, bytes, 16);
        memory_copy(into, digits, 8);
        memory_copy(into + 9, digits + 8, 4);
        memory_copy(into + 14, digits + 12, 4);
        memory_copy(into + 19, digits + 16, 4);
        memory_copy(into + 24, digits + 20, 12);
        into[8] = into[13] = into[18] = into[23] = '-';
        into[36] = end;
}

static positive storage_hex_padded(p8 address_to into, positive value,
                                    positive width, bool upper)
{
        p8 digits[2 * sizeof(positive)];
        positive count = positive_into_base(digits, value, 16, upper);
        positive padding = difference_or_zero(width, count);

        memory_fill(into, '0', padding);
        memory_copy(into + padding, digits, count);
        into[padding + count] = end;
        return padding + count;
}

/* Fourteen recognisers name their type and six copy a sixteen-byte UUID;
   each spelled the three-argument store and the length by hand. */
static fn storage_set_type(storage_identity address_to identity,
                           string_address type)
{
        p8 address_to stop = string_copy_max_end(
            identity->type, type, sizeof(identity->type) - 1);
        identity->type_length = (positive)(stop - identity->type);
}

static fn storage_set_uuid(storage_identity address_to identity,
                           p8 address_to bytes)
{
        storage_uuid_bytes(identity->uuid, bytes);
        identity->uuid_length = 36;
}

static fn storage_uuid_fat(p8 address_to into, p32 serial)
{
        storage_hex_padded(into, (positive)(serial >> 16), 4, true);
        into[4] = '-';
        storage_hex_padded(into + 5, (positive)(serial & 0xffff), 4, true);
}

static bool storage_power_of_two(positive value)
{
        return value && !(value & (value - 1));
}

static bool storage_log_matches(positive value, positive logarithm)
{
        return logarithm < positive_bits &&
               value == ((positive)1 << logarithm);
}

static bool storage_uuid_present(p8 address_to uuid)
{
        return memory_count(uuid, 16, 0) != 16;
}

static bool storage_header_word(p8 address_to word, positive room)
{
        positive at = 0;

        while (at < room && word[at])
        {
                if (word[at] < 0x21 || word[at] > 0x7e)
                        return false;
                at++;
        }

        return at && at < room;
}

static bool storage_uuid_header(p8 address_to uuid, positive room)
{
        if (room < 37 || uuid[36])
                return false;

        for (positive at = 0; at < 36; at++)
        {
                p8 byte = uuid[at];

                if (at == 8 || at == 13 || at == 18 || at == 23)
                {
                        if (byte != '-')
                                return false;
                }
                else if (digit_known(byte, 16) >= 16)
                        return false;
        }

        return true;
}

static bool storage_utf8_add(p8 address_to into, positive room,
                             positive address_to out, positive codepoint)
{
        if (*out >= room)
                return false;
        positive made = memory_utf8_encode(into + *out, room - 1 - *out,
                                           codepoint);
        *out += made;
        return made != 0;
}

static fn storage_utf16_label(p8 address_to into, positive room,
                              positive address_to length,
                              p8 address_to source, positive characters)
{
        positive out = 0;

        for (positive at = 0; at < characters; at++)
        {
                p16 character = storage_le16(source + at * 2);
                positive codepoint = character;

                if (!character)
                        continue;
                if (character >= 0xd800 && character <= 0xdbff)
                {
                        p16 low;

                        if (at + 1 >= characters)
                                continue;
                        low = storage_le16(source + (at + 1) * 2);
                        if (low < 0xdc00 || low > 0xdfff)
                                continue;
                        codepoint = 0x10000 +
                                    (((positive)character - 0xd800) << 10) +
                                    ((positive)low - 0xdc00);
                        at++;
                }
                else if (character >= 0xdc00 && character <= 0xdfff)
                        continue;

                if (!storage_utf8_add(into, room, address_of out, codepoint))
                        break;
        }

        if (room)
                into[out] = end;
        address_to length = out;
}

static fn storage_probe_ext(storage_identity address_to identity,
                            p8 address_to bytes, positive have)
{
        p32 compatible;
        p32 incompatible;

        if (have < 1024 + 256 || storage_le16(bytes + 1024 + 56) != 0xef53)
                return;

        compatible = storage_le32(bytes + 1024 + 92);
        incompatible = storage_le32(bytes + 1024 + 96);

        if (incompatible & (0x40 | 0x80 | 0x200))
                storage_set_type(identity, (string_address)"ext4");
        else if (compatible & 0x4)
                storage_set_type(identity, (string_address)"ext3");
        else
                storage_set_type(identity, (string_address)"ext2");

        storage_set_uuid(identity, bytes + 1024 + 104);
        storage_trimmed(identity->label, sizeof(identity->label),
                        address_of identity->label_length,
                        bytes + 1024 + 120, 16, false);
}

static fn storage_probe_fat(storage_identity address_to identity,
                            p8 address_to bytes, positive have,
                            p64 address_to signature_offset)
{
        positive sector;
        bool fat32;
        positive type_at;
        positive serial_at;
        positive label_at;

        if (have < 512 || bytes[510] != 0x55 || bytes[511] != 0xaa)
                return;

        sector = storage_le16(bytes + 11);
        if (!storage_power_of_two(sector) || sector < 512 || sector > 4096 ||
            !storage_power_of_two(bytes[13]))
                return;

        fat32 = storage_bytes(bytes, have, 82, (p8 address_to)"FAT32   ", 8);
        type_at = fat32 ? 82 : 54;

        if (!fat32 && !storage_bytes(bytes, have, type_at,
                                     (p8 address_to)"FAT", 3))
                return;

        serial_at = fat32 ? 67 : 39;
        label_at = fat32 ? 71 : 43;
        if (signature_offset)
                *signature_offset = storage_bytes(bytes, have, 82,
                    (p8 address_to)"FAT32", 5) ? 82 : 54;
        storage_set_type(identity, (string_address)"vfat");
        storage_uuid_fat(identity->uuid, storage_le32(bytes + serial_at));
        identity->uuid_length = 9;
        storage_trimmed(identity->label, sizeof(identity->label),
                        address_of identity->label_length,
                        bytes + label_at, 11, true);

        if (string_equals(identity->label, "NO NAME"))
        {
                identity->label[0] = end;
                identity->label_length = 0;
        }
}

static fn storage_probe_exfat_label(bipolar handle,
                                    storage_identity address_to identity,
                                    p8 address_to boot, positive have)
{
        p8 sector[STORAGE_PROBE_ROOM];
        positive sector_shift;
        positive cluster_shift;
        p64 heap;
        p32 root;
        p64 sector_number;
        p64 offset;
        positive got;

        if (have < 120)
                return;

        sector_shift = boot[108];
        cluster_shift = boot[109];
        heap = storage_le32(boot + 88);
        root = storage_le32(boot + 96);

        if (sector_shift < 9 || sector_shift > 12 || cluster_shift > 25 ||
            root < 2 || (p64)(root - 2) > ((p64)-1 >> cluster_shift))
                return;

        sector_number = heap + ((p64)(root - 2) << cluster_shift);
        if (sector_number < heap || sector_number > ((p64)bipolar_max >> sector_shift))
                return;

        offset = sector_number << sector_shift;
        got = storage_read(handle, sector, (positive)1 << sector_shift, offset);

        for (positive at = 0; at <= got && 32 <= got - at; at += 32)
        {
                positive letters;

                if (sector[at] == 0)
                        break;
                if (sector[at] != 0x83)
                        continue;

                letters = sector[at + 1];
                if (letters > 15)
                        letters = 15;
                storage_utf16_label(identity->label, sizeof(identity->label),
                                    address_of identity->label_length,
                                    sector + at + 2, letters);
                break;
        }
}

static fn storage_probe_exfat(bipolar handle,
                              storage_identity address_to identity,
                              p8 address_to bytes, positive have)
{
        p32 fat_at;
        p32 fat_length;
        p32 heap_at;
        p32 clusters;
        p32 root;
        positive fats;
        positive sector_shift;
        positive cluster_shift;

        if (have < 512 || !storage_bytes(bytes, have, 0,
                                         (p8 address_to)"\xeb\x76\x90", 3) ||
            !storage_bytes(bytes, have, 3,
                           (p8 address_to)"EXFAT   ", 8) ||
            memory_count(bytes + 11, 53, 0) != 53 ||
            storage_le16(bytes + 510) != 0xaa55)
                return;

        fat_at = storage_le32(bytes + 80);
        fat_length = storage_le32(bytes + 84);
        heap_at = storage_le32(bytes + 88);
        clusters = storage_le32(bytes + 92);
        root = storage_le32(bytes + 96);
        sector_shift = bytes[108];
        cluster_shift = bytes[109];
        fats = bytes[110];

        if (sector_shift < 9 || sector_shift > 12 ||
            cluster_shift > 25 - sector_shift || fats < 1 || fats > 2 ||
            fat_at < 24 || !fat_length || !clusters || root < 2 ||
            (p64)root > (p64)clusters + 1 ||
            (p64)fat_at + (p64)fat_length * fats > heap_at)
                return;

        storage_set_type(identity, (string_address)"exfat");
        storage_uuid_fat(identity->uuid, storage_le32(bytes + 100));
        identity->uuid_length = 9;
        storage_probe_exfat_label(handle, identity, bytes, have);
}

static fn storage_probe_ntfs(storage_identity address_to identity,
                             p8 address_to bytes, positive have)
{
        positive sector_size;
        positive sectors_per_cluster;
        p64 sectors;
        p64 mft_cluster;

        if (have < 512 || !storage_bytes(bytes, have, 3,
                                         (p8 address_to)"NTFS    ", 8) ||
            (bytes[0] != 0xeb && bytes[0] != 0xe9) ||
            storage_le16(bytes + 510) != 0xaa55)
                return;

        sector_size = storage_le16(bytes + 11);
        sectors_per_cluster = bytes[13];
        sectors = storage_le64(bytes + 40);
        mft_cluster = storage_le64(bytes + 48);
        if (!storage_power_of_two(sector_size) || sector_size < 256 ||
            sector_size > STORAGE_PROBE_ROOM ||
            !storage_power_of_two(sectors_per_cluster) ||
            sectors_per_cluster > 128 ||
            !sectors || mft_cluster >= sectors / sectors_per_cluster)
                return;

        storage_set_type(identity, (string_address)"ntfs");
        storage_hex_padded(identity->uuid, (positive)storage_le64(bytes + 72),
                           16, true);
        identity->uuid_length = 16;
}

static fn storage_probe_ntfs_label(bipolar handle,
                                   storage_identity address_to identity,
                                   p8 address_to boot, positive have)
{
        p8 record[STORAGE_PROBE_ROOM];
        positive sector_size;
        positive sectors_per_cluster;
        positive cluster_size;
        p8 record_code;
        positive record_size;
        p64 mft_cluster;
        p64 mft_offset;
        p64 volume_offset;
        positive got;
        positive usa_at;
        positive usa_count;
        p16 usa_value;
        positive attribute_at;
        positive used;

        if (have < 80)
                return;

        sector_size = storage_le16(boot + 11);
        sectors_per_cluster = boot[13];
        if (!storage_power_of_two(sector_size) || sector_size < 256 ||
            sector_size > STORAGE_PROBE_ROOM ||
            !storage_power_of_two(sectors_per_cluster) ||
            sectors_per_cluster > STORAGE_PROBE_ROOM / sector_size)
                return;

        cluster_size = sector_size * sectors_per_cluster;
        record_code = boot[64];
        if (record_code & 0x80)
        {
                positive shift = 256 - record_code;

                if (shift > 12)
                        return;
                record_size = (positive)1 << shift;
        }
        else
        {
                if (!record_code || record_code > STORAGE_PROBE_ROOM / cluster_size)
                        return;
                record_size = cluster_size * record_code;
        }

        if (record_size < 128 || record_size > sizeof(record))
                return;

        mft_cluster = storage_le64(boot + 48);
        if (mft_cluster > (p64)bipolar_max / cluster_size)
                return;
        mft_offset = mft_cluster * cluster_size;
        if (mft_offset > (p64)bipolar_max - 3 * record_size)
                return;
        volume_offset = mft_offset + 3 * record_size;
        got = storage_read(handle, record, record_size, volume_offset);

        if (got < 32 || !storage_bytes(record, got, 0,
                                       (p8 address_to)"FILE", 4))
                return;

        usa_at = storage_le16(record + 4);
        usa_count = storage_le16(record + 6);
        if (usa_count < 2 || usa_count > record_size / sector_size + 1 ||
            usa_at > got || usa_count * 2 > got - usa_at)
                return;

        usa_value = storage_le16(record + usa_at);
        for (positive which = 1; which < usa_count; which++)
        {
                positive end_at = which * sector_size - 2;

                if (end_at + 2 > got || storage_le16(record + end_at) != usa_value)
                        return;
                memory_copy(record + end_at, record + usa_at + which * 2, 2);
        }

        attribute_at = storage_le16(record + 20);
        used = storage_le32(record + 24);
        if (used > got)
                used = got;

        while (attribute_at <= used && 24 <= used - attribute_at)
        {
                p32 type = storage_le32(record + attribute_at);
                positive length = storage_le32(record + attribute_at + 4);

                if (type == 0xffffffff)
                        break;
                if (length < 24 || length > used - attribute_at)
                        break;

                if (type == 0x60 && !record[attribute_at + 8])
                {
                        positive value_length =
                            storage_le32(record + attribute_at + 16);
                        positive value_at =
                            storage_le16(record + attribute_at + 20);

                        if (value_at <= length && value_length <= length - value_at)
                                storage_utf16_label(
                                    identity->label, sizeof(identity->label),
                                    address_of identity->label_length,
                                    record + attribute_at + value_at,
                                    value_length / 2);
                        return;
                }

                attribute_at += length;
        }
}

static fn storage_probe_erofs(storage_identity address_to identity,
                              p8 address_to bytes, positive have)
{
        if (have < 1024 + 80 || storage_le32(bytes + 1024) != 0xe0f5e1e2)
                return;

        storage_set_type(identity, (string_address)"erofs");
        storage_set_uuid(identity, bytes + 1024 + 48);
        storage_trimmed(identity->label, sizeof(identity->label),
                        address_of identity->label_length,
                        bytes + 1024 + 64, 16, false);
}

/* XFS stores its primary superblock in big endian at byte zero.  The geometry
   cross-checks are intentional: XFSB alone is a four-byte file signature, not
   enough authority to choose a filesystem driver for mount. */
static fn storage_probe_xfs(storage_identity address_to identity,
                            p8 address_to bytes, positive have)
{
        positive block_size;
        positive sector_size;
        positive inode_size;
        positive ag_blocks;
        positive ag_count;
        p64 data_blocks;
        positive version;

        if (have < 208 ||
            !storage_bytes(bytes, have, 0, (p8 address_to)"XFSB", 4))
                return;

        block_size = storage_be32(bytes + 4);
        data_blocks = storage_be64(bytes + 8);
        ag_blocks = storage_be32(bytes + 84);
        ag_count = storage_be32(bytes + 88);
        version = storage_be16(bytes + 100) & 15;
        sector_size = storage_be16(bytes + 102);
        inode_size = storage_be16(bytes + 104);

        if (!storage_power_of_two(block_size) || block_size < 512 ||
            block_size > 65536 || !storage_power_of_two(sector_size) ||
            sector_size < 512 || sector_size > block_size ||
            !storage_power_of_two(inode_size) || inode_size < 256 ||
            inode_size > block_size ||
            storage_be16(bytes + 106) != block_size / inode_size ||
            !storage_log_matches(block_size, bytes[120]) ||
            !storage_log_matches(sector_size, bytes[121]) ||
            !storage_log_matches(inode_size, bytes[122]) ||
            !data_blocks || !ag_blocks || !ag_count ||
            (version != 4 && version != 5) ||
            (p64)(ag_count - 1) * ag_blocks >= data_blocks ||
            data_blocks > (p64)ag_count * ag_blocks ||
            !storage_uuid_present(bytes + 32))
                return;

        storage_set_type(identity, (string_address)"xfs");
        storage_set_uuid(identity, bytes + 32);
        storage_trimmed(identity->label, sizeof(identity->label),
                        address_of identity->label_length,
                        bytes + 108, 12, true);
}

/* F2FS keeps two copies; the first begins at 1024 and is wholly inside the
   common probe.  Address ordering and segment accounting reject a magic-only
   block while allowing feature growth after the fixed header. */
static fn storage_probe_f2fs(storage_identity address_to identity,
                             p8 address_to bytes, positive have)
{
        p8 address_to super = bytes + 1024;
        positive sector_log;
        positive sectors_per_block_log;
        positive block_log;
        positive blocks_per_segment_log;
        p64 segment_parts = 0;
        p64 block_count;
        positive segment_count;
        positive main_address;

        if (have < 1024 + 1148 || storage_le32(super) != 0xf2f52010)
                return;

        sector_log = storage_le32(super + 8);
        sectors_per_block_log = storage_le32(super + 12);
        block_log = storage_le32(super + 16);
        blocks_per_segment_log = storage_le32(super + 20);
        block_count = storage_le64(super + 36);
        segment_count = storage_le32(super + 48);
        main_address = storage_le32(super + 92);

        for (positive at = 0; at < 5; at++)
                segment_parts += storage_le32(super + 52 + at * 4);

        if (storage_le16(super + 4) < 1 || sector_log < 9 ||
            sector_log > 12 || sectors_per_block_log > 3 ||
            sector_log + sectors_per_block_log != block_log ||
            block_log != 12 || blocks_per_segment_log != 9 ||
            !storage_le32(super + 24) || !storage_le32(super + 28) ||
            !block_count || !segment_count || segment_parts > segment_count ||
            storage_le32(super + 72) > storage_le32(super + 76) ||
            storage_le32(super + 76) >= storage_le32(super + 80) ||
            storage_le32(super + 80) >= storage_le32(super + 84) ||
            storage_le32(super + 84) >= storage_le32(super + 88) ||
            storage_le32(super + 88) >= main_address ||
            main_address >= block_count ||
            storage_le32(super + 96) != 3 ||
            storage_le32(super + 100) != 1 ||
            storage_le32(super + 104) != 2 ||
            !storage_uuid_present(super + 108))
                return;

        storage_set_type(identity, (string_address)"f2fs");
        storage_set_uuid(identity, super + 108);
        storage_utf16_label(identity->label, sizeof(identity->label),
                            address_of identity->label_length,
                            super + 124, 512);
}

static bool storage_squashfs_table(p64 offset, p64 bytes_used)
{
        return offset == (p64)-1 ||
               (offset >= 96 && offset < bytes_used);
}

static fn storage_probe_squashfs(storage_identity address_to identity,
                                 p8 address_to bytes, positive have)
{
        positive block_size;
        positive block_log;
        p64 bytes_used;

        if (have < 96 || storage_le32(bytes) != 0x73717368)
                return;

        block_size = storage_le32(bytes + 12);
        block_log = storage_le16(bytes + 22);
        bytes_used = storage_le64(bytes + 40);

        if (!storage_le32(bytes + 4) ||
            !storage_power_of_two(block_size) || block_size < 4096 ||
            block_size > (1 << 20) ||
            !storage_log_matches(block_size, block_log) ||
            storage_le16(bytes + 20) < 1 ||
            storage_le16(bytes + 20) > 6 ||
            storage_le16(bytes + 28) != 4 || storage_le16(bytes + 30) != 0 ||
            bytes_used < 96 ||
            !storage_squashfs_table(storage_le64(bytes + 48), bytes_used) ||
            !storage_squashfs_table(storage_le64(bytes + 56), bytes_used) ||
            !storage_squashfs_table(storage_le64(bytes + 64), bytes_used) ||
            !storage_squashfs_table(storage_le64(bytes + 72), bytes_used) ||
            !storage_squashfs_table(storage_le64(bytes + 80), bytes_used) ||
            !storage_squashfs_table(storage_le64(bytes + 88), bytes_used) ||
            storage_le64(bytes + 64) == (p64)-1 ||
            storage_le64(bytes + 72) == (p64)-1)
                return;

        storage_set_type(identity, (string_address)"squashfs");
}

static bool storage_udf_tag(p8 address_to descriptor, positive have,
                            positive identifier, positive location)
{
        positive checksum = 0;

        if (have < 16 || storage_le16(descriptor) != identifier ||
            storage_le16(descriptor + 2) < 2 ||
            storage_le32(descriptor + 12) != location)
                return false;

        // The tag's own checksum byte is left out of the sum over the tag.
        checksum = memory_sum_bytes(descriptor, 16) - descriptor[4];

        return (p8)checksum == descriptor[4];
}

static fn storage_udf_dstring(p8 address_to into, positive room,
                              positive address_to length,
                              p8 address_to text, positive field_size)
{
        positive encoded;
        positive out = 0;

        address_to length = 0;

        if (!room || field_size < 2)
                return;

        encoded = text[field_size - 1];
        if (encoded < 2 || encoded > field_size - 1)
        {
                into[0] = end;
                return;
        }

        if (text[0] == 8)
        {
                for (positive at = 1; at < encoded; at++)
                {
                        if (!text[at])
                                continue;
                        if (!storage_utf8_add(into, room, address_of out,
                                              text[at]))
                                break;
                }
        }
        else if (text[0] == 16)
        {
                for (positive at = 1; at + 1 < encoded; at += 2)
                {
                        positive codepoint = storage_be16(text + at);

                        if (!codepoint)
                                continue;
                        if (!storage_utf8_add(into, room, address_of out,
                                              codepoint))
                                break;
                }
        }

        into[out] = end;
        address_to length = out;
}

static bool storage_udf_metadata(bipolar handle,
                                 storage_identity address_to identity)
{
        p8 descriptor[4096];
        positive block_sizes[] = {512, 1024, 2048, 4096};

        for (positive which = 0;
             which < array_count(block_sizes); which++)
        {
                positive block = block_sizes[which];
                positive have = storage_read(handle, descriptor, block,
                                             (p64)256 * block);
                positive extent_length;
                positive extent_location;

                if (!storage_udf_tag(descriptor, have, 2, 256))
                        continue;

                extent_length = storage_le32(descriptor + 16);
                extent_location = storage_le32(descriptor + 20);

                if (extent_length < block || extent_length % block ||
                    !extent_location ||
                    (p64)extent_location > (p64)bipolar_max / block)
                        continue;

                have = storage_read(handle, descriptor, block,
                                    (p64)extent_location * block);

                if (!storage_udf_tag(descriptor, have, 1,
                                     extent_location) || have < 200)
                        continue;

                storage_udf_dstring(identity->label,
                                    sizeof(identity->label),
                                    address_of identity->label_length,
                                    descriptor + 24, 32);

                {
                        p8 volume_set[STORAGE_LABEL_ROOM];
                        positive volume_set_length = 0;

                        storage_udf_dstring(volume_set, sizeof(volume_set),
                                            address_of volume_set_length,
                                            descriptor + 72, 128);

                        if (volume_set_length >= 16)
                        {
                                bool hexadecimal = true;

                                for (positive at = 0; at < 16; at++)
                                        if (digit_known(volume_set[at], 16) == 16)
                                                hexadecimal = false;

                                if (hexadecimal)
                                {
                                        memory_copy(identity->uuid,
                                                    volume_set, 16);
                                        identity->uuid[16] = end;
                                        identity->uuid_length = 16;
                                }
                        }
                }

                return true;
        }

        return false;
}

/* The UDF Volume Recognition Sequence is made of 2048-byte ECMA-167
   structures beginning at sector 16.  Walk a bounded sixteen descriptors and
   require the complete BEA -> NSR -> TEA order, not an isolated NSR word. */
static fn storage_probe_udf(bipolar handle,
                            storage_identity address_to identity,
                            p64 address_to signature_offset)
{
        p8 descriptor[2048];
        bool beginning = false;
        bool namespace_seen = false;

        for (positive sector = 16; sector < 32; sector++)
        {
                positive have = storage_read(handle, descriptor,
                                             sizeof(descriptor), sector * 2048);
                string_address name;

                if (have != sizeof(descriptor) || descriptor[6] != 1)
                        return;

                name = descriptor + 1;

                /*
                        A bridge disc -- what mkisofs -udf and every DVD
                        writes -- puts the ISO 9660 descriptors first, and
                        those carry their type in byte 0 where an ECMA-167
                        structure has a zero. They are stepped over, as
                        libblkid steps over them, and only the three UDF
                        words are held to the zero: refusing the sequence at
                        its first descriptor named every such disc iso9660.
                */
                if (memory_is_5(name, 'C', 'D', '0', '0', '1'))
                        continue;

                if (descriptor[0])
                        return;

                if (memory_is_5(name, 'B', 'E', 'A', '0', '1'))
                {
                        if (beginning || namespace_seen)
                                return;
                        beginning = true;
                        if (signature_offset)
                                *signature_offset = sector * 2048 + 1;
                }
                else if (memory_is_5(name, 'N', 'S', 'R', '0', '2') ||
                         memory_is_5(name, 'N', 'S', 'R', '0', '3'))
                {
                        if (!beginning || namespace_seen)
                                return;
                        namespace_seen = true;
                }
                else if (memory_is_5(name, 'T', 'E', 'A', '0', '1'))
                {
                        if (!beginning || !namespace_seen ||
                            !storage_udf_metadata(handle, identity))
                                return;

                        storage_set_type(identity, (string_address)"udf");
                        return;
                }
                else if (!memory_is_5(name, 'B', 'O', 'O', 'T', '2'))
                        return;
        }
}

static fn storage_probe_luks(storage_identity address_to identity,
                             p8 address_to bytes, positive have)
{
        static const p8 magic[6] = {'L', 'U', 'K', 'S', 0xba, 0xbe};
        positive version;

        if (have < 592 || memory_compare(bytes, magic, sizeof(magic)))
                return;

        version = storage_be16(bytes + 6);

        if (version == 1)
        {
                bool slots_valid = true;

                if (!storage_header_word(bytes + 8, 32) ||
                    !storage_header_word(bytes + 40, 32) ||
                    !storage_header_word(bytes + 72, 32) ||
                    !storage_be32(bytes + 104) ||
                    !storage_be32(bytes + 108) ||
                    storage_be32(bytes + 108) > 4096 ||
                    !storage_be32(bytes + 164) ||
                    !storage_uuid_header(bytes + 168, 40))
                        return;

                for (positive slot = 0; slot < 8; slot++)
                {
                        p8 address_to key = bytes + 208 + slot * 48;
                        p32 active = storage_be32(key);

                        if (active == 0x00ac71f3)
                        {
                                if (!storage_be32(key + 4) ||
                                    !storage_be32(key + 40) ||
                                    !storage_be32(key + 44))
                                        slots_valid = false;
                        }
                        else if (active != 0x0000dead)
                                slots_valid = false;
                }

                if (!slots_valid)
                        return;
        }
        else if (version == 2)
        {
                p64 header_size = storage_be64(bytes + 8);

                if (header_size < 16384 || header_size > (16 << 20) ||
                    (header_size & 4095) || !storage_be64(bytes + 16) ||
                    !storage_header_word(bytes + 72, 32) ||
                    memory_count(bytes + 104, 64, 0) == 64 ||
                    !storage_uuid_header(bytes + 168, 40) ||
                    storage_be64(bytes + 256) != 0)
                        return;
        }
        else
                return;

        storage_set_type(identity, (string_address)"crypto_LUKS");
        storage_trimmed(identity->uuid, sizeof(identity->uuid),
                        address_of identity->uuid_length,
                        bytes + 168, 40, false);

        if (version == 2)
                storage_trimmed(identity->label, sizeof(identity->label),
                                address_of identity->label_length,
                                bytes + 24, 48, false);
}

static bool storage_swap_magic(bipolar handle, p8 address_to bytes,
                               positive have, p64 address_to offset,
                               bool address_to modern)
{
        p8 tail[10];
        static const positive page_sizes[] = {
            2048, 4096, 8192, 16384, 32768, 65536, 131072,
        };

        for (positive at = 0;
             at < array_count(page_sizes); at++)
        {
                positive page = page_sizes[at];
                p8 address_to signature;

                if (bytes && have >= page)
                        signature = bytes + page - 10;
                else
                {
                        /* Modern swap carries version 1 in the fixed header.
                           Gate the extra page-size probes on it so every
                           ordinary unknown device does not pay four preads. */
                        if (have < 1068 || storage_le32(bytes + 1024) != 1)
                        {
                                p8 header[4];

                                if (storage_read(handle, header,
                                                 sizeof(header), 1024) !=
                                        sizeof(header) ||
                                    storage_le32(header) != 1)
                                        break;
                        }
                        if (storage_read(handle, tail, sizeof(tail), page - 10) !=
                            sizeof(tail))
                                continue;
                        signature = tail;
                }

                if (!memory_compare(signature, "SWAPSPACE2", 10))
                {
                        address_to modern = true;
                        address_to offset = page - 10;
                        return true;
                }
                if (!memory_compare(signature, "SWAP-SPACE", 10))
                {
                        address_to modern = false;
                        address_to offset = page - 10;
                        return true;
                }
        }

        return false;
}

static fn storage_probe_swap(bipolar handle,
                             storage_identity address_to identity,
                             p8 address_to bytes, positive have,
                             p64 address_to signature_offset)
{
        p64 offset;
        bool modern;

        if (!storage_swap_magic(handle, bytes, have,
                                address_of offset, address_of modern))
                return;

        if (signature_offset)
                *signature_offset = offset;
        storage_set_type(identity, (string_address)"swap");

        if (modern && have >= 1068)
        {
                if (storage_uuid_present(bytes + 1036))
                        storage_set_uuid(identity, bytes + 1036);
                storage_trimmed(identity->label, sizeof(identity->label),
                                address_of identity->label_length,
                                bytes + 1052, 16, false);
        }
}

/*
        The superblock is at 0x10000, past the block every other recogniser
        shares, so this reads its own. It used to read over the shared block
        instead, which was only ever correct because it happened to be
        ordered after the last recogniser that still wanted those bytes --
        a fourteenth recogniser placed between them would have been handed
        the Btrfs superblock and told it was sector zero.
*/
static fn storage_probe_btrfs(bipolar handle,
                              storage_identity address_to identity)
{
        p8 bytes[STORAGE_PROBE_ROOM];
        positive have = storage_read(handle, bytes, sizeof(bytes), 0x10000);
        positive sector_size;
        positive node_size;

        if (have < 0x12b + 256 || !storage_bytes(bytes, have, 0x40,
                           (p8 address_to)"_BHRfS_M", 8) ||
            storage_le64(bytes + 0x30) != 0x10000 ||
            !storage_le64(bytes + 0x70))
                return;

        sector_size = storage_le32(bytes + 0x90);
        node_size = storage_le32(bytes + 0x94);
        if (!storage_power_of_two(sector_size) || sector_size < 4096 ||
            sector_size > 65536 || !storage_power_of_two(node_size) ||
            node_size < sector_size || node_size > 65536)
                return;

        storage_set_type(identity, (string_address)"btrfs");
        storage_set_uuid(identity, bytes + 0x20);
        storage_trimmed(identity->label, sizeof(identity->label),
                        address_of identity->label_length,
                        bytes + 0x12b, 256, false);
}

static bool storage_iso_descriptor(bipolar handle, p8 address_to bytes,
                                   storage_iso_volume address_to volume,
                                   positive address_to read_bytes)
{
        positive have = storage_read(handle, bytes, 2048, 0x8000);

        if (read_bytes)
                address_to read_bytes = have;

        if (have < 72 || bytes[0] != 1 || bytes[6] != 1 ||
            !storage_bytes(bytes, have, 1,
                           (p8 address_to)"CD001", 5))
                return false;

        if (volume)
        {
                volume->sectors = have >= 88 ? storage_le32(bytes + 80) : 0;
                volume->sector_size =
                    have >= 132 ? storage_le16(bytes + 128) : 0;
        }
        return true;
}

/* The volume descriptor is at 0x8000, and is likewise read apart from the
   block the recognisers before this one are reading. */
static fn storage_probe_iso9660(bipolar handle,
                                storage_identity address_to identity)
{
        p8 bytes[2048];

        if (!storage_iso_descriptor(handle, bytes, null, null))
                return;

        storage_set_type(identity, (string_address)"iso9660");
        storage_trimmed(identity->label, sizeof(identity->label),
                        address_of identity->label_length,
                        bytes + 40, 32, true);

        /* libblkid derives the ISO identity from its volume creation time.
           storage_read zeroes unread tail bytes, so a short descriptor cannot
           accidentally satisfy the digit check. */
        {
                bool digits = string_span_max(bytes + 813, 16,
                                              string_set_digits) == 16;

                if (digits)
                {
                        positive source = 813;
                        positive out = 0;
                        p8 groups[7] = {4, 2, 2, 2, 2, 2, 2};

                        for (positive group = 0; group < 7; group++)
                        {
                                if (group)
                                        identity->uuid[out++] = '-';
                                memory_copy(identity->uuid + out,
                                            bytes + source, groups[group]);
                                out += groups[group];
                                source += groups[group];
                        }
                        identity->uuid[out] = end;
                        identity->uuid_length = out;
                }
        }
}

/*
        Partition identity is already parsed and validated by the kernel.

        /sys/dev/block is indexed by the device number, so this also works
        when the caller used /dev/disk/by-* or another alias whose basename
        says nothing about its parent disk.  The kernel's GPT parser checks
        the header and entry-array CRCs and its DOS parser checks table bounds
        before either value is published in uevent; consuming that result
        avoids a second, inevitably drifting partition-table implementation.
*/
static fn storage_partition_value(storage_identity address_to identity,
                                  string_address key,
                                  p8 address_to value, positive length)
{
        if (string_equals(key, "PARTUUID"))
        {
                positive copied = min(length, sizeof(identity->partuuid) - 1);

                memory_copy(identity->partuuid, value, copied);
                identity->partuuid[copied] = end;
                identity->partuuid_length = copied;
        }
        else if (string_equals(key, "PARTNAME"))
        {
                positive copied = min(length, sizeof(identity->partlabel) - 1);

                memory_copy(identity->partlabel, value, copied);
                identity->partlabel[copied] = end;
                identity->partlabel_length = copied;
        }
}

static inline INLINE p8 address_to storage_line_next(
    p8 address_to address_to cursor, p8 address_to limit);

static fn storage_probe_partition(string_address path,
                                  storage_identity address_to identity)
{
        file_facts facts;
        p8 sysfs[96];
        p8 text[4096];
        positive used;
        positive major;
        positive minor;
        bipolar got;

        if (!file_look(AT_FDCWD, path, 0, address_of facts) ||
            (facts.mode & MODE_FORMAT) != MODE_BLOCK)
                return;

        major = facts.rdev_major;
        minor = facts.rdev_minor;
        used = sizeof("/sys/dev/block/") - 1;
        memory_copy(sysfs, "/sys/dev/block/", used);
        used += positive_into_string(sysfs + used, major);
        sysfs[used++] = ':';
        used += positive_into_string(sysfs + used, minor);
        memory_copy(sysfs + used, "/uevent", sizeof("/uevent"));

        got = file_slurp_once_at(AT_FDCWD, sysfs, text, sizeof(text));
        if (got <= 0)
                return;

        {
                /* The slurp leaves text[got] terminated, so even a last line
                   with no newline ends in a NUL. */
                p8 address_to cursor = text;
                p8 address_to line;

                while ((line = storage_line_next(address_of cursor, text + got)))
                {
                        p8 address_to equal = (p8 address_to)string_first_of(
                            line, '=');

                        if (equal && equal > line)
                        {
                                *equal = end;
                                storage_partition_value(
                                    identity, line, equal + 1,
                                    string_length(equal + 1));
                        }
                }
        }
}

enum
{
        STORAGE_LUKS, STORAGE_XFS, STORAGE_SQUASHFS, STORAGE_F2FS,
        STORAGE_EXFAT, STORAGE_NTFS, STORAGE_FAT, STORAGE_EXT, STORAGE_EROFS,
        STORAGE_SWAP, STORAGE_BTRFS, STORAGE_UDF, STORAGE_ISO9660,
        STORAGE_PROBES,
};

/* Both first-match discovery and complete signature enumeration use these
   recognisers, including NTFS's conditional secondary metadata read. */
static fn storage_probe_kind(p8 kind, bipolar handle,
                              storage_identity address_to identity,
                              p8 address_to bytes, positive have,
                              p64 address_to signature_offset)
{
        switch (kind)
        {
        case STORAGE_LUKS: storage_probe_luks(identity, bytes, have); break;
        case STORAGE_XFS: storage_probe_xfs(identity, bytes, have); break;
        case STORAGE_SQUASHFS: storage_probe_squashfs(identity, bytes, have); break;
        case STORAGE_F2FS: storage_probe_f2fs(identity, bytes, have); break;
        case STORAGE_EXFAT: storage_probe_exfat(handle, identity, bytes, have); break;
        case STORAGE_NTFS:
                storage_probe_ntfs(identity, bytes, have);
                if (identity->type_length)
                        storage_probe_ntfs_label(handle, identity, bytes, have);
                break;
        case STORAGE_FAT: storage_probe_fat(identity, bytes, have, signature_offset); break;
        case STORAGE_EXT: storage_probe_ext(identity, bytes, have); break;
        case STORAGE_EROFS: storage_probe_erofs(identity, bytes, have); break;
        case STORAGE_SWAP: storage_probe_swap(handle, identity, bytes, have, signature_offset); break;
        case STORAGE_BTRFS: storage_probe_btrfs(handle, identity); break;
        case STORAGE_UDF: storage_probe_udf(handle, identity, signature_offset); break;
        case STORAGE_ISO9660: storage_probe_iso9660(handle, identity); break;
        }
}

static const struct { p32 offset; p8 length; } storage_signature_locations[] = {
    [STORAGE_LUKS] = {0, 6}, [STORAGE_XFS] = {0, 4},
    [STORAGE_SQUASHFS] = {0, 4}, [STORAGE_F2FS] = {0x400, 4},
    [STORAGE_EXFAT] = {3, 8}, [STORAGE_NTFS] = {3, 8},
    [STORAGE_FAT] = {54, 5}, [STORAGE_EXT] = {0x438, 2},
    [STORAGE_EROFS] = {0x400, 4}, [STORAGE_SWAP] = {0, 10},
    [STORAGE_BTRFS] = {0x10040, 8}, [STORAGE_UDF] = {0, 5},
    [STORAGE_ISO9660] = {0x8001, 5},
};

/* Enumerate every filesystem/container recogniser over one open descriptor.
   The recognisers remain the sole source of validation and metadata; wipefs
   merely receives their exact magic locations. */
static positive storage_each_signature_handle(
    bipolar handle, string_address path,
    storage_signature_visitor visitor, address_any context)
{
        p8 bytes[STORAGE_PROBE_ROOM];
        static const p8 order[] = {
            STORAGE_LUKS, STORAGE_XFS, STORAGE_SQUASHFS, STORAGE_F2FS,
            STORAGE_EXFAT, STORAGE_FAT, STORAGE_SWAP, STORAGE_NTFS,
            STORAGE_EXT, STORAGE_EROFS, STORAGE_BTRFS, STORAGE_UDF,
            STORAGE_ISO9660,
        };
        positive found = 0;
        positive have = storage_read(handle, bytes, sizeof(bytes), 0);
        for (positive at = 0; at < array_count(order); at++)
        {
                storage_signature signature = {0};
                signature.identity.path = path;
                p8 kind = order[at];
                signature.offset = storage_signature_locations[kind].offset;
                signature.length = storage_signature_locations[kind].length;
                signature.usage = kind == STORAGE_LUKS ? (string_address)"crypto"
                                : kind == STORAGE_SWAP ? (string_address)"other"
                                : (string_address)"filesystem";
                storage_probe_kind(kind, handle, address_of signature.identity,
                                   bytes, have, address_of signature.offset);
                if (!signature.identity.type_length)
                        continue;
                found++;
                if (signature.length <= sizeof(signature.magic) &&
                    storage_read(handle, signature.magic, signature.length,
                                 signature.offset) == signature.length &&
                    !visitor(address_of signature, context))
                        break;
        }
        return found;
}

static bipolar storage_each_signature(
    string_address path, storage_signature_visitor visitor,
    address_any context)
{
        bipolar handle = system_open_at(AT_FDCWD, path,
                                        FILE_READ | O_CLOEXEC);

        if (handle < 0)
                return handle;
        positive found = storage_each_signature_handle(
            handle, path, visitor, context);
        system_close(handle);
        return (bipolar)found;
}

/*
        Probe one block device or image.  The path remains borrowed; all
        metadata is owned by `identity`.  false means unreadable or unknown.
*/
bool storage_probe_device(string_address path,
                          storage_identity address_to identity)
{
        p8 bytes[STORAGE_PROBE_ROOM];
        bipolar handle;
        positive have;

        memory_zero(identity, sizeof(*identity));
        identity->path = path;
        storage_probe_partition(path, identity);
        handle = system_open_at(AT_FDCWD, path,
                               FILE_READ | O_CLOEXEC);
        if (handle < 0)
                return identity->partuuid_length || identity->partlabel_length;

        have = storage_read(handle, bytes, sizeof(bytes), 0);

        /* Container and fixed-superblock formats precede the deliberately
           broad boot-sector families. */
        for (p8 kind = 0; kind < STORAGE_PROBES && !identity->type_length; kind++)
                storage_probe_kind(kind, handle, identity, bytes, have, null);

        system_close(handle);
        return identity->type_length || identity->partuuid_length ||
               identity->partlabel_length;
}

static const storage_tag_descriptor address_to storage_tag_find(
    string_address tag, positive length)
{
        for (positive at = 0; at < array_count(storage_tags);
             at++)
                if (file_same_word(tag, length, storage_tags[at].name))
                        return storage_tags + at;

        return null;
}

static string_address storage_tag_value(
    storage_identity address_to identity,
    const storage_tag_descriptor address_to descriptor)
{
        if (descriptor->show == STORAGE_SHOW_DEVNAME)
                return identity->path;

        return memory_load_unaligned(
                   positive,
                   (p8 address_to)identity + descriptor->length_offset)
                   ? (p8 address_to)identity + descriptor->value_offset
                   : null;
}

static bool storage_identity_matches(
    storage_identity address_to identity,
    storage_selector address_to selector)
{
        string_address value;

        if (!selector->tag_length)
                return true;

        const storage_tag_descriptor address_to descriptor = storage_tag_find(
            selector->tag, selector->tag_length);
        if (!descriptor)
                return false;
        value = storage_tag_value(identity, descriptor);
        return value && string_equals(value, selector->value);
}

static bool storage_selector_parse(string_address text,
                                   storage_selector address_to selector)
{
        string_address equal = string_first_of(text, '=');

        if (!equal || equal == text)
                return false;

        selector->tag = text;
        selector->tag_length = (positive)(equal - text);
        selector->value = equal + 1;

        return true;
}

typedef bool (*storage_visitor)(storage_identity address_to identity,
                                address_any context);

typedef bool (*storage_path_visitor)(string_address path,
                                     address_any context);

/* The class directory is the kernel's one complete block-device census.
   Keep its checked dirent walk shared by metadata consumers; callbacks must
   consume the stack-backed path before returning. */
static bool storage_each_block_path(storage_path_visitor visit,
                                    address_any context)
{
        bipolar directory;
        p8 block[STORAGE_DEVICE_BLOCK];
        bool stopped = false;

        directory = system_open_at(AT_FDCWD,
                                  "/sys/class/block",
                                  FILE_READ | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0)
                return false;

        while (!stopped)
        {
                bipolar got = system_read_directory(directory, block,
                                                    sizeof(block));
                positive at = 0;

                if (got <= 0)
                        break;

                while (at < (positive)got)
                {
                        struct linux_dirent64 address_to entry =
                            (struct linux_dirent64 address_to)(block + at);
                        positive remaining = (positive)got - at;
                        positive record;
                        positive name_room;
                        positive name_length;
                        p8 path[STORAGE_PATH_ROOM];

                        if (remaining < 19)
                        {
                                stopped = true;
                                break;
                        }

                        record = entry->d_reclen;
                        if (record < 20 || record > remaining)
                        {
                                stopped = true;
                                break;
                        }

                        at += record;
                        name_room = record - 19;
                        name_length = string_length_max(entry->d_name, name_room);
                        if (!name_length || name_length == name_room ||
                            (name_length == 1 && entry->d_name[0] == '.') ||
                            (name_length == 2 && entry->d_name[0] == '.' &&
                             entry->d_name[1] == '.'))
                                continue;

                        if (name_length > sizeof(path) - sizeof("/dev/"))
                                continue;

                        memory_copy(path, "/dev/", sizeof("/dev/") - 1);
                        memory_copy(path + sizeof("/dev/") - 1,
                                    entry->d_name, name_length);
                        path[sizeof("/dev/") - 1 + name_length] = end;

                        if (!visit(path, context))
                        {
                                stopped = true;
                                break;
                        }
                }
        }

        system_close(directory);
        return stopped;
}

typedef struct
{
        storage_visitor visit;
        address_any context;
} storage_device_visit;

static bool storage_device_probe_visit(string_address path,
                                       address_any opaque)
{
        storage_device_visit address_to request =
            (storage_device_visit address_to)opaque;
        storage_identity identity;

        return !storage_probe_device(path, address_of identity) ||
               request->visit(address_of identity, request->context);
}

static bool storage_each_device(storage_visitor visit, address_any context)
{
        storage_device_visit request = {visit, context};
        return storage_each_block_path(storage_device_probe_visit,
                                       address_of request);
}

typedef struct
{
        storage_selector selector;
        p8 address_to path;
        positive room;
        bool found;
} storage_resolve_context;

static bool storage_resolve_visit(
    storage_identity address_to identity, address_any opaque)
{
        storage_resolve_context address_to context =
            (storage_resolve_context address_to)opaque;

        if (!storage_identity_matches(identity, address_of context->selector))
                return true;

        if (!context->room ||
            string_length(identity->path) >= context->room)
                return true;

        string_copy(context->path, identity->path);
        context->found = true;
        return false;
}

/* Resolve filesystem and partition tags through the same engine as findfs. */
bool storage_resolve_tag(string_address expression, p8 address_to path,
                         positive room)
{
        storage_resolve_context context;

        memory_zero(address_of context, sizeof(context));
        if (!storage_selector_parse(expression, address_of context.selector))
                return false;

        context.path = path;
        context.room = room;
        storage_each_device(storage_resolve_visit, address_of context);
        return context.found;
}

typedef enum
{
        STORAGE_OUTPUT_FULL,
        STORAGE_OUTPUT_VALUE,
        STORAGE_OUTPUT_DEVICE,
        STORAGE_OUTPUT_EXPORT
} storage_output_mode;

typedef struct
{
        writer output;
        storage_output_mode mode;
        storage_selector selector;
        positive shown;
        positive found;
        bool select_seen;
        bool first_only;
} storage_blkid_context;

static positive storage_show_tag(string_address tag, positive length)
{
        const storage_tag_descriptor address_to descriptor =
            storage_tag_find(tag, length);

        return descriptor ? descriptor->show : 0;
}

/* One storage-family escaping engine.  blkid leaves spaces literal and quotes
   them; findmnt --raw must instead escape spaces, while quotes need no special
   treatment there. */
static fn storage_write_hex_escaped(writer output, string_address value,
                                    bool escape_space, bool escape_quote)
{
        writer_hex_escaped(output, value, string_length(value),
                           HEX_CONTROL | HEX_TAB | HEX_SLASH | HEX_HIGH |
                           (escape_space ? HEX_SPACE : 0) |
                           (escape_quote ? HEX_QUOTE : 0));
}

static fn storage_output_field(writer output, string_address name,
                               string_address value, bool exported)
{
        string_format(output, exported ? "%s=" : " %s=\"", name);
        storage_write_hex_escaped(output, value, false, true);
        output(exported ? "\n" : "\"", 1);
}

static bool storage_blkid_visit(
    storage_identity address_to identity, address_any opaque)
{
        storage_blkid_context address_to context =
            (storage_blkid_context address_to)opaque;

        if (!storage_identity_matches(identity, address_of context->selector))
                return true;

        context->found++;

        positive available = STORAGE_SHOW_DEVNAME;

        for (positive at = 0; at + 1 < array_count(storage_tags); at++)
                if (storage_tag_value(identity, storage_tags + at))
                        available |= storage_tags[at].show;

        /* A recognised device remains a successful blkid query even when a
           requested tag is absent.  Only `-o device` prints independently of
           the selected tag; the field-oriented formats emit no empty row. */
        if (context->select_seen && !(context->shown & available) &&
            context->mode != STORAGE_OUTPUT_DEVICE)
                return !context->first_only;

        if (context->mode == STORAGE_OUTPUT_DEVICE)
                string_format(context->output, "%s\n", identity->path);
        else
        {
                bool exported = context->mode == STORAGE_OUTPUT_EXPORT;

                if (exported && context->found > 1)
                        context->output("\n", 1);

                if (exported)
                        storage_output_field(context->output,
                            (string_address)"DEVNAME", identity->path, true);

                if (context->mode == STORAGE_OUTPUT_FULL)
                        string_format(context->output, "%s:", identity->path);

                positive eligible = context->shown;
                if (context->mode != STORAGE_OUTPUT_VALUE && !context->select_seen)
                        eligible = positive_max;
                if (exported || (context->mode == STORAGE_OUTPUT_FULL &&
                                 !context->select_seen))
                        eligible &= ~((positive)STORAGE_SHOW_DEVNAME);

                for (positive at = 0; at < array_count(storage_tags); at++)
                {
                        const storage_tag_descriptor address_to tag =
                            storage_tags + at;
                        string_address value = storage_tag_value(identity, tag);

                        if (!value || !(eligible & tag->show))
                                continue;

                        /* A label is whatever the stick says, and -o value
                           put it on the terminal whole while every other
                           format here spells it: one tool with two answers
                           for the same field.  Safe spells the bytes a
                           terminal acts on and leaves UTF-8 and the
                           backslash alone, so $(blkid -o value -s UUID)
                           still reads what it always read; reference
                           writes it as util-linux does. */
                        if (context->mode == STORAGE_OUTPUT_VALUE)
                        {
#if MOONWATER_STRICT >= STRICT_SAFE
                                writer_hex_escaped(context->output, value,
                                                   string_length(value),
                                                   HEX_CONTROL | HEX_TAB);
                                context->output("\n", 1);
#else
                                string_format(context->output, "%s\n", value);
#endif
                        }
                        else
                                storage_output_field(context->output, tag->name,
                                                     value, exported);
                }

                if (context->mode == STORAGE_OUTPUT_FULL)
                        context->output("\n", 1);
        }

        return !context->first_only;
}

/* Storage retains exact long names and its own diagnostics. Failed reads
   leave the caller's last value intact; an unexpected attachment is unknown. */
static COLD b32 storage_argument_next(argument_cursor address_to taking,
    const argument_option address_to options, string_address address_to value)
{
        argument_match match;
        b32 option = argument_option_take(taking, options, false, &match);
        if (option > 0)
                *value = match.value;
        return option == ARGUMENT_UNEXPECTED ? ARGUMENT_UNKNOWN : option;
}

/*
        util-linux compatible core.  Syntax errors are 4, no recognised
        device is 2, success is 0.  The writers make it equally usable from
        the in-process builtin and the multicall entry.
*/
b32 storage_blkid_run(positive argc, string_address address_to argv,
                      writer output, writer error)
{
        static const argument_option options[] = {
            {"uuid", 'U', ARGUMENT_REQUIRED},
            {"label", 'L', ARGUMENT_REQUIRED},
            {"match-tag", 's', ARGUMENT_REQUIRED},
            {"match-token", 't', ARGUMENT_REQUIRED},
            {"output", 'o', ARGUMENT_REQUIRED},
            {"cache-file", 'c', ARGUMENT_REQUIRED},
            {"garbage-collect", 'g'},
            {null},
        };
        storage_blkid_context context;
        string_address inline_devices[8];
        string_address address_to devices = inline_devices;
        positive device_room = array_count(inline_devices);
        positive device_count = 0;
        argument_cursor taking = {.argc = argc, .argv = argv, .at = 1};
        string_address value;
        b32 option;

        memory_zero(address_of context, sizeof(context));
        context.output = output;

        while ((option = storage_argument_next(address_of taking, options, address_of value)) != ARGUMENT_END)
        {
                if (option == ARGUMENT_OPERAND)
                {
                        if (device_count == device_room)
                        {
                                if (devices == inline_devices)
                                {
                                        string_address address_to grown = null;
                                        positive grown_room = 0;

                                        if (!array_store_reserve(
                                                grown, grown_room, 0,
                                                device_count + 1, 8))
                                                goto usage;
                                        memory_copy_apart(grown, inline_devices,
                                                          device_count *
                                                              sizeof(grown[0]));
                                        devices = grown;
                                        device_room = grown_room;
                                }
                                else if (!array_store_reserve(
                                             devices, device_room,
                                             device_count, device_count + 1, 8))
                                        goto usage;
                        }
                        devices[device_count++] = value;
                        continue;
                }

                if (option == 'U' || option == 'L')
                {
                        context.selector.tag = option == 'U' ? (string_address)"UUID"
                                                             : (string_address)"LABEL";
                        context.selector.tag_length = option == 'U' ? 4 : 5;
                        context.selector.value = value;
                        context.mode = STORAGE_OUTPUT_DEVICE;
                        context.first_only = true;
                        continue;
                }

                /* This build probes each device: there is no cache to read
                   or to collect, so both are accepted and do nothing. */
                if (option == 'c' || option == 'g')
                        continue;

                if (option == 's')
                {
                        positive length = string_length(value);

                        context.select_seen = true;
                        context.shown |= storage_show_tag(value, length);
                        continue;
                }

                if (option == 't')
                {
                        if (!storage_selector_parse(value, address_of context.selector))
                                goto usage;
                        continue;
                }

                if (option == 'o')
                {
                        static const string_address names[] = {
                            [STORAGE_OUTPUT_FULL] = "full", [STORAGE_OUTPUT_VALUE] = "value",
                            [STORAGE_OUTPUT_DEVICE] = "device", [STORAGE_OUTPUT_EXPORT] = "export",
                        };
                        context.mode = string_table_find(
                            value, names, sizeof(names[0]), array_count(names));
                        if (context.mode == array_count(names))
                                goto usage;
                        continue;
                }

                goto usage;
        }

        if (device_count)
        {
                for (positive at = 0; at < device_count; at++)
                {
                        storage_identity identity;

                        if (storage_probe_device(devices[at], address_of identity) &&
                            !storage_blkid_visit(address_of identity,
                                                 address_of context))
                                break;
                }
        }
        else
                storage_each_device(storage_blkid_visit, address_of context);

        {
                b32 answer = context.found ? 0 : 2;

                if (devices != inline_devices)
                        array_store_release(devices, device_room, device_count);
                return answer;
        }

usage:
        error("blkid: usage: blkid [-s TAG] [-o full|value|device|export] "
              "[-t TAG=VALUE] [-U UUID] [-L LABEL] [DEVICE ...]\n", 0);
        if (devices != inline_devices)
                array_store_release(devices, device_room, device_count);
        return 4;
}

b32 storage_findfs_run(positive argc, string_address address_to argv,
                       writer output, writer error)
{
        p8 path[STORAGE_PATH_ROOM];

        // Two for a usage error, as upstream answers: one is "not found".
        if (argc != 2)
        {
                error("findfs: bad usage\n"
                      "Try 'findfs --help' for more information.\n", 0);
                return 2;
        }

        /* findfs doubles as the ordinary source-specifier normalizer used by
           mount callers: a path with no TAG= prefix passes through unchanged.
           util-linux preserves even an empty word here, including its line. */
        if (!*string_first_of_or_end(argv[1], '='))
        {
                return string_report(output, 0, "%s\n", argv[1]);
        }

        {
                storage_selector selector;

                if (!storage_selector_parse(argv[1], address_of selector))
                {
                        error("findfs: expected NAME=value\n", 0);
                        return 1;
                }
        }

        if (!storage_resolve_tag(argv[1], path, sizeof(path)))
        {
                return string_report(error, 1, "findfs: unable to resolve '%s'\n", argv[1]);
        }

        return string_report(output, 0, "%s\n", path);
}

/* ---- The read-only half: mountinfo and fstab, parsed once for everyone. ---- */

/*
        The read-only half of the common Linux storage interface.

        mountinfo is the authority for what this process can see.  fstab is
        only policy for what it may choose to mount.  Keeping their parsers
        here gives mount, umount, findmnt, mountpoint and findfs one spelling
        of both files instead of six subtly different ones.

        The input and entry tables grow through memory_reserve, whose copying,
        growth and allocation floor is in lib.c assembly on every target.
        Parsing begins only after the input has stopped moving; entry strings
        are therefore zero-copy views into the owned input block.  There is no
        line, field, path or entry ceiling.
*/

#define STORAGE_OPEN_PATH    010000000
// This machine's: arm64's O_NOFOLLOW is not x86_64's and riscv64's.
#define STORAGE_OPEN_NOFOLLOW O_NOFOLLOW

/*
        Shared API (the definitions live together because shell sources are
        one translation unit):

          storage_mount_table_load / storage_mount_table_release
          storage_mount_find_target
          storage_fstab_table_load / storage_fstab_table_release

        A successful load owns both table blocks until release.  Callers may
        iterate entry[0..count), and every string remains valid until then.
*/
typedef struct
{
        string_address source;
        string_address target;
        string_address type;
        string_address options;
        string_address dump;
        string_address pass;
        bool defaults;
        bool noauto;
} storage_fstab;

typedef struct
{
        byte_store text;
        storage_fstab address_to entry;
        positive entry_room;
        positive count;
        bool malformed;
} storage_fstab_table;


#define STORAGE_TABLE_RELEASE(name, type)                                    \
        fn name(type address_to table)                                       \
        {                                                                    \
                byte_store_release(address_of table->text);                  \
                array_store_release(table->entry, table->entry_room,         \
                                    table->count);                            \
        }

STORAGE_TABLE_RELEASE(storage_mount_table_release, storage_mount_table)
STORAGE_TABLE_RELEASE(storage_fstab_table_release, storage_fstab_table)
#undef STORAGE_TABLE_RELEASE

static bool storage_octal(p8 address_to at, p8 address_to value)
{
        positive used = 0;
        positive decoded = string_digits_octal_max(at, 3, address_of used);

        if (used != 3)
                return false;

        address_to value = (p8)decoded;
        return true;
}

/* One decoding step shared by the mountinfo and fstab readers: a complete
   backslash-octal escape becomes its byte, anything else copies through. */
static inline INLINE fn storage_unescape_step(p8 address_to address_to read,
                                              p8 address_to address_to write)
{
        p8 address_to at = address_to read;
        p8 address_to out = address_to write;
        p8 value;

        if (*at == '\\' && storage_octal(at + 1, address_of value))
        {
                *out++ = value;
                at += 4;
        }
        else
                *out++ = *at++;

        address_to read = at;
        address_to write = out;
}

/* mountinfo and fstab both use backslash-octal.  Decode every valid escape,
   including \134 itself; limiting this to \040 is how names with tabs or
   backslashes become impossible to round-trip. */
static fn storage_unescape(string_address field)
{
        p8 address_to read = (p8 address_to)string_first_of(field, '\\');
        p8 address_to write;

        /* Most mountinfo fields contain no escape at all.  The assembly scan
           lets those fields remain zero-copy instead of rewriting every byte
           just to discover that there was nothing to decode. */
        if (!read)
                return;

        write = read;

        while (*read)
                storage_unescape_step(address_of read, address_of write);

        *write = end;
}

/* Both kernel tables are newline records over the same owned byte store.
   Return one mutable, terminated record and advance the caller's cursor. */
static inline INLINE p8 address_to storage_line_next(
    p8 address_to address_to cursor, p8 address_to limit)
{
        p8 address_to line = address_to cursor;
        p8 address_to newline;

        if (line >= limit)
                return null;

        newline = (p8 address_to)memory_first_of(
            line, '\n', (positive)(limit - line));
        address_to cursor = newline ? newline + 1 : limit;

        if (newline)
                address_to newline = end;

        return line;
}

/* Return one whitespace-delimited field and terminate it in place. */
static string_address storage_field(p8 address_to address_to cursor)
{
        return string_token_next(null, " \t", cursor);
}

/* Parse one line after its newline has already become NUL. */
static bool storage_mount_line(storage_mount_table address_to table,
                               p8 address_to line)
{
        string_address first[6];
        string_address separator;
        string_address type;
        string_address source;
        string_address filesystem_options;
        p8 address_to cursor = line;
        positive id;
        positive parent;

        for (positive at = 0; at < 6; at++)
        {
                first[at] = storage_field(address_of cursor);

                if (!first[at])
                        return false;
        }

        do
        {
                separator = storage_field(address_of cursor);

                if (!separator)
                        return false;
        }
        while (string_compare(separator, (string_address) "-"));

        /* The kernel writes an empty source as nothing between two blanks
           (mount -t tmpfs "" /mnt).  Collapsing that run would read the
           options as the source and refuse the whole table; findmnt shows
           it as an empty SOURCE. */
        type = storage_field(address_of cursor);
        if (cursor && (*cursor == ' ' || *cursor == '\t'))
        {
                source = (string_address) "";
                cursor++;
        }
        else
                source = storage_field(address_of cursor);
        filesystem_options = storage_field(address_of cursor);

        if (!type || !source || !filesystem_options ||
            !string_digits_exact(first[0], address_of id) ||
            !string_digits_exact(first[1], address_of parent))
                return false;

        if (!array_store_reserve(table->entry, table->entry_room,
                                 table->count, table->count + 1, 32))
                return false;

        storage_unescape(first[3]);
        storage_unescape(first[4]);
        storage_unescape(first[5]);
        storage_unescape(type);
        storage_unescape(source);
        storage_unescape(filesystem_options);

        table->entry[table->count++] = (storage_mount){
            .id = id,
            .parent_id = parent,
            .device = first[2],
            .root = first[3],
            .target = first[4],
            .options = first[5],
            .type = type,
            .source = source,
            .filesystem_options = filesystem_options,
        };

        return true;
}

bool storage_mount_table_load(storage_mount_table address_to table,
                              writer diagnostic)
{
        memory_fill(table, 0, sizeof(*table));

        if (!file_store_slurp((string_address) "/proc/self/mountinfo",
                              address_of table->text))
        {
                if (diagnostic)
                        diagnostic(str("cannot read /proc/self/mountinfo\n"));
                return false;
        }

        p8 address_to cursor = table->text.bytes;
        p8 address_to limit = cursor + table->text.used;
        p8 address_to line;

        while ((line = storage_line_next(address_of cursor, limit)))
        {
                if (*line && !storage_mount_line(table, line))
                {
                        if (diagnostic)
                                diagnostic(str("invalid /proc/self/mountinfo line\n"));
                        storage_mount_table_release(table);
                        return false;
                }
        }

        return true;
}

static string_address storage_comma_next(string_address address_to cursor,
                                         positive address_to length)
{
        string_address at = address_to cursor;
        string_address comma;

        if (!at || !*at)
                return null;

        comma = string_first_of_or_end(at, ',');
        address_to length = (positive)(comma - at);
        address_to cursor = *comma ? comma + 1 : null;
        return at;
}

static PURE bool storage_option_has_length(string_address options,
                                      string_address wanted,
                                      positive wanted_length)
{
        string_address cursor = options;
        string_address at;
        positive length;

        while ((at = storage_comma_next(address_of cursor, address_of length)))
        {
                if (length == wanted_length &&
                    !string_compare_max(at, wanted, length))
                        return true;
        }

        return false;
}

static PURE bool storage_option_has(string_address options, string_address wanted)
{
        return storage_option_has_length(options, wanted,
                                         string_length(wanted));
}

/* fstab permits blank/comment lines and comments after fields.  Quotes are
   ordinary bytes in this grammar, just as they are in util-linux; \040 and
   \011 are the portable way to carry whitespace.  Treating quotes as shell
   syntax silently turns an invalid distro fstab into a different mount. */
static positive storage_fstab_fields(p8 address_to line,
                                     string_address address_to field,
                                     positive room)
{
        p8 address_to read = line;
        p8 address_to write = line;
        positive count = 0;

        while (*read)
        {
                read += string_span_of_set(read, " \t");

                if (!*read || *read == '#')
                        break;

                if (count == room)
                        return count + 1;

                field[count++] = write;

                while (*read && *read != ' ' && *read != '\t')
                        storage_unescape_step(address_of read,
                                              address_of write);

                /* Advance the reader before terminating the compacted field:
                   with no escapes read == write at the delimiter. */
                read += string_span_of_set(read, " \t");

                *write++ = end;
        }

        return count;
}

bool storage_fstab_table_load(storage_fstab_table address_to table,
                              string_address path, bool missing_ok,
                              writer diagnostic)
{
        memory_fill(table, 0, sizeof(*table));

        if (!file_store_slurp(path, address_of table->text))
        {
                if (!missing_ok && diagnostic)
                        string_format(diagnostic, "cannot read %s\n", path);

                return missing_ok;
        }

        p8 address_to cursor = table->text.bytes;
        p8 address_to limit = cursor + table->text.used;
        p8 address_to line;
        positive line_number = 0;

        while ((line = storage_line_next(address_of cursor, limit)))
        {
                string_address fields[7];
                positive count;

                line_number++;

                count = storage_fstab_fields(line, fields, 7);

                if (count)
                {
                        if (count < 4 || count > 6)
                        {
                                if (diagnostic)
                                        string_format(diagnostic,
                                                      "%s: parse error at line %p -- ignored\n",
                                                      path, line_number);
                                table->malformed = true;
                                continue;
                        }

                        if (!array_store_reserve(
                                table->entry, table->entry_room, table->count,
                                table->count + 1, 32))
                        {
                                storage_fstab_table_release(table);
                                return false;
                        }

                        table->entry[table->count++] = (storage_fstab){
                            .source = fields[0],
                            .target = fields[1],
                            .type = fields[2],
                            .options = fields[3],
                            .dump = count > 4 ? fields[4] : (string_address) "0",
                            .pass = count > 5 ? fields[5] : (string_address) "0",
                            .defaults = storage_option_has(fields[3],
                                                           (string_address) "defaults"),
                            .noauto = storage_option_has(fields[3],
                                                         (string_address) "noauto"),
                        };
                }
        }

        return true;
}

PURE storage_mount address_to storage_mount_find_target(
    storage_mount_table address_to table, string_address target)
{
        /* The last record is the visible top of a stacked mount. */
        for (positive at = table->count; at; at--)
                if (!string_compare(table->entry[at - 1].target, target))
                        return table->entry + at - 1;

        return null;
}

/* libmount's match_fstype: one leading `no` inverts the whole list, so
   `nofoo,bar` means `nofoo,nobar`; a token's own `no` prefix is read
   literally, and names compare without case. */
static PURE bool storage_type_match(string_address list, string_address type)
{
        bool negated = false;
        string_address cursor = list;
        string_address at;
        positive length;

        if (!list)
                return true;

        if (list[0] == 'n' && list[1] == 'o')
        {
                negated = true;
                cursor = list + 2;
        }

        while ((at = storage_comma_next(address_of cursor, address_of length)))
        {
                if (at[0] == 'n' && at[1] == 'o' &&
                    file_same_word(at + 2, length - 2, type))
                        return false;
                if (file_same_word(at, length, type))
                        return !negated;
        }

        return negated;
}

/* One column schema drives option parsing, headings and record projection.
   A zero offset names a computed numeric column; every textual member begins
   after the two ids, so zero is an unambiguous sentinel. */
#define STORAGE_COLUMNS(X)                                                   \
        X(SOURCE, "SOURCE", __builtin_offsetof(storage_mount, source))      \
        X(TARGET, "TARGET", __builtin_offsetof(storage_mount, target))      \
        X(FSTYPE, "FSTYPE", __builtin_offsetof(storage_mount, type))        \
        X(OPTIONS, "OPTIONS", __builtin_offsetof(storage_mount, options))   \
        X(FSROOT, "FSROOT", __builtin_offsetof(storage_mount, root))        \
        X(MAJMIN, "MAJ:MIN", __builtin_offsetof(storage_mount, device))     \
        X(ID, "ID", 0) X(PARENT, "PARENT", 0)                             \
        X(VFS_OPTIONS, "VFS-OPTIONS",                                      \
          __builtin_offsetof(storage_mount, options))                        \
        X(FS_OPTIONS, "FS-OPTIONS",                                        \
          __builtin_offsetof(storage_mount, filesystem_options))

#define STORAGE_COLUMN_ENUM(symbol, name, offset) STORAGE_##symbol,
enum storage_column
{
        STORAGE_COLUMNS(STORAGE_COLUMN_ENUM)
        STORAGE_COLUMN_MAX
};
#undef STORAGE_COLUMN_ENUM

typedef struct
{
        string_address name;
        p16 offset;
} storage_column_descriptor;

#define STORAGE_COLUMN_ENTRY(symbol, text, member_offset)                   \
        [STORAGE_##symbol] = {(string_address)text, (p16)(member_offset)},
static const storage_column_descriptor storage_column_table[] = {
    STORAGE_COLUMNS(STORAGE_COLUMN_ENTRY)};
#undef STORAGE_COLUMN_ENTRY
#undef STORAGE_COLUMNS

typedef struct
{
        p8 columns[STORAGE_COLUMN_MAX];
        positive count;
        string_address operand;
        /*  A relative or symlinked spelling names the same mount as its
            absolute one: --mountpoint and the bare operand are looked up by
            both, because the word may equally be a source that is no path
            at all. */
        string_address operand_path;
        string_address source;
        string_address target;
        string_address target_path;
        string_address second;
        string_address second_path;
        string_address type;
        string_address option_filter;
        bool path_query;
        bool mountpoint_query;
        bool no_headings;
        bool raw;
        bool no_fsroot;
        bool pairs;
        bool first_only;
        bool invert;
        bool list;
        bool submounts;
        /*  Set only while a tree is being written: the row being rendered
            and the shape of the rows around it, which is all the TARGET
            cell needs to draw its prefix. */
        const struct storage_findmnt_line address_to lines;
        positive line;
} storage_findmnt_options;

/*      One row of the tree: which mount, which row stands above it, how deep
        that leaves it, and whether it is the last of its parent's children.
        The prefix is drawn from those three facts alone. */
struct storage_findmnt_line
{
        positive index;
        positive parent;
        positive depth;
        bool last;
};

static PURE bool storage_rw_opposite(string_address options,
                                     string_address option, positive length)
{
        return length == 2 &&
            ((memory_is_2(option, 'r', 'w') &&
              storage_option_has_length(options, (string_address)"ro", 2)) ||
             (memory_is_2(option, 'r', 'o') &&
              storage_option_has_length(options, (string_address)"rw", 2)));
}

/* The filesystem column wins when its ro/rw state overrides the VFS column.
   Both a literal option and the positive half of a requested no-option ask
   this same question. */
static PURE bool storage_mount_option_present(storage_mount address_to mount,
                                               string_address option,
                                               positive length)
{
        bool present = storage_option_has_length(mount->options, option, length) ||
            storage_option_has_length(mount->filesystem_options, option, length);

        return present && !storage_rw_opposite(mount->filesystem_options,
                                               option, length);
}

static PURE bool storage_mount_options_match(storage_mount address_to mount,
                                        string_address list)
{
        string_address cursor = list;
        string_address at;
        positive length;

        if (!list)
                return true;

        while ((at = storage_comma_next(address_of cursor, address_of length)))
        {
                if (!length)
                        return false;

                bool present = storage_mount_option_present(mount, at, length);

                /* libmount first honours a literal `no...` option.  When
                   there is none, it treats the spelling as a request that
                   the positive option be absent: norw matches a read-only
                   mount, while nodev still matches the actual nodev flag. */
                if (!present && length > 2 && at[0] == 'n' && at[1] == 'o')
                        present = !storage_mount_option_present(
                            mount, at + 2, length - 2);

                if (!present)
                        return false;
        }

        return true;
}

/*      As in the listing tools: an empty list is refused without a word, an
        unknown name is named, and a name written twice is a column written
        twice.  `unknown` is left holding the name that was not recognised. */
#define STORAGE_COLUMN_NAME 64
#define STORAGE_COLUMNS_EMPTY NAME_LIST_EMPTY

static b32 storage_columns(string_address list,
                           storage_findmnt_options address_to options,
                           p8 address_to unknown)
{
        /* + appends to the current selection, including an earlier -o. */
        if (string_is(list, '+'))
                list++;
        else
                options->count = 0;
        return name_list_columns(list, storage_column_table,
            sizeof(storage_column_table[0]), STORAGE_COLUMN_MAX,
            options->columns, address_of options->count, unknown,
            STORAGE_COLUMN_NAME);
}

#define storage_column_name(column) storage_column_table[(column)].name

/* Count and write the same bounded cell pieces. Counting returns the original
   byte width: raw/pairs escaping does not participate in padded columns.

   The padded listing spells what libsmartcols spells in the C locale, a
   control byte or one past ASCII as \xNN and a backslash as \x5c where an x
   follows it, so no name reads as a spelling; it is as wide as what it
   shows. It wrote them whole: a mount point is a name anyone who can mount
   -- a FUSE user, a stick's label under /run/media -- chooses, and findmnt
   put its escapes on root's terminal. */
#define STORAGE_FINDMNT_SPELLED (HEX_CONTROL | HEX_TAB | HEX_HIGH)

static positive storage_findmnt_value(writer output, string_address value,
                                      positive length, bool raw, bool pairs)
{
        if (raw || pairs)
        {
                if (output)
                        writer_hex_escaped(output, value, length,
                            HEX_CONTROL | HEX_TAB | HEX_SLASH | HEX_HIGH |
                            (pairs ? HEX_QUOTE : HEX_SPACE));
                return length;
        }

        positive shown = length;
        positive plain = 0;
        // Where LC_CTYPE is UTF-8 a printable character past ASCII is shown
        // whole, as libsmartcols shows it; a C1 control or a byte that
        // starts no character is spelled, a byte at a time.
        bool utf8 = text_locale_utf8();

        for (positive at = 0; at < length;)
        {
                p8 byte = (p8)value[at];
                positive step = 1;
                bool printable = false;

                if (byte >= 0x80 && utf8)
                        step = file_terminal_step((const p8 address_to)value + at,
                                                  length - at, true,
                                                  address_of printable);
                else
                        printable =
                            !(escape_categories[byte] & STORAGE_FINDMNT_SPELLED) &&
                            !(byte == '\\' && at + 1 < length &&
                              value[at + 1] == 'x');
                if (printable)
                {
                        at += step;
                        continue;
                }
                shown += 3 * step;
                if (output)
                {
                        // A writer takes a length of 0 as "to the end".
                        if (at > plain)
                                output(value + plain, at - plain);
                        writer_hex_escaped(output, value + at, step,
                                           STORAGE_FINDMNT_SPELLED | HEX_SLASH);
                }
                at += step;
                plain = at;
        }
        if (output && length > plain)
                output(value + plain, length - plain);
        return shown;
}

static PURE inline INLINE bool storage_filesystem_option_represented(
    storage_mount address_to mount, string_address option, positive length)
{
        return storage_option_has_length(mount->options, option, length) ||
            storage_rw_opposite(mount->options, option, length);
}

static positive storage_combined_options_write(writer output,
                                         storage_mount address_to mount,
                                         bool raw, bool pairs)
{
        positive length = 0;

        for (positive filesystem = 0; filesystem < 2; filesystem++)
        {
                string_address cursor = filesystem ? mount->filesystem_options
                                                   : mount->options;
                string_address at;
                positive token_length;

                while ((at = storage_comma_next(address_of cursor,
                                                 address_of token_length)))
                {
                        if (!token_length ||
                            (filesystem &&
                             storage_filesystem_option_represented(
                                 mount, at, token_length)))
                                continue;

                        bool overridden = !filesystem && storage_rw_opposite(
                            mount->filesystem_options, at, token_length);
                        string_address shown = overridden
                            ? (memory_is_2(at, 'r', 'w')
                                   ? (string_address)"ro"
                                   : (string_address)"rw")
                            : at;

                        if (length)
                                length += storage_findmnt_value(
                                    output, ",", 1, raw, pairs);
                        length += storage_findmnt_value(
                            output, shown, token_length, raw, pairs);
                }
        }
        return length;
}

static PURE bool storage_source_has_root(storage_mount address_to mount)
{
        return mount->root && mount->root[0] &&
               !string_equals(mount->root, "/");
}

/*      `|-` for a child with siblings after it and "`-" for the last, with
        one "| " or "  " for every level above -- the ASCII set, which is
        what the reference draws outside a UTF-8 locale. */
static positive storage_findmnt_prefix(writer output,
                                       const struct storage_findmnt_line
                                           address_to lines,
                                       positive row)
{
        bool stack[64];
        positive depth = 0;
        positive at = lines[row].parent;

        if (!lines[row].depth)
                return 0;
        while (at != positive_max && lines[at].depth &&
               depth < array_count(stack))
        {
                stack[depth++] = lines[at].last;
                at = lines[at].parent;
        }
        for (positive i = depth; i; i--)
                if (output)
                        output((address_any)(stack[i - 1] ? "  " : "| "), 2);
        if (output)
                output((address_any)(lines[row].last ? "`-" : "|-"), 2);
        return (depth + 1) * 2;
}

static positive storage_findmnt_cell(writer output,
                                     storage_mount address_to mount,
                                     enum storage_column column,
                                     storage_findmnt_options address_to options)
{
        string_address value = storage_column_name(column);
        p8 number[32];
        positive drawn = 0;
        if (mount && column == STORAGE_TARGET && options->lines)
                drawn = storage_findmnt_prefix(output, options->lines,
                                               options->line);
        if (mount)
        {
                if (column == STORAGE_OPTIONS)
                        return storage_combined_options_write(
                            output, mount, options->raw, options->pairs);
                if (column == STORAGE_ID || column == STORAGE_PARENT)
                {
                        positive_into_string(number, column == STORAGE_ID
                            ? mount->id : mount->parent_id);
                        value = number;
                }
                else
                        value = memory_load_unaligned(string_address,
                            (p8 address_to)mount + storage_column_table[column].offset);
        }
        positive length = drawn + storage_findmnt_value(
            output, value, string_length(value), options->raw, options->pairs);
        if (mount && column == STORAGE_SOURCE && !options->no_fsroot &&
            storage_source_has_root(mount))
        {
                length += storage_findmnt_value(output, "[", 1, false, false);
                length += storage_findmnt_value(output, mount->root,
                    string_length(mount->root), options->raw, options->pairs);
                length += storage_findmnt_value(output, "]", 1, false, false);
        }
        return length;
}

static bool storage_source_matches(storage_mount address_to mount,
                                   string_address wanted)
{
        positive source_length;
        positive root_length;

        if (!string_compare(wanted, mount->source))
                return true;
        if (!storage_source_has_root(mount))
                return false;

        source_length = string_length(mount->source);
        root_length = string_length(mount->root);
        return string_length(wanted) == source_length + root_length + 2 &&
               !memory_compare(wanted, mount->source, source_length) &&
               wanted[source_length] == '[' &&
               !memory_compare(wanted + source_length + 1,
                               mount->root, root_length) &&
               wanted[source_length + root_length + 1] == ']';
}

typedef struct {
        storage_mount_table address_to table;
        struct storage_findmnt_line address_to lines;
        storage_findmnt_options address_to options;
} storage_findmnt_view;

static positive storage_findmnt_field(writer output, address_any context,
                                      positive row, positive column)
{
        storage_findmnt_view address_to view = context;
        view->options->line = row;
        storage_mount address_to mount = row == TABLE_HEADING ? null
            : view->table->entry + (view->lines ? view->lines[row].index : row);
        return storage_findmnt_cell(output, mount, column, view->options);
}

static inline INLINE table_cell storage_findmnt_get(address_any context, positive row,
                                      positive column, p8 address_to scratch)
{
        (void)scratch;
        if (row == TABLE_HEADING)
        {
                string_address name = storage_column_name(column);
                return (table_cell){.text = {name, string_length(name)}};
        }
        storage_findmnt_view address_to view = context;
        return (table_cell){.text = {null, view->options->raw || view->options->pairs
            ? 0 : storage_findmnt_field(null, context, row, column)}};
}

static PURE bool storage_findmnt_match(storage_mount address_to mount,
                                  storage_findmnt_options address_to options,
                                  bool have_query_id, positive query_id)
{
        bool matched = true;

        if (options->source &&
            !storage_source_matches(mount, options->source))
                matched = false;

        if (matched && !storage_type_match(options->type, mount->type))
                matched = false;

        if (matched && !storage_mount_options_match(
                           mount, options->option_filter))
                matched = false;

        if (matched && have_query_id && mount->id != query_id)
                matched = false;

        if (matched && options->operand &&
            string_compare(options->operand, mount->target) &&
            (!options->operand_path ||
             string_compare(options->operand_path, mount->target)) &&
            !storage_source_matches(mount, options->operand))
                matched = false;

        if (matched && options->target && !options->path_query &&
            string_compare(options->target, mount->target) &&
            (!options->target_path ||
             string_compare(options->target_path, mount->target)))
                matched = false;

        if (matched && options->second &&
            string_compare(options->second, mount->target) &&
            (!options->second_path ||
             string_compare(options->second_path, mount->target)))
                matched = false;

        if (options->invert &&
            (options->operand || options->source || options->target ||
             options->type || options->option_filter || have_query_id))
                return !matched;
        return matched;
}

/*      Defined below, beside mountpoint's own use of it: the absolute
        spelling of an open handle, read out of /proc/self/fd. */
static p8 address_to storage_fd_path(bipolar handle, positive address_to room);

/*      The absolute spelling of a word that names something, or null for a
        word that names nothing -- a source such as `tmpfs` is no path and
        stays as it was written.  Extra open flags let umount leave the last
        symlink unfollowed. */
static p8 address_to storage_word_path(string_address word,
                                       positive address_to room,
                                       positive extra_flags)
{
        bipolar handle = system_open_at(AT_FDCWD, word,
                                        STORAGE_OPEN_PATH | O_CLOEXEC |
                                            extra_flags);
        p8 address_to resolved;

        address_to room = 0;
        if (handle < 0)
                return null;
        resolved = storage_fd_path(handle, room);
        system_close(handle);
        return resolved;
}

/*      The rows a tree is written from.

        A mount that answers the query becomes a row; one that does not still
        lets its children through, attached to the nearest row above them,
        which is how a filtered tree keeps its shape. A mount the walk never
        reached -- its parent is outside this table -- starts a row of its
        own afterwards, so nothing is dropped. --submounts asks the opposite:
        each answer is a root and everything under it comes along, answer or
        not. */
typedef struct
{
        struct storage_findmnt_line address_to lines;
        positive room;
        positive count;
        bool failed;
} storage_findmnt_forest;

static positive storage_findmnt_line_add(storage_findmnt_forest address_to state,
                                         positive index, positive parent,
                                         positive depth)
{
        if (!array_store_reserve(state->lines, state->room, state->count,
                                 state->count + 1, 32))
        {
                state->failed = true;
                return positive_max;
        }
        /*  The row just added is the last of its parent's children until
            another arrives, and then it is not. */
        for (positive at = 0; at < state->count; at++)
                if (state->lines[at].parent == parent)
                        state->lines[at].last = false;
        state->lines[state->count] = (struct storage_findmnt_line){
            .index = index, .parent = parent, .depth = depth, .last = true};
        return state->count++;
}

static fn storage_findmnt_walk(storage_mount_table address_to table,
                               storage_findmnt_options address_to options,
                               bool have_query_id, positive query_id,
                               storage_findmnt_forest address_to state,
                               p8 address_to done, positive index,
                               positive parent, positive depth, bool submounts)
{
        positive row = parent;
        positive below = depth;

        if (state->failed || done[index])
                return;
        if (submounts || storage_findmnt_match(table->entry + index, options,
                                               have_query_id, query_id))
        {
                done[index] = 1;
                row = storage_findmnt_line_add(state, index, parent, depth);
                if (state->failed)
                        return;
                below = depth + 1;
        }
        /*  Children come in the order they were mounted, which is the order
            of their ids and not the order of the file: /proc/self/mountinfo
            lists a mount where its parent put it, and two siblings can be
            written the other way round. */
        positive after = 0;
        bool started = false;
        for (;;)
        {
                positive chosen = positive_max;
                positive chosen_id = 0;

                for (positive at = 0; at < table->count; at++)
                {
                        positive id = table->entry[at].id;

                        if (at == index || table->entry[at].parent_id !=
                                               table->entry[index].id)
                                continue;
                        if (started && id <= after)
                                continue;
                        if (chosen == positive_max || id < chosen_id)
                        {
                                chosen = at;
                                chosen_id = id;
                        }
                }
                if (chosen == positive_max)
                        break;
                after = chosen_id;
                started = true;
                storage_findmnt_walk(table, options, have_query_id, query_id,
                                     state, done, chosen, row, below, submounts);
        }
}

static bool storage_findmnt_rows(storage_mount_table address_to table,
                                 storage_findmnt_options address_to options,
                                 bool have_query_id, positive query_id,
                                 bool submounts,
                                 struct storage_findmnt_line address_to address_to out,
                                 positive address_to out_room,
                                 positive address_to out_count)
{
        storage_findmnt_forest state = {0};
        p8 address_to done = null;
        positive done_room = 0;
        positive done_count = 0;

        address_to out = null;
        address_to out_room = 0;
        address_to out_count = 0;
        if (table->count)
        {
                if (!array_store_reserve(done, done_room, done_count,
                                         table->count, 64))
                        return false;
                done_count = table->count;
                memory_zero(done, done_count);
        }

        if (submounts)
                for (positive at = 0; at < table->count; at++)
                {
                        if (done[at] ||
                            !storage_findmnt_match(table->entry + at, options,
                                                   have_query_id, query_id))
                                continue;
                        storage_findmnt_walk(table, options, have_query_id,
                                             query_id, address_of state, done,
                                             at, positive_max, 0, true);
                        if (options->first_only)
                                break;
                }
        else
        {
                for (positive at = 0; at < table->count; at++)
                {
                        bool rooted = true;

                        for (positive other = 0; other < table->count; other++)
                                if (other != at &&
                                    table->entry[other].id ==
                                        table->entry[at].parent_id)
                                {
                                        rooted = false;
                                        break;
                                }
                        if (rooted)
                                storage_findmnt_walk(table, options,
                                                     have_query_id, query_id,
                                                     address_of state, done, at,
                                                     positive_max, 0, false);
                }
                for (positive at = 0; at < table->count; at++)
                        if (!done[at] &&
                            storage_findmnt_match(table->entry + at, options,
                                                  have_query_id, query_id))
                                storage_findmnt_walk(table, options,
                                                     have_query_id, query_id,
                                                     address_of state, done, at,
                                                     positive_max, 0, false);
        }
        array_store_release(done, done_room, done_count);
        address_to out = state.lines;
        address_to out_room = state.room;
        address_to out_count = state.count;
        return !state.failed;
}

static fn storage_findmnt_release(storage_findmnt_options address_to options,
                                  positive operand_room, positive target_room,
                                  positive second_room)
{
        if (options->operand_path)
                memory_free((p8 address_to)options->operand_path, operand_room);
        if (options->target_path)
                memory_free((p8 address_to)options->target_path, target_room);
        if (options->second_path)
                memory_free((p8 address_to)options->second_path, second_room);
}

/* Reentrant core used unchanged by builtin and multicall dispatch. */
b32 storage_findmnt(positive argc, string_address address_to argv,
                    writer output, writer diagnostic)
{
        static const argument_option arguments[] = {
            {"noheadings", 'n'},
            {"raw", 'r'},
            {"list", 'l'},
            {"nofsroot", 'v'},
            {"pairs", 'P'},
            {"first-only", 'f'},
            {"invert", 'i'},
            {"submounts", 'R'},
            {"source", 'S', ARGUMENT_REQUIRED},
            {"target", 'T', ARGUMENT_REQUIRED},
            {"mountpoint", 'M', ARGUMENT_REQUIRED},
            {"types", 't', ARGUMENT_REQUIRED},
            {"options", 'O', ARGUMENT_REQUIRED},
            {"output", 'o', ARGUMENT_REQUIRED},
            {"hyperlink", 'y', ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
            {null},
        };
        storage_findmnt_options options = {
            .columns = {STORAGE_TARGET, STORAGE_SOURCE,
                        STORAGE_FSTYPE, STORAGE_OPTIONS},
            .count = 4,
        };
        storage_mount_table table;
        positive query_id = 0;
        bool have_query_id = false;
        positive widths[STORAGE_COLUMN_MAX] = {0};
        argument_cursor taking = {.argc = argc, .argv = argv, .at = 1};
        string_address value;
        b32 option;

        while ((option = storage_argument_next(address_of taking, arguments, address_of value)) !=
               ARGUMENT_END)
        {
                if (option == ARGUMENT_OPERAND)
                {
                        /*  Two operands: the first names a source or a
                            mountpoint, the second a mountpoint alone. A
                            third and any after it are read and ignored, the
                            way the reference ignores them. */
                        if (!options.operand)
                                options.operand = *value ? value
                                                         : (string_address) "/";
                        else if (!options.second)
                                options.second = *value ? value
                                                        : (string_address) "/";
                        continue;
                }

                if (option == ARGUMENT_MISSING)
                {
                        if (diagnostic)
                                diagnostic(str("findmnt: option needs an argument\n"));
                        return 1;
                }

                if (option == 'n')
                        options.no_headings = true;
                else if (option == 'r')
                        options.raw = true;
                else if (option == 'v')
                        options.no_fsroot = true;
                else if (option == 'l')
                        options.list = true;
                else if (option == 'R')
                        options.submounts = true;
                else if (option == 'P')
                {
                        options.pairs = true;
                        options.no_headings = true;
                }
                else if (option == 'f')
                        options.first_only = true;
                else if (option == 'i')
                        options.invert = true;
                else if (option == 'S')
                        options.source = value;
                else if (option == 'T' || option == 'M')
                {
                        options.target = *value ? value : (string_address) "/";
                        if (option == 'T')
                                options.path_query = true;
                        else
                                options.mountpoint_query = true;
                }
                else if (option == 'y')
                        ; /* The hyperlink choice: no terminal, no links. */
                else if (option == 't')
                        options.type = value;
                else if (option == 'O')
                        options.option_filter = value;
                else if (option != 'o')
                {
                        if (diagnostic)
                                diagnostic(str("findmnt: unsupported option\n"));
                        return 1;
                }
                else
                {
                        p8 unknown[STORAGE_COLUMN_NAME];
                        b32 fault = storage_columns(value, address_of options,
                                                    unknown);

                        if (fault == STORAGE_COLUMNS_EMPTY)
                                return 1;
                        if (fault)
                        {
                                if (diagnostic)
                                        string_format(diagnostic,
                                                      "findmnt: unknown column: %s\n",
                                                      unknown);
                                return 1;
                        }
                }
        }

        {
                static const argument_exclusive_pair shapes[] = {
                    {'l', (string_address)"list"},
                    {'P', (string_address)"pairs"},
                    {'r', (string_address)"raw"}};

                if (argument_exclusive_refuse(diagnostic, (string_address)"findmnt", argc, argv, arguments, false, shapes, array_count(shapes)))
                        return 1;
        }

        if ((options.path_query && options.mountpoint_query) ||
            (options.operand &&
             (options.source || options.path_query || options.mountpoint_query)))
        {
                if (diagnostic)
                        diagnostic(str("findmnt: incompatible query arguments\n"));
                return 1;
        }

        if (!storage_mount_table_load(address_of table, diagnostic))
                return 1;

        positive operand_room = 0;
        positive target_room = 0;
        positive second_room = 0;
        if (options.operand)
                options.operand_path = (string_address)storage_word_path(
                    options.operand, address_of operand_room, 0);
        if (options.mountpoint_query && options.target)
                options.target_path = (string_address)storage_word_path(
                    options.target, address_of target_room, 0);
        if (options.second)
                options.second_path = (string_address)storage_word_path(
                    options.second, address_of second_room, 0);

        if (options.path_query)
        {
                file_facts facts;

                if (!file_look_at(options.target, address_of facts))
                {
                        storage_mount_table_release(address_of table);
                        storage_findmnt_release(address_of options, operand_room,
                                                target_room, second_room);
                        return 1;
                }

                query_id = facts.mount_id;
                have_query_id = true;
        }

        positive matched = 0;
        bool direct = options.raw || options.pairs;
        /*      A query names one mount, so the shape a tree would show is
                already chosen: the reference draws the tree only for the
                whole table, and --submounts is what asks for the subtree
                under a query instead. */
        bool filtered = options.source || options.target || options.operand ||
                        options.second;
        bool submounts = options.submounts &&
                         (filtered || options.type || options.option_filter);
        bool tree = !direct && !options.list && !options.first_only &&
                    !filtered && !submounts;
        struct storage_findmnt_line address_to lines = null;
        positive line_room = 0;
        positive line_count = 0;

        if (tree || submounts)
        {
                if (!storage_findmnt_rows(address_of table, address_of options,
                                          have_query_id, query_id, submounts,
                                          address_of lines, address_of line_room,
                                          address_of line_count))
                {
                        storage_mount_table_release(address_of table);
                        storage_findmnt_release(address_of options, operand_room,
                                                target_room, second_room);
                        return 1;
                }
                /*  --list and the export shapes keep the subtree
                    --submounts reached and drop the drawing of it. */
                options.lines = direct || options.list ? null : lines;
                matched = line_count;

        }
        else
        for (positive at = 0; at < table.count; at++)
        {
                storage_mount address_to mount = table.entry + at;

                if (!storage_findmnt_match(mount, address_of options,
                                           have_query_id, query_id))
                        continue;

                table.entry[matched++] = *mount;
                if (options.first_only)
                        break;
        }

        if (matched)
        {
                storage_findmnt_view data = {&table, lines, &options};
                table_view view = {.output = output, .context = &data,
                    .cell = storage_findmnt_get, .write = storage_findmnt_field,
                    .order = options.columns, .order_size = 1, .count = options.count,
                    .separator = " ", .pairs = options.pairs};
                if (!direct) table_measure(&view, matched, !options.no_headings,
                                            false, true, 0, widths, null);
                if (!options.no_headings) table_row(&view, TABLE_HEADING, direct ? null : widths);
                for (positive at = 0; at < matched; at++)
                        table_row(&view, at, direct ? null : widths);
        }

        array_store_release(lines, line_room, line_count);
        storage_mount_table_release(address_of table);
        storage_findmnt_release(address_of options, operand_room, target_room,
                                second_room);

        return matched ? 0 : 1;
}

/* readlink(/proc/self/fd/N) gives the followed, absolute spelling without a
   PATH_MAX buffer.  The link can grow between calls, so equality means retry. */
static p8 address_to storage_fd_path(bipolar handle, positive address_to room)
{
        p8 name[64];
        p8 number[32];
        byte_store path = {0};
        positive used = positive_into_string(number, (positive)handle);

        memory_copy_apart(name, "/proc/self/fd/", 14);
        memory_copy_apart(name + 14, number, used + 1);

        while (true)
        {
                positive want = path.room ? path.room * 2 : 256;
                bipolar got;

                if (want <= path.room ||
                    !byte_store_reserve(address_of path, want, 256))
                        break;

                got = system_read_link_at(AT_FDCWD, name, path.bytes,
                                          path.room - 1);
                if (got < 0)
                        break;

                if ((positive)got < path.room - 1)
                {
                        path.bytes[got] = end;
                        address_to room = path.room;
                        return path.bytes;
                }
        }

        byte_store_release(address_of path);
        address_to room = 0;
        return null;
}

/* An argument list mountpoint cannot use: no operand, or a second one. */
static COLD b32 storage_mountpoint_usage(writer diagnostic)
{
        if (diagnostic)
                diagnostic(str("mountpoint: bad usage\n"
                               "Try 'mountpoint --help' for more information.\n"));
        return 1;
}

/* Reentrant core used unchanged by builtin and multicall dispatch. */
b32 storage_mountpoint(positive argc, string_address address_to argv,
                       writer output, writer diagnostic)
{
        static const argument_option options[] = {
            {"quiet", 'q'},
            {"fs-devno", 'd'},
            {"devno", 'x'},
            {"nofollow", 'N', ARGUMENT_LONG_ONLY},
            {"show", 'S', ARGUMENT_LONG_ONLY},
            {null},
        };
        bool quiet = false;
        bool fs_devno = false;
        bool devno = false;
        bool nofollow = false;
        bool show = false;
        string_address path = null;
        argument_cursor taking = {.argc = argc, .argv = argv, .at = 1};
        string_address value;
        b32 option;

        while ((option = storage_argument_next(address_of taking, options, address_of value)) != ARGUMENT_END)
        {
                if (option == 'q')
                        quiet = true;
                else if (option == 'd')
                        fs_devno = true;
                else if (option == 'x')
                        devno = true;
                else if (option == 'N')
                        nofollow = true;
                else if (option == 'S')
                        show = true;
                else if (option == ARGUMENT_OPERAND)
                {
                        if (path)
                                return storage_mountpoint_usage(diagnostic);
                        path = value;
                }
                else
                {
                        if (diagnostic)
                                diagnostic(str("mountpoint: unsupported option\n"));
                        return 1;
                }
        }

        if (!path)
                return storage_mountpoint_usage(diagnostic);

        if (devno && nofollow)
        {
                if (diagnostic)
                        diagnostic(str("mountpoint: --devno and --nofollow are mutually exclusive\n"));
                return 1;
        }

        if (devno)
        {
                file_facts facts;

                if (!file_look(AT_FDCWD, path,
                               nofollow ? AT_SYMLINK_NOFOLLOW : 0,
                               address_of facts))
                {
                        if (!quiet && diagnostic)
                                string_format(diagnostic,
                                              "mountpoint: %s: cannot inspect\n",
                                              path);
                        return 1;
                }

                if ((facts.mode & MODE_FORMAT) != MODE_BLOCK)
                {
                        if (!quiet && diagnostic)
                                string_format(diagnostic,
                                              "mountpoint: %s: not a block device\n",
                                              path);
                        return 32;
                }

                return string_report(output, 0, "%p:%p\n", (positive)facts.rdev_major, (positive)facts.rdev_minor);
        }

        bipolar handle = system_open_at(AT_FDCWD,
                                       path,
                                       STORAGE_OPEN_PATH |
                                           O_CLOEXEC |
                                           (nofollow ? STORAGE_OPEN_NOFOLLOW : 0));
        bool mounted = false;
        bool inspected = false;
        file_facts here;

        if (handle < 0)
        {
                if (!quiet && diagnostic)
                        string_format(diagnostic, "mountpoint: %s: %s\n", path,
                                      strerror((b32)-handle));
                return 1;
        }

        /*      --show needs statmount(2), which is not called here.  It is
                refused after the path has been reached and only where it
                would have been the answer: --devno asks about a block device
                instead, and --nofollow takes the older path that never
                consults the mount table by name. */
        bool symlink_kept = false;
        if (nofollow)
        {
                file_facts itself;

                symlink_kept = file_look(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW,
                                         address_of itself) &&
                               (itself.mode & MODE_FORMAT) == MODE_LINK;
        }
        if (show && !devno && !symlink_kept)
        {
                system_close(handle);
                if (diagnostic)
                        diagnostic(str("mountpoint: --show is not supported on this system\n"));
                return 1;
        }

        {
                file_facts parent;
                positive resolved_room = 0;
                p8 address_to resolved = storage_fd_path(handle,
                                                         address_of resolved_room);
                bool have_here = file_look(handle, (string_address) "",
                                           AT_EMPTY_PATH, address_of here);

                if (resolved && !string_compare(resolved,
                                                (string_address) "/"))
                {
                        mounted = true;
                        inspected = have_here;
                }
                else if (resolved)
                {
                        positive length = string_length(resolved);

                        length -= path_trailing_slashes(resolved, length);

                        p8 address_to slash = memory_last_of(
                            resolved + 1, '/', length - 1);
                        length = slash ? (positive)(slash - resolved) + 1 : 1;

                        length -= path_trailing_slashes(resolved, length);

                        resolved[length] = end;

                        if (have_here && file_look_at(resolved,
                                                     address_of parent))
                        {
                                mounted = here.mount_id != parent.mount_id;
                                inspected = true;
                        }
                }

                if (resolved)
                        memory_free(resolved, resolved_room);

                system_close(handle);
        }

        if (!inspected)
        {
                if (!quiet && diagnostic)
                        string_format(diagnostic,
                                      "mountpoint: %s: cannot inspect\n", path);
                return 1;
        }

        if (mounted && fs_devno)
        {
                return string_report(output, 0, "%p:%p\n", (positive)here.device_major, (positive)here.device_minor);
        }

        if (!quiet)
        {
                if (output && path)
                        output((address_any)path, string_length(path));
                string_address suffix = mounted
                    ? (string_address) " is a mountpoint\n"
                    : (string_address) " is not a mountpoint\n";
                if (output && suffix)
                        output((address_any)suffix, string_length(suffix));
        }

        /* util-linux reserves 1 for invocation/inspection errors and uses
           32 for the ordinary, script-readable "not a mountpoint" answer. */
        return mounted ? 0 : 32;
}

/* ---- The mount and umount interface over that one parsed view. ---- */

/*
        The common Linux mount interface.

        This file deliberately owns no dispatch.  A shell builtin and a
        multicall executable call storage_mount_command or
        storage_umount_command with the same argc/argv and writer, so neither
        path has an option parser, an fstab parser, or syscall policy of its
        own.

        The table loaders in the section above are public to the rest of
        the storage family.  Mount consumes that one parsed view; blkid-backed
        identity resolution enters through the source resolver without
        teaching this file about on-disk signatures.
*/

#include "../lib.util.c"

#define STORAGE_BIND_CHANGEABLE (MS_RDONLY | MS_NOSUID | \
                                 MS_NODEV | MS_NOEXEC | \
                                 MS_NOATIME | MS_NODIRATIME | \
                                 MS_RELATIME | MS_NOSYMFOLLOW)

#define storage_word(word, wanted) string_equals((word), (wanted))

#define storage_prefix(word, prefix)                                        \
        (!string_compare_max((word), (string_address)(prefix),              \
                             sizeof(prefix) - 1))

typedef byte_store storage_mount_word;

/* util-linux's answer to an option it does not know, worded the same by
   mount and umount: the cluster letter the cursor has just stepped past for
   a short spelling, the whole word for a long one, then --help either way. */
static COLD fn storage_option_unknown(writer diagnostic, string_address program,
                                      argument_cursor address_to taking)
{
        p8 letter[2] = {(p8)(taking->letters ? taking->letters[-1] : end), end};

        string_format(diagnostic,
                      taking->letters ? "%s: invalid option -- '%s'\n"
                                        "Try '%s --help' for more information.\n"
                                      : "%s: unrecognized option '%s'\n"
                                        "Try '%s --help' for more information.\n",
                      program,
                      taking->letters ? (string_address)letter : taking->word,
                      program);
}

static bool storage_mount_tag(storage_mount_word address_to word,
                              string_address tag, positive tag_length,
                              string_address value)
{
        positive value_length = string_length(value);
        positive wanted;

        if (tag_length > positive_max - 2 ||
            value_length > positive_max - tag_length - 2)
                return false;
        wanted = tag_length + value_length + 2;
        if (!byte_store_reserve(word, wanted, 64))
                return false;

        memory_copy_apart(word->bytes, tag, tag_length);
        word->bytes[tag_length] = '=';
        memory_copy_apart(word->bytes + tag_length + 1, value,
                          value_length + 1);
        word->used = wanted;
        return true;
}

typedef struct
{
        positive flags;
        positive mentioned;
        positive propagation;
        byte_store data;
        bool noauto;
        bool nofail;
        bool unsupported_loop;
        bool fake;
        bool verbose;
} storage_mount_options;

static fn storage_options_free(storage_mount_options address_to options)
{
        byte_store_release(address_of options->data);
        memory_fill(options, 0, sizeof(*options));
}

static bool storage_data_add(storage_mount_options address_to options,
                             string_address item, positive length)
{
        positive extra = (options->data.used ? 1 : 0) + 1;

        if (length > positive_max - extra)
                return false;
        extra += length;

        if (extra > positive_max - options->data.used ||
            !byte_store_reserve(address_of options->data,
                                options->data.used + extra, 64))
                return false;
        if (options->data.used)
                options->data.bytes[options->data.used++] = ',';
        memory_copy_apart(options->data.bytes + options->data.used,
                          item, length);
        options->data.used += length;
        options->data.bytes[options->data.used] = 0;
        return true;
}

typedef struct {
        string_address name;
        p32 set_length;
        p32 clear_action;
} storage_mount_option;

enum { STORAGE_OPTION_FLAGS, STORAGE_OPTION_PROPAGATION,
       STORAGE_OPTION_NOAUTO, STORAGE_OPTION_NOFAIL, STORAGE_OPTION_LOOP };

#define STORAGE_OPTION_LENGTH_SHIFT 26
#define STORAGE_OPTION_FLAG_MASK (((p32)1 << STORAGE_OPTION_LENGTH_SHIFT) - 1)
#define STORAGE_OPTION_ACTION_SHIFT 28
#define O(name, set, clear, action)                                         \
        {(string_address)(name),                                            \
         (p32)(set) | ((p32)(sizeof(name) - 1)                              \
                        << STORAGE_OPTION_LENGTH_SHIFT),                    \
         (p32)(clear) | ((p32)(action) << STORAGE_OPTION_ACTION_SHIFT)}

/* Length and action live above Linux's highest mount flag, leaving the hot
   table at two words and one pointer per spelling. */
static const storage_mount_option storage_mount_option_table[] = {
    O("ro", MS_RDONLY, 0, STORAGE_OPTION_FLAGS),
    O("rw", 0, MS_RDONLY, STORAGE_OPTION_FLAGS),
    O("suid", 0, MS_NOSUID, STORAGE_OPTION_FLAGS),
    O("nosuid", MS_NOSUID, 0, STORAGE_OPTION_FLAGS),
    O("dev", 0, MS_NODEV, STORAGE_OPTION_FLAGS),
    O("nodev", MS_NODEV, 0, STORAGE_OPTION_FLAGS),
    O("exec", 0, MS_NOEXEC, STORAGE_OPTION_FLAGS),
    O("noexec", MS_NOEXEC, 0, STORAGE_OPTION_FLAGS),
    O("sync", MS_SYNCHRONOUS, 0, STORAGE_OPTION_FLAGS),
    O("async", 0, MS_SYNCHRONOUS, STORAGE_OPTION_FLAGS),
    O("dirsync", MS_DIRSYNC, 0, STORAGE_OPTION_FLAGS),
    O("mand", MS_MANDLOCK, 0, STORAGE_OPTION_FLAGS),
    O("nomand", 0, MS_MANDLOCK, STORAGE_OPTION_FLAGS),
    O("atime", 0, MS_NOATIME, STORAGE_OPTION_FLAGS),
    O("noatime", MS_NOATIME, 0, STORAGE_OPTION_FLAGS),
    O("diratime", 0, MS_NODIRATIME, STORAGE_OPTION_FLAGS),
    O("nodiratime", MS_NODIRATIME, 0, STORAGE_OPTION_FLAGS),
    O("relatime", MS_RELATIME, MS_STRICTATIME, STORAGE_OPTION_FLAGS),
    O("norelatime", 0, MS_RELATIME, STORAGE_OPTION_FLAGS),
    O("strictatime", MS_STRICTATIME, MS_RELATIME, STORAGE_OPTION_FLAGS),
    O("nostrictatime", 0, MS_STRICTATIME, STORAGE_OPTION_FLAGS),
    O("lazytime", MS_LAZYTIME, 0, STORAGE_OPTION_FLAGS),
    O("nolazytime", 0, MS_LAZYTIME, STORAGE_OPTION_FLAGS),
    O("symfollow", 0, MS_NOSYMFOLLOW, STORAGE_OPTION_FLAGS),
    O("nosymfollow", MS_NOSYMFOLLOW, 0, STORAGE_OPTION_FLAGS),
    O("bind", MS_BIND, 0, STORAGE_OPTION_FLAGS),
    O("rbind", MS_BIND | MS_REC, 0, STORAGE_OPTION_FLAGS),
    O("move", MS_MOVE, 0, STORAGE_OPTION_FLAGS),
    O("remount", MS_REMOUNT, 0, STORAGE_OPTION_FLAGS),
    O("silent", MS_SILENT, 0, STORAGE_OPTION_FLAGS),
    O("loud", 0, MS_SILENT, STORAGE_OPTION_FLAGS),
    O("shared", MS_SHARED, 0, STORAGE_OPTION_PROPAGATION),
    O("rshared", MS_SHARED | MS_REC, 0, STORAGE_OPTION_PROPAGATION),
    O("slave", MS_SLAVE, 0, STORAGE_OPTION_PROPAGATION),
    O("rslave", MS_SLAVE | MS_REC, 0, STORAGE_OPTION_PROPAGATION),
    O("private", MS_PRIVATE, 0, STORAGE_OPTION_PROPAGATION),
    O("rprivate", MS_PRIVATE | MS_REC, 0, STORAGE_OPTION_PROPAGATION),
    O("unbindable", MS_UNBINDABLE, 0, STORAGE_OPTION_PROPAGATION),
    O("runbindable", MS_UNBINDABLE | MS_REC, 0, STORAGE_OPTION_PROPAGATION),
    O("noauto", 0, 0, STORAGE_OPTION_NOAUTO),
    O("nofail", 0, 0, STORAGE_OPTION_NOFAIL),
    O("loop", 0, 0, STORAGE_OPTION_LOOP),
    O("defaults", 0, 0, STORAGE_OPTION_FLAGS),
    O("auto", 0, 0, STORAGE_OPTION_FLAGS),
    O("user", 0, 0, STORAGE_OPTION_FLAGS),
    O("users", 0, 0, STORAGE_OPTION_FLAGS),
    O("owner", 0, 0, STORAGE_OPTION_FLAGS),
    O("group", 0, 0, STORAGE_OPTION_FLAGS),
    O("nouser", 0, 0, STORAGE_OPTION_FLAGS),
    O("_netdev", 0, 0, STORAGE_OPTION_FLAGS),
};
#undef O

static bool storage_options_parse(storage_mount_options address_to out,
                                  string_address list)
{
        string_address cursor = list;
        string_address at;
        positive length;

        while ((at = storage_comma_next(address_of cursor, address_of length)))
        {
                positive set = 0;
                positive clear = 0;
                bool consume = false;

                /* getopt/libmount accept redundant commas in -o lists. */
                if (!length)
                        continue;

                for (positive i = 0; i < array_count(storage_mount_option_table); i++)
                {
                        const storage_mount_option address_to option =
                            storage_mount_option_table + i;
                        positive option_length = option->set_length >>
                                                 STORAGE_OPTION_LENGTH_SHIFT;

                        if (length != option_length ||
                            memory_compare(at, option->name, length))
                                continue;

                        positive action = option->clear_action >>
                                          STORAGE_OPTION_ACTION_SHIFT;

                        set = option->set_length & STORAGE_OPTION_FLAG_MASK;
                        clear = option->clear_action & STORAGE_OPTION_FLAG_MASK;
                        if (action == STORAGE_OPTION_PROPAGATION)
                        {
                                /* Propagation is its own mount(2) call once
                                   the mount exists; carried in the flags it
                                   would turn `bind,rshared` into an rbind. */
                                out->propagation = set;
                                set = 0;
                        }
                        else if (action == STORAGE_OPTION_NOAUTO)
                                out->noauto = true;
                        else if (action == STORAGE_OPTION_NOFAIL)
                                out->nofail = true;
                        else if (action == STORAGE_OPTION_LOOP)
                                out->unsupported_loop = true;
                        consume = true;
                        break;
                }

                if (!consume)
                        consume = (length >= 2 &&
                                   ((at[0] == 'x' || at[0] == 'X') &&
                                    at[1] == '-')) ||
                                  (length >= 8 &&
                                   !memory_compare(at, "comment=", 8));

                out->mentioned |= set | clear;
                out->flags = (out->flags | set) & ~clear;
                if (!consume && !storage_data_add(out, at, length))
                        return false;
        }
        return true;
}

static bool storage_options_merge(storage_mount_options address_to into,
                                  storage_mount_options address_to extra)
{
        positive operations = MS_BIND | MS_MOVE |
                              MS_REMOUNT | MS_REC;

        into->flags = (into->flags & ~extra->mentioned) |
                      (extra->flags & extra->mentioned) |
                      (extra->flags & operations);
        into->mentioned |= extra->mentioned;
        if (extra->propagation)
                into->propagation = extra->propagation;
        into->noauto |= extra->noauto;
        into->nofail |= extra->nofail;
        into->unsupported_loop |= extra->unsupported_loop;

        return !extra->data.used ||
               storage_data_add(into, extra->data.bytes, extra->data.used);
}

/*
        Since Linux 2.6.26, an ordinary remount resets unspecified VFS flags.
        A bind remount has the same trap for the subset it can change.  Read
        the live flags first and apply only the options the caller mentioned;
        otherwise `remount,ro` quietly clears nosuid/nodev/noexec.
*/
static bool storage_remount_options(string_address target,
                                    storage_mount_options address_to asked,
                                    storage_mount_options address_to effective)
{
        storage_mount_table table;
        bool loaded;
        bool parsed = true;

        memory_fill(effective, 0, sizeof(*effective));
        loaded = storage_mount_table_load(address_of table, null);

        if (loaded)
        {
                /*      The table spells its targets absolutely, so a word
                        the caller wrote as `b` finds nothing there and the
                        remount then asks for flags the kernel locked when
                        it handed this mount over: EPERM, where the
                        reference keeps nosuid and nodev and succeeds. */
                positive room = 0;
                p8 address_to resolved = storage_word_path(target,
                                                           address_of room, 0);
                storage_mount address_to live =
                    storage_mount_find_target(address_of table,
                                              resolved ? (string_address)resolved
                                                       : target);

                if (live)
                        parsed = storage_options_parse(effective,
                                                       live->options);

                if (resolved)
                        memory_free(resolved, room);
                storage_mount_table_release(address_of table);
        }

        if (!parsed || !storage_options_merge(effective, asked))
        {
                storage_options_free(effective);
                return false;
        }

        effective->flags |= MS_REMOUNT;
        effective->mentioned |= MS_REMOUNT;
        return true;
}

static bipolar storage_source(string_address source,
                              string_address address_to resolved,
                              p8 address_to path, positive room)
{
        *resolved = source;

        if (storage_prefix(source, "UUID=") || storage_prefix(source, "LABEL=") ||
            storage_prefix(source, "PARTUUID=") ||
            storage_prefix(source, "PARTLABEL="))
        {
                if (!storage_resolve_tag(source, path, room))
                        return -ERROR_NO_ENTRY;
                *resolved = path;
        }
        return 0;
}

/*      The refusal line every mount path says, the errno spelled as the
        reference does. */
static fn storage_mount_failed(writer diagnostic, string_address program,
                               string_address source, string_address target,
                               bipolar answer)
{
        string_format(diagnostic, "%s: %s on %s failed: %s\n", program,
                      source ? source : (string_address)"none",
                      target ? target : (string_address)"none",
                      strerror((b32)(answer < 0 ? -answer : answer)));
}

static bipolar storage_mount_one(string_address source, string_address target,
                                 string_address type,
                                 storage_mount_options address_to options)
{
        string_address resolved;
        p8 resolved_path[4096];
        storage_identity identity;
        storage_mount_options effective;
        bool effective_live = false;
        bipolar answer = storage_source(source, address_of resolved,
                                        resolved_path, sizeof(resolved_path));

        if (answer)
                return answer;
        if (options->unsupported_loop)
                return -ERROR_INVALID;
        /* --fake does everything but the mount-related system calls. */
        if (options->fake)
                return 0;

        /* mount(2) itself does not implement util-linux's `-t auto`. */
        if ((!type || storage_word(type, (string_address)"auto")) &&
            !(options->flags & (MS_BIND | MS_MOVE |
                                MS_REMOUNT)) &&
            storage_probe_device(resolved, address_of identity))
                type = identity.type;

        if (options->flags & MS_MOVE)
                return system_mount(resolved, target, 0, MS_MOVE, 0);

        /* A propagation change alone names only a target; combined with a
           real mount it follows that mount as a second call below. */
        if (options->propagation && !type &&
            storage_word(source, (string_address)"none") &&
            !(options->flags & ~(MS_REC)))
                return system_mount(0, target, 0,
                                     options->propagation, 0);

        bool bind = (options->flags & MS_BIND) != 0;

        if (bind && !(options->flags & MS_REMOUNT))
        {
                positive bind_flags = MS_BIND |
                                      (options->flags & MS_REC);
                answer = system_mount(resolved, target, 0, bind_flags, 0);
                if (answer)
                        return answer;
        }

        /* bind(2) ignores VFS restrictions on the first call. Both kinds of
           remount merge the requested changes with the live options. */
        if ((options->flags & MS_REMOUNT) ||
            (bind && (options->mentioned & STORAGE_BIND_CHANGEABLE)))
        {
                if (!storage_remount_options(target, options,
                                             address_of effective))
                        return -ERROR_NO_MEMORY;
                effective_live = true;
        }

        if (bind)
        {
                if (effective_live)
                        answer = system_call_5(
                            syscall(mount), 0, (positive)target, 0,
                            MS_BIND | MS_REMOUNT |
                                (effective.flags & STORAGE_BIND_CHANGEABLE),
                            0);
        }
        else
        {
                storage_mount_options address_to used =
                    effective_live ? address_of effective : options;

                answer = system_call_5(
                    syscall(mount), (positive)resolved, (positive)target,
                    (positive)type, used->flags,
                    (positive)(used->data.used ? used->data.bytes : null));
        }

        if (effective_live)
                storage_options_free(address_of effective);

        if (!answer && options->propagation)
                answer = system_mount(0, target, 0,
                                       options->propagation, 0);
        return answer;
}


/*      ignored says the record was passed over rather than mounted: noauto,
        swap, a type the filter excludes. mount -a counts those in neither
        column, and the difference is the whole exit status -- one failure
        beside one ignored record is 32, "all failed", not 64. */

static b32 storage_mount_fstab_record(string_address program,
                                      storage_fstab address_to record,
                                      storage_mount_options address_to extra,
                                      string_address type_filter, bool explicit,
                                      writer diagnostic, writer write,
                                      bool address_to ignored)
{
        storage_mount_options options;
        string_address selected_type = record->type;
        bipolar answer;
        bool tolerated;

        memory_fill(address_of options, 0, sizeof(options));
        if (!storage_options_parse(address_of options, record->options) ||
            (extra && !storage_options_merge(address_of options, extra)))
        {
                storage_options_free(address_of options);
                return string_report(diagnostic, 1, "%s: no memory\n", program);
        }
        if (extra)
        {
                options.fake = extra->fake;
                options.verbose = extra->verbose;
        }
        address_to ignored = false;

        /* With one fstab operand, util-linux treats a single positive -t as
           an override.  Under -a it is a filter. */
        if (explicit && type_filter &&
            !string_first_of(type_filter, ',') &&
            !(type_filter[0] == 'n' && type_filter[1] == 'o' &&
              type_filter[2]))
                selected_type = type_filter;

        if ((!explicit && options.noauto) ||
            (!explicit && (storage_word(record->type, "swap") ||
                           storage_word(record->type, "ignore"))) ||
            (!explicit && !storage_type_match(type_filter, record->type)) ||
            (explicit && selected_type == record->type &&
             !storage_type_match(type_filter, record->type)))
        {
                if (options.verbose && write)
                        string_format(write, "%s: ignored\n", record->target);
                storage_options_free(address_of options);
                address_to ignored = true;
                return 0;
        }

        answer = storage_mount_one(record->source, record->target, selected_type,
                                   address_of options);
        tolerated = answer && options.nofail && !explicit;
        if (!answer && options.verbose && write)
                string_format(write, "%s: successfully mounted\n", record->target);
        if (answer && !tolerated)
                storage_mount_failed(diagnostic, program, record->source,
                                     record->target, answer);
        storage_options_free(address_of options);
        return answer && !tolerated ? 32 : 0;
}

static b32 storage_mount_fstab(string_address program, string_address wanted,
                              bool all, storage_mount_options address_to extra,
                              string_address type_filter, string_address fstab,
                              writer diagnostic, writer write)
{
        storage_fstab_table table;
        bool loaded = storage_fstab_table_load(address_of table, fstab, false,
                                               diagnostic);
        b32 failed = 0;
        bool found = false;
        positive mounted = 0;
        storage_mount_table active;
        bool have_active = false;

        if (!loaded)
                return 1;

        if (all)
                have_active = storage_mount_table_load(address_of active, null);

        for (positive at = 0; at < table.count; at++)
        {
                storage_fstab address_to record = table.entry + at;
                if (!all && !storage_word(wanted, record->source) &&
                    !storage_word(wanted, record->target))
                        continue;
                found = true;
                if (all && have_active &&
                    storage_mount_find_target(address_of active,
                                              record->target) &&
                    !storage_option_has(record->options,
                                        (string_address)"remount"))
                {
                        if (extra && extra->verbose && write)
                                string_format(write, "%s: already mounted\n",
                                              record->target);
                        continue;
                }
                bool ignored = false;
                b32 one = storage_mount_fstab_record(program, record, extra,
                                                     type_filter, !all,
                                                     diagnostic, write,
                                                     address_of ignored);
                failed |= one;
                if (!one && !ignored)
                        mounted++;
                if (!all)
                        break;
        }

        /* util-linux: 64 when some of -a succeeded, 32 when a mount failed. */
        if (all && failed && mounted)
                failed = 64;
        if (!all && !found)
        {
                string_format(diagnostic, "%s: %s not found in %s\n", program,
                              wanted, fstab);
                failed = 1;
        }
        if (have_active)
                storage_mount_table_release(address_of active);
        storage_fstab_table_release(address_of table);
        return failed;
}

static b32 storage_mount_list(writer write, writer diagnostic,
                              string_address type_filter)
{
        storage_mount_table table;
        bool loaded = storage_mount_table_load(address_of table, diagnostic);

        if (!loaded)
                return 1;

        for (positive at = 0; at < table.count; at++)
        {
                storage_mount address_to record = table.entry + at;
                if (storage_type_match(type_filter, record->type))
                {
                        /* util-linux writes the target's control bytes
                           as '?' and the source's whole; safe spells the
                           source the same way, since a FUSE user names it. */
#if MOONWATER_STRICT >= STRICT_SAFE
                        file_write_controls_hidden(write, record->source, 0);
#else
                        write(record->source, 0);
#endif
                        write(str(" on "));
                        file_write_controls_hidden(write, record->target, 0);
                        string_format(write, " type %s (", record->type);
                        storage_combined_options_write(write, record, false,
                                                       false);
                        if (write)
                                write(str(")\n"));
                }
        }
        storage_mount_table_release(address_of table);
        return 0;
}

b32 storage_mount_command(positive argc, string_address address_to argv,
                          writer write, writer diagnostic)
{
        static const argument_option arguments[] = {
            {"all", 'a'},
            {"types", 't', ARGUMENT_REQUIRED},
            {"options", 'o', ARGUMENT_REQUIRED},
            {"fstab", 'T', ARGUMENT_REQUIRED},
            {"label", 'L', ARGUMENT_REQUIRED},
            {"uuid", 'U', ARGUMENT_REQUIRED},
            {"source", 'S', ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
            {"target", 'X', ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
            {"read-only", 'r'},
            {"read-write", 'w'},
            {"bind", 'B'},
            {"rbind", 'R'},
            {"move", 'M'},
            {"make-shared", '1', ARGUMENT_LONG_ONLY},
            {"make-rshared", '2', ARGUMENT_LONG_ONLY},
            {"make-private", '3', ARGUMENT_LONG_ONLY},
            {"make-rprivate", '4', ARGUMENT_LONG_ONLY},
            {"make-slave", '5', ARGUMENT_LONG_ONLY},
            {"make-rslave", '6', ARGUMENT_LONG_ONLY},
            {"make-unbindable", '7', ARGUMENT_LONG_ONLY},
            {"make-runbindable", '8', ARGUMENT_LONG_ONLY},
            {"verbose", 'v'},
            {"no-mtab", 'n'},
            {"no-canonicalize", 'c'},
            {"internal-only", 'i'},
            {"sloppy", 's'},
            {"fake", 'f'},
            {"show-labels", 'l'},
            {"fork", 'F'},
            {"ro", 'r'},
            {"rw", 'w'},
            {null},
        };
        string_address type = null;
        string_address fstab = (string_address)"/etc/fstab";
        string_address named_source = null;
        string_address named_target = null;
        string_address operand[2] = {null, null};
        storage_mount_word tag_source;
        storage_mount_options options;
        positive operands = 0;
        bool all = false;
        /*  --no-canonicalize keeps the target as it was written, in what is
            done and in what --verbose says was done. */
        bool canonical = true;
        argument_cursor taking = {.argc = argc, .argv = argv, .at = 1};
        string_address value;
        b32 option;
        b32 status = 1;

        memory_fill(address_of tag_source, 0, sizeof(tag_source));
        memory_fill(address_of options, 0, sizeof(options));

        while ((option = storage_argument_next(address_of taking, arguments, address_of value)) !=
               ARGUMENT_END)
        {
                if (option == ARGUMENT_OPERAND)
                {
                        if (operands >= 2)
                        {
                                string_format(diagnostic,
                                              "mount: too many operands\n");
                                goto done;
                        }
                        operand[operands++] = value;
                }
                else if (option == ARGUMENT_MISSING)
                        goto missing_option;
                else if (option == ARGUMENT_UNKNOWN)
                {
                        storage_option_unknown(diagnostic,
                                               (string_address)"mount",
                                               address_of taking);
                        goto done;
                }
                else if (option == 'a')
                        all = true;
                else if (option == 'f')
                        options.fake = true;
                else if (option == 'v')
                        options.verbose = true;
                else if (option == 'F')
                        ; /* One device at a time: nothing to fork for. */
                else if (option == 'r' || option == 'w')
                {
                        if (!storage_options_parse(
                                address_of options,
                                option == 'r' ? (string_address)"ro" :
                                                (string_address)"rw"))
                                goto no_memory;
                }
                else if (option == 'B' || option == 'R')
                {
                        positive flags = MS_BIND |
                            (option == 'R' ? MS_REC : 0);
                        options.flags |= flags;
                        options.mentioned |= flags;
                }
                else if (option == 'M')
                {
                        options.flags |= MS_MOVE;
                        options.mentioned |= MS_MOVE;
                }
                else if (option == 'c')
                        canonical = false;
                else if (option == 't')
                        type = value;
                else if (option == 'o')
                {
                        if (!storage_options_parse(address_of options, value))
                                goto no_memory;
                }
                else if (option == 'T')
                        fstab = value;
                else if (option == 'L' || option == 'U')
                {
                        if (!storage_mount_tag(
                                address_of tag_source,
                                option == 'L' ? (string_address)"LABEL" :
                                                (string_address)"UUID",
                                option == 'L' ? sizeof("LABEL") - 1 :
                                                sizeof("UUID") - 1,
                                value))
                                goto no_memory;
                        named_source = tag_source.bytes;
                }
                else if (option == 'S')
                        named_source = value;
                else if (option == 'X')
                        named_target = value;
                else if (option >= '1' && option <= '8')
                {
                        static const positive propagation[] = {
                            MS_SHARED,
                            MS_SHARED | MS_REC,
                            MS_PRIVATE,
                            MS_PRIVATE | MS_REC,
                            MS_SLAVE,
                            MS_SLAVE | MS_REC,
                            MS_UNBINDABLE,
                            MS_UNBINDABLE | MS_REC,
                        };
                        options.propagation = propagation[option - '1'];
                }
        }

        {
                static const argument_exclusive_pair attach[] = {
                    {'B', (string_address)"bind"},
                    {'M', (string_address)"move"},
                    {'R', (string_address)"rbind"}};

                if (argument_exclusive_refuse(diagnostic, (string_address)"mount", argc, argv, arguments, false, attach, array_count(attach)))
                        goto done;
        }

        if (named_source)
        {
                if (operands > 1 || (named_target && operands))
                {
                        string_format(diagnostic, "mount: too many operands\n");
                        goto done;
                }
                if (operands == 1)
                {
                        operand[1] = operand[0];
                        operand[0] = named_source;
                        operands = 2;
                }
                else if (named_target)
                {
                        operand[0] = named_source;
                        operand[1] = named_target;
                        operands = 2;
                }
                else
                {
                        operand[0] = named_source;
                        operands = 1;
                }
        }
        else if (named_target)
        {
                if (operands > 1)
                {
                        string_format(diagnostic, "mount: too many operands\n");
                        goto done;
                }
                if (operands == 1)
                {
                        operand[1] = named_target;
                        operands = 2;
                }
                else
                {
                        operand[0] = named_target;
                        operands = 1;
                }
        }

        if (all)
        {
                if (operands)
                {
                        string_format(diagnostic, "mount: -a takes no operands\n");
                        goto done;
                }
                status = storage_mount_fstab((string_address)"mount", null, true,
                                             address_of options, type, fstab,
                                             diagnostic, write);
                goto done;
        }
        if (!operands)
        {
                /* Listing is what mount does with no operand; asking it to
                   change a mount and naming none is a usage error. */
                if (options.mentioned || options.propagation ||
                    options.data.used || named_source || named_target)
                {
                        string_format(diagnostic, "mount: bad usage\n"
                                      "Try 'mount --help' for more information.\n");
                        status = 1;
                        goto done;
                }
                status = storage_mount_list(write, diagnostic, type);
                goto done;
        }
        if (operands == 1)
        {
                /* Propagation changes name only a target and never consult
                   fstab. Remount can infer source/type from mountinfo. */
                /*  A propagation change stands alone even beside other
                    options, so long as one of them is the recursion the
                    change itself asked for: the reference reads the rest as
                    attributes of that one operation and never looks the
                    target up in fstab. */
                if (options.propagation &&
                    (!(options.flags & (MS_BIND | MS_MOVE |
                                        MS_REMOUNT)) ||
                     ((options.flags | options.propagation) &
                      MS_REC)))
                {
                        bipolar answer = storage_mount_one((string_address)"none",
                                                           operand[0], null,
                                                           address_of options);
                        if (answer)
                                storage_mount_failed(diagnostic,
                                                     (string_address)"mount",
                                                     null, operand[0], answer);
                        else if (options.verbose)
                        {
                                /* util-linux names the followed, absolute
                                   spelling here, not the word it was given. */
                                positive room = 0;
                                p8 address_to resolved = storage_word_path(
                                    operand[0], address_of room, 0);
                                string_format(write,
                                              "mount: %s propagation flags changed.\n",
                                              resolved ? (string_address)resolved
                                                       : operand[0]);
                                if (resolved)
                                        memory_free(resolved, room);
                        }
                        status = answer ? 32 : 0;
                }
                else if (options.flags & MS_REMOUNT)
                {
                        storage_mount_table table;
                        if (storage_mount_table_load(address_of table, diagnostic))
                        {
                                storage_mount address_to live =
                                    storage_mount_find_target(address_of table,
                                                              operand[0]);
                                bipolar answer;

                                if (!live)
                                {
                                        string_format(diagnostic,
                                                      "mount: %s is not mounted\n",
                                                      operand[0]);
                                        status = 32;
                                }
                                else
                                {
                                        answer = storage_mount_one(live->source,
                                                                   live->target,
                                                                   live->type,
                                                                   address_of options);
                                        if (answer)
                                                storage_mount_failed(
                                                    diagnostic,
                                                    (string_address)"mount",
                                                    live->source, live->target,
                                                    answer);
                                        status = answer ? 32 : 0;
                                }
                                storage_mount_table_release(address_of table);
                        }
                }
                else
                        status = storage_mount_fstab((string_address)"mount",
                                                     operand[0], false,
                                                     address_of options, type,
                                                     fstab, diagnostic, write);
                goto done;
        }
        else
        {
                bipolar answer;
                p8 tag_path[4096];

                /* util-linux answers 32 for a mount(2) that failed, 1 for
                   an invocation it could not make sense of, such as a tag
                   no device carries. */
                if ((storage_prefix(operand[0], "UUID=") ||
                     storage_prefix(operand[0], "LABEL=") ||
                     storage_prefix(operand[0], "PARTUUID=") ||
                     storage_prefix(operand[0], "PARTLABEL=")) &&
                    !storage_resolve_tag(operand[0], tag_path, sizeof(tag_path)))
                {
                        string_format(diagnostic, "mount: %s: can't find %s.\n",
                                      operand[1], operand[0]);
                        status = 1;
                        goto done;
                }
                /*  --no-canonicalize hands the target to the kernel as it
                    was written, and the mount API does not follow a trailing
                    symlink there: the reference refuses such a target where
                    the canonical spelling would have reached the directory
                    under it. */
                if (!canonical && !options.fake)
                {
                        file_facts itself;

                        if (file_look(AT_FDCWD, operand[1], AT_SYMLINK_NOFOLLOW,
                                      address_of itself) &&
                            (itself.mode & MODE_FORMAT) == MODE_LINK)
                        {
                                storage_mount_failed(diagnostic,
                                                     (string_address)"mount",
                                                     operand[0], operand[1],
                                                     -ERROR_INVALID);
                                status = 32;
                                goto done;
                        }
                }
                /*  A move and a remount asked for together: the reference
                    takes the remount, which leaves the move undone, and
                    calls that a subsequent operation that failed -- the
                    usage answer 1, not the 32 a refused mount(2) leaves. A
                    target that is not there at all never gets that far. */
                if ((options.flags & MS_MOVE) &&
                    (options.flags & MS_REMOUNT) && !options.fake)
                {
                        positive room = 0;
                        p8 address_to shown = canonical
                            ? storage_word_path(operand[1],
                                                address_of room, 0)
                            : null;

                        if (shown || !canonical)
                        {
                                string_format(diagnostic,
                                              "mount: %s: filesystem was mounted,"
                                              " but any subsequent operation"
                                              " failed: Invalid argument.\n",
                                              shown ? (string_address)shown
                                                    : operand[1]);
                                if (shown)
                                        memory_free(shown, room);
                                status = 1;
                                goto done;
                        }
                }
                answer = storage_mount_one(operand[0], operand[1], type,
                                           address_of options);
                if (answer)
                        storage_mount_failed(diagnostic, (string_address)"mount",
                                             operand[0], operand[1], answer);
                else if (options.verbose)
                {
                        /*  The reference names both words the way it
                            resolved them: the canonical path where the word
                            is one, and the word itself where it is not, so
                            `tmpfs` stays `tmpfs` while `c` becomes its
                            absolute spelling. --no-canonicalize resolves
                            neither. And it says which operation it did:
                            moved, bound, or mounted -- with a propagation
                            change reported on a line of its own after it. */
                        positive shown_room = 0;
                        positive source_room = 0;
                        p8 address_to shown = canonical
                            ? storage_word_path(operand[1],
                                                address_of shown_room, 0)
                            : null;
                        p8 address_to source_shown = canonical
                            ? storage_word_path(operand[0],
                                                address_of source_room, 0)
                            : null;
                        string_address target = shown ? (string_address)shown
                                                      : operand[1];
                        string_address source = source_shown
                            ? (string_address)source_shown : operand[0];

                        if (options.flags & MS_MOVE)
                                string_format(write, "mount: %s moved to %s.\n",
                                              source, target);
                        else if (options.flags & MS_BIND)
                                string_format(write, "mount: %s bound on %s.\n",
                                              source, target);
                        else
                                string_format(write, "mount: %s mounted on %s.\n",
                                              source, target);
                        if (options.propagation &&
                            !(options.flags & (MS_MOVE |
                                               MS_BIND)))
                                string_format(write,
                                              "mount: %s propagation flags changed.\n",
                                              target);
                        if (shown)
                                memory_free(shown, shown_room);
                        if (source_shown)
                                memory_free(source_shown, source_room);
                }
                status = answer ? 32 : 0;
                goto done;
        }

missing_option:
        string_format(diagnostic, "mount: option requires an argument\n");
        goto done;
no_memory:
        string_format(diagnostic, "mount: no memory\n");
done:
        storage_options_free(address_of options);
        byte_store_release(address_of tag_source);
        return status;
}

/* The mount a spelling names, or null when the table has none. */
static PURE storage_mount address_to storage_umount_target(
    storage_mount_table address_to table, string_address asked)
{
        storage_mount address_to found = null;

        /* Last wins: stacked mounts are unmounted from the top. */
        for (positive at = 0; at < table->count; at++)
                if (table->entry[at].target &&
                    (storage_word(table->entry[at].source, asked) ||
                     storage_word(table->entry[at].target, asked)))
                        found = table->entry + at;
        return found;
}

/*      Every operational refusal umount(8) reports is 32; 1 is reserved for
        usage -- a spelling that was never a mount point under --recursive or
        --all-targets, which do their own table lookup and say so before any
        syscall. A plain umount of a path the table does not hold still takes
        umount(2), which is why "not mounted" and "no mount point specified"
        both leave 32 behind. */
/*      util-linux says what went wrong in its own words rather than the
        kernel's: a path that is nothing to the mount table is "not mounted",
        one the kernel refuses to let go is "target is busy", and a path the
        kernel never saw at all is "no mount point specified". */
static PURE string_address storage_umount_reason(b32 number)
{
        if (number == ERROR_NOT_PERMITTED)
                return (string_address)"must be superuser to unmount.";
        if (number == ERROR_BUSY)
                return (string_address)"target is busy.";
        if (number == ERROR_INVALID)
                return (string_address)"not mounted.";
        if (number == ERROR_NO_ENTRY)
                return (string_address)"no mount point specified.";
        return null;
}

/*      --read-only remounts what it could not unmount, and it needs the
        source to do it: the reference reads that from the mount table, so a
        spelling the table never answered for -- `--no-canonicalize` naming a
        symlink, say -- keeps the busy refusal instead of quietly turning the
        filesystem read-only under the caller. */
static b32 storage_umount_one(writer diagnostic, string_address program,
                             string_address target, string_address type,
                             string_address source,
                             positive flags, bool read_only,
                             bool verbose, bool quiet, bool fake)
{
        bipolar answer = fake ? 0
            : system_call_2(syscall(umount2), (positive)target, flags);

        if (answer && read_only && source && answer == -ERROR_BUSY)
        {
                storage_mount_options remount;

                memory_fill(address_of remount, 0, sizeof(remount));
                remount.flags = MS_REMOUNT | MS_RDONLY;
                remount.mentioned = MS_REMOUNT | MS_RDONLY;
                answer = storage_mount_one(source, target,
                                           null, address_of remount);
        }
        if (answer)
        {
                b32 number = (b32)(answer < 0 ? -answer : answer);
                string_address reason = storage_umount_reason(number);
                /*  --quiet is only about the path that was never a mount
                    point; every other refusal is still said. */
                if (!(quiet && number == ERROR_INVALID))
                {
                        if (reason)
                                string_format(diagnostic, "%s: %s: %s\n",
                                              program, target, reason);
                        else
                                string_format(diagnostic, "%s: %s failed: %s\n",
                                              program, target,
                                              strerror(number));
                }
                return 32;
        }
        if (verbose)
                string_format(diagnostic, "%s: %s (%s) unmounted\n", program,
                              target, type ? type : (string_address)"none");
        return 0;
}

static b32 storage_umount_recursive(writer diagnostic, string_address program,
                                    storage_mount_table address_to table,
                                    string_address root, string_address shown,
                                    string_address types,
                                    positive flags, bool read_only, bool verbose,
                                    bool quiet, bool fake)
{
        b32 failed = 0;
        positive longest = positive_max;
        bool found = false;

        /* Repeated longest-path selection avoids another allocation and
           guarantees children leave before their parent even if proc changes
           record order. Equal lengths are distinct siblings. */
        for (;;)
        {
                positive selected = positive_max;
                positive selected_length = 0;

                for (positive at = 0; at < table->count; at++)
                {
                        storage_mount address_to record = table->entry + at;
                        positive length;

                        if (!record->target)
                                continue;
                        length = string_length(record->target);
                        if (length < longest &&
                            length > selected_length &&
                            realpath_under(root, record->target) &&
                            storage_type_match(types, record->type))
                        {
                                selected = at;
                                selected_length = length;
                        }
                }
                if (selected == positive_max)
                        break;

                longest = selected_length;
                /* All siblings at this depth. */
                for (positive at = 0; at < table->count; at++)
                {
                        storage_mount address_to record = table->entry + at;
                        if (record->target &&
                            string_length(record->target) == selected_length &&
                            realpath_under(root, record->target) &&
                            storage_type_match(types, record->type))
                        {
                                found = true;
                                failed |= storage_umount_one(diagnostic, program,
                                                             record->target,
                                                             record->type,
                                                             record->source, flags,
                                                             read_only, verbose,
                                                             quiet, fake);
                                record->target = null;
                        }
                }
        }
        if (!found)
        {
                /*  The recursive walk says "not mounted" without the stop
                    the single form puts after it, and --quiet is about this
                    sentence as it is about the other. */
                if (!quiet)
                        string_format(diagnostic, "%s: %s: not mounted\n",
                                      program, shown);
                return 1;
        }
        return failed;
}

b32 storage_umount_command(positive argc, string_address address_to argv,
                           writer write, writer diagnostic)
{
        static const argument_option arguments[] = {
            {"all", 'a'},
            {"lazy", 'l'},
            {"force", 'f'},
            {"recursive", 'R'},
            {"read-only", 'r'},
            {"types", 't', ARGUMENT_REQUIRED},
            {"verbose", 'v'},
            {"no-mtab", 'n'},
            {"no-canonicalize", 'c'},
            {"internal-only", 'i'},
            {"all-targets", 'A'},
            {"quiet", 'q'},
            {"detach-loop", 'd'},
            {"test-opts", 'O', ARGUMENT_REQUIRED},
            {"fake", 'F', ARGUMENT_LONG_ONLY},
            {null},
        };
        /*  umount(8) takes no UMOUNT_NOFOLLOW: only its set-user-id path
            does, and then it chdirs to the parent first. So a symlink named
            to umount(2) is followed by the kernel, which is how
            `umount --no-canonicalize clink` reaches the mount under it. */
        positive flags = 0;
        string_address types = null;
        string_address address_to operand = null;
        positive operand_room = 0;
        positive operands = 0;
        bool all = false;
        bool all_targets = false;
        bool recursive = false;
        bool read_only = false;
        bool verbose = false;
        bool canonical = true;
        bool quiet = false;
        bool loop_detach = false;
        bool fake = false;
        argument_cursor taking = {.argc = argc, .argv = argv, .at = 1};
        string_address value;
        b32 option;
        storage_mount_table table;
        bool loaded;
        b32 failed = 0;

        (void)write;
        while ((option = storage_argument_next(address_of taking, arguments, address_of value)) != ARGUMENT_END)
        {
                if (option == ARGUMENT_OPERAND)
                {
                        if (!array_store_reserve(operand, operand_room,
                                                 operands, operands + 1, 16))
                        {
                                string_format(diagnostic,
                                              "umount: no memory\n");
                                goto failed_early;
                        }
                        operand[operands++] = value;
                }
                else if (option == ARGUMENT_MISSING)
                        goto missing_option;
                else if (option == ARGUMENT_UNKNOWN)
                {
                        storage_option_unknown(diagnostic,
                                               (string_address)"umount",
                                               address_of taking);
                        goto failed_early;
                }
                else if (option == 'a')
                        all = true;
                else if (option == 'A')
                        all_targets = true;
                else if (option == 'q')
                        quiet = true;
                else if (option == 'F')
                        fake = true;
                else if (option == 'd')
                        loop_detach = true;
                else if (option == 'O' || option == 'n')
                        ; /* Every mount matches an empty option filter, and
                             there is no mtab to leave alone. */
                else if (option == 'l')
                        flags |= MNT_DETACH;
                else if (option == 'f')
                        flags |= MNT_FORCE;
                else if (option == 'R')
                        recursive = true;
                else if (option == 'r')
                        read_only = true;
                else if (option == 'v')
                        verbose = true;
                else if (option == 'c')
                        canonical = false;
                else if (option == 't')
                        types = value;
        }

        {
                static const argument_exclusive_pair reach[] = {
                    {'a', (string_address)"all"},
                    {'A', (string_address)"all-targets"}};
                static const argument_exclusive_pair depth[] = {
                    {'r', (string_address)"read-only"},
                    {'R', (string_address)"recursive"}};

                if (argument_exclusive_refuse(diagnostic, (string_address)"umount", argc, argv, arguments, false, reach, array_count(reach)) ||
                    argument_exclusive_refuse(diagnostic, (string_address)"umount", argc, argv, arguments, false, depth, array_count(depth)))
                        goto failed_early;
        }

        if (!all && !operands)
        {
                string_format(diagnostic, "umount: missing operand\n");
                goto failed_early;
        }

        if (recursive && types)
        {
                string_format(diagnostic,
                              "umount: options --recursive and --types cannot be combined\n");
                goto failed_early;
        }
        if (all && operands)
        {
                string_format(diagnostic,
                              "umount: -a takes no operands\n");
                goto failed_early;
        }

        loaded = storage_mount_table_load(address_of table, diagnostic);
        if (!loaded)
                goto failed_early;

        if (all)
        {
                /* Never dismantle the root mount. */
                for (positive i = table.count; i; i--)
                {
                        storage_mount address_to record = table.entry + i - 1;
                        if (!storage_word(record->target, "/") &&
                            storage_type_match(types, record->type))
                                failed |= storage_umount_one(
                                    diagnostic, (string_address)"umount",
                                    record->target, record->type,
                                    record->source, flags,
                                    read_only, verbose, quiet, fake);
                }
        }

        for (positive i = 0; i < operands; i++)
        {
                /* util-linux looks a spelling up by its canonical path, so a
                   relative or symlinked target names the same mount. */
                /* The table holds absolute targets, so a relative word is
                   made absolute even under --no-canonicalize; that option
                   says not to follow the last symlink, not to leave the
                   spelling as it was typed. */
                positive resolved_room = 0;
                p8 address_to resolved = storage_word_path(
                    operand[i], address_of resolved_room,
                    canonical ? 0 : STORAGE_OPEN_NOFOLLOW);
                string_address asked = resolved ? (string_address)resolved
                                                : operand[i];
                b32 answer;
                if (all_targets)
                {
                        /*  --all-targets names one filesystem, by any of its
                            mount points or by its source, and unmounts every
                            mount of that same device -- the newest record
                            first, stopping at the first refusal. A spelling
                            the table does not hold never reaches umount(2),
                            so it is the usage answer 1 and its sentence ends
                            without a stop. */
                        storage_mount address_to chosen =
                            storage_umount_target(address_of table, asked);

                        if (!chosen && asked != operand[i])
                                chosen = storage_umount_target(
                                    address_of table, operand[i]);
                        if (!chosen)
                        {
                                if (!quiet)
                                        string_format(
                                            diagnostic, "umount: %s: %s\n",
                                            operand[i],
                                            resolved ? "not mounted"
                                                     : "not found");
                                answer = 1;
                        }
                        else
                        {
                                string_address device = chosen->device;

                                answer = 0;
                                for (positive at = table.count; at && !answer;
                                     at--)
                                {
                                        storage_mount address_to record =
                                            table.entry + at - 1;

                                        if (!record->target ||
                                            !storage_word(record->device,
                                                          device))
                                                continue;
                                        if (recursive)
                                                answer = storage_umount_recursive(
                                                    diagnostic,
                                                    (string_address)"umount",
                                                    address_of table,
                                                    record->target,
                                                    record->target, types,
                                                    flags, read_only, verbose,
                                                    quiet, fake);
                                        else
                                        {
                                                answer = storage_umount_one(
                                                    diagnostic,
                                                    (string_address)"umount",
                                                    record->target,
                                                    record->type,
                                                    record->source, flags,
                                                    read_only, verbose, quiet,
                                                    fake);
                                                record->target = null;
                                        }
                                }
                        }
                }
                else
                {
                        storage_mount address_to found =
                            storage_umount_target(address_of table, asked);
                        if (!found && asked != operand[i])
                                found = storage_umount_target(address_of table,
                                                              operand[i]);
                        /*  A path the table does not hold is named to the
                            kernel as it was written; one it holds is named by
                            the table's own spelling, which is how the
                            reference names it. */
                        string_address target = found ? found->target
                                                      : operand[i];
                        if (recursive)
                                answer = storage_umount_recursive(
                                    diagnostic, (string_address)"umount",
                                    address_of table,
                                    found ? found->target : asked, operand[i],
                                    types, flags, read_only, verbose, quiet,
                                    fake);
                        else
                        {
                                answer = storage_umount_one(
                                    diagnostic, (string_address)"umount", target,
                                    found ? found->type : null,
                                    found ? found->source : null, flags, read_only,
                                    verbose, quiet, fake);
                                /*  --detach-loop frees the loop device the
                                    source names once the filesystem is gone,
                                    and it needs a source to name one. Nothing
                                    here owns a loop device, so the step is a
                                    no-op -- but where the table never answered
                                    for this target there is no source at all,
                                    and the reference calls that a failure
                                    after a successful unmount. */
                                if (!answer && loop_detach && !found && !fake)
                                {
                                        string_format(
                                            diagnostic,
                                            "umount: %s: filesystem was unmounted,"
                                            " but any subsequent operation failed:"
                                            " Invalid argument.\n",
                                            target);
                                        answer = 1;
                                }
                        }
                }
                /*  Every operand contributes its own answer: two refusals of
                    32 are 64, not 32, and only a total past 255 saturates. */
                failed = failed + answer > 255 ? 255 : failed + answer;
                if (resolved)
                        memory_free(resolved, resolved_room);
        }

        storage_mount_table_release(address_of table);
        array_store_release(operand, operand_room, operands);
        return failed;

missing_option:
        string_format(diagnostic, "umount: option requires an argument\n");
failed_early:
        array_store_release(operand, operand_room, operands);
        return 1;
}

/* ---- The other direction: the tables and filesystems an install writes. ---- */

/*
        New partition tables and filesystems, for a disk Moonwater installs to.

        blkid, findfs and mount beside this read these structures; this is the
        other direction, and only as much of it as an install needs: a GPT, a
        FAT32 system partition the firmware loads the image from, and an ext4
        the kernel keeps /bowls, /root and /home on. Nothing here picks a disk.
        The caller hands over an open descriptor and a byte range, and every
        write is positional, so one descriptor on the whole disk formats both
        partitions and a regular file stands in for a disk in the checks.

        Each layout is the one the reference tools write for the same request,
        so their checkers are the oracle: sfdisk --verify for the table, the
        kernel's vfat and the firmware for the system partition, and e2fsck -fn
        for the ext4.
*/

#define STORAGE_FORMAT_ZERO_CHUNK ((positive)1 << 20)

typedef struct
{
        p8 type[16];
        p8 unique[16];
        p64 first;
        p64 last;
        string_address name;
} storage_format_partition;

/* Everything a format would otherwise invent, handed in so that two formats
   of the same size given the same identity are the same bytes. */
typedef struct
{
        p8 uuid[16];
        p8 hash_seed[16];
        p32 time;
        string_address label;
} storage_format_identity;

/* Every field here is little-endian on disk, as the machine floor is; the
   one big-endian field, ext4's jbd2 header, takes the network store. */
#define storage_put16(at, value) memory_store_unaligned(p16, (at), (value))
#define storage_put32(at, value) memory_store_unaligned(p32, (at), (value))
#define storage_put64(at, value) memory_store_unaligned(p64, (at), (value))
#define storage_put32_be(at, value) network_store_32((at), (p32)(value))

static bipolar storage_format_write(bipolar handle, p8 address_to bytes,
                                    positive length, p64 offset)
{
        bipolar written = storage_write(handle, bytes, length, offset);

        return written < 0 ? written : 0;
}

static bipolar storage_format_zero(bipolar handle, p64 offset, p64 length)
{
        p8 address_to zeros;
        bipolar failed = 0;

        if (!length)
                return 0;

        zeros = memory_checked(STORAGE_FORMAT_ZERO_CHUNK);
        if (!zeros)
                return -ERROR_NO_MEMORY;

        memory_zero(zeros, STORAGE_FORMAT_ZERO_CHUNK);

        while (length && !failed)
        {
                positive part = length < STORAGE_FORMAT_ZERO_CHUNK
                                    ? (positive)length
                                    : STORAGE_FORMAT_ZERO_CHUNK;

                failed = storage_format_write(handle, zeros, part, offset);
                offset += part;
                length -= part;
        }

        memory_free(zeros, STORAGE_FORMAT_ZERO_CHUNK);
        return failed;
}

/*
        CRC-32C the way ext4 and jbd2 chain it is hash_crc32c: no inversion on
        the way in or out, so a seed of ~0 starts a superblock's and a previous
        result continues one. hash_crc32 is the other polynomial, GPT's, and
        chains the same way, so a GPT sum is its complement from a seed of ~0.
*/

// GPT -----------------------------------------------------------

/* Type GUIDs in their on-disk order: the first three fields little-endian. */
static p8 storage_gpt_system_type[16] = {
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b};
static p8 storage_gpt_linux_type[16] = {
    0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
    0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};

#define STORAGE_GPT_ENTRIES 128
#define STORAGE_GPT_ENTRY 128
#define STORAGE_GPT_HEADER 92
#define STORAGE_GPT_NAME 36

static p64 storage_gpt_table_sectors(p32 sector_size)
{
        return (STORAGE_GPT_ENTRIES * STORAGE_GPT_ENTRY + sector_size - 1) /
               sector_size;
}

/* Where partitions may go on a disk this size, so the caller can place them
   before asking for the table. False for a geometry no table fits. */
static bool storage_gpt_span(p64 sectors, p32 sector_size,
                             p64 address_to first, p64 address_to last)
{
        p64 table;

        if (sector_size < 512 || sector_size > 4096 ||
            (sector_size & (sector_size - 1)))
                return false;

        table = storage_gpt_table_sectors(sector_size);
        if (sectors < 2 * (2 + table) + 1)
                return false;

        address_to first = 2 + table;
        address_to last = sectors - 2 - table;
        return true;
}

static fn storage_gpt_header(p8 address_to sector, p64 self, p64 other,
                             p64 first, p64 last, p8 address_to disk,
                             p64 entries, p32 entries_crc)
{
        memory_zero(sector, STORAGE_GPT_HEADER);
        memory_copy(sector, "EFI PART", 8);
        storage_put32(sector + 8, 0x00010000);
        storage_put32(sector + 12, STORAGE_GPT_HEADER);
        storage_put64(sector + 24, self);
        storage_put64(sector + 32, other);
        storage_put64(sector + 40, first);
        storage_put64(sector + 48, last);
        memory_copy(sector + 56, disk, 16);
        storage_put64(sector + 72, entries);
        storage_put32(sector + 80, STORAGE_GPT_ENTRIES);
        storage_put32(sector + 84, STORAGE_GPT_ENTRY);
        storage_put32(sector + 88, entries_crc);
        storage_put32(sector + 16,
                      ~hash_crc32(~(p32)0, sector, STORAGE_GPT_HEADER));
}

/*
        A protective MBR, both headers and both entry arrays.

        The backup goes down first and the protective MBR last, so a write that
        stops part way leaves a disk no tool mistakes for a finished table.
*/
static bipolar storage_format_gpt(bipolar handle, p64 sectors, p32 sector_size,
                                  p8 address_to disk,
                                  storage_format_partition address_to parts,
                                  positive count)
{
        p64 first_usable;
        p64 last_usable;
        p64 table_sectors;
        positive table_bytes;
        p8 address_to table;
        p8 address_to sector;
        p32 entries_crc;
        bipolar failed;

        if (!storage_gpt_span(sectors, sector_size, address_of first_usable,
                              address_of last_usable) ||
            count > STORAGE_GPT_ENTRIES)
                return -ERROR_INVALID;

        for (positive at = 0; at < count; at++)
        {
                if (parts[at].first < first_usable ||
                    parts[at].last > last_usable ||
                    parts[at].first > parts[at].last)
                        return -ERROR_INVALID;

                for (positive other = 0; other < at; other++)
                        if (parts[at].first <= parts[other].last &&
                            parts[other].first <= parts[at].last)
                                return -ERROR_INVALID;
        }

        table_sectors = storage_gpt_table_sectors(sector_size);
        table_bytes = (positive)(table_sectors * sector_size);
        table = memory_checked(table_bytes + sector_size);
        if (!table)
                return -ERROR_NO_MEMORY;

        sector = table + table_bytes;
        memory_zero(table, table_bytes + sector_size);

        for (positive at = 0; at < count; at++)
        {
                p8 address_to entry = table + at * STORAGE_GPT_ENTRY;
                string_address name = parts[at].name;

                memory_copy(entry, parts[at].type, 16);
                memory_copy(entry + 16, parts[at].unique, 16);
                storage_put64(entry + 32, parts[at].first);
                storage_put64(entry + 40, parts[at].last);

                for (positive letter = 0;
                     name && letter < STORAGE_GPT_NAME && name[letter]; letter++)
                        storage_put16(entry + 56 + 2 * letter, (p8)name[letter]);
        }

        entries_crc = ~hash_crc32(~(p32)0, table,
                                  STORAGE_GPT_ENTRIES * STORAGE_GPT_ENTRY);

        failed = storage_format_write(handle, table, table_bytes,
                                      (sectors - 1 - table_sectors) *
                                          sector_size);

        if (!failed)
        {
                storage_gpt_header(sector, sectors - 1, 1, first_usable,
                                   last_usable, disk,
                                   sectors - 1 - table_sectors, entries_crc);
                failed = storage_format_write(handle, sector, sector_size,
                                              (sectors - 1) * sector_size);
        }

        if (!failed)
                failed = storage_format_write(handle, table, table_bytes,
                                              2 * (p64)sector_size);

        if (!failed)
        {
                memory_zero(sector, sector_size);
                storage_gpt_header(sector, 1, sectors - 1, first_usable,
                                   last_usable, disk, 2, entries_crc);
                failed = storage_format_write(handle, sector, sector_size,
                                              sector_size);
        }

        if (!failed)
        {
                p8 address_to slot = sector + 446;

                memory_zero(sector, sector_size);
                slot[2] = 0x02;
                slot[4] = 0xee;
                slot[5] = slot[6] = slot[7] = 0xff;
                storage_put32(slot + 8, 1);
                storage_put32(slot + 12, sectors - 1 > 0xffffffff
                                             ? 0xffffffff
                                             : sectors - 1);
                sector[510] = 0x55;
                sector[511] = 0xaa;
                failed = storage_format_write(handle, sector, sector_size, 0);
        }

        memory_free(table, table_bytes + sector_size);
        return failed;
}

// FAT32 ---------------------------------------------------------

#define STORAGE_FAT_RESERVED 32
#define STORAGE_FAT_CLUSTER 4096
#define STORAGE_FAT32_FEWEST 65525
#define STORAGE_FAT32_MOST 0x0ffffff4

/*
        A FAT32 with nothing in it but its label, the shape firmware expects
        of an EFI system partition.

        Clusters are 4 KiB whatever the sector size. Each FAT is sized from an
        over-estimate of the clusters it has to describe, then the clusters
        are counted from what is left; the estimate only shrinks, so one
        recount is enough and the tables are never short.
*/
static bipolar storage_format_fat32(bipolar handle, p64 offset, p64 bytes,
                                    p32 sector_size, p64 hidden,
                                    storage_format_identity address_to identity)
{
        p64 total = bytes / sector_size;
        p32 per_cluster;
        p64 fat_sectors;
        p64 clusters;
        p64 data;
        p8 address_to sector;
        bipolar failed;

        if (sector_size < 512 || sector_size > STORAGE_FAT_CLUSTER ||
            (sector_size & (sector_size - 1)) || total > 0xffffffff ||
            total <= STORAGE_FAT_RESERVED)
                return -ERROR_INVALID;

        per_cluster = STORAGE_FAT_CLUSTER / sector_size;
        fat_sectors = (((total - STORAGE_FAT_RESERVED) / per_cluster + 2) * 4 +
                       sector_size - 1) /
                      sector_size;

        if (STORAGE_FAT_RESERVED + 2 * fat_sectors >= total)
                return -ERROR_INVALID;

        clusters = (total - STORAGE_FAT_RESERVED - 2 * fat_sectors) / per_cluster;
        if (clusters < STORAGE_FAT32_FEWEST || clusters > STORAGE_FAT32_MOST)
                return -ERROR_INVALID;

        data = STORAGE_FAT_RESERVED + 2 * fat_sectors;

        failed = storage_format_zero(handle, offset,
                                     (data + per_cluster) * sector_size);
        if (failed)
                return failed;

        sector = memory_checked(sector_size);
        if (!sector)
                return -ERROR_NO_MEMORY;

        //      The boot sector, and its copy at sector 6.
        memory_zero(sector, sector_size);
        sector[0] = 0xeb;
        sector[1] = 0x58;
        sector[2] = 0x90;
        memory_copy(sector + 3, "MSWIN4.1", 8);
        storage_put16(sector + 11, sector_size);
        sector[13] = (p8)per_cluster;
        storage_put16(sector + 14, STORAGE_FAT_RESERVED);
        sector[16] = 2;
        sector[21] = 0xf8;
        storage_put16(sector + 24, 63);
        storage_put16(sector + 26, 255);
        storage_put32(sector + 28, hidden > 0xffffffff ? 0xffffffff : hidden);
        storage_put32(sector + 32, total);
        storage_put32(sector + 36, fat_sectors);
        storage_put32(sector + 44, 2);
        storage_put16(sector + 48, 1);
        storage_put16(sector + 50, 6);
        sector[64] = 0x80;
        sector[66] = 0x29;
        memory_copy(sector + 67, identity->uuid, 4);
        memory_fill(sector + 71, ' ', 11);
        for (positive at = 0; identity->label && at < 11 && identity->label[at];
             at++)
                sector[71 + at] = byte_to_upper(identity->label[at]);
        memory_copy(sector + 82, "FAT32   ", 8);
        //      A BIOS that jumps here anyway asks for the next boot device.
        sector[90] = 0xcd;
        sector[91] = 0x18;
        sector[510] = 0x55;
        sector[511] = 0xaa;

        failed = storage_format_write(handle, sector, sector_size, offset);
        if (!failed)
                failed = storage_format_write(handle, sector, sector_size,
                                              offset + 6 * (p64)sector_size);

        //      The label again, as the root directory's first entry.
        if (!failed)
        {
                p8 label[11];

                memory_copy(label, sector + 71, 11);
                memory_zero(sector, sector_size);
                memory_copy(sector, label, 11);
                sector[11] = 0x08;
                failed = storage_format_write(handle, sector, sector_size,
                                              offset + data * sector_size);
        }

        //      FSInfo, and its copy at sector 7.
        if (!failed)
        {
                memory_zero(sector, sector_size);
                storage_put32(sector, 0x41615252);
                storage_put32(sector + 484, 0x61417272);
                storage_put32(sector + 488, clusters - 1);
                storage_put32(sector + 492, 3);
                storage_put32(sector + 508, 0xaa550000);
                failed = storage_format_write(handle, sector, sector_size,
                                              offset + sector_size);
        }
        if (!failed)
                failed = storage_format_write(handle, sector, sector_size,
                                              offset + 7 * (p64)sector_size);

        //      Both FATs: the media byte, a clean volume, and the root's chain.
        if (!failed)
        {
                memory_zero(sector, sector_size);
                storage_put32(sector, 0x0ffffff8);
                storage_put32(sector + 4, 0x0fffffff);
                storage_put32(sector + 8, 0x0fffffff);
                failed = storage_format_write(
                    handle, sector, sector_size,
                    offset + STORAGE_FAT_RESERVED * (p64)sector_size);
        }
        if (!failed)
                failed = storage_format_write(
                    handle, sector, sector_size,
                    offset + (STORAGE_FAT_RESERVED + fat_sectors) * sector_size);

        memory_free(sector, sector_size);
        return failed;
}

// ext4 ----------------------------------------------------------

/*
        An ext4 as `mkfs.ext4 -b 4096 -I 256 -i 16384 -m 0 -O ^flex_bg,
        ^resize_inode,^64bit,^orphan_file,^metadata_csum_seed -E
        lazy_itable_init=1` lays it out: has_journal ext_attr dir_index
        filetype extent sparse_super large_file huge_file dir_nlink
        extra_isize metadata_csum.

        Without flex_bg every group keeps its own bitmaps and inode table at
        its start, so where anything is is arithmetic on the group number and
        nothing needs allocating. Without 64bit the descriptors are 32 bytes
        and the filesystem stops at 2^32 blocks, 16 TiB; the install caps the
        partition there. Every block bitmap is written, but only group 0's
        inode table is: the others are marked uninitialised, which is what
        lets a terabyte format in megabytes of writes, and the kernel's lazy
        init thread zeroes them after the first mount.
*/
#define STORAGE_EXT4_BLOCK 4096
#define STORAGE_EXT4_PER_GROUP 32768
#define STORAGE_EXT4_INODE 256
#define STORAGE_EXT4_INODES_PER_BLOCK (STORAGE_EXT4_BLOCK / STORAGE_EXT4_INODE)
#define STORAGE_EXT4_RATIO 16384
#define STORAGE_EXT4_DESCRIPTOR 32
#define STORAGE_EXT4_FIRST_INODE 11
#define STORAGE_EXT4_ROOT 2
#define STORAGE_EXT4_JOURNAL 8
#define STORAGE_EXT4_LOST 11
#define STORAGE_EXT4_LOST_BLOCKS 4
#define STORAGE_EXT4_JOURNAL_MOST 16384
#define STORAGE_EXT4_FEWEST ((p64)16384)
#define STORAGE_EXT4_MOST ((p64)0xffffffff)
#define STORAGE_EXT4_TAIL (STORAGE_EXT4_BLOCK - 12)

#define STORAGE_EXT4_INODE_UNINIT 0x0001
#define STORAGE_EXT4_ITABLE_ZEROED 0x0004
#define STORAGE_EXT4_EXTENTS_FLAG 0x80000

typedef struct
{
        p64 blocks;
        p32 groups;
        p32 inodes_per_group;
        p32 table_blocks;
        p32 descriptor_blocks;
        p32 journal_blocks;
        p32 journal_group;
        p64 journal_start;
        p64 root_block;
} storage_ext4_plan;

static bool storage_ext4_backup(p64 group)
{
        static const p8 bases[] = {3, 5, 7};

        if (group <= 1)
                return true;

        for (positive at = 0; at < array_count(bases); at++)
        {
                p64 power = bases[at];

                while (power < group)
                        power *= bases[at];

                if (power == group)
                        return true;
        }

        return false;
}

static p64 storage_ext4_group_blocks(storage_ext4_plan address_to plan,
                                     p64 group)
{
        return group + 1 < plan->groups ? STORAGE_EXT4_PER_GROUP
                                        : plan->blocks -
                                              group * STORAGE_EXT4_PER_GROUP;
}

//      Superblock and descriptor copies, where the group carries them.
static p64 storage_ext4_front(storage_ext4_plan address_to plan, p64 group)
{
        return storage_ext4_backup(group) ? 1 + plan->descriptor_blocks : 0;
}

static p64 storage_ext4_overhead(storage_ext4_plan address_to plan,
                                 p64 group)
{
        return storage_ext4_front(plan, group) + 2 + plan->table_blocks;
}

/*
        The numbers mke2fs arrives at: an inode per 16 KiB spread evenly and
        rounded up to whole table blocks, and a last group dropped when it
        could not hold its own metadata and fifty blocks besides. The journal
        follows e2fsprogs' size table up to 64 MiB and goes in the first group
        from the middle of the disk with room for it in one extent.
*/
static bool storage_ext4_layout(storage_ext4_plan address_to plan, p64 bytes)
{
        p64 blocks = bytes / STORAGE_EXT4_BLOCK;

        memory_zero(plan, sizeof(address_to plan));

        if (blocks < STORAGE_EXT4_FEWEST || blocks > STORAGE_EXT4_MOST)
                return false;

        for (;;)
        {
                p64 groups = (blocks + STORAGE_EXT4_PER_GROUP - 1) /
                             STORAGE_EXT4_PER_GROUP;
                p64 inodes = blocks * STORAGE_EXT4_BLOCK / STORAGE_EXT4_RATIO;
                p64 per_group = (inodes + groups - 1) / groups;
                p64 last;

                per_group = (per_group + STORAGE_EXT4_INODES_PER_BLOCK - 1) /
                            STORAGE_EXT4_INODES_PER_BLOCK *
                            STORAGE_EXT4_INODES_PER_BLOCK;
                if (per_group > STORAGE_EXT4_BLOCK * 8)
                        per_group = STORAGE_EXT4_BLOCK * 8;

                plan->blocks = blocks;
                plan->groups = (p32)groups;
                plan->inodes_per_group = (p32)per_group;
                plan->table_blocks = (p32)(per_group /
                                           STORAGE_EXT4_INODES_PER_BLOCK);
                plan->descriptor_blocks =
                    (p32)((groups * STORAGE_EXT4_DESCRIPTOR +
                           STORAGE_EXT4_BLOCK - 1) /
                          STORAGE_EXT4_BLOCK);

                last = storage_ext4_group_blocks(plan, groups - 1);
                if (groups > 1 &&
                    last < storage_ext4_overhead(plan, groups - 1) + 50)
                {
                        blocks -= last;
                        continue;
                }

                break;
        }

        plan->journal_blocks = plan->blocks < 32768           ? 1024
                               : plan->blocks < 256 * 1024     ? 4096
                               : plan->blocks < 512 * 1024     ? 8192
                                                               : STORAGE_EXT4_JOURNAL_MOST;

        plan->root_block = storage_ext4_overhead(plan, 0);

        for (p64 tried = 0; tried < plan->groups; tried++)
        {
                p64 group = (plan->blocks / 2 / STORAGE_EXT4_PER_GROUP + tried) %
                            plan->groups;
                p64 taken = storage_ext4_overhead(plan, group) +
                            (group ? 0 : 1 + STORAGE_EXT4_LOST_BLOCKS);

                if (storage_ext4_group_blocks(plan, group) >=
                    taken + plan->journal_blocks)
                {
                        plan->journal_group = (p32)group;
                        plan->journal_start = group * STORAGE_EXT4_PER_GROUP + taken;
                        return true;
                }
        }

        return false;
}

static p64 storage_ext4_used(storage_ext4_plan address_to plan, p64 group)
{
        return storage_ext4_overhead(plan, group) +
               (group ? 0 : 1 + STORAGE_EXT4_LOST_BLOCKS) +
               (group == plan->journal_group ? plan->journal_blocks : 0);
}

static fn storage_bits_set(p8 address_to bitmap, p64 from, p64 to)
{
        while (from < to && from % 8)
        {
                bitmap[from / 8] |= (p8)(1 << (from % 8));
                from++;
        }

        if (to - from >= 8)
        {
                memory_fill(bitmap + from / 8, (b8)0xff, (positive)((to - from) / 8));
                from += (to - from) / 8 * 8;
        }

        while (from < to)
        {
                bitmap[from / 8] |= (p8)(1 << (from % 8));
                from++;
        }
}

static p32 storage_ext4_inode_seed(p32 filesystem_seed, p32 number)
{
        p8 word[4];

        storage_put32(word, number);
        filesystem_seed = hash_crc32c(filesystem_seed, word, 4);
        storage_put32(word, 0);
        return hash_crc32c(filesystem_seed, word, 4);
}

/*
        One inode. Reserved ones are empty but still carry a checksum, the
        way mke2fs writes them; the bad-block inode alone also carries the
        format's time. A file gets its blocks as one extent held in the inode.
*/
static fn storage_ext4_inode(p8 address_to inode, p32 seed, p32 number,
                             p16 mode, p16 links, p64 size, p64 start,
                             p32 count, p32 time, bool extra)
{
        memory_zero(inode, STORAGE_EXT4_INODE);
        storage_put16(inode, mode);
        storage_put32(inode + 0x04, size);
        storage_put32(inode + 0x08, time);
        storage_put32(inode + 0x0c, time);
        storage_put32(inode + 0x10, time);
        storage_put16(inode + 0x1a, links);
        storage_put32(inode + 0x1c, (p64)count * (STORAGE_EXT4_BLOCK / 512));
        storage_put32(inode + 0x6c, size >> 32);

        if (count)
        {
                storage_put32(inode + 0x20, STORAGE_EXT4_EXTENTS_FLAG);
                storage_put16(inode + 0x28, 0xf30a);
                storage_put16(inode + 0x2a, 1);
                storage_put16(inode + 0x2c, 4);
                storage_put16(inode + 0x38, count);
                storage_put16(inode + 0x3a, start >> 32);
                storage_put32(inode + 0x3c, start);
        }

        if (extra)
        {
                storage_put16(inode + 0x80, 32);
                storage_put32(inode + 0x90, time);
        }

        seed = hash_crc32c(storage_ext4_inode_seed(seed, number), inode,
                              STORAGE_EXT4_INODE);
        storage_put16(inode + 0x7c, seed);
        if (extra)
                storage_put16(inode + 0x82, seed >> 16);
}

static fn storage_ext4_entry(p8 address_to at, p32 inode, p64 length,
                             string_address name)
{
        positive name_length = string_length(name);

        storage_put32(at, inode);
        storage_put16(at + 4, length);
        at[6] = (p8)name_length;
        at[7] = inode ? 2 : 0;
        memory_copy(at + 8, name, name_length);
}

static fn storage_ext4_tail(p8 address_to block, p32 seed, p32 inode)
{
        p8 address_to tail = block + STORAGE_EXT4_TAIL;

        memory_zero(tail, 12);
        storage_put16(tail + 4, 12);
        tail[7] = 0xde;
        storage_put32(tail + 8,
                      hash_crc32c(storage_ext4_inode_seed(seed, inode),
                                     block, STORAGE_EXT4_TAIL));
}

static fn storage_ext4_super(p8 address_to super,
                             storage_ext4_plan address_to plan,
                             storage_format_identity address_to identity,
                             p8 address_to journal_map, p64 free_blocks,
                             p32 group)
{
        p64 inodes = (p64)plan->groups * plan->inodes_per_group;
        p64 journal_bytes = (p64)plan->journal_blocks * STORAGE_EXT4_BLOCK;

        memory_zero(super, 1024);
        storage_put32(super + 0x00, inodes);
        storage_put32(super + 0x04, plan->blocks);
        storage_put32(super + 0x0c, free_blocks);
        storage_put32(super + 0x10, inodes - STORAGE_EXT4_FIRST_INODE);
        storage_put32(super + 0x18, 2);
        storage_put32(super + 0x1c, 2);
        storage_put32(super + 0x20, STORAGE_EXT4_PER_GROUP);
        storage_put32(super + 0x24, STORAGE_EXT4_PER_GROUP);
        storage_put32(super + 0x28, plan->inodes_per_group);
        storage_put32(super + 0x30, identity->time);
        storage_put16(super + 0x36, 0xffff);
        storage_put16(super + 0x38, 0xef53);
        //      A copy is not known to be clean: a check that falls back to
        //      one should look.
        storage_put16(super + 0x3a, group ? 0 : 1);
        storage_put16(super + 0x3c, 1);
        storage_put32(super + 0x40, identity->time);
        storage_put32(super + 0x4c, 1);
        storage_put32(super + 0x54, STORAGE_EXT4_FIRST_INODE);
        storage_put16(super + 0x58, STORAGE_EXT4_INODE);
        storage_put16(super + 0x5a, group);
        storage_put32(super + 0x5c, 0x0004 | 0x0008 | 0x0020);
        storage_put32(super + 0x60, 0x0002 | 0x0040);
        storage_put32(super + 0x64,
                      0x0001 | 0x0002 | 0x0008 | 0x0020 | 0x0040 | 0x0400);
        memory_copy(super + 0x68, identity->uuid, 16);
        for (positive at = 0; identity->label && at < 16 && identity->label[at];
             at++)
                super[0x78 + at] = identity->label[at];
        storage_put32(super + 0xe0, STORAGE_EXT4_JOURNAL);
        memory_copy(super + 0xec, identity->hash_seed, 16);
        super[0xfc] = 1;
        super[0xfd] = 1;
        storage_put32(super + 0x100, 0x0004 | 0x0008);
        storage_put32(super + 0x108, identity->time);
        memory_copy(super + 0x10c, journal_map, 60);
        storage_put32(super + 0x148, journal_bytes >> 32);
        storage_put32(super + 0x14c, journal_bytes);
        storage_put16(super + 0x15c, 32);
        storage_put16(super + 0x15e, 32);
        storage_put32(super + 0x160, 1);
        super[0x175] = 1;
        storage_put32(super + 0x3fc, hash_crc32c(~(p32)0, super, 0x3fc));
}

static bipolar storage_format_ext4(bipolar handle, p64 offset, p64 bytes,
                                   storage_format_identity address_to identity)
{
        storage_ext4_plan plan;
        positive descriptor_bytes;
        p8 address_to descriptors;
        p8 address_to block;
        p32 seed;
        p64 free_blocks = 0;
        bipolar failed = 0;

        if (!storage_ext4_layout(address_of plan, bytes))
                return -ERROR_INVALID;

        seed = hash_crc32c(~(p32)0, identity->uuid, 16);
        descriptor_bytes = (positive)plan.descriptor_blocks * STORAGE_EXT4_BLOCK;
        descriptors = memory_checked(descriptor_bytes + STORAGE_EXT4_BLOCK);
        if (!descriptors)
                return -ERROR_NO_MEMORY;

        block = descriptors + descriptor_bytes;
        memory_zero(descriptors, descriptor_bytes + STORAGE_EXT4_BLOCK);

        //      Nothing of an earlier filesystem left where a probe looks first.
        failed = storage_format_write(handle, block, STORAGE_EXT4_BLOCK, offset);

        //      A block bitmap for every group, and every group's descriptor.
        for (p64 group = 0; group < plan.groups && !failed; group++)
        {
                p8 address_to descriptor = descriptors +
                                           group * STORAGE_EXT4_DESCRIPTOR;
                p64 start = group * STORAGE_EXT4_PER_GROUP;
                p64 bitmap = start + storage_ext4_front(address_of plan, group);
                p64 group_blocks = storage_ext4_group_blocks(address_of plan, group);
                p64 used = storage_ext4_used(address_of plan, group);
                p64 free_inodes = plan.inodes_per_group -
                                  (group ? 0 : STORAGE_EXT4_FIRST_INODE);

                memory_zero(block, STORAGE_EXT4_BLOCK);
                storage_bits_set(block, 0, storage_ext4_overhead(address_of plan, group));
                if (!group)
                        storage_bits_set(block, plan.root_block,
                                         plan.root_block + 1 +
                                             STORAGE_EXT4_LOST_BLOCKS);
                if (group == plan.journal_group)
                        storage_bits_set(block, plan.journal_start - start,
                                         plan.journal_start - start +
                                             plan.journal_blocks);
                storage_bits_set(block, group_blocks, STORAGE_EXT4_BLOCK * 8);

                storage_put32(descriptor + 0x00, bitmap);
                storage_put32(descriptor + 0x04, bitmap + 1);
                storage_put32(descriptor + 0x08, bitmap + 2);
                storage_put16(descriptor + 0x0c, group_blocks - used);
                storage_put16(descriptor + 0x0e, free_inodes);
                storage_put16(descriptor + 0x10, group ? 0 : 2);
                storage_put16(descriptor + 0x12, group ? STORAGE_EXT4_INODE_UNINIT
                                                       : STORAGE_EXT4_ITABLE_ZEROED);
                storage_put16(descriptor + 0x18,
                              hash_crc32c(seed, block, STORAGE_EXT4_BLOCK));
                storage_put16(descriptor + 0x1c, free_inodes);
                free_blocks += group_blocks - used;

                failed = storage_format_write(handle, block, STORAGE_EXT4_BLOCK,
                                              offset + bitmap * STORAGE_EXT4_BLOCK);
        }

        //      Group 0's inode bitmap: the reserved inodes and lost+found.
        if (!failed)
        {
                memory_zero(block, STORAGE_EXT4_BLOCK);
                storage_bits_set(block, 0, STORAGE_EXT4_FIRST_INODE);
                storage_bits_set(block, plan.inodes_per_group,
                                 STORAGE_EXT4_BLOCK * 8);
                storage_put16(descriptors + 0x1a,
                              hash_crc32c(seed, block,
                                             plan.inodes_per_group / 8));
                failed = storage_format_write(
                    handle, block, STORAGE_EXT4_BLOCK,
                    offset + (storage_ext4_front(address_of plan, 0) + 1) *
                                 STORAGE_EXT4_BLOCK);
        }

        for (p64 group = 0; group < plan.groups; group++)
        {
                p8 address_to descriptor = descriptors +
                                           group * STORAGE_EXT4_DESCRIPTOR;
                p8 word[4];
                p32 sum;

                storage_put32(word, group);
                sum = hash_crc32c(seed, word, 4);
                sum = hash_crc32c(sum, descriptor, STORAGE_EXT4_DESCRIPTOR);
                storage_put16(descriptor + 0x1e, sum);
        }

        //      Group 0's inode table, zeroed whole, then its first block.
        if (!failed)
                failed = storage_format_zero(
                    handle,
                    offset + (storage_ext4_front(address_of plan, 0) + 2) *
                                 STORAGE_EXT4_BLOCK,
                    (p64)plan.table_blocks * STORAGE_EXT4_BLOCK);

        p8 journal_map[60];

        if (!failed)
        {
                p64 table = offset + (storage_ext4_front(address_of plan, 0) + 2) *
                                         STORAGE_EXT4_BLOCK;

                memory_zero(block, STORAGE_EXT4_BLOCK);

                for (p32 number = 1; number < STORAGE_EXT4_FIRST_INODE; number++)
                        storage_ext4_inode(block + (number - 1) * STORAGE_EXT4_INODE,
                                           seed, number, 0, 0, 0, 0, 0,
                                           number == 1 ? identity->time : 0,
                                           false);

                storage_ext4_inode(block + (STORAGE_EXT4_ROOT - 1) * STORAGE_EXT4_INODE,
                                   seed, STORAGE_EXT4_ROOT, 040755, 3,
                                   STORAGE_EXT4_BLOCK, plan.root_block, 1,
                                   identity->time, true);
                storage_ext4_inode(block + (STORAGE_EXT4_JOURNAL - 1) * STORAGE_EXT4_INODE,
                                   seed, STORAGE_EXT4_JOURNAL, 0100600, 1,
                                   (p64)plan.journal_blocks * STORAGE_EXT4_BLOCK,
                                   plan.journal_start, plan.journal_blocks,
                                   identity->time, true);
                storage_ext4_inode(block + (STORAGE_EXT4_LOST - 1) * STORAGE_EXT4_INODE,
                                   seed, STORAGE_EXT4_LOST, 040700, 2,
                                   STORAGE_EXT4_LOST_BLOCKS * STORAGE_EXT4_BLOCK,
                                   plan.root_block + 1, STORAGE_EXT4_LOST_BLOCKS,
                                   identity->time, true);

                memory_copy(journal_map,
                            block + (STORAGE_EXT4_JOURNAL - 1) * STORAGE_EXT4_INODE + 0x28,
                            60);

                failed = storage_format_write(handle, block, STORAGE_EXT4_BLOCK,
                                              table);
        }

        //      The root directory, then lost+found's four blocks.
        if (!failed)
        {
                memory_zero(block, STORAGE_EXT4_BLOCK);
                storage_ext4_entry(block, STORAGE_EXT4_ROOT, 12, ".");
                storage_ext4_entry(block + 12, STORAGE_EXT4_ROOT, 12, "..");
                storage_ext4_entry(block + 24, STORAGE_EXT4_LOST,
                                   STORAGE_EXT4_TAIL - 24, "lost+found");
                storage_ext4_tail(block, seed, STORAGE_EXT4_ROOT);
                failed = storage_format_write(handle, block, STORAGE_EXT4_BLOCK,
                                              offset + plan.root_block *
                                                           STORAGE_EXT4_BLOCK);
        }

        for (p64 at = 0; at < STORAGE_EXT4_LOST_BLOCKS && !failed; at++)
        {
                memory_zero(block, STORAGE_EXT4_BLOCK);
                if (!at)
                {
                        storage_ext4_entry(block, STORAGE_EXT4_LOST, 12, ".");
                        storage_ext4_entry(block + 12, STORAGE_EXT4_ROOT,
                                           STORAGE_EXT4_TAIL - 12, "..");
                }
                else
                        storage_ext4_entry(block, 0, STORAGE_EXT4_TAIL, "");
                storage_ext4_tail(block, seed, STORAGE_EXT4_LOST);
                failed = storage_format_write(handle, block, STORAGE_EXT4_BLOCK,
                                              offset + (plan.root_block + 1 + at) *
                                                           STORAGE_EXT4_BLOCK);
        }

        //      The journal: zeroed, so nothing left there can replay, and clean.
        if (!failed)
                failed = storage_format_zero(handle,
                                             offset + plan.journal_start *
                                                          STORAGE_EXT4_BLOCK,
                                             (p64)plan.journal_blocks *
                                                 STORAGE_EXT4_BLOCK);
        if (!failed)
        {
                memory_zero(block, STORAGE_EXT4_BLOCK);
                storage_put32_be(block + 0x00, 0xc03b3998);
                storage_put32_be(block + 0x04, 4);
                storage_put32_be(block + 0x0c, STORAGE_EXT4_BLOCK);
                storage_put32_be(block + 0x10, plan.journal_blocks);
                storage_put32_be(block + 0x14, 1);
                storage_put32_be(block + 0x18, 1);
                memory_copy(block + 0x30, identity->uuid, 16);
                storage_put32_be(block + 0x40, 1);
                failed = storage_format_write(handle, block, STORAGE_EXT4_BLOCK,
                                              offset + plan.journal_start *
                                                           STORAGE_EXT4_BLOCK);
        }

        /*
                Descriptors and superblocks last, backups before the primary:
                until the primary lands there is no filesystem here for
                anything to mount half made.
        */
        for (p64 group = plan.groups; group-- > 0 && !failed;)
        {
                p64 start = group * STORAGE_EXT4_PER_GROUP;

                if (!storage_ext4_backup(group))
                        continue;

                failed = storage_format_write(handle, descriptors, descriptor_bytes,
                                              offset + (start + 1) *
                                                           STORAGE_EXT4_BLOCK);
                if (failed)
                        break;

                memory_zero(block, STORAGE_EXT4_BLOCK);
                storage_ext4_super(block + (group ? 0 : 1024), address_of plan,
                                   identity, journal_map, free_blocks, (p32)group);
                failed = storage_format_write(handle, block, STORAGE_EXT4_BLOCK,
                                              offset + start * STORAGE_EXT4_BLOCK);
        }

        memory_free(descriptors, descriptor_bytes + STORAGE_EXT4_BLOCK);
        return failed;
}
